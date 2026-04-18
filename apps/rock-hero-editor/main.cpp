#include "main_window.h"

#include <JuceHeader.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero
{

class RockHeroEditorApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
    {
        return ProjectInfo::projectName;
    }

    const juce::String getApplicationVersion() override
    {
        return ProjectInfo::versionString;
    }

    bool moreThanOneInstanceAllowed() override
    {
        return false;
    }

    void initialise(const juce::String& /*command_line*/) override
    {
        m_main_window = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        m_main_window.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<MainWindow> m_main_window;
};

} // namespace rock_hero

// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
START_JUCE_APPLICATION(rock_hero::RockHeroEditorApplication)
