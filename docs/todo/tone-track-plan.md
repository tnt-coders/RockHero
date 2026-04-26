# Guitar Tone Track — Implementation Plan

## Goal

Add a second track to the Rock Hero audio engine that accepts live guitar input through ASIO,
routes it through a user-configured VST plugin chain, and supports automation for any plugin
parameter the user chooses to automate over the song timeline. All authoring of the tone track
happens in `rock-hero-editor`. The
`rock-hero-game` uses the same `rock-hero-audio-engine` backend to play back the configured tone in
real
time — it makes no edits.

---

## Scope Boundary

The distinction between the two executables is strict:

| Capability | `rock-hero-editor` | `rock-hero-game` |
|---|---|---|
| Load and play backing track | Yes | Yes |
| Load tone timeline from `Song` blob | Yes | Yes |
| Enable ASIO guitar input | Yes | Yes |
| Add / remove / reorder VST plugins | **Yes** | No |
| Edit plugin parameter values | **Yes** | No |
| Author automation curves | **Yes** | No |
| Serialize tone timeline to blob | **Yes** | No |

The game treats the `Song` data model as read-only during gameplay. It loads the serialized tone
timeline (authored in the editor) and plays it back. It never modifies the chain or automation.

---

## API Design Decisions

### `enableGuitarInput` is explicit in both apps

`loadToneTimeline` does not open the ASIO device automatically. Both the editor and the game call
`enableGuitarInput` explicitly after loading. This separation means:

- ASIO failure surfaces at the right place (`enableGuitarInput`), not inside a deserialization call.
- The editor can load and inspect a tone timeline without connecting a guitar.
- Both apps handle device selection and error display in their own way.

### Error handling: `std::expected<void, GuitarInputError>`

`enableGuitarInput` returns `std::expected<void, GuitarInputError>` with a typed error enum rather
than `bool`, `juce::Result`, or exceptions. The reasons:

- "No ASIO device found" is an **anticipated** failure mode, not an exceptional circumstance.
  Exceptions are for unexpected failures. `std::expected` signals at the type level that failure
  is a normal outcome.
- `juce::Result` carries a `juce::String` error message, which would couple the `AudioEngine`
  public header to `juce_core` for no benefit — see the API convention below.
- `std::expected` is C++23, which this project already targets.
- A typed enum lets the game branch on specific failure cases to show distinct error messages
  without string parsing.

```cpp
enum class GuitarInputError
{
    NoAsioDeviceFound,
    ChannelOutOfRange,
    DeviceAlreadyInUse,
};

std::expected<void, GuitarInputError> enableGuitarInput(int asio_channel);
```

The game shows a clear error message for each case:
- `NoAsioDeviceFound` → "No ASIO audio interface detected. Check your audio interface and drivers."
- `ChannelOutOfRange` → "Invalid ASIO input channel selected."
- `DeviceAlreadyInUse` → "Audio interface is in use by another application."

The error is surfaced when the player attempts to start a song, not at launch.

### API convention: standard types over JUCE types in public headers

The `AudioEngine` public header is the isolation boundary. Its callers — including `rock-hero-game`
— should not need JUCE headers just to call it. All new `AudioEngine` public method signatures use
standard C++ types:

| Use case | Type | Not |
|---|---|---|
| File path | `std::filesystem::path` | `juce::File` |
| Text | `std::string` / `std::string_view` | `juce::String` |
| Fallible operation | `std::expected<T, E>` | `juce::Result` |
| Nullable value | `std::optional<T>` | `juce::Optional<T>` |
| Owning collection | `std::vector<T>` | `juce::Array<T>` |
| Non-owning view | `std::span<T>` | `juce::ArrayRef<T>` |

JUCE types remain free to use inside `audio_engine.cpp` and anywhere else that is already
JUCE-coupled for a non-substitutable reason (UI component inheritance, JUCE framework overrides).
See `docs/std-types-plan.md` for the full type audit of existing code.

---

## Data Model

The `tone_timeline_ref` field on `Song` already exists as an opaque serialized blob. The `song`
library never interprets it — that belongs to `rock-hero-audio-engine`. The blob format is defined
here.

### Structs (in `libs/rock-hero-audio-engine`, not `libs/song`)

```cpp
// New header:
// libs/rock-hero-audio-engine/include/rock_hero_audio_engine/tone_timeline.h

namespace rock_hero
{

struct AutomationPoint
{
    double time_seconds;
    float  value;           // normalized [0.0, 1.0] relative to parameter range
    float  curve_tension;   // -1.0 = ease-in, 0.0 = linear, +1.0 = ease-out
};

struct ParameterAutomation
{
    int              plugin_chain_index;
    std::string      param_id;
    std::vector<AutomationPoint> points;
};

struct ToneTimeline
{
    // Ordered list of VST plugin UIDs in the chain (position = chain index).
    std::vector<std::string> plugin_uids;

    // Per-parameter automation curves.
    std::vector<ParameterAutomation> automations;
};

} // namespace rock_hero
```

`ToneTimeline` serializes to/from JSON. The blob stored in `Song::tone_timeline_ref` is this JSON
as a UTF-8 `std::string`. `rock-hero-audio-engine` owns the serialization logic; `song` never sees
the
parsed form.

---

## AudioEngine API Changes

### Playback API (used by both `rock-hero-editor` and `rock-hero-game`)

```cpp
// Open the ASIO input device and route it to the tone track.
// Must be called on the message thread. Call after loadToneTimeline.
std::expected<void, GuitarInputError> enableGuitarInput(int asio_channel);

// Close the ASIO input and stop routing to the tone track.
void disableGuitarInput();

// Deserialize a ToneTimeline blob and rebuild the plugin chain + automation curves.
// Stops playback first if the transport is running.
// Must be called on the message thread.
bool loadToneTimeline(const std::string& blob);
```

Typical call sequence in the game:
```cpp
engine.loadFile(song.audio_asset_ref);
engine.loadToneTimeline(song.tone_timeline_ref);
auto result = engine.enableGuitarInput(asio_channel);
if (!result) { showError(result.error()); return; }
engine.play();
```

### Authoring API (used only by `rock-hero-editor`)

```cpp
// --- Plugin chain management ---

// Scan search_paths for VST3 plugins. Results are cached in the PluginManager.
void scanForPlugins(std::span<const std::filesystem::path> search_paths);

struct PluginInfo
{
    std::string uid;
    std::string name;
    std::string manufacturer;
};
std::vector<PluginInfo> getAvailablePlugins() const;

// Insert a plugin at the end of the chain. Returns chain index, or -1 on failure.
// Pre-activates the plugin before inserting to avoid audio-thread CPU spikes.
int  addPlugin(const std::string& plugin_uid);
void removePlugin(int chain_index);
void movePlugin(int from_index, int to_index);
int  getPluginCount() const;

// --- Parameter introspection and control ---

struct ParamInfo
{
    std::string id;
    std::string name;
    float       min_val;
    float       max_val;
    float       default_val;
};
std::vector<ParamInfo> getPluginParameters(int chain_index) const;
void  setParameterValue(int chain_index, const std::string& param_id, float value);
float getParameterValue(int chain_index, const std::string& param_id) const;

// --- Automation authoring ---

void setParameterAutomation(int chain_index, const std::string& param_id,
                            std::span<const AutomationPoint> points);
std::vector<AutomationPoint> getParameterAutomation(int chain_index,
                                                     const std::string& param_id) const;
void clearAllAutomation();

// --- Persistence ---

// Serialize the current plugin chain and automation into a JSON blob
// suitable for storing in Song::tone_timeline_ref.
std::string serializeToneTimeline() const;
```

---

## Phase 1 — ASIO Input and Live Monitoring

**Goal:** Guitar audio flows through the app in real time. No plugins yet — raw signal only.

### `AudioEngine` constructor

Replace `createSingleTrackEdit` with a manually constructed `Edit` that has two `AudioTrack`s:
- Track 0: backing track (existing behavior unchanged)
- Track 1: tone track (guitar — initially empty, no input assigned)

### `enableGuitarInput` implementation

1. Call `m_engine->getDeviceManager()` to get the ASIO device list.
2. Return `GuitarInputError::NoAsioDeviceFound` if no ASIO device is present.
3. Validate `asio_channel` against device channel count; return `GuitarInputError::ChannelOutOfRange` if out of range.
4. If the device is already exclusively held, return `GuitarInputError::DeviceAlreadyInUse`.
5. Assign the input device to the tone track.
6. Enable monitoring on the track's `InputDeviceInstance` (`setLivePlayEnabled(true)`).
7. Store the active channel index for `disableGuitarInput`.

### `AudioEngine` constructor initialise call

Change `m_engine->getDeviceManager().initialise(0, 2)` to `initialise(num_inputs, 2)` where
`num_inputs` reflects the ASIO device's input count (queried after device enumeration).

---

## Phase 2 — VST Plugin Chain

**Goal:** Guitar signal routes through a configurable chain of VST3 plugins.

### Plugin scanning

`scanForPlugins` uses `te::Engine::getPluginManager()` and `te::ExternalPlugin`'s VST3 format to
scan the provided directories. Results are stored in Tracktion's `KnownPluginList`. This is a
blocking operation and must not be called from the audio thread. The editor triggers it once at
startup (or on user request).

### Chain management

`addPlugin` / `removePlugin` / `movePlugin` all operate on the tone track's `pluginList`.
All mutations stop the transport first (Tracktion requires this to avoid mid-stream graph rebuilds).

**Pre-activation rule:** When adding a plugin, call `plugin->initialiseFully()` on the message
thread before the transport restarts. This forces the plugin to allocate its resources now,
avoiding a CPU spike on the audio thread when the plugin first processes audio.

**Bypass ramp rule:** Never hard-switch plugin bypass. Always use a 5–10ms parameter ramp on the
bypass gain. This matches the constraint documented in `ARCHITECTURE.md`.

---

## Phase 3 — Parameter Automation

**Goal:** Any plugin parameter can be keyframed over the song timeline, but automation data is
only authored, serialized, and displayed for the parameters the user has explicitly chosen to
automate. Playback stays in sync with the transport during both editor preview and game playback.

### Tracktion internals

Each `ExternalPlugin::Parameter` in Tracktion has a `getCurve()` method returning an
`AutomationCurve&`. Keyframes are added via `AutomationCurve::addPoint(TimePosition, float value, float curve_tension)` on the message thread. Tracktion reads the curve on the audio thread during
playback — it owns the synchronization. On seek or loop, Tracktion snaps all parameters to the
correct curve value automatically.

### `setParameterAutomation` implementation

1. Get the plugin at `chain_index` from the tone track's `pluginList`.
2. Find the parameter by `param_id`.
3. Clear any existing curve points.
4. Add each `AutomationPoint` as a Tracktion curve keyframe, mapping `time_seconds` to
   `te::TimePosition::fromSeconds(p.time_seconds)` and `value` to the parameter's normalized range.

### Serialization format

`serializeToneTimeline` produces a JSON blob:

```json
{
  "plugin_uids": ["vst3:SomeAmpSim:uid", "vst3:WahEffect:uid"],
  "automations": [
    {
      "plugin_chain_index": 0,
      "param_id": "Gain",
      "points": [
        { "time_seconds": 0.0, "value": 0.5, "curve_tension": 0.0 },
        { "time_seconds": 30.0, "value": 0.8, "curve_tension": -0.5 }
      ]
    }
  ]
}
```

`loadToneTimeline` parses this blob, calls `addPlugin` for each UID in order, then calls
`setParameterAutomation` for each automation entry. If a plugin UID is not found in the scanned
plugin list, `loadToneTimeline` returns `false` and logs the missing UID.

### Editor UI — Automation Lane Component

An `AutomationLane` JUCE component displays one parameter's curve below the waveform display.
It is in `rock-hero-editor` only and is never seen by the game. The editor can inspect all
automatable parameters exposed by a plugin, but it only creates and shows lanes for parameters the
user has armed for automation.

- Shares the horizontal time axis and zoom/scroll state with `WaveformDisplay`.
- Click on empty area → add keyframe via `setParameterAutomation`.
- Drag a keyframe → update time and value.
- Right-click a keyframe → adjust curve tension or delete.
- Arms/disarms per-parameter (only armed parameters show a lane).
- Reads curve data from `getParameterAutomation`; writes via `setParameterAutomation`.

The component knows nothing about Tracktion internals — it communicates exclusively through the
`AudioEngine` authoring API.

---

## Phase 4 — Tone Blending (Future)

**Goal:** Smoothly blend between two independent plugin chains over the course of a song.

### Architecture options

| Option | Pros | Cons |
|---|---|---|
| Two `AudioTrack`s monitoring the same ASIO input, volume automation on each | Simple, pure Tracktion | Both chains always running; idle chain still uses CPU |
| Tracktion `RackType` with two internal sub-chains and a mix node | Clean encapsulation, one routing point | `RackType` API is less documented; more setup |
| Single chain with per-chain output gain parameters automated | Fits existing single-tone-track model | Custom gain math; slightly non-standard |

**Recommended approach:** Two `AudioTrack`s (Tracks 1 and 2) both assigned the same ASIO input
channel. Output gain on each track is a standard plugin parameter and can be fully automated using
the Phase 3 automation system. Setting Track 1 gain to 1.0 and Track 2 gain to 0.0 at a given
time point, then smoothly sweeping to 0.0 / 1.0, produces a crossfade. If CPU becomes a problem
(two full chains running), migrate to `RackType`.

The `ToneTimeline` data model naturally extends to two chains — `plugin_uids` becomes a list of
chains, and `plugin_chain_index` in `ParameterAutomation` selects which chain a curve belongs to.

---

## Execution Order

```
Phase 1  ASIO input + monitoring         Guitar audio flows through the app
Phase 2  VST plugin chain                Guitar → effects → output
Phase 3  Parameter automation            Tones evolve over the song timeline
Phase 4  Tone blending                   Crossfade between two full chains
```

Each phase is independently shippable and leaves `AudioEngine`'s isolation boundary clean.
