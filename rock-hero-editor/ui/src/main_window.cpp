#include "main_window.h"

#include <cassert>
#include <rock_hero/editor/ui/editor.h>
#include <utility>

namespace rock_hero::editor::ui
{

// Installs the composed editor UI into the top-level JUCE window shell.
MainWindow::MainWindow(
    const juce::String& title, std::unique_ptr<Editor> editor, ExitCallback exit_callback)
    : juce::DocumentWindow(
          title,
          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
              juce::ResizableWindow::backgroundColourId),
          juce::DocumentWindow::allButtons)
    , m_exit_callback(std::move(exit_callback))
    , m_editor(std::move(editor))
{
    assert(m_editor != nullptr);

    setUsingNativeTitleBar(true);
    if (m_editor != nullptr)
    {
        setContentNonOwned(&m_editor->component(), true);
    }
    setResizable(true, false);
    centreWithSize(1280, 800);
    setVisible(true);
}

// Removes JUCE's non-owning pointers before the owned editor content is destroyed.
MainWindow::~MainWindow()
{
    // Null out DocumentWindow's non-owning pointer before m_editor is destroyed.
    // Otherwise ~ResizableWindow would call removeChildComponent on a dangling pointer.
    clearContentComponent();
}

// Routes the native close button through the same guarded exit flow as File > Exit.
void MainWindow::closeButtonPressed()
{
    requestExit();
}

// Routes platform quit requests through the same guarded exit flow as the close button.
void MainWindow::requestExit()
{
    if (m_editor != nullptr)
    {
        m_editor->requestExit();
        return;
    }

    if (m_exit_callback)
    {
        m_exit_callback();
    }
}

// Starts project restore after the editor feature is installed in the window.
void MainWindow::restoreLastOpenProject()
{
    if (m_editor != nullptr)
    {
        m_editor->restoreLastOpenProject();
    }
}

} // namespace rock_hero::editor::ui
