#include "main_window/main_window.h"

#include "main_window/composed_character_filter.h"

#include <algorithm>
#include <cassert>
#include <rock_hero/editor/ui/main_window/editor.h>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_main_window_min_width{1280};
constexpr int g_main_window_min_height{720};
constexpr int g_main_window_restore_width{1920};
constexpr int g_main_window_restore_height{1080};

// Returns the primary display's work area in logical pixels, falling back to the restore size
// when no display information is available.
[[nodiscard]] juce::Rectangle<int> primaryDisplayWorkArea() noexcept
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

    return user_bounds;
}

// Keeps the restored native window inside the OS work area. A 1920x1080 restored window does not
// fit on a 1080p Windows desktop once the title bar and taskbar are included.
[[nodiscard]] juce::Rectangle<int> restoredMainWindowBounds() noexcept
{
    const juce::Rectangle<int> work_area = primaryDisplayWorkArea();

    const juce::Rectangle<int> preferred_bounds{
        0,
        0,
        std::min(g_main_window_restore_width, work_area.getWidth()),
        std::min(g_main_window_restore_height, work_area.getHeight()),
    };

    return preferred_bounds.withCentre(work_area.getCentre()).constrainedWithin(work_area);
}

} // namespace

// Installs the composed editor UI into the top-level JUCE window shell.
MainWindow::MainWindow(const juce::String& title, std::unique_ptr<Editor> editor)
    : juce::DocumentWindow(
          title,
          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
              juce::ResizableWindow::backgroundColourId),
          juce::DocumentWindow::allButtons)
    , m_editor(std::move(editor))
{
    assert(m_editor != nullptr);

    setUsingNativeTitleBar(true);
    if (m_editor != nullptr)
    {
        setContentNonOwned(&m_editor->component(), true);
        // A key listener runs before the component's own keyPressed
        // (ComponentPeer::handleKeyPress), so the filter sees every press before the command
        // dispatch in keyPressed below, with no ordering rule to keep. An OS-composed character
        // (Alt plus numpad digits on Windows) arrives bare and would otherwise match a chord —
        // `Alt+7`, `Alt+6` composes `L` and would toggle legato.
        m_composed_character_filter = std::make_unique<ComposedCharacterFilter>();
        addKeyListener(m_composed_character_filter.get());
    }
    setResizable(true, false);
    const juce::Rectangle<int> restore_bounds = restoredMainWindowBounds();
    setResizeLimits(
        std::min(g_main_window_min_width, restore_bounds.getWidth()),
        std::min(g_main_window_min_height, restore_bounds.getHeight()),
        8192,
        8192);
    // setFullScreen's maximizing ShowWindow paints and presents one Direct2D frame at the
    // pre-maximize client rect (during WM_SHOWWINDOW, before the maximized size is applied), and
    // the DXGI swap chain stretches that frame across the maximized window until the next paint
    // lands after startup work releases the message loop. Laying the window out at the work-area
    // size first keeps that stretched frame imperceptible; restored-size bounds here show up as a
    // 1080p frame stretched over the whole screen.
    setBounds(primaryDisplayWorkArea());
    setFullScreen(true);
    // The peer captured the work-area bounds above as its un-maximize rect; record the real
    // restored-window rect instead, as ResizableWindow::restoreWindowStateFromString does.
    if (juce::ComponentPeer* const peer = getPeer())
    {
        peer->setNonFullScreenBounds(restore_bounds);
    }
    setVisible(true);

    // JUCE's show path does not request native foreground activation, so ask once at startup for
    // foreground keyboard focus after the peer exists. Windows can still refuse this request.
    if (juce::ComponentPeer* const peer = getPeer())
    {
        peer->requestForegroundKeyboardFocus();
    }
}

// Removes JUCE's non-owning pointers before the owned editor content is destroyed.
MainWindow::~MainWindow()
{
    // Detach the key listener before the object behind it is destroyed; the key-listener list
    // holds non-owning pointers.
    if (m_composed_character_filter != nullptr)
    {
        removeKeyListener(m_composed_character_filter.get());
    }
    // Null out DocumentWindow's non-owning pointer before m_editor is destroyed.
    // Otherwise ~ResizableWindow would call removeChildComponent on a dangling pointer.
    clearContentComponent();
}

// Routes the native close button through the same guarded exit flow as File > Exit.
void MainWindow::closeButtonPressed()
{
    requestExit();
}

// The command mapping set is the single chord-to-command matcher — since plan 53 Phase 1b every
// keybind, grammar verbs included, dispatches through it — and the window shell is where a press
// ends up, so this is the one place the typing gate has to live
// (docs/plans/completed/keyboard-focus-rows.md, 4.0a). A focused text editor declines every key
// it does not type — Tab, Insert, the F-keys, Ctrl chords, Ctrl+Z once its own history is spent —
// and without the gate each of those would run a chart command behind the field. While the peer
// has a text input target nothing is dispatched; the press then reaches only JUCE's unclaimed-Tab
// fallback, which moves focus and so commits the value. Declining must have no side effects, since
// macOS asks twice for a refused key while a text target exists. Key up/down no longer reaches the
// mapping set at all: no command wants it today, and the first hold-style command needs a
// keyStateChanged forward under this same condition.
bool MainWindow::keyPressed(const juce::KeyPress& key)
{
    if (m_editor != nullptr)
    {
        juce::ComponentPeer* const peer = getPeer();
        const bool typing = peer != nullptr && peer->findCurrentTextInputTarget() != nullptr;
        if (!typing && m_editor->commandManager().getKeyMappings()->keyPressed(key, this))
        {
            return true;
        }
    }
    return juce::DocumentWindow::keyPressed(key);
}

// Routes platform quit requests through the same guarded exit flow as the close button. The editor
// owns the guards and the quit call; the window only forwards.
void MainWindow::requestExit()
{
    if (m_editor != nullptr)
    {
        m_editor->requestExit();
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
