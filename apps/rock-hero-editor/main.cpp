#include <juce_gui_basics/juce_gui_basics.h>

#include "main_window.h"

namespace rock_hero
{

class RockHeroEditorApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
    {
        return "Rock Hero Editor";
    }

    const juce::String getApplicationVersion() override
    {
        return "0.0.1";
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
