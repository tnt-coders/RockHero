#include "main_window.h"

#include <rock_hero/audio/engine.h>
#include <rock_hero/core/session.h>
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
    , m_session(std::make_unique<core::Session>())
{
    m_session->addTrack("Full Mix");
    m_editor =
        std::make_unique<ui::Editor>(*m_session, *m_audio_engine, *m_audio_engine, *m_audio_engine);

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

// Routes the native close button through JUCE so normal application shutdown runs.
void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace rock_hero
