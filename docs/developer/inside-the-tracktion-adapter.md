\page guide_tracktion_adapter Inside the Tracktion Adapter

*Applies to: Repo-wide — everything here sits behind the `Engine`, which both products consume.*

`rock-hero-common/audio/src/tracktion/` is where the project actually touches Tracktion Engine:
every class that subclasses or plugs into a Tracktion/JUCE extension point lives here as its own
named unit, composed by the engine's per-port translation units. Nothing in this folder appears
in any public header — if you are reading this while working outside `common/audio`, these
classes are implementation detail you reach only through the ports. This page is the map of what
each unit is for and the design ideas that repeat across them.

# The audible graph: one rack, many tone branches

`multi_tone_rack.{h,cpp}` builds the live rig's processing graph as a **tone rack**: one parallel
branch per tone in the song — each branch holding that tone's plugin chain and a hidden
`ToneBranchGainPlugin` (`tone_branch_gain_plugin.h`, an automatable per-branch gain) — summed to
one rack output placed on the instrument track. Switching tones is `setAudibleBranch(...)`: a
click-free, per-sample-smoothed gain crossfade, not a graph rebuild. Two properties matter when
extending it:

- **Adding an empty branch is cheap by design** (`addEmptyToneBranch`): it is a ValueTree edit
  Tracktion coalesces into one async rebuild that reuses live plugin instances — playback never
  stops.
- The editor edits **only the audible branch's chain** (`insertIntoBranch`, `removeFromBranch`,
  `moveWithinBranch`); the chain snapshot the UI renders is exactly that branch.

`live_rig_gain_plugin.h` is the same hidden-plugin idea applied to the rig's input/output gain
stages: a private `tracktion::Plugin` that cannot be moved or added to racks, applying a
smoothed `common::audio::Gain`. Hidden structural plugins like these are excluded from the
user-visible `chain_index`.

# Plugin hygiene and undo capture

Three units keep hosted plugins honest:

- `plugin_dirty_tracking.{h,cpp}` — the **gesture gate** described in \ref guide_undo —
  parameter changes mark dirty, only GUI gestures mark user intent, and only intent-carrying
  settled transactions become undo edits.
- `plugin_state_hygiene.{h,cpp}` — strips derived automation curves and stale tempo-remap flags
  out of plugin state trees before they persist, so saved tone documents carry only authored
  truth (derived state is rebuilt on load — the same derived-over-authored rule the song format
  follows).
- `plugin_window.{h,cpp}` — the plugin editor window wrapper: within-session bounds restore, the
  Windows message hook, and the key-forwarding policy (Ctrl+Z/Y always to Rock Hero's undo;
  Space yields to plugin text fields). See \ref guide_signal_chain.

# Timing and automation bridges

- `tempo_mirror.{h,cpp}` — the **one-way** projection of RockHero's `TempoMap` into the edit's
  tempo sequence so hosted plugins read host tempo; never read back (see
  \ref guide_musical_time).
- `tone_automation_curve.{h,cpp}` — reads and writes tone-chain parameter automation curves on
  the Tracktion side; the musical truth lives in `song.json`, and the Tracktion curve is derived
  from it.

*Design in flux: tone parameter automation is under active development
(`docs/plans/in-progress/tone-parameter-automation-plan.md`) — treat `tone_automation_curve`'s
surface as moving.*

# Device and framework glue

- `engine_behaviors.{h,cpp}` — Rock Hero's `EngineBehaviour`/`UIBehaviour` customization points,
  including pinning Tracktion's beat length to quarter notes (which the tempo mirror relies on)
  and describing the single instrument input / stereo output Rock Hero exposes.
- `tracktion_instrument_wave_device_mapping.{h,cpp}` — pure, testable mapping from JUCE active
  channel masks to Tracktion wave-device descriptions (which physical input channel becomes "the
  instrument").
- `monitoring_mode_transition.{h,cpp}` — pure, testable policy for the mutually-exclusive live
  and calibration monitoring toggles.
- `tracktion_thumbnail.{h,cpp}` — the `IThumbnail` adapter over `tracktion::SmartThumbnail`
  that draws the editor's waveform (see \ref guide_2d_views).

# The pattern to copy

Notice what the small units above have in common: wherever a decision could be expressed as pure
data-in/data-out (`monitoring_mode_transition`, `tracktion_instrument_wave_device_mapping`), it
is a framework-free function unit with its own tests, even though it lives in the adapter layer —
only the residue that must touch Tracktion objects stays framework-bound. When you add adapter
behavior, split it the same way, and give any new framework subclass its own named unit here
rather than defining it inline in an engine translation unit (the rule is
"Framework-Adapter Units" in \ref design_architectural_principles).

Silent steps when adding an adapter unit: the `.h`/`.cpp` pair in this folder, the
`target_sources` entry in `rock-hero-common/audio/CMakeLists.txt`, composition from the right
`engine_*.cpp` slice, and — if the pure-policy split applies — a headless test in
`rock-hero-common/audio/tests/`.
