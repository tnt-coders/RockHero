\page guide_audio_device Audio Device Settings and Config Stores

*Applies to: Editor + game — the staged-settings and persistence layers are shared.*

Audio-device routing is the one editor feature built as a **self-contained sub-MVC beside the
main editor MVC**, plus a persistence design that lets the editor mirror the game's audio
configuration without ever being able to corrupt it. Both ideas are worth knowing before touching
anything device-shaped.

# The staged-settings transaction (`common/audio`)

`AudioDeviceSettings` (`src/device/audio_device_settings.cpp`) wraps the hardware port
(`IAudioDeviceConfiguration`) as a **staged-edit transaction**: constructing it captures the
currently-open route and *closes the device* so the user edits routing without holding hardware;
a staged preview device probes capabilities; then exactly one of `apply()` (open the staged
route), `cancel()` (reopen the captured route), or `commit()` (keep whatever is live) ends the
transaction — with a destructor backstop restore for native window closes. Its listener chain
re-broadcasts hardware-port changes upward: port → `AudioDeviceSettings` → the settings
controller → `updateView()`.

# The sub-MVC (editor)

The dialog is a miniature of the main architecture, but **ephemeral and self-owned**: the JUCE
window content (`audio_device_settings_window.cpp`) constructs and owns all three pieces —
the `AudioDeviceSettings` transaction, the framework-free `AudioDeviceSettingsController`
(implementing `IAudioDeviceSettingsController`, listening on the settings object rather than the
raw port), and the `AudioDeviceSettingsView` (implementing the three-method
`IAudioDeviceSettingsView`: `setState`, `requestClose`, `setApplying`). `EditorController` never
owns any of it.

Its only couplings to the main editor are deliberate and narrow: the shared port, two
close/teardown callbacks, and an injected **dispatcher** — a function the editor wires to
`onAudioDeviceChangeRequested` (`audio_device_handlers.cpp`) so blocking device work runs behind
the editor's busy overlay. With no dispatcher supplied, the controller runs synchronously, which
is exactly how its tests drive it. Reach for this shape when a modal feature owns a genuine
multi-step transaction of its own; reach for the ordinary action pipeline otherwise.

Around the dialog sit three main-MVC pieces: `AudioDeviceFailureOverlay` (the editor-wide
blocking overlay whenever no device is open, driven by view state, offering Retry / Open
Settings), `GameAudioRecommendationDialog` (the startup suggestion to adopt the game's settings),
and `audioDeviceStatusText` (the menu-bar status line).

# Persistence: two stores, one of them untouchable

`AudioConfigStore` (`common/audio`, `src/settings/audio_config_store.cpp`) is a
`juce::PropertiesFile`-backed per-app store for the active device route and route-keyed input
calibration. It opens `ReadWrite` or **`ReadOnly`**, and every setter checks that mode first,
failing with a typed `CouldNotSave` before touching the file.

The editor composes stores through `EditorAudioConfigStore`
(`editor/core/src/audio/editor_audio_config_store.cpp`): its own read-write store, plus — when
"use game audio settings" is on — a **fresh read-only view of the game's file** as the active
source. Reads mirror the game; any write while mirroring fails loudly instead of silently
redirecting into the editor's file. That read-only store *is* the design: a mechanism that fails
loudly was chosen over a flag someone could forget to check (see "no code that lies" in the
project's conventions). `gameSourceState()` reports `Available` only when the game's route has a
resolved input identity *and* a matching calibration.

The calibration UI reflects the same source split: when mirroring the game, the input-calibration
window shows controls visible-but-disabled ("derived from game settings"), and the signal chain
surfaces `InputCalibrationStatus` in its view state.

# The game's first-run setup

`NativeAudioSetupMachine` (`game/core/src/audio/native_audio_setup.cpp`) is a pure state machine
(`Idle → SelectingDevice → CalibratingGain → Ready`, terminal `Failed`) with a side-effecting
driver. On device apply it writes **two records in one step**: the shared store's
`ActiveDeviceRoute` (serialized state + resolved input identity) and the game-private
`GameAudioConfig` mapping that route to player slot 0 — written together so the shared mirror and
the slot mapping cannot drift. This route→player-slot mapping is the seed of future multiplayer
input plumbing.

# Extending this area — silent steps

1. New dialog rows/intents extend `IAudioDeviceSettingsController` + the view state + the
   `toViewState()` projection in the controller `.cpp` — the sub-MVC's own triple, not the main
   editor's.
2. Device actions in the main editor land in `audio_device_handlers.cpp`; anything that reopens
   a device goes through `applyAudioSourceAndRoute(...)` so it paints the busy overlay and
   re-evaluates the failure prompt exactly once.
3. New persisted config belongs in `AudioConfigStore` behind `IAudioConfigStore` — with strict
   parsing that treats corrupt values as absence, and setters that respect the read-only gate.
4. Tests: the controller runs dispatcher-less and synchronous; the store fakes are
   `ConfigurableAudioDeviceConfiguration` and `InMemoryAudioConfigStore`.
