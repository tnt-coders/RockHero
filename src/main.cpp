#include <juce_gui_basics/juce_gui_basics.h>

#include "EditorWindow.h"

namespace rock_hero
{

class RockHeroApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
    {
        return "Rock Hero";
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
        m_main_window = std::make_unique<EditorWindow>(getApplicationName());
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
    std::unique_ptr<EditorWindow> m_main_window;
};

} // namespace rock_hero

START_JUCE_APPLICATION(rock_hero::RockHeroApplication)
