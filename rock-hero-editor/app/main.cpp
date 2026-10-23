#include <JuceHeader.h>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/engine/engine.h>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/settings/audio_config_store.h>
#include <rock_hero/common/core/highway/highway_settings_store.h>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/core/shared/shared_settings_file.h>
#include <rock_hero/common/core/shared/user_data_folders.h>
#include <rock_hero/common/ui/highway_theme/highway_theme_registry.h>
#include <rock_hero/editor/core/settings/editor_settings.h>
#include <rock_hero/editor/core/tasks/juce_editor_task_runner.h>
#include <rock_hero/editor/core/tasks/juce_message_thread_scheduler.h>
#include <rock_hero/editor/ui/main_window/editor.h>
#include <rock_hero/editor/ui/main_window/main_window.h>
#include <rock_hero/editor/ui/shared/editor_look_and_feel.h>
#include <utility>

namespace rock_hero::editor::app
{

// Pull in the logging facade so the composition root can install and use the logger without
// importing the whole rock_hero::common::core namespace.
using rock_hero::common::core::Logger;

namespace
{

constexpr std::size_t g_max_log_file_size_bytes = static_cast<std::size_t>(8U * 1024U * 1024U);

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
        .tone_timeline = engine,
        .tone_automation = engine,
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

        const std::filesystem::path log_file =
            rock_hero::common::core::logFile(rock_hero::common::core::Product::Editor);
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

        // The built-in themes are required product content: without one the editor has no string
        // colors to draw with, so a broken install says so and quits instead of opening.
        std::expected<
            rock_hero::common::ui::HighwayThemeRegistry,
            rock_hero::common::ui::HighwayThemeError>
            loaded_themes = rock_hero::editor::ui::loadInstalledHighwayThemes();
        if (!loaded_themes.has_value())
        {
            RH_LOG_ERROR("editor.app", "{}", loaded_themes.error().message);
            juce::NativeMessageBox::showAsync(
                juce::MessageBoxOptions::makeOptionsOk(
                    juce::MessageBoxIconType::WarningIcon,
                    "Rock Hero Editor cannot start",
                    juce::String::fromUTF8(loaded_themes.error().message.c_str())),
                [](int) { juce::JUCEApplicationBase::quit(); });
            return;
        }
        const rock_hero::common::ui::HighwayThemeRegistry& highway_themes =
            m_highway_themes.emplace(std::move(*loaded_themes));

        m_audio_engine = std::make_unique<rock_hero::common::audio::Engine>(
            rock_hero::common::audio::perUserEngineFolders());
        m_editor_settings = std::make_unique<rock_hero::editor::core::EditorSettings>(
            rock_hero::common::core::settingsFile(rock_hero::common::core::Product::Editor));
        m_editor_task_runner = std::make_unique<rock_hero::editor::core::JuceEditorTaskRunner>();
        m_message_thread_scheduler =
            std::make_unique<rock_hero::editor::core::JuceMessageThreadScheduler>();

        // The settings both products share: the audio device route and input calibrations, and the
        // player's highway settings, which the editor's theme and palette pickers write.
        m_shared_settings_file = std::make_unique<rock_hero::common::core::SharedSettingsFile>(
            rock_hero::common::core::sharedSettingsFile());
        m_audio_config_store =
            std::make_unique<rock_hero::common::audio::AudioConfigStore>(*m_shared_settings_file);
        m_highway_settings_store = std::make_unique<rock_hero::common::core::HighwaySettingsStore>(
            *m_shared_settings_file);

        // The engine implements both ILiveInput and IAudioDeviceConfiguration; the shared monitor
        // and the controller both read the audio settings through the one store.
        m_live_input_monitor = std::make_unique<rock_hero::common::audio::LiveInputMonitor>(
            *m_audio_engine, *m_audio_engine, *m_audio_config_store);

        // Installed before the first window exists, so every component, plugin windows included,
        // draws with it from its first paint.
        m_look_and_feel = std::make_unique<rock_hero::editor::ui::EditorLookAndFeel>();
        juce::Desktop::getInstance().setDefaultLookAndFeel(m_look_and_feel.get());

        auto editor = std::make_unique<rock_hero::editor::ui::Editor>(
            makeEditorAudioPorts(*m_audio_engine),
            rock_hero::editor::ui::Editor::Services{
                .settings = *m_editor_settings,
                .task_runner = *m_editor_task_runner,
                .message_thread_scheduler = *m_message_thread_scheduler,
                .audio_config_store = *m_audio_config_store,
                .live_input_monitor = *m_live_input_monitor,
                .highway_settings_store = *m_highway_settings_store,
                .highway_themes = highway_themes,
            },
            &juce::JUCEApplicationBase::quit);

        m_main_window = std::make_unique<rock_hero::editor::ui::MainWindow>(
            getApplicationName(), std::move(editor));
    }

    // Releases the editor window before JUCE tears down the application object. The task runner
    // is reset after the editor so its destructor joins any outstanding worker before the
    // audio engine teardown begins.
    void shutdown() override
    {
        m_main_window.reset();
        m_live_input_monitor.reset();
        m_highway_settings_store.reset();
        m_audio_config_store.reset();
        m_shared_settings_file.reset();
        m_message_thread_scheduler.reset();
        m_editor_task_runner.reset();
        m_editor_settings.reset();
        m_audio_engine.reset();
        m_highway_themes.reset();

        // Unset only once the engine's plugin windows are gone too, and before it is destroyed: a
        // JUCE look-and-feel must not die while the desktop still names it the default.
        if (m_look_and_feel != nullptr)
        {
            juce::Desktop::getInstance().setDefaultLookAndFeel(nullptr);
            m_look_and_feel.reset();
        }

        // Plugin-scan child processes return from initialise before installing logging, so only
        // tear it down when this instance actually started it.
        if (m_logging_started)
        {
            RH_LOG_INFO("editor.app", "Rock Hero Editor shutdown complete");
            Logger::shutdown();
        }
    }

    // Routes platform quit requests through the editor's guarded exit, so unsaved work is offered
    // for saving; with no window up, quits directly.
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

    // The desktop's default look-and-feel while the editor runs. Released last of the UI objects,
    // after the engine whose plugin windows also draw with it.
    std::unique_ptr<rock_hero::editor::ui::EditorLookAndFeel> m_look_and_feel;

    // Owns Tracktion-backed playback for the lifetime of the editor window.
    std::unique_ptr<rock_hero::common::audio::Engine> m_audio_engine;

    // Owns app-local editor settings persistence used by controller restore policy.
    std::unique_ptr<rock_hero::editor::core::EditorSettings> m_editor_settings;

    // Every highway theme that loaded. The window lists them and draws with them by reference, so
    // the registry is released after it.
    std::optional<rock_hero::common::ui::HighwayThemeRegistry> m_highway_themes;

    // Owns the settings file both products share. Released after the stores over it.
    std::unique_ptr<rock_hero::common::core::SharedSettingsFile> m_shared_settings_file;

    // Owns the audio-config store both products share. Released after the monitor and editor that
    // read through it.
    std::unique_ptr<rock_hero::common::audio::AudioConfigStore> m_audio_config_store;

    // Owns the player's highway settings store both products share. Released after the editor that
    // reads and writes through it.
    std::unique_ptr<rock_hero::common::core::HighwaySettingsStore> m_highway_settings_store;

    // Owns the shared calibrate-first live-input monitoring service the controller drives. Composed
    // over the engine's live-input/device ports and the audio-config store, and released before
    // the store and engine it references during shutdown.
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
