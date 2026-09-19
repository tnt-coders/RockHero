# The Gain block and the meter-only signal-chain panel

*Ruled 2026-09-18. Phase 1 (the meter-only panel) is BUILT; Phase 2 (the built-in Gain block) is
deferred — a third-party gain plugin serves as the balance tool until it exists.*

## The ruling

A tone's level is part of its signal chain and nothing else. The editor carries no per-tone level
as a control outside the chain: the charter balances tones with a gain plugin inserted into the
chain like any other, at any position. The signal-chain panel is **meter, chain, meter** — both
meters global, both full height — and input calibration is reached from the Audio menu, or from
the panel's own "not calibrated" state.

Why this and not the alternatives argued the same day:

- **A per-tone level outside the chain** (shipped as the "Level" slider beside the output meter,
  `6c7d6935`, deleted by Phase 1) was correct data drawn in a lying place. The output meter is
  global — it reads the rig after every tone — and the matched-pair layout beside the global Input
  group made the slider read as global too. A label and a tooltip cannot outvote a preattentive
  layout claim, and moving the slider into a pinned tail cell would still have left one special
  control that is not a plugin.
- **A global output gain saved in the arrangement** matches the layout but removes the per-tone
  balance the charter needs, and a survey showed it touches every reader of the switch result, the
  tone designer handlers, two snapshot fields, a dozen tests and both formats.
- **The Gain block** is one mechanism for every level change, at the head of a chain (pad or push
  into a pedal) or at its tail (balance). The level is visible exactly where it acts, as a block
  with its value, and travels in the tone file because the chain is the tone file. Until it is
  built, any third-party gain plugin plays the same role — which is why the panel could go first.

What stays ruled from the same day: a tone switch is a branch-gain crossfade between always-loaded
branches and nothing else (`167a61a2`); input gain is global device calibration in user config; the
game's player-monitor volume is its own post-rack stage (`setMonitorGain`) and never chart data;
there is no per-tone input gain.

## Phase 1 — the meter-only panel (BUILT)

The order reversed on the day the plan was executed: the charter already owns a third-party gain
plugin, so nothing was lost by deleting the level control before the built-in block existed, and
the panel stopped lying a phase earlier.

**Deleted** — the record of what went:

- The per-tone level as a concept outside the chain: `ILiveRig::outputGain`/`setOutputGain`, the
  `output_gain` field on `LiveRigSnapshot`, `LiveRigLoadResult` and `AudibleToneState`, the level on
  `ToneBranchGainPlugin` (`6c7d6935`) — the branch plugin is back to pure 0..1 audibility, one
  smoother on the branch gain alone — `Engine::Impl::audibleBranchGain`, and the
  `swapAudibleChainPlugins` gain argument.
- The tone document key `slots[0].outputGainDb` — one key, carried by the in-package tone and the
  `.tone` file alike, which share one document shape. A format change in place with no legacy
  reader: the key is simply not read or written, and the one existing project re-balances with gain
  plugins.
- In the editor: the Level slider, `OutputGainSliderLookAndFeel`, the value box, the tooltip and
  the `SharedResourcePointer<TooltipWindow>` that existed for it, the
  `onOutputGainPreviewChanged`/`onOutputGainChanged` listener pair and their controller entry
  points, the gesture undo (`pushOutputGainUndoEntry`, `applyOutputGainChange`, `OutputGainEdit`,
  `applyOutputGainEdit`, the "Set Output Gain to" label), `output_gain`/
  `output_gain_controls_enabled` on `SignalChainViewState`, `m_output_gain_db` and its preview
  latch, `EditorEditContext::output_gain_db`, the readers in `tone_designer_handlers.cpp`, and the
  fader-follow compare in `syncAudibleTone` — a switch carries no level.
- The Calibrate button under the input meter, and `test_editor_controller_output_gain.cpp`.

**Kept:** both meters, now sharing top and bottom baselines and running the panel's whole free
height; the "Input" and "Output" captions, both now true; the input stage and the post-rack monitor
stage untouched.

**Layout.** Input group = caption + meter; chain row; Output group = caption + meter. Both end
groups take one width, `g_meter_group_width` (48 px), off opposite edges below the same header
band, so their meters share one pair of baselines by construction. The 20 px value box (under the
24 px target floor) went with the sliders.

**Calibration** is reached two ways, both following the one `input_calibrate_enabled` flag: the
Audio menu's **Calibrate Input...** command (`EditorCommandId::CalibrateInput`, `0x1C01`, no
default chord; the menu bar gained an Audio menu and `Alt+A` with it), and a button inside the
panel's disabled state, shown only where calibration is what would fix it.

## Phase 2 — the Gain block (deferred)

One built-in plugin, insertable from the plugin picker. This is not a built-in plugin framework:
exactly one type, and every seam it opens is opened for it alone until a second built-in exists.

**What exists.** `LiveRigGainPlugin` (`rock-hero-common/audio/src/tracktion/live_rig_gain_plugin.*`)
is already a Tracktion plugin doing stereo dB gain with a message-thread `setGain` and an atomic
audio-thread target. It is registered only through `RockHeroEngineBehavior::createCustomPlugin`
(`engine_behaviors.cpp`), a state-restore hook, never in the plugin catalog.

**What is missing, seam by seam** (facts as of `6c7d6935`; re-verify before building):

1. **Catalog.** `knownPluginCatalog()` (`engine_plugin_host.cpp`) emits one candidate per
   scanned `juce::PluginDescription` and nothing else; insertion instantiates only
   `tracktion::ExternalPlugin` and refuses a candidate without a description. The built-in needs a
   catalog entry that is not a scan result and an insert path that creates it through the engine's
   own factory.
2. **Record shape.** `PluginRecord` (`tone_document.h`) carries scanner identity plus a REQUIRED
   `tracktion_state_ref` sidecar; `PluginChainEntry` (`plugin_chain_snapshot.h`) is scanner-shaped
   too. Neither has a built-in-vs-external axis. Ruling for the design: a chain entry is a sum type
   — external (scanner identity + state sidecar) or built-in (type name + its state) — spelled as
   one discriminated record, not as optional fields that must agree by hand. Whether a built-in's
   state also goes through the sidecar mechanism (its ValueTree serialises like any plugin's) or
   inline is the one format question to settle first; lean sidecar, because then capture, restore
   and undo stay one code path.
3. **Capture and restore.** `captureActiveRig` refuses any non-`ExternalPlugin` in a branch chain
   (`engine_live_rig.cpp`, "Only external plugins can be captured right now"), and
   `isStructuralLiveRigPlugin` knows structural plugins by four fixed ids, so a fifth
   `LiveRigGainPlugin` instance would be swept as a user plugin. Capture learns to persist a
   built-in's state; the structural test stays id-based (a chain Gain block is a user plugin and
   SHOULD be swept).
4. **Rack admission.** `LiveRigGainPlugin::canBeAddedToRack()` returns false
   (`live_rig_gain_plugin.cpp`); branch chains live inside the rack. Either the flag flips for the
   chain use or the chain Gain block is a sibling class sharing the DSP — decide by reading why the
   flag was set (probably to keep the structural stages out of racks), and prefer one class.
5. **Editing surface.** External plugins open their own windows. The Gain block needs one small
   window: a dB control with type-to-set and drag, gesture-scoped undo through the existing
   plugin-state memento path (full-state restore, `project_undo_fidelity_nonnegotiable`). No knob;
   a horizontal bar readout.
6. **Tile.** The chain row draws the block like any plugin tile with a name ("Gain") and its value
   in the caption band, so the level is readable without opening it. Display-type overrides
   (`BranchDisplayMetadata`) apply as to any block.
7. **Game.** Nothing: a Gain block in a branch chain renders like any plugin.

**Tests.** Catalog lists the built-in; insert creates it in the audible branch at the chosen slot;
save/load round-trips a chain containing it with its dB; undo of a value edit restores through the
memento path; the game loads a tone containing one; a tone file exports and imports it.

**Sighting.** Insert Gain at the head of a chain and at the tail; set −6 dB on the tail block; the
tile shows the value; the tone file round-trips it; the game plays the tone at that level.

## Open questions

1. Built-in state through the sidecar or inline in the record (lean sidecar).
2. One class with `canBeAddedToRack` flipped, or a chain sibling sharing the DSP (lean one class).
3. Tile value formatting for a block whose whole state is one number (caption "Gain · −6.0 dB"?).

## Relation to other records

- `docs/plans/in-progress/tone-active-vs-selected.md` — the active tone the panel binds to;
  unchanged.
- `docs/plans/roadmap/21-game-audio-engine-and-session.md` — the monitor stage; unchanged by this
  plan.
- `docs/tracking/backlog.md` — the float-precision note on the branch level (`6c7d6935`) was
  retired by Phase 1, which deleted that store.
