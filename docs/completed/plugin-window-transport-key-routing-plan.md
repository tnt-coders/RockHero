# Plugin-Window Transport-Key Routing Plan

Status: completed - implemented and verified with Archetype Nolly and Gateway (VST3). The goal was to
make global keyboard shortcuts (Space = Play/Pause, and Ctrl+Z / Ctrl+Y = Undo / Redo) work while a
hosted VST3 plugin editor window is focused, **without** stealing those keys when the plugin's own
focused control (e.g. a text/preset field) needs them.

## Problem

A native plugin editor window with keyboard focus receives `WM_KEYDOWN` directly from the OS; our
host window never gets a clean shot at the key. To make transport shortcuts work over a focused
plugin window we install a thread-local `WH_GETMESSAGE` hook (`PluginWindow::windowsShortcutHook` in
`rock-hero-common/audio/src/engine.cpp`) that intercepts the key before the plugin's window proc.

The committed feature (commit `23dbc4f7`, "Implement space bar keyboard shortcut while plugin window
is open") intercepts and **unconditionally** captures Space as Play/Pause. That regresses text entry
inside plugins: typing a space into a plugin's preset/search field toggles transport instead of
inserting a space. The same hazard applies to the existing Ctrl+Z / Ctrl+Y capture. We need to
capture the key as a shortcut **only when the focused plugin control does not want it.**

## What we ruled out (verified, not assumed)

These were traced in the vendored JUCE / VST3 SDK under `external/tracktion_engine/`. Re-verify
against current submodule state before relying on any file:line below.

- **`WM_GETDLGCODE` is useless for JUCE plugins.** JUCE returns `DLGC_WANTALLKEYS` unconditionally
  (`.../juce_gui_basics/native/juce_Windowing_windows.cpp`, `WM_GETDLGCODE` case), so "ask the
  control which keys it wants" always says "all of them."
- **A JUCE plugin editor is a single HWND.** Its knobs and text fields are lightweight components
  painted inside one window, so `GetFocus()` cannot tell which child has focus.
- **REAPER detection is not the mechanism.** JUCE's `isReaper()` is used only for a window-resizing
  quirk (`.../juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp`), never for keyboard.
  Keyboard handling is governed by the host-agnostic VST3 contract, not host sniffing.
- **The VST3 standard mechanism is `IPlugView::onKeyDown`.** The SDK is explicit
  (`.../VST3_SDK/pluginterfaces/gui/iplugview.h`): *"A view implementation must not handle keyboard
  events by the means of platform callbacks, but let the host pass them to the view. The host
  depends on a proper return value when `IPlugView::onKeyDown` is called."* It returns `kResultTrue`
  only if the plugin really handled the key. This is delivery through the VST3 contract, not a
  side-effect-free focus query.
- **Our JUCE VST3 host does not call `onKeyDown`.** `VST3PluginWindow` in
  `.../juce_audio_processors/format_types/juce_VST3PluginFormat.cpp` comments *"most, if not all,
  plugins do their own keyboard hooks"* and just returns `true` from `keyPressed`. So the standard
  handshake was unused on our side until this work.

## The decisive evidence: a probe across two real plugins

A temporary probe was added to the hook to log, for every unmodified Space over a plugin window,
both candidate signals side by side: the return value from delivering the key through the plugin's
VST3 `onKeyDown` contract and the OS system caret (`GetGUIThreadInfo().hwndCaret`). Tested with
Neural DSP **Archetype Nolly** and **Gateway**, both loaded as VST3; both let Space type into their
text fields in REAPER.

| Plugin  | Text field focused? | `onKeyDown` handled | system caret present |
|---------|---------------------|---------------------|----------------------|
| Nolly   | yes                 | **false**           | **true**             |
| Nolly   | no                  | false               | false                |
| Gateway | yes                 | **true**            | **false**            |
| Gateway | no                  | false               | false                |

**The two plugins are inverted.** Each answers exactly one of the two signals:

- **Nolly** uses native text input: it creates a Win32 system caret but does **not** honor the VST3
  keyboard contract (`onKeyDown` always returns false). One of the "plugins do their own keyboard
  hooks" cases.
- **Gateway** honors the VST3 contract (`onKeyDown` is true exactly when its text field is focused)
  but draws its own caret, so no system caret exists.

This is why a caret-only heuristic fixed Nolly but not Gateway, and why an `onKeyDown`-only approach
would fix Gateway but not Nolly. It also resolves the REAPER question without assumption: REAPER must
consult both kinds of signal (or equivalents), because real plugins split across the two mechanisms.

## Decision: a per-key strategy

This is not the only conceivable solution, but it is the most practical robust route for Windows
native VST3 plugin windows in Rock Hero's current Tracktion/JUCE architecture. Main-window JUCE
shortcuts cannot see keys after focus moves inside the plugin's native window, and direct transport
calls from the plugin window would bypass editor-core command policy.

Space and Ctrl+Z / Ctrl+Y are handled **differently**, because they ask different questions:

- **Space** asks "does the focused control want this key at all?" Typing a space must keep working,
  so the plugin (or its text field) wins whenever it wants the key.
- **Ctrl+Z / Ctrl+Y** are always routed to Rock Hero's global undo. This follows the settled undo
  decision (RockHero-owned mementos, Tracktion as backend; *unified cross-domain Ctrl+Z = single
  source of truth*). We deliberately override the plugin's own undo rather than ask whether it
  "wants" the key.

### Space: union of the two authoritative signals

Treat Space as "the focused control wants it" when **either** the plugin claims it via the VST3
contract **or** the OS shows an active text caret, preferring the standards-based signal. These are
complementary input-ownership signals, not two stacked heuristics: `onKeyDown` delivers the key to
the plugin view and returns the plugin's explicit VST3 handled/unhandled result (preferred); the
system caret is the OS's evidence that a native text field owns input (fallback for plugins that
ignore the contract). The probe proved both are required because the test plugins each answer only
one (Nolly: caret only; Gateway: `onKeyDown` only).

#### Routing logic (Space, in the native hook)

```
onKeyDown(Space) == true   -> plugin accepted the delivered key:
                              swallow the native message, do NOT play/pause   [Gateway path]

onKeyDown(Space) == false  -> system caret present?
                              yes -> a native text field owns input:
                                    let the native message through, do NOT play/pause  [Nolly path]
                              no  -> no control wants it:
                                    play/pause, swallow the native message    [empty / knob path]
```

Double-delivery handling differs per branch and is deliberate:

- After `onKeyDown == true`, the key was already delivered through the view, so the native message
  must be **swallowed** to avoid a second delivery.
- In the caret branch the key was **not** delivered by `onKeyDown`, so the native message must be
  **passed through** for the plugin's own handler to insert it.

Calling `onKeyDown` speculatively is a managed compatibility risk, not a pure query. The VST3
contract requires the plugin to return `kResultTrue` only when it really handled the key, so a
`kResultFalse` result should mean the key was not consumed. Buggy plugins can still violate that
contract, which is why the routing remains plugin-window-specific and should be manually verified
with real plugins.

##### Judgment call (Space)

The caret branch deliberately favors *letting the plugin keep the key*. A plugin that leaves a stray
system caret alive while not editing could therefore occasionally suppress Play/Pause. We accept this
trade because the failure mode (Play/Pause occasionally not firing) is far less harmful than the
alternative (silently eating a user's space mid-edit). Revisit if a real plugin exhibits the
stray-caret behavior.

### Ctrl+Z / Ctrl+Y: pure global-undo override

Capture Ctrl+Z / Ctrl+Y unconditionally and route them to Rock Hero's global undo/redo whenever a
plugin window is focused - this is the current committed behavior, kept as-is. Do **not** consult
`onKeyDown` or the caret for these keys. This is both simpler than the Space path (no VST3 modifier
translation, so the `kCommandKey`/`kControlKey` ambiguity never arises) and the correct expression of
the single-source-of-truth undo decision.

Crucially, this does **not** generally break text-field undo. Plugins that feed text edits into their
own state have those edits captured as Rock Hero undo mementos, so the global-override Ctrl+Z restores
the prior chunk - which includes the prior text value. The text reverts through Rock Hero's unified
stack, not the plugin's text widget. Archetype Nolly is confirmed to behave this way: with the native
hook swallowing Ctrl+Z before Nolly's field can see it, typed text still undoes - which is only
possible because Nolly marks its plugin state dirty on every keystroke (its known "dirty-*" quirk,
see the preset-undo work) and Rock Hero captures each change.

A caret exception here would be actively *harmful*: it would let the widget revert its own buffer
behind Rock Hero's back, desyncing the two stacks for exactly the plugins where text undo currently
works. That is why Ctrl+Z / Ctrl+Y use a pure override and not the Space-style union.

#### Residual case

Plugins that do **not** push text into their state (commit only on Enter/blur, with no per-keystroke
dirty signal) have no per-keystroke mementos, so Ctrl+Z would undo the last Rock Hero entry instead,
and the widget's in-field undo is unavailable. This is rare and accepted; a caret exception cannot
reliably help these plugins (Gateway-style fields expose no system caret) and would harm the
capturing ones.

## Implementation

### Verification results (Nolly + Gateway, VST3)

Confirmed in the running editor:

- **Space, Nolly text field** -> withheld via the caret branch (`disposition=text_caret`); space types.
- **Space, Gateway text field** -> withheld via the contract branch (`disposition=plugin_onkeydown`);
  space types.
- **Space, knob/empty (both)** -> Play/Pause fires.
- **Ctrl+Z (both)** -> routed to Rock Hero global undo with a real `setPluginState` restore.
- **Gateway text-edit capture:** typing in a Gateway field *does* emit `Plugin state edit started` /
  `entry_pushed "Edit Gateway"`, like Nolly. So the pure Ctrl+Z / Ctrl+Y override gives coherent
  text undo through Rock Hero's stack for **both** plugins; the residual in-field-undo gap does not
  apply to either of these.

### JUCE fork (already applied, `rock-hero` branch of the `juce` submodule)

Minimal, format-agnostic, and reusable as the production mechanism (not throwaway probe scaffolding):

- `juce_audio_processors_headless/processors/juce_AudioPluginInstance.h` - new virtual
  `bool sendKeyDownToPluginView (juce_wchar character, int keyCode, int modifiers)`, default
  `return false`. Uses primitives, not `juce::KeyPress`, because the headless module has no GUI
  dependency.
- `juce_audio_processors/format_types/juce_VST3PluginFormat.cpp` -
  - `VST3PluginWindow::sendKeyDownToView(...)` calls `view->onKeyDown(...)` and returns
    `== kResultTrue`.
  - `VST3PluginInstance::sendKeyDownToPluginView(...)` override bridges from the instance to its
    active editor via `dynamic_cast<VST3PluginWindow*>(getActiveEditor())` (both types live in the
    same translation unit). `getActiveEditor()` is populated because Tracktion creates the editor
    through `inst->createEditorIfNeeded()` (`tracktion_ExternalPlugin.cpp`).

Keep these changes as small as possible; the JUCE fork is intentionally minimal.

### Engine (`rock-hero-common/audio/src/engine.cpp`)

1. **Remove the temporary probe** in `windowsShortcutHook` and `PluginWindow::pluginViewHandlesKey`
   once routing lands (the probe currently calls `onKeyDown` *and* still fires Play/Pause for
   measurement).
2. **Reach the plugin instance** from the focused `PluginWindow`:
   `dynamic_cast<tracktion::ExternalPlugin*>(&m_plugin)` -> `getAudioPluginInstance()` ->
   `sendKeyDownToPluginView(...)`.
3. **System-caret helper:** restore `textInputHasFocus()` (`GetGUIThreadInfo(GetCurrentThreadId())`,
   `hwndCaret != nullptr`) as the caret branch of the **Space** union. This is now part of the
   principled solution, not a standalone band-aid.
4. **Apply the union routing for Space only**, replacing the current unconditional Space capture. The
   hook must decide all three Space outcomes (swallow + play/pause, swallow + no play/pause,
   pass-through + no play/pause), so the Space decision likely moves into the hook rather than staying
   purely inside `commandForWindowsKeyMessage`. Space is ASCII, so the only `onKeyDown` call needed is
   `onKeyDown(0x20, 0, 0)` - no VST3 modifier translation, and the `kCommandKey`/`kControlKey`
   ambiguity is avoided entirely.
5. **Leave Ctrl+Z / Ctrl+Y unchanged.** They keep the current unconditional capture and routing to
   `onUndoRequested` / `onRedoRequested` (Rock Hero global undo). Do not call `onKeyDown` or check the
   caret for these keys (see the Ctrl+Z / Ctrl+Y decision above).

### Tests

Unit tests drive the observer directly via `RecordingPluginHost` and cannot exercise the Win32 hook,
`GetGUIThreadInfo`, or `IPlugView::onKeyDown`. The Space union routing (and the unchanged Ctrl+Z /
Ctrl+Y capture) is therefore integration-only on Windows. Keep the existing `RecordingPluginHost` /
`editor_controller` transport tests; add a code comment noting the native-hook routing is not
unit-testable, rather than a test that fakes it.

## Known limitations

- **Plugins that use neither mechanism** (no `onKeyDown`, no system caret, e.g. a fully custom GUI
  drawing its own caret without `onKeyDown`) will still have Space captured while typing. Both test
  plugins fall into one camp or the other, but this residual gap is real.
- The honest long-term escape hatch for that gap is a **per-plugin "send all keyboard input to
  plugin" override**, mirroring REAPER / Ardour / Mixbus. Out of scope here; note it as the future
  home for unfixable cases rather than widening the heuristic.

## Completed change

This completed change contains:

- The Tracktion submodule pointer update to the `rock-hero` branch commit that advances the nested
  JUCE submodule for the `sendKeyDownToPluginView` / VST3 `onKeyDown` bridge.
- The `engine.cpp` Space routing implementation: `textInputHasFocus()`,
  `pluginViewHandlesKey()`, `CommandKeyDisposition`, `disposeCommandKey()`, and the native-hook
  dispatch behavior in `windowsShortcutHook`.
- The completed plan and verification notes for the Nolly and Gateway VST3 behavior.

Ctrl+Z / Ctrl+Y remain unchanged as Rock Hero global undo/redo shortcuts. The verification-only
logging has been removed.

## References

- `docs/design/architecture.md`, `docs/design/architectural-principles.md` - Tracktion stays
  confined to `common/audio`; the JUCE/VST3 keyboard plumbing belongs behind the audio adapter, and
  `editor/core` only sees the existing `PluginWindowCommandObserver` callbacks.
- VST3 SDK `iplugview.h` - the `onKeyDown` contract quoted above.
- `rock-hero-common/audio/src/engine.cpp` - `PluginWindow`, `commandForWindowsKeyMessage`,
  `windowsShortcutHook`.
- `rock-hero-common/audio/include/rock_hero/common/audio/i_plugin_host.h` -
  `PluginWindowCommandObserver`.
