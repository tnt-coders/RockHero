#include "main_window.h"

#include <algorithm>
#include <cassert>
#include <rock_hero/editor/ui/editor.h>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_main_window_min_width{1280};
constexpr int g_main_window_min_height{720};
constexpr int g_main_window_restore_width{1920};
constexpr int g_main_window_restore_height{1080};

// Keeps the restored native window inside the OS work area. A 1920x1080 restored window does not
// fit on a 1080p Windows desktop once the title bar and taskbar are included.
[[nodiscard]] juce::Rectangle<int> restoredMainWindowBounds() noexcept
{
    const auto* const display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    if (display == nullptr)
    {
        return {0, 0, g_main_window_restore_width, g_main_window_restore_height};
    }

    const juce::Rectangle<int> user_bounds = display->userBounds.toNearestInt();
    if (user_bounds.isEmpty())
    {
        return {0, 0, g_main_window_restore_width, g_main_window_restore_height};
    }

    const juce::Rectangle<int> preferred_bounds{
        0,
        0,
        std::min(g_main_window_restore_width, user_bounds.getWidth()),
        std::min(g_main_window_restore_height, user_bounds.getHeight()),
    };

    return preferred_bounds.withCentre(user_bounds.getCentre()).constrainedWithin(user_bounds);
}

} // namespace

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
    const juce::Rectangle<int> restore_bounds = restoredMainWindowBounds();
    setResizeLimits(
        std::min(g_main_window_min_width, restore_bounds.getWidth()),
        std::min(g_main_window_min_height, restore_bounds.getHeight()),
        8192,
        8192);
    setBounds(restore_bounds);
    setVisible(true);
    setFullScreen(true);
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
