# The Gain block and the meter-only signal-chain panel

*Parked 2026-09-18: the per-tone output level stands (`6c7d6935`); this direction was built as
`d16429c8` and reverted the same day. Re-verify against the code before reopening.*

*Ruled 2026-09-18. Queued behind the harmonic display follow-ups
(`harmonic-display-followups.md`); nothing here is built. Two phases, each sighted before the next.*

## The ruling

A tone's level is part of its signal chain and nothing else. The editor stops carrying a per-tone
level as a control outside the chain: the charter balances tones with a **Gain block**, a built-in
plugin inserted into the chain like any other, at any position. The signal-chain panel becomes
**meter, chain, meter** — both meters global, both full height — and input calibration moves to
settings, reachable from the panel only through the existing "not calibrated" state.

Why this and not the alternatives argued the same day:

- **A per-tone level outside the chain** (shipped today as the "Level" slider beside the output
  meter, `6c7d6935`) is correct data drawn in a lying place. The output meter is global — it reads
  the rig after every tone — and the matched-pair layout beside the global Input group makes the
  slider read as global too. A label and a tooltip cannot outvote a preattentive layout claim, and
  moving the slider into a pinned tail cell still leaves one special control that is not a plugin.
- **A global output gain saved in the arrangement** matches the layout but removes the per-tone
  balance the charter needs, and a survey showed it touches every reader of the switch result, the
  tone designer handlers, two snapshot fields, a dozen tests and both formats.
- **The Gain block** is one mechanism for every level change, at the head of a chain (pad or push
  into a pedal) or at its tail (balance). The level is visible exactly where it acts, as a block
  with its value, and travels in the tone file because the chain is the tone file.

What stays ruled from the same day: a tone switch is a branch-gain crossfade between always-loaded
branches and nothing else (`167a61a2`); input gain is global device calibration in user config; the
game's player-monitor volume is its own post-rack stage (`setMonitorGain`) and never chart data;
there is no per-tone input gain.

## Phase 1 — the Gain block

One built-in plugin, insertable from the plugin picker. This is not a built-in plugin framework:
exactly one type, and every seam it opens is opened for it alone until a second built-in exists.

**What exists.** `LiveRigGainPlugin` (`rock-hero-common/audio/src/tracktion/live_rig_gain_plugin.*`)
is already a Tracktion plugin doing stereo dB gain with a message-thread `setGain` and an atomic
audio-thread target. It is registered only through `RockHeroEngineBehavior::createCustomPlugin`
(`engine_behaviors.cpp:101-116`), a state-restore hook, never in the plugin catalog.

**What is missing, seam by seam** (facts as of `6c7d6935`; re-verify before building):

1. **Catalog.** `knownPluginCatalog()` (`engine_plugin_host.cpp:344-380`) emits one candidate per
   scanned `juce::PluginDescription` and nothing else; insertion (`:956-984`) instantiates only
   `tracktion::ExternalPlugin` and refuses a candidate without a description (`:897-907`). The
   built-in needs a catalog entry that is not a scan result and an insert path that creates it
   through the engine's own factory.
2. **Record shape.** `PluginRecord` (`tone_document.h:25-59`) carries scanner identity plus a
   REQUIRED `tracktion_state_ref` sidecar; `PluginChainEntry` (`plugin_chain_snapshot.h:22-63`) is
   scanner-shaped too. Neither has a built-in-vs-external axis. Ruling for the design: a chain entry
   is a sum type — external (scanner identity + state sidecar) or built-in (type name + its state)
   — spelled as one discriminated record, not as optional fields that must agree by hand. Whether a
   built-in's state also goes through the sidecar mechanism (its ValueTree serialises like any
   plugin's) or inline is the one format question to settle first; lean sidecar, because then
   capture, restore and undo stay one code path.
3. **Capture and restore.** `captureActiveRig` refuses any non-`ExternalPlugin` in a branch chain
   (`engine_live_rig.cpp:765-775`, "Only external plugins can be captured right now"), and
   `isStructuralLiveRigPlugin` (`:107-115`) knows structural plugins by four fixed ids, so a fifth
   `LiveRigGainPlugin` instance would be swept as a user plugin. Capture learns to persist a
   built-in's state; the structural test stays id-based (a chain Gain block is a user plugin and
   SHOULD be swept).
4. **Rack admission.** `LiveRigGainPlugin::canBeAddedToRack()` returns false
   (`live_rig_gain_plugin.cpp:88-91`); branch chains live inside the rack. Either the flag flips
   for the chain use or the chain Gain block is a sibling class sharing the DSP — decide by reading
   why the flag was set (probably to keep the structural stages out of racks), and prefer one class.
5. **Editing surface.** External plugins open their own windows. The Gain block needs one small
   window: a dB control with type-to-set and drag, gesture-scoped undo through the existing
   plugin-state memento path (full-state restore, `project_undo_fidelity_nonnegotiable`). No knob;
   a horizontal bar readout, the same shape ruled for the tail cell that this plan supersedes.
6. **Tile.** The chain row draws the block like any plugin tile with a name ("Gain") and its value
   in the caption band, so the level is readable without opening it. Display-type overrides
   (`BranchDisplayMetadata`) apply as to any block.
7. **Game.** Nothing: a Gain block in a branch chain renders like any plugin.

**Deletions in Phase 1.** None yet; the block lands beside the existing Level slider so it can be
sighted against it.

**Tests.** Catalog lists the built-in; insert creates it in the audible branch at the chosen slot;
save/load round-trips a chain containing it with its dB; undo of a value edit restores through the
memento path; the game loads a tone containing one; a tone file exports and imports it.

**Sighting.** Insert Gain at the head of a chain and at the tail; set −6 dB on the tail block; the
tile shows the value; the tone file round-trips it; the game plays the tone at that level.

## Phase 2 — the meter-only panel

Depends on Phase 1 being sighted, because it removes the only other way to set a tone's level.

**Deleted:**

- The per-tone level as a concept outside the chain: `ILiveRig::outputGain`/`setOutputGain`, the
  `output_gain` field on `LiveRigSnapshot`, `LiveRigLoadResult` and `AudibleToneState`, the level on
  `ToneBranchGainPlugin` (`6c7d6935`) — the branch plugin returns to pure 0..1 audibility — and the
  `swapAudibleChainPlugins` gain argument.
- The tone document key `slots[0].outputGainDb` — one key, carried by the in-package tone and the
  `.tone` file alike, which share one document shape (format change in place, no legacy reader; the
  one existing project re-balances with Gain blocks). Update `docs/developer/file-formats.md` and
  `docs/plans/roadmap/50-tone-designer-and-tone-files.md` (`:19`, `:182`: a tone is chain + plugin
  state).
- In the editor: the Level slider, `OutputGainSliderLookAndFeel`, the value box, the
  `onOutputGainPreviewChanged`/`onOutputGainChanged` listener pair, the gesture undo
  (`signal_chain_handlers.cpp:116-198`, `signal_chain_edits.cpp:408-425`, the "Set Output Gain"
  label), `output_gain`/`output_gain_controls_enabled` on `SignalChainViewState`, the readers in
  `tone_designer_handlers.cpp` (`:80`, `:162`, `:678`), and the tests that pin them
  (`test_editor_controller_output_gain.cpp` and the output-gain cases in the sections, engine and
  tone-file suites).
- The Calibrate button under the input meter. Calibration lives in the audio settings; the panel's
  existing "not calibrated" disabled state gains a button that opens it.

**Kept:** both meters, now sharing top and bottom baselines and extending through the freed space;
the "Input" and "Output" captions, both now true; the input gain slider only if the meter group
still needs a device-level control beside the meter — lean remove, since calibration owns it.

**Layout.** Input group = caption, meter, (input gain?); chain row; Output group = caption, meter.
Both end groups the same width. The 20 px value box (under the 24 px target floor) disappears with
the sliders. Re-check `g_output_gain_width` and the input group width so the two are one constant.

**Tests.** The signal-chain view state has no level; the panel binds a chain containing a Gain block
and shows its value on the tile; the disabled state exposes the calibration action; engine and game
suites lose the level cases and gain "a switch moves only branch gains".

**Sighting.** Panel at two widths (chain fits; chain scrolls) with a four-plugin chain including a
tail Gain block: the two meters read the same dBFS at the same height; nothing outside the chain
edits the tone; the uncalibrated state offers calibration in place.

## Open questions

1. Built-in state through the sidecar or inline in the record (lean sidecar).
2. One class with `canBeAddedToRack` flipped, or a chain sibling sharing the DSP (lean one class).
3. Whether the input gain slider survives beside the input meter once Calibrate is gone.
4. Tile value formatting for a block whose whole state is one number (caption "Gain · −6.0 dB"?).

## Relation to other records

- `docs/plans/completed/tone-active-vs-selected.md` — the active tone the panel binds to;
  unchanged.
- `docs/plans/roadmap/21-game-audio-engine-and-session.md` — the monitor stage; unchanged by this
  plan.
- `docs/tracking/backlog.md` — the float-precision note on the branch level (`6c7d6935`) is retired
  by Phase 2, which deletes that store.
