/*!
\file plugin_window.h
\brief Host window for external plugin editors plus the window-command protocol it emits.
*/

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/plugin/plugin_window_shortcuts.h>
#include <string_view>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rock_hero::common::audio
{

/*! \brief Routes window commands to the host without coupling this adapter to editor-core. */
using PluginWindowCommandDispatcher = std::function<void(PluginWindowCommand)>;

/*! \brief Top-level JUCE window that Tracktion owns through PluginWindowState::pluginWindow. */
class PluginWindow final : public juce::DocumentWindow
{
public:
    /*!
    \brief Creates a window only when Tracktion can supply a concrete plugin editor component.
    \param plugin Tracktion plugin whose editor should be hosted.
    \param command_dispatcher Host callback receiving forwarded window commands.
    \return The hosting window, or null when the plugin has no editor.
    */
    [[nodiscard]] static std::unique_ptr<juce::Component> create(
        tracktion::Plugin& plugin, PluginWindowCommandDispatcher command_dispatcher);

    /*!
    \brief Takes ownership of Tracktion's editor component and lets plugin size changes drive
    bounds.
    \param plugin Tracktion plugin whose editor should be hosted.
    \param editor Editor wrapper created by the plugin.
    \param command_dispatcher Host callback receiving forwarded window commands.
    */
    PluginWindow(
        tracktion::Plugin& plugin, std::unique_ptr<tracktion::Plugin::EditorComponent> editor,
        PluginWindowCommandDispatcher command_dispatcher);

    /*! \brief Copying is disabled; the window owns native window state. */
    PluginWindow(const PluginWindow&) = delete;

    /*! \brief Copy assignment is disabled; the window owns native window state. */
    PluginWindow& operator=(const PluginWindow&) = delete;

    /*! \brief Moving is disabled; Tracktion and the native hook hold stable pointers. */
    PluginWindow(PluginWindow&&) = delete;

    /*! \brief Move assignment is disabled; Tracktion and the native hook hold stable pointers. */
    PluginWindow& operator=(PluginWindow&&) = delete;

    /*! \brief Flushes any plugin state touched by the editor before Tracktion releases the
    window. */
    ~PluginWindow() override;

    /*!
    \brief Replaces the shortcut bindings every plugin window matches keys against.

    Shared by all windows because they mirror one editor keymap. The editor pushes bindings
    through `IPluginHost::setPluginWindowShortcuts` after keymap restore and on every mapping
    change; until the first push, the built-in defaults apply so an editor-less engine keeps the
    historical behavior. Must be called on the message thread — the same thread the JUCE
    key-press path and the Win32 message hook read the bindings on, so no synchronization is
    needed.

    \param bindings Chord lists for the three forwarded commands.
    */
    static void setShortcutBindings(PluginWindowShortcutBindings bindings);

    /*! \brief Recreates the plugin editor when Tracktion asks the host window to refresh its
    content. */
    void recreateEditor();

    /*!
    \brief Drops the current editor synchronously, then recreates it on a short timer.

    Tracktion calls this hook from ExternalPlugin::forceFullReinitialise() right before it tears
    down and replaces the underlying AudioPluginInstance. The current editor is bound to the dying
    instance, so it must be released now; the replacement editor must be created *after*
    forceFullReinitialise() finishes installing the new instance, which is why creation is
    deferred onto the message loop.
    */
    void recreateEditorAsync();

    /*! \brief Routes the close button back through Tracktion so its window state stays
    authoritative. */
    void closeButtonPressed() override;

    /*! \brief Routes native system-close requests through the same Tracktion-owned close path. */
    void userTriedToCloseWindow() override;

    /*!
    \brief Reports an unscaled desktop factor so the native peer is not scaled twice.
    \return Always 1.0; plugin editors receive native scale notifications themselves.
    */
    [[nodiscard]] float getDesktopScaleFactor() const override;

    /*!
    \brief Forwards shortcuts that JUCE receives before the plugin editor consumes them.
    \param key Pressed key.
    \return True when the key was consumed as a host shortcut.
    */
    bool keyPressed(const juce::KeyPress& key) override;

    /*! \brief Persists the latest bounds so reopening can restore the user's window position. */
    void moved() override;

    /*! \brief Persists resized bounds while leaving DocumentWindow to manage the content
    layout. */
    void resized() override;

private:
    bool handleCommandShortcut(const juce::KeyPress& key, std::string_view source);

    [[nodiscard]] static std::string_view commandName(PluginWindowCommand command) noexcept;

    void dispatchCommandShortcut(PluginWindowCommand command, std::string_view source);

    void postCommandShortcut(PluginWindowCommand command, std::string_view source);

#if JUCE_WINDOWS
    [[nodiscard]] static bool isKeyDown(int virtual_key) noexcept;

    [[nodiscard]] static bool textInputHasFocus() noexcept;

    [[nodiscard]] static std::optional<PluginWindowShortcutKey> namedKeyForVirtualKey(
        WPARAM virtual_key) noexcept;

    [[nodiscard]] static std::optional<PluginWindowCommand> commandForWindowsKeyMessage(
        const MSG& message);

    [[nodiscard]] bool ownsNativeWindow(HWND window) const;

    [[nodiscard]] static PluginWindow* windowForNativeMessage(HWND window);

    [[nodiscard]] bool pluginViewHandlesKey(
        juce::juce_wchar character, int key_code, int modifiers);

    // How the native hook should treat a key that matches a plugin-window shortcut.
    enum class CommandKeyDisposition : std::uint8_t
    {
        FireShortcut,   // No plugin control wants the key: post the command, swallow the message.
        PluginConsumed, // The plugin handled the key via onKeyDown (already delivered): swallow.
        PassToPlugin,   // A native text field owns input: leave the message for the plugin.
    };

    [[nodiscard]] CommandKeyDisposition disposeCommandKey(PluginWindowCommand command);

    static LRESULT CALLBACK windowsShortcutHook(int code, WPARAM w_param, LPARAM l_param);

    void registerWindowsShortcutWindow();

    void unregisterWindowsShortcutWindow();

    struct WindowsHookState
    {
        HHOOK hook{};
        std::vector<PluginWindow*> windows;
    };

    // Defined in the source file: clang cannot evaluate a nested class's default member
    // initializers in an inline initializer while the enclosing class is still incomplete.
    static WindowsHookState s_windows_hook_state;
#endif

    void setEditor(std::unique_ptr<tracktion::Plugin::EditorComponent> editor);

    void storeWindowBounds();

    // Tracktion plugin whose editor is hosted by this window.
    tracktion::Plugin& m_plugin;

    // Tracktion owns this window and remains the source of truth for close/show state.
    tracktion::PluginWindowState& m_window_state;

    // Tracktion editor wrapper installed as the non-owned DocumentWindow content component.
    std::unique_ptr<tracktion::Plugin::EditorComponent> m_editor;

    // Routes host-window commands to the application without coupling this adapter to editor-core.
    PluginWindowCommandDispatcher m_command_dispatcher;

    // Prevents construction-time resized callbacks from overwriting Tracktion's default position.
    bool m_update_stored_bounds = false;
};

} // namespace rock_hero::common::audio
