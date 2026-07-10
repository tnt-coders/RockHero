#include "tracktion/plugin_window.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <rock_hero/common/core/shared/logger.h>
#include <string_view>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

[[nodiscard]] int normalizedAsciiKeyCode(int key_code) noexcept
{
    if (key_code >= 'A' && key_code <= 'Z')
    {
        return key_code - 'A' + 'a';
    }
    return key_code;
}

[[nodiscard]] bool hasCommandShortcutModifier(const juce::KeyPress& key) noexcept
{
    const juce::ModifierKeys modifiers = key.getModifiers();
    return modifiers.isCommandDown() && !modifiers.isAltDown();
}

[[nodiscard]] bool isUndoShortcut(const juce::KeyPress& key) noexcept
{
    return hasCommandShortcutModifier(key) && !key.getModifiers().isShiftDown() &&
           normalizedAsciiKeyCode(key.getKeyCode()) == 'z';
}

[[nodiscard]] bool isRedoShortcut(const juce::KeyPress& key) noexcept
{
    return hasCommandShortcutModifier(key) && !key.getModifiers().isShiftDown() &&
           normalizedAsciiKeyCode(key.getKeyCode()) == 'y';
}

[[nodiscard]] bool isPlayPauseShortcut(const juce::KeyPress& key) noexcept
{
    return key == juce::KeyPress{juce::KeyPress::spaceKey};
}

} // namespace

std::unique_ptr<juce::Component> PluginWindow::create(
    tracktion::Plugin& plugin, PluginWindowCommandDispatcher command_dispatcher)
{
    std::unique_ptr<tracktion::Plugin::EditorComponent> editor = plugin.createEditor();
    if (editor == nullptr)
    {
        return {};
    }

    return std::make_unique<PluginWindow>(plugin, std::move(editor), std::move(command_dispatcher));
}

PluginWindow::PluginWindow(
    tracktion::Plugin& plugin, std::unique_ptr<tracktion::Plugin::EditorComponent> editor,
    PluginWindowCommandDispatcher command_dispatcher)
    : juce::DocumentWindow(
          plugin.getName(), juce::Colours::darkgrey,
          juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
    , m_plugin(plugin)
    , m_window_state(*plugin.windowState)
    , m_command_dispatcher(std::move(command_dispatcher))
{
    setUsingNativeTitleBar(true);
    setWantsKeyboardFocus(true);

    // Configure the default ResizableWindow constrainer as a backstop. setEditor() will
    // replace this with the plugin editor's own constrainer when one is supplied; these
    // values only take effect for editors that allow resizing but don't provide a
    // constrainer of their own. The onscreen amounts also guard against restored bounds
    // landing on a monitor that no longer exists — the title bar (top) must stay fully
    // onscreen so the user can always drag the window back.
    getConstrainer()->setMinimumOnscreenAmounts(0x10000, 50, 30, 50);
    setResizeLimits(100, 50, 4000, 4000);

    setEditor(std::move(editor));

    // Restore the full saved rectangle on within-session reopen when the editor supports
    // resizing; otherwise fall back to the editor's natural size at Tracktion's chosen
    // position. Tracktion's own choosePositionForPluginWindow() returns only a Point, so
    // size would be lost across close/reopen without this branch. If an editor returns
    // empty bounds (signaling "host, pick a size"), the default ResizableWindow
    // constrainer's setResizeLimits floor of 100x50 takes over.
    const bool editor_allows_resizing = m_editor != nullptr && m_editor->allowWindowResizing();
    // Local copy so the guard and the dereference read the same value (also keeps clang-tidy's
    // optional tracking satisfied across the member access).
    const auto saved_bounds = m_window_state.lastWindowBounds;
    if (editor_allows_resizing && saved_bounds.has_value())
    {
        setBoundsConstrained(*saved_bounds);
    }
    else
    {
        setBoundsConstrained(getLocalBounds() + m_window_state.choosePositionForPluginWindow());
    }

    m_update_stored_bounds = true;
#if JUCE_WINDOWS
    registerWindowsShortcutWindow();
#endif
}

PluginWindow::~PluginWindow()
{
#if JUCE_WINDOWS
    unregisterWindowsShortcutWindow();
#endif
    m_update_stored_bounds = false;
    m_plugin.edit.flushPluginStateIfNeeded(m_plugin);
    setEditor(nullptr);
}

// setEditor() always clears existing state before installing the new editor, so no separate
// setEditor(nullptr) call is needed first.
void PluginWindow::recreateEditor()
{
    setEditor(m_plugin.createEditor());
}

// Standard Tracktion-host pattern (see external/tracktion_engine/examples/common/PluginWindow.h
// and the default PluginWindowState::recreateWindowIfShowing): drop the current editor
// synchronously, then recreate it on a short timer. The "Async" suffix in
// UIBehaviour::recreatePluginWindowContentAsync is contractual — Tracktion depends on this
// being deferred.
void PluginWindow::recreateEditorAsync()
{
    setEditor(nullptr);

    juce::Timer::callAfterDelay(50, [safe_this = juce::Component::SafePointer<PluginWindow>{this}] {
        if (auto* const window = safe_this.getComponent())
        {
            window->recreateEditor();
        }
    });
}

void PluginWindow::closeButtonPressed()
{
    m_window_state.closeWindowExplicitly();
}

void PluginWindow::userTriedToCloseWindow()
{
    closeButtonPressed();
}

float PluginWindow::getDesktopScaleFactor() const
{
    return 1.0F;
}

bool PluginWindow::keyPressed(const juce::KeyPress& key)
{
    if (handleCommandShortcut(key, "window"))
    {
        return true;
    }

    return juce::DocumentWindow::keyPressed(key);
}

void PluginWindow::moved()
{
    storeWindowBounds();
}

void PluginWindow::resized()
{
    juce::DocumentWindow::resized();
    storeWindowBounds();
}

bool PluginWindow::handleCommandShortcut(const juce::KeyPress& key, std::string_view source)
{
    if (isUndoShortcut(key))
    {
        postCommandShortcut(PluginWindowCommand::Undo, source);
        return true;
    }

    if (isRedoShortcut(key))
    {
        postCommandShortcut(PluginWindowCommand::Redo, source);
        return true;
    }

    if (isPlayPauseShortcut(key))
    {
        postCommandShortcut(PluginWindowCommand::PlayPause, source);
        return true;
    }

    return false;
}

std::string_view PluginWindow::commandName(PluginWindowCommand command) noexcept
{
    switch (command)
    {
        case PluginWindowCommand::Undo:
        {
            return "Undo";
        }
        case PluginWindowCommand::Redo:
        {
            return "Redo";
        }
        case PluginWindowCommand::PlayPause:
        {
            return "Play/Pause";
        }
    }

    return "Unknown";
}

void PluginWindow::dispatchCommandShortcut(PluginWindowCommand command, std::string_view source)
{
    if (!m_command_dispatcher)
    {
        return;
    }

    RH_LOG_INFO(
        "audio.engine",
        "Plugin window shortcut forwarded source={:?} command={:?}",
        source,
        commandName(command));
    m_command_dispatcher(command);
}

// The source view must reference static-storage text (call sites pass literals): the dispatch
// runs after this call returns, and capturing the view keeps every closure member
// nothrow-copyable so posting the message cannot leak an exception.
void PluginWindow::postCommandShortcut(PluginWindowCommand command, std::string_view source)
{
    const juce::Component::SafePointer<PluginWindow> safe_this{this};
    juce::MessageManager::callAsync([safe_this, command, source] {
        if (auto* const window = safe_this.getComponent())
        {
            window->dispatchCommandShortcut(command, source);
        }
    });
}

#if JUCE_WINDOWS
bool PluginWindow::isKeyDown(int virtual_key) noexcept
{
    return (GetKeyState(virtual_key) & 0x8000) != 0;
}

// Reports whether a text field currently owns keyboard input on this thread. Frameworks that
// accept text create a Win32 system caret on focus and destroy it on blur (native edit controls
// by design; JUCE in textInputRequired/dismissPendingTextInput), so a live caret is a
// cross-framework signal that the focused control wants character keys. This is the fallback
// branch of the Space union for plugins that take keys via native hooks instead of honoring the
// VST3 onKeyDown contract (e.g. Archetype Nolly). The gap is plugins whose text fields create no
// system caret (e.g. Gateway), which are covered by the onKeyDown branch instead.
bool PluginWindow::textInputHasFocus() noexcept
{
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (GetGUIThreadInfo(GetCurrentThreadId(), &info) == 0)
    {
        return false;
    }

    return info.hwndCaret != nullptr;
}

std::optional<PluginWindowCommand> PluginWindow::commandForWindowsKeyMessage(const MSG& message)
{
    if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN)
    {
        return std::nullopt;
    }

    constexpr LPARAM repeat_flag = 1LL << 30;
    if ((message.lParam & repeat_flag) != 0)
    {
        return std::nullopt;
    }

    const bool control_down = isKeyDown(VK_CONTROL);
    const bool alt_down = isKeyDown(VK_MENU);
    const bool shift_down = isKeyDown(VK_SHIFT);

    if (!control_down && !alt_down && !shift_down && message.wParam == VK_SPACE)
    {
        return PluginWindowCommand::PlayPause;
    }

    if (!control_down || alt_down || shift_down)
    {
        return std::nullopt;
    }

    if (message.wParam == 'Z')
    {
        return PluginWindowCommand::Undo;
    }

    if (message.wParam == 'Y')
    {
        return PluginWindowCommand::Redo;
    }

    return std::nullopt;
}

bool PluginWindow::ownsNativeWindow(HWND window) const
{
    const juce::ComponentPeer* const peer = getPeer();
    if (peer == nullptr)
    {
        return false;
    }

    auto* const native_handle = static_cast<HWND>(peer->getNativeHandle());
    return native_handle != nullptr &&
           (window == native_handle || IsChild(native_handle, window) != 0);
}

PluginWindow* PluginWindow::windowForNativeMessage(HWND window)
{
    for (PluginWindow* const plugin_window : s_windows_hook_state.windows)
    {
        if (plugin_window != nullptr && plugin_window->ownsNativeWindow(window))
        {
            return plugin_window;
        }
    }

    return nullptr;
}

// Delivers a key to the hosted plugin through the VST3 keyboard contract
// (IPlugView::onKeyDown via our JUCE fork) and reports whether the plugin accepted it. The
// contract says a false return means the key was not consumed; buggy plugins can still violate
// that, so this speculative delivery is intentionally limited to the Space routing path.
bool PluginWindow::pluginViewHandlesKey(juce::juce_wchar character, int key_code, int modifiers)
{
    if (auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(&m_plugin))
    {
        if (juce::AudioPluginInstance* const instance = external_plugin->getAudioPluginInstance())
        {
            return instance->sendKeyDownToPluginView(character, key_code, modifiers);
        }
    }

    return false;
}

// Decides how to route a shortcut key over a focused plugin window. Space yields to the plugin's
// focused control when it wants the key -- preferring the VST3 onKeyDown contract, falling back
// to the system caret -- so typing into plugin text fields keeps working. Undo/Redo always go to
// Rock Hero's global undo (single source of truth) and never yield.
PluginWindow::CommandKeyDisposition PluginWindow::disposeCommandKey(PluginWindowCommand command)
{
    if (command != PluginWindowCommand::PlayPause)
    {
        return CommandKeyDisposition::FireShortcut;
    }

    if (pluginViewHandlesKey(' ', 0, 0))
    {
        return CommandKeyDisposition::PluginConsumed;
    }

    if (textInputHasFocus())
    {
        return CommandKeyDisposition::PassToPlugin;
    }

    return CommandKeyDisposition::FireShortcut;
}

LRESULT CALLBACK PluginWindow::windowsShortcutHook(int code, WPARAM w_param, LPARAM l_param)
{
    if (code >= 0 && w_param == PM_REMOVE)
    {
        // The WH_GETMESSAGE contract delivers the MSG through LPARAM, so this
        // integer-to-pointer reinterpret_cast is imposed by the Win32 ABI.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
        auto* const message = reinterpret_cast<MSG*>(l_param);
        if (message != nullptr)
        {
            const std::optional<PluginWindowCommand> command =
                commandForWindowsKeyMessage(*message);
            if (command.has_value())
            {
                if (PluginWindow* const window = windowForNativeMessage(message->hwnd);
                    window != nullptr)
                {
                    switch (window->disposeCommandKey(*command))
                    {
                        case CommandKeyDisposition::FireShortcut:
                        {
                            window->postCommandShortcut(*command, "native_hook");
                            message->message = WM_NULL;
                            message->wParam = 0;
                            message->lParam = 0;
                            break;
                        }
                        case CommandKeyDisposition::PluginConsumed:
                        {
                            // onKeyDown already delivered the key; drop the native copy so the
                            // plugin does not receive it twice.
                            message->message = WM_NULL;
                            message->wParam = 0;
                            message->lParam = 0;
                            break;
                        }
                        case CommandKeyDisposition::PassToPlugin:
                        {
                            // A native text field owns input; leave the message for the plugin.
                            break;
                        }
                    }
                }
            }
        }
    }

    return CallNextHookEx(s_windows_hook_state.hook, code, w_param, l_param);
}

void PluginWindow::registerWindowsShortcutWindow()
{
    s_windows_hook_state.windows.push_back(this);
    if (s_windows_hook_state.hook == nullptr)
    {
        s_windows_hook_state.hook =
            SetWindowsHookExW(WH_GETMESSAGE, windowsShortcutHook, nullptr, GetCurrentThreadId());
    }
}

void PluginWindow::unregisterWindowsShortcutWindow()
{
    std::erase(s_windows_hook_state.windows, this);
    if (s_windows_hook_state.windows.empty() && s_windows_hook_state.hook != nullptr)
    {
        UnhookWindowsHookEx(s_windows_hook_state.hook);
        s_windows_hook_state.hook = nullptr;
    }
}

PluginWindow::WindowsHookState PluginWindow::s_windows_hook_state{};
#endif

// Installs Tracktion's editor wrapper while preserving plugin-owned resize notifications.
void PluginWindow::setEditor(std::unique_ptr<tracktion::Plugin::EditorComponent> editor)
{
    setConstrainer(nullptr);
    clearContentComponent();
    m_editor.reset();

    if (editor != nullptr)
    {
        m_editor = std::move(editor);
        setContentNonOwned(m_editor.get(), true);
    }

    const bool allow_window_resizing = m_editor == nullptr || m_editor->allowWindowResizing();
    setResizable(allow_window_resizing, false);

    if (m_editor != nullptr && allow_window_resizing)
    {
        setConstrainer(m_editor->getBoundsConstrainer());
    }
}

// Caches the latest user-driven bounds on Tracktion's window state so a closed-and-reopened
// plugin window restores its last position within the session. This path only updates in-memory
// window state and intentionally does not call Edit::pluginChanged(); durable per-arrangement
// window bounds are a separate, unimplemented concern (see docs/todo/plugin-window-persistence.md).
void PluginWindow::storeWindowBounds()
{
    if (m_update_stored_bounds)
    {
        m_window_state.lastWindowBounds = getBounds();
    }
}

} // namespace rock_hero::common::audio
