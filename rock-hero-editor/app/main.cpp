#include <JuceHeader.h>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <rock_hero/common/audio/engine.h>
#include <rock_hero/common/core/application_identity.h>
#include <rock_hero/common/core/logger.h>
#include <rock_hero/editor/core/editor_settings.h>
#include <rock_hero/editor/core/juce_editor_task_runner.h>
#include <rock_hero/editor/core/juce_message_thread_scheduler.h>
#include <rock_hero/editor/ui/editor.h>
#include <rock_hero/editor/ui/main_window.h>
#include <utility>

namespace rock_hero::editor::app
{

// Pull in the logging facade so the composition root can install and use the logger without
// importing the whole rock_hero::common::core namespace.
using rock_hero::common::core::Logger;

namespace
{

constexpr std::size_t max_log_file_size_bytes = 8U * 1024U * 1024U;

// Resolves the rolling editor log file under the same "Rock Hero" app-data folder as editor
// settings, matching the location users and developers already look in.
[[nodiscard]] std::filesystem::path editorLogFile()
{
    const juce::File log_file =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile(juce::String{common::core::applicationDataFolderName().data()})
            .getChildFile("Rock Hero Editor.log");
    return std::filesystem::path{log_file.getFullPathName().toStdString()};
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
        .live_input = engine,
        .meter_source = engine,
    };
}

} // namespace

// JUCE application object that owns the editor window lifecycle.
class RockHeroEditorApplication : public juce::JUCEApplication
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
                    .max_file_size_bytes = max_log_file_size_bytes,
                });
        m_logging_started = logging_result.has_value();
        if (logging_result.has_value())
        {
            RH_LOG_INFO("editor.app", "Rock Hero Editor started log_file={}", log_file.string());
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

        auto editor = std::make_unique<rock_hero::editor::ui::Editor>(
            makeEditorAudioPorts(*m_audio_engine),
            rock_hero::editor::ui::Editor::Services{
                .settings = *m_editor_settings,
                .task_runner = *m_editor_task_runner,
                .message_thread_scheduler = *m_message_thread_scheduler,
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
START_JUCE_APPLICATION(rock_hero::editor::app::RockHeroEditorApplication)
