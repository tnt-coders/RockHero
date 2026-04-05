#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero
{

class GameWindow : public juce::DocumentWindow
{
public:
    using juce::DocumentWindow::DocumentWindow;

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class RockHeroGameApplication : public juce::JUCEApplication
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
        m_main_window = std::make_unique<GameWindow>(
            getApplicationName(),
            juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                juce::ResizableWindow::backgroundColourId),
            juce::DocumentWindow::allButtons);

        m_main_window->setUsingNativeTitleBar(true);
        m_main_window->setResizable(true, false);
        m_main_window->centreWithSize(800, 600);
        m_main_window->setVisible(true);
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
    std::unique_ptr<GameWindow> m_main_window;
};

} // namespace rock_hero

START_JUCE_APPLICATION(rock_hero::RockHeroGameApplication)
