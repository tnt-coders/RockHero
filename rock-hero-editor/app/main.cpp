#include <JuceHeader.h>
#include <cstddef>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <rock_hero/common/audio/engine/engine.h>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/settings/audio_config_identity.h>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/editor/core/audio/editor_audio_config_store.h>
#include <rock_hero/editor/core/settings/editor_settings.h>
#include <rock_hero/editor/core/tasks/juce_editor_task_runner.h>
#include <rock_hero/editor/core/tasks/juce_message_thread_scheduler.h>
#include <rock_hero/editor/ui/main_window/editor.h>
#include <rock_hero/editor/ui/main_window/main_window.h>
#include <string_view>
#include <utility>

namespace rock_hero::editor::app
{

// Pull in the logging facade so the composition root can install and use the logger without
// importing the whole rock_hero::common::core namespace.
using rock_hero::common::core::Logger;

namespace
{

constexpr std::size_t g_max_log_file_size_bytes = static_cast<std::size_t>(8U * 1024U * 1024U);

// Resolves the rolling editor log file under the same "Rock Hero" app-data folder as editor
// settings, matching the location users and developers already look in.
[[nodiscard]] std::filesystem::path editorLogFile()
{
    const std::string_view folder_name = common::core::applicationDataFolderName();
    const juce::File log_file =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile(juce::String{folder_name.data(), folder_name.size()})
            .getChildFile("Rock Hero Editor.log");
    return common::core::pathFromJuceFile(log_file);
}

// Resolves the game's audio-config file by the same per-user path AudioConfigStore derives for the
// game's application name, so the editor's read-only view targets exactly the file the game writes.
// Composed from the shared app-data folder and the game audio-config application name, with zero
// game-code linkage.
[[nodiscard]] std::filesystem::path gameAudioConfigFile()
{
    const std::string_view folder_name = common::core::applicationDataFolderName();
    const std::string_view application_name =
        rock_hero::common::audio::gameAudioConfigApplicationName();
    juce::PropertiesFile::Options options;
    options.applicationName = juce::String{application_name.data(), application_name.size()};
    options.filenameSuffix = ".settings";
    options.folderName = juce::String{folder_name.data(), folder_name.size()};
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    return common::core::pathFromJuceFile(options.getDefaultFile());
}

// Maps the concrete Tracktion-backed engine into the editor's narrow audio-port bundle. This
// stays in app composition because only the composition root knows one object backs every port.
[[nodiscard]] rock_hero::editor::ui::Editor::AudioPorts makeEditorAudioPorts(
    rock_hero::common::audio::Engine& engine)
{
    return rock_hero::editor::ui::Editor::AudioPorts{
        .transport = engine,
        .song_audio = engine,
        .thumbnail_factory = engine,
        .audio_devices = engine,
        .plugin_host = engine,
        .live_rig = engine,
        .tone_automation = engine,
        .live_input = engine,
        .meter_source = engine,
        .playback_clock = engine,
    };
}

} // namespace

// JUCE application object that owns the editor window lifecycle.
class RockHeroEditor : public juce::JUCEApplication
{
public:
    // Provides JUCE with the generated project name used for app metadata and windows.
    const juce::String getApplicationName() override
    {
        return ProjectInfo::projectName;
    }

    // Provides JUCE with the generated project version for app metadata.
    const juce::String getApplicationVersion() override
    {
        return ProjectInfo::versionString;
    }

    // Keeps startup single-instance while letting helper child processes serve plugin scanning.
    bool moreThanOneInstanceAllowed() override
    {
        const std::string command_line =
            juce::JUCEApplicationBase::getCommandLineParameters().toStdString();
        return rock_hero::common::audio::Engine::isPluginScanChildProcessCommandLine(command_line);
    }

    // Creates the editor window once JUCE has entered application startup.
    void initialise(const juce::String& command_line) override
    {
        const std::string command_line_text = command_line.toStdString();
        if (rock_hero::common::audio::Engine::startPluginScanChildProcess(command_line_text))
        {
            return;
        }

        const std::filesystem::path log_file = editorLogFile();
        const std::expected<void, rock_hero::common::core::LoggerError> logging_result =
            Logger::init(
                Logger::Config{
                    .log_file = log_file,
                    .max_file_size_bytes = g_max_log_file_size_bytes,
                });
        m_logging_started = logging_result.has_value();
        if (logging_result.has_value())
        {
            RH_LOG_INFO("editor.app", "Rock Hero Editor started log_file={:?}", log_file.string());
        }
        else
        {
            juce::Logger::writeToLog(
                "Rock Hero Editor logging could not be started; continuing without logging: " +
                juce::String::fromUTF8(logging_result.error().message.c_str()));
        }

        m_audio_engine = std::make_unique<rock_hero::common::audio::Engine>();
        m_editor_settings = std::make_unique<rock_hero::editor::core::EditorSettings>();
        m_editor_task_runner = std::make_unique<rock_hero::editor::core::JuceEditorTaskRunner>();
        m_message_thread_scheduler =
            std::make_unique<rock_hero::editor::core::JuceMessageThreadScheduler>();

        // Editor audio-config store: reads and writes delegate to the editor's own read-write store
        // or a read-only view of the game's file, per the active source. Injected everywhere the
        // editor's audio config is read so the device route and calibration follow the active source.
        m_editor_audio_config_store =
            std::make_unique<rock_hero::editor::core::EditorAudioConfigStore>(
                m_editor_settings->audioConfigStore(), gameAudioConfigFile());

        // The controller resolves the "use game audio settings" toggle itself at startup
        // (selecting the game source, or staging the unavailable/recommendation prompts) before it
        // restores the device route, so composition only builds and injects the store.

        // The engine implements both ILiveInput and IAudioDeviceConfiguration; the store is the
        // swappable IAudioConfigStore& the shared monitor and the controller both read through.
        m_live_input_monitor = std::make_unique<rock_hero::common::audio::LiveInputMonitor>(
            *m_audio_engine, *m_audio_engine, *m_editor_audio_config_store);

        auto editor = std::make_unique<rock_hero::editor::ui::Editor>(
            makeEditorAudioPorts(*m_audio_engine),
            rock_hero::editor::ui::Editor::Services{
                .settings = *m_editor_settings,
                .task_runner = *m_editor_task_runner,
                .message_thread_scheduler = *m_message_thread_scheduler,
                .audio_config_store = *m_editor_audio_config_store,
                .live_input_monitor = *m_live_input_monitor,
                .editor_audio_config_store = m_editor_audio_config_store.get(),
            },
            &juce::JUCEApplicationBase::quit);

        m_main_window = std::make_unique<rock_hero::editor::ui::MainWindow>(
            getApplicationName(), std::move(editor), &juce::JUCEApplicationBase::quit);
        m_main_window->restoreLastOpenProject();
    }

    // Releases the editor window before JUCE tears down the application object. The task runner
    // is reset after the editor so its destructor joins any outstanding worker before the
    // audio engine teardown begins.
    void shutdown() override
    {
        m_main_window.reset();
        m_live_input_monitor.reset();
        m_editor_audio_config_store.reset();
        m_message_thread_scheduler.reset();
        m_editor_task_runner.reset();
        m_editor_settings.reset();
        m_audio_engine.reset();

        // Plugin-scan child processes return from initialise before installing logging, so only
        // tear it down when this instance actually started it.
        if (m_logging_started)
        {
            RH_LOG_INFO("editor.app", "Rock Hero Editor shutdown complete");
            Logger::shutdown();
        }
    }

    // Handles platform quit requests through JUCE's normal quit path.
    void systemRequestedQuit() override
    {
        if (m_main_window != nullptr)
        {
            m_main_window->requestExit();
            return;
        }

        quit();
    }

private:
    // Tracks whether this instance installed the logging backend, so shutdown only tears it down
    // for the real editor process and not for early-returning plugin-scan child processes.
    bool m_logging_started = false;

    // Owns Tracktion-backed playback for the lifetime of the editor window.
    std::unique_ptr<rock_hero::common::audio::Engine> m_audio_engine;

    // Owns app-local editor settings persistence used by controller restore policy.
    std::unique_ptr<rock_hero::editor::core::EditorSettings> m_editor_settings;

    // Owns the editor audio-config store over the editor's own store and a read-only view of the
    // game's audio-config file. Constructed after the settings it wraps and released after the
    // monitor and editor that read through it but before those settings.
    std::unique_ptr<rock_hero::editor::core::EditorAudioConfigStore> m_editor_audio_config_store;

    // Owns the shared calibrate-first live-input monitoring service the controller drives. Composed
    // over the engine's live-input/device ports and the editor audio-config store, and released
    // before the store, settings, and engine it references during shutdown.
    std::unique_ptr<rock_hero::common::audio::LiveInputMonitor> m_live_input_monitor;

    // Owns the JUCE-backed editor task runner used for background project IO. Outlives the
    // editor so the controller's task_runner pointer remains valid for the editor's lifetime.
    std::unique_ptr<rock_hero::editor::core::JuceEditorTaskRunner> m_editor_task_runner;

    // Owns the JUCE-backed scheduler used for busy presentation callbacks.
    std::unique_ptr<rock_hero::editor::core::JuceMessageThreadScheduler> m_message_thread_scheduler;

    // Owns the editor window after JUCE startup and releases it during shutdown.
    std::unique_ptr<rock_hero::editor::ui::MainWindow> m_main_window;
};

} // namespace rock_hero::editor::app

// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
START_JUCE_APPLICATION(rock_hero::editor::app::RockHeroEditor)
