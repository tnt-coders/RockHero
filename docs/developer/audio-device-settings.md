\page guide_audio_device Audio Device Settings and Config Stores

*Applies to: Editor + game — the staged-settings and persistence layers are shared.*

Audio-device routing is the one editor feature built as a **self-contained sub-MVC beside the
main editor MVC**, plus one audio-settings store that the editor and the game share. Both ideas
are worth knowing before touching anything device-shaped.

# The staged-settings transaction (`common/audio`)

`AudioDeviceSettings` (`src/device/audio_device_settings.cpp`) wraps the hardware port
(`IAudioDeviceConfiguration`) as a **staged-edit transaction**: constructing it captures the
user's route (the live hardware setup, else the saved choice) and *hands the engine to the silent
device* so the user edits routing without holding hardware; a staged preview device probes
capabilities; then exactly one of `apply()` (open the staged route and save it as the user's
choice) or `cancel()` (reopen the captured route, saving nothing) ends the transaction — with a
destructor backstop restore for native window closes. `apply()` is the one place a device route
is saved, in the shared `IAudioConfigStore`: a device change the engine makes by itself (a
fallback, a reopen) is never a choice. Every route it opens goes through the port's no-fallback
`restoreSerializedDeviceState`, never `setCurrentAudioDeviceType()`. Its listener chain
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

Beside the dialog sits one main-MVC piece, `audioDeviceStatusText` (the menu-bar status line).
The editor never blocks itself without hardware: the engine runs its silent device
(`null_audio_device.h`), so playback, the chain and every edit keep working with the audio going
nowhere, and the status line reads `[audio device closed]`. Only live input needs the hardware.
The settings window is the repair path; while it stages, it hands the engine to the silent device
so the hardware is free, and every route it opens goes through the engine's no-fallback restore.
The editor opens it from view state (`EditorViewState::audio_device_settings_open`), like any
prompt: the menu-bar button only asks the controller, and with no usable saved route (a first run,
or a saved route that could not be read) the controller opens it at startup so the user chooses
one. A saved device that does not open at startup, or a running one lost outside the window, raises
`AudioDeviceLostPrompt` instead: the engine's reason, with Audio Settings and Close. The controller
tells a lost device from one never running by its last observation, `m_audio_device_open`.

# Persistence: one shared store

`AudioConfigStore` (`common/audio`, `src/settings/audio_config_store.cpp`) holds the device
route and the route-keyed input calibrations in one file, `Rock Hero/Rock Hero Audio.settings`,
which the editor and the game both read and write: the same hardware, and the same calibrated
level for the same guitar, in both products. Each composition root constructs its own
`AudioConfigStore` and injects it as `IAudioConfigStore&`.

Both products may run at once, so every operation opens a fresh `juce::PropertiesFile` rather
than trusting an in-memory copy, and the store names one `juce::InterProcessLock` as the file's
process lock. A write holds that lock across its whole read-modify-write, so neither product
clobbers a key the other just wrote; JUCE's lock is re-entrant, so its own load and save nest
inside. The options come from the one settings-file location policy,
`common::core::settingsFileOptions` (`common/core` `shared/settings_file_options.h`) — the shared
per-user folder, the `.settings` suffix, and a write-through save with no timer, so an
acknowledged write is on disk before the call returns — plus that `processLock`.

The three persisted property names for one input route (`backendName`, `inputDeviceName`,
`inputChannelIndex`) are declared beside `InputDeviceIdentity` itself
(`common/audio` `input/input_device_identity.h`) because both the shared store's XML and the game
settings file's JSON write them: a rename in one file alone would silently drop the user's saved
input-device selection, since a missing property reads as absence rather than an error.

# The shared measurement

Both products run the strum measurement the same way: `LiveInputMonitor::beginMeasurement`, then
`sample()` once per tick at `inputCalibrationSampleRateHz()`. Each sample returns the raw level and,
while a measurement runs, an `InputCalibrationProgress`: a stage with the windows it has left (the
editor's countdown reads them, never a count of its own), a committed gain, or a failure. The
measurement listens to the player's hardest playing for a fixed span from the first window it
hears and sets the gain so the playing's ceiling, a high percentile of the window peaks, lands on
`inputCalibrationTargetPeakDb()`; that target and every interface gain derive from the one
reference, `inputLevelReferenceDbu()` (`input/input_calibration.h`). A
measurement a gate run ended (a device change, a session closing) reports itself as a failure at
the next sample, once, so neither driver keeps its own record of having started one.

Why live input is off is worded once, by `liveInputStatusText` beside `LiveInputMonitoringStatus`
(`common/audio` `input/live_input_monitoring_status.h`): the editor's signal-chain message and the
game's `LiveInputOff` refusal both take their sentence from it, never their own copy.

# Known interfaces

The primary calibration path is the player's interface, not a measurement: `knownInterfaces()`
(`common/audio` `input/known_interfaces.h`) is the table of interfaces whose instrument-input
level at 0 dBFS is known. Each row authors that dBu figure, the input setting it holds for, how it
was established (`KnownInterfaceBasis`) and its source; `knownInterfaceGain()` derives the gain
from the one reference through the one quantizer, and `knownInterfaceBasisText()` words how far to
trust it. No row stores a gain, and no document copies a figure.

# The game's first-run setup

`NativeAudioSetupMachine` (`game/core/src/audio/native_audio_setup.cpp`) is a pure state machine
(`Idle → SelectingDevice → CalibratingGain → Ready`, terminal `Failed`) with a side-effecting
driver. The settings apply saves the device route; the driver then writes the game-private
`GameAudioConfig` mapping the resolved input route to player slot 0. This route→player-slot
mapping is the seed of future multiplayer input plumbing.

# Extending this area — silent steps

1. New dialog rows/intents extend `IAudioDeviceSettingsController` + the view state + the
   `toViewState()` projection in the controller `.cpp` — the sub-MVC's own triple, not the main
   editor's.
2. Device actions in the main editor land in `audio_device_handlers.cpp`; blocking device work
   goes through `onAudioDeviceChangeRequested` so it paints the busy overlay once, and the saved
   route is applied by `restoreAudioDeviceState()`. Opening the window goes through
   `openAudioDeviceSettings()`, never the view, so every opening pauses playback and re-runs the
   live-input gate.
3. New persisted config belongs in `AudioConfigStore` behind `IAudioConfigStore` — with strict
   parsing that treats corrupt values as absence, and setters that take the inter-process lock
   across their read-modify-write. A property name two files must agree on is declared beside the
   type it belongs to, never once per store; a new settings *file* takes its options from
   `settingsFileOptions`, never its own copy.
4. Tests: the controller runs dispatcher-less and synchronous; the store fakes are
   `ConfigurableAudioDeviceConfiguration` and `InMemoryAudioConfigStore`. An empty store is a first
   run, which opens the settings window, so an editor test of anything else starts from
   `savedRouteAudioConfigStore()`.
5. Adding a known interface is one row in `known_interfaces.cpp`, in model order, every field
   named: the model as its maker spells it, the input setting as a lower-case phrase completing
   "Set the interface to ___", the dBu at 0 dBFS, the basis and the source. The table test
   enforces the order and the phrasing; nothing else needs updating.
