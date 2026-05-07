#include "main_window.h"

#include <rock_hero/audio/engine.h>
#include <rock_hero/ui/editor.h>

namespace rock_hero
{

// Owns the editor runtime dependencies before creating the editor feature that stores references.
MainWindow::MainWindow(const juce::String& title)
    : juce::DocumentWindow(
          title,
          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
              juce::ResizableWindow::backgroundColourId),
          juce::DocumentWindow::allButtons)
    , m_audio_engine(std::make_unique<audio::Engine>())
{
    // Engine currently implements each editor-facing audio port. Passing it for each role keeps
    // Editor dependent on narrow interfaces rather than on the concrete Tracktion adapter. The
    // editor owns its Session internally through EditCoordinator so app code cannot bypass edits.
    m_editor = std::make_unique<ui::Editor>(
        *m_audio_engine, *m_audio_engine, *m_audio_engine, [this] { closeWindow(); });

    setUsingNativeTitleBar(true);
    setContentNonOwned(&m_editor->component(), true);
    setResizable(true, false);
    centreWithSize(800, 300);
    setVisible(true);
}

// Removes JUCE's non-owning pointers before owned content and engine members are destroyed.
MainWindow::~MainWindow()
{
    // Null out DocumentWindow's non-owning pointer before m_editor is destroyed.
    // Otherwise ~ResizableWindow would call removeChildComponent on a dangling pointer.
    clearContentComponent();
}

// Routes the native close button through the same guarded exit flow as File > Exit.
void MainWindow::closeButtonPressed()
{
    m_editor->requestExit();
}

// Runs normal JUCE application shutdown after the editor has allowed the close to continue.
void MainWindow::closeWindow()
{
    if (auto* app = juce::JUCEApplicationBase::getInstance(); app != nullptr)
    {
        app->systemRequestedQuit();
    }
}

} // namespace rock_hero
