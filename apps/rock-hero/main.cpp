#include <JuceHeader.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero
{

// Temporary game shell window used until the SDL/bgfx gameplay content owns the view.
class MainWindow : public juce::DocumentWindow
{
public:
    using juce::DocumentWindow::DocumentWindow;

    // Routes the native close button through JUCE so normal application shutdown runs.
    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

// JUCE application object that owns the temporary game window lifecycle.
class RockHeroApplication : public juce::JUCEApplication
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

    // Keeps startup single-instance while the app has no multi-window/session model.
    bool moreThanOneInstanceAllowed() override
    {
        return false;
    }

    // Creates the temporary shell window during JUCE startup before gameplay rendering exists.
    void initialise(const juce::String& /*command_line*/) override
    {
        // The game app is still a shell window, so construct the DocumentWindow
        // directly here until there is a dedicated content component to own.
        m_main_window = std::make_unique<MainWindow>(
            getApplicationName(),
            juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                juce::ResizableWindow::backgroundColourId),
            juce::DocumentWindow::allButtons);

        m_main_window->setUsingNativeTitleBar(true);
        m_main_window->setResizable(true, false);
        m_main_window->centreWithSize(800, 600);
        m_main_window->setVisible(true);
    }

    // Releases the shell window before JUCE tears down the application object.
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
    std::unique_ptr<MainWindow> m_main_window;
};

} // namespace rock_hero

// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
START_JUCE_APPLICATION(rock_hero::RockHeroApplication)
