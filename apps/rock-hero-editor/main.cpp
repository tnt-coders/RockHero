#include "main_window.h"

#include <JuceHeader.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero
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
        m_main_window = std::make_unique<MainWindow>(getApplicationName());
    }

    // Releases the editor window before JUCE tears down the application object.
    void shutdown() override
    {
        m_main_window.reset();
    }

    // Handles platform quit requests through JUCE's normal quit path.
    void systemRequestedQuit() override
    {
        quit();
    }

private:
    // Owns the editor window after JUCE startup and releases it during shutdown.
    std::unique_ptr<MainWindow> m_main_window;
};

} // namespace rock_hero

// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
START_JUCE_APPLICATION(rock_hero::RockHeroEditorApplication)
