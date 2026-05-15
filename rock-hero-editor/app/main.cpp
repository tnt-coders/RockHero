#include <JuceHeader.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <rock_hero/common/audio/engine.h>
#include <rock_hero/editor/core/editor_settings.h>
#include <rock_hero/editor/ui/editor.h>
#include <rock_hero/editor/ui/main_window.h>

namespace rock_hero::editor::app
{

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

    // Keeps startup single-instance while editor session coordination is not implemented.
    bool moreThanOneInstanceAllowed() override
    {
        return false;
    }

    // Creates the editor window once JUCE has entered application startup.
    void initialise(const juce::String& /*command_line*/) override
    {
        m_audio_engine = std::make_unique<rock_hero::common::audio::Engine>();
        m_editor_settings = std::make_unique<rock_hero::editor::core::EditorSettings>();

        // Engine implements each editor-facing audio port. Passing it for each role keeps UI code
        // dependent on narrow interfaces rather than on the concrete Tracktion adapter.
        auto editor = std::make_unique<rock_hero::editor::ui::Editor>(
            *m_audio_engine,
            *m_audio_engine,
            *m_audio_engine,
            *m_audio_engine,
            rock_hero::editor::core::EditorController::Services{
                .exit_function = &juce::JUCEApplicationBase::quit,
                .settings = m_editor_settings.get(),
            });

        m_main_window = std::make_unique<rock_hero::editor::ui::MainWindow>(
            getApplicationName(), std::move(editor), &juce::JUCEApplicationBase::quit);
        m_main_window->restoreLastOpenProject();
    }

    // Releases the editor window before JUCE tears down the application object.
    void shutdown() override
    {
        m_main_window.reset();
        m_editor_settings.reset();
        m_audio_engine.reset();
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
    // Owns Tracktion-backed playback for the lifetime of the editor window.
    std::unique_ptr<rock_hero::common::audio::Engine> m_audio_engine;

    // Owns app-local editor settings persistence used by controller restore policy.
    std::unique_ptr<rock_hero::editor::core::EditorSettings> m_editor_settings;

    // Owns the editor window after JUCE startup and releases it during shutdown.
    std::unique_ptr<rock_hero::editor::ui::MainWindow> m_main_window;
};

} // namespace rock_hero::editor::app

// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
START_JUCE_APPLICATION(rock_hero::editor::app::RockHeroEditorApplication)
