#include "main_window.h"

#include <rock_hero/editor/ui/editor.h>
#include <utility>

namespace rock_hero::editor::ui
{

// Installs the composed editor UI around app-owned runtime dependencies.
MainWindow::MainWindow(
    const juce::String& title, common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IThumbnailFactory& thumbnail_factory, core::EditorSettings& settings,
    ExitCallback exit_callback)
    : juce::DocumentWindow(
          title,
          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
              juce::ResizableWindow::backgroundColourId),
          juce::DocumentWindow::allButtons)
    , m_exit_callback(std::move(exit_callback))
{
    m_editor = std::make_unique<Editor>(
        transport,
        audio,
        thumbnail_factory,
        core::EditorController::Services{
            .exit_function = m_exit_callback,
            .settings = &settings,
        });

    setUsingNativeTitleBar(true);
    setContentNonOwned(&m_editor->component(), true);
    setResizable(true, false);
    centreWithSize(1280, 800);
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

// Starts settings-backed project restore after the editor feature is installed in the window.
void MainWindow::restoreLastOpenProject()
{
    if (m_editor != nullptr)
    {
        m_editor->restoreLastOpenProject();
    }
}

} // namespace rock_hero::editor::ui
