# The Gain block and the meter-only signal-chain panel

*Phase 2 (the meter-only panel) is superseded 2026-10-02 by
`docs/plans/completed/tone-output-pan-knob.md` (built the same day): the per-tone level stays
beside the output meter and gains a pan knob above it. Phase 1, the Gain block, stands, and is
ruled to be the same plugin type as the tone's output stage.*

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

Phase 1 is now delivered as a hosted JUCE processor from our own plugin repo, ruled 2026-10-02 in
`bundled-gain-plugin.md` (R1-R8): no built-in type and none of the seams below are built. From
here to the Tests paragraph is the record of route (A), a Tracktion built-in, kept because the
rulings about the SLOT (the stage's position, store, id and UI) still hold and because the
comparison in that plan rests on these seams.

**Ruled 2026-10-02: the tone's output stage and the Gain block are ONE plugin type.**
`ToneOutputPlugin` (`rock-hero-common/audio/src/tracktion/tone_output_plugin.h`, built in
`31cbed52`: level and pan as unattached `AutomatableParameter`s, the balanced pan law, a 5 ms
de-zipper) is the Gain block's implementation already. The output stage is the instance the rack
places at its fixed slot (`multi_tone_rack.cpp`, `addBranchStages`); a Gain block is an instance the
charter inserts into a chain. Everything that differs between them is a property of the SLOT and
belongs to the slot's owner, never to a flag on the type:

- *Position and removability.* The rack adapter holds the stage in `ToneRackBranch::output` and
  chain blocks in `ToneRackBranch::chain`. Nothing in Tracktion or in this tree consults
  `canBeMoved()` (`rg` over `external/tracktion_engine/modules` and the tree, 2026-10-02); the
  overrides on all three private plugins were deleted that day, and `canBeAddedToRack()` is the
  base default `true` (`tracktion_Plugin.h:290`), so the stage's type admits itself to a rack
  exactly as a chain block must.
- *The authored store.* The stage's value is the tone document's `outputGainDb`/`outputPan`,
  applied through `setOutput`; a block's value is its state sidecar, like any chain plugin's.
- *The durable automation id.* The stage's is derived (`toneOutputPluginId`, `<ref>#output`); a
  block's is minted like any chain plugin's. The two never collide (a minted id is a UUID).
- *The UI.* The stage is the panel's Output fader and pan knob; a block is a tile and a small
  editor window.

*Narrowed the same day to gain only (one knob with a typed dB box; the stage keeps level and
pan), then superseded by the hosted route in `bundled-gain-plugin.md`. The one-type ruling above
is void; the slot bullets stand.*

Nothing shipped in `31cbed52` has to be undone: the derived id and the picker's "Output" submenu
describe the stage's slot, not the type. The class and its `xmlTypeName` take the one gain type's
name when the second instance arrives; the type string lives only in memory (the edit is created
and never saved or reloaded, `engine.cpp` `createEdit`), so that rename is free.

**What the clean shape requires, seam by seam** (facts as of `31cbed52`; re-verify before
building):

1. **State-backed values.** The plugin's ValueTree carries level and pan, written ONLY by
   `setOutput` (the parameters stay unattached, so a playing curve never writes back —
   `tracktion_AutomatableParameter.cpp:1404-1408`), and the constructor seeds from state. Today
   `createState()` carries only the type and the values live on the parameters' explicit values
   alone; that is enough for the stage, whose store is the tone document, but a chain block
   persists through its sidecar, and the full-state undo memento (`capturePluginState`) is that
   same ValueTree. This answers the sidecar-or-inline question: sidecar.
2. **Catalog and record shape.** `PluginCandidate` (`i_plugin_host.h`), `PluginRecord`
   (`tone_document.h`) and `PluginChainEntry` (`plugin_chain_snapshot.h`) are scanner-shaped. A
   built-in needs one discriminator: a sum type, external (scanner identity + sidecar) or built-in
   (type name + sidecar), never optional fields that must agree by hand. `knownPluginCatalog()`
   (`engine_plugin_host.cpp:344`) lists the built-in once, not from a scan;
   `insertPluginCandidateToTrack` (`:890`) creates it through
   `createNewPlugin(ToneOutputPlugin::createState())` where it creates an `ExternalPlugin` today
   (`:957`).
3. **Capture, export, restore.** Six refusals demand an `ExternalPlugin`: `engine_live_rig.cpp:770`
   (capture), `:1535` (export), `:1592` (audible state); `engine_plugin_host.cpp:776` (the edit
   observer), `:1216` (state capture), `:1400` (state restore). A built-in's captured state is its
   ValueTree as-is (`flushPluginStateToValueTree` is external-only); each generalises to "a plugin
   whose state the sidecar carries". `isStructuralLiveRigPlugin` (`:108`) stays id-based: a chain
   block is a user plugin and SHOULD be swept.
4. **Editor window.** `Plugin::createEditor()` returns nothing by default
   (`tracktion_Plugin.h:386`), so `PluginWindow` would open empty; the type overrides it with a
   fader-and-knob component. `OutputStageLookAndFeel` (`signal_chain_view.cpp`) draws those
   controls for the panel today in `editor/ui`, while the window content is created in
   `common/audio` (`plugin_window.cpp`); the drawing moves to `common/ui` so both surfaces draw
   one control. Gesture-scoped undo goes through the plugin-state memento path (full-state
   restore, `project_undo_fidelity_nonnegotiable`).
5. **Name.** `getName()` returns "Output", which the automation picker's submenu and the lane
   labels show. A chain instance is "Gain". Tracktion's base has no per-instance name
   (`getName()` is pure virtual), so either the state carries the name or the listing seam
   (`engine_tone_automation.cpp:74`, which already lists the stage last) labels the stage. Decide
   when building; lean the state, since the tile and the window show the same name.
6. **Tile.** The chain row draws the block like any plugin tile with its name and its value in the
   caption band, so the level is readable without opening it. Display-type overrides
   (`BranchDisplayMetadata`) apply as to any block.
7. **Lanes and the game.** Free: `listPluginAutomatableParameters` and `findBranchPlugin` are
   generic over `tracktion::Plugin`, and the game plays the rack.
8. **Then fold `LiveRigGainPlugin` in.** The input and monitor stages
   (`live_rig_gain_plugin.*`) are the same rule — a smoothed dB level — on a type with no
   automatable parameter, an atomic target, a 20 ms ramp and `canBeAddedToRack() == false`. Once
   the one gain type exists, both stages become instances of it (pan at centre), deleting a class,
   its tests and the third restatement of the smoothing and apply loop. Two decisions for the user
   first: the ramp (the stage's 5 ms must stay under the 10 ms crossfade; the input and monitor
   stages have no such bound and ramp 20 ms today), and automation writing — today no RockHero
   parameter can be recorded only because every automatable parameter lives in the rack, where
   `getAutomationMode` answers `read` (`tracktion_AutomatableParameter.cpp:1662-1672`) while the
   transport's `automationWrite` defaults to true (`tracktion_AutomationRecordManager.cpp:80`); a
   track-level automatable stage would be the first outside that guarantee, so the fold states the
   policy with `setWritingAutomation(false)` at `createEdit`.

### What Phase 1 builds now (2026-10-02)

The hosted route, steps 0-6 under "First deliverable" in `bundled-gain-plugin.md`: the plugin
repo's `GainProcessor` and its knob editor, the `RockHero` `juce::AudioPluginFormat` in
`common/audio/src/tracktion/`, its registration, and the catalog constant. Tracktion's
`ExternalPlugin` is the adapter, already written: parameters, state, editor, lanes and undo all
come for free. The identity authority, the generalised refusals and the built-in editor of route
(A) are withdrawn with it. Size: about two days plus the repository bootstrap.

**Tests.** Catalog lists Gain with format "RockHero" and no file; insert creates it in the audible
branch at the chosen slot as an `ExternalPlugin` holding a `GainProcessor`;
save/load round-trips a chain containing it with its dB; undo of a value edit restores through the
memento path; the game loads a tone containing one; a tone file exports and imports it.

**Sighting.** Insert Gain at the head of a chain and at the tail; set −6 dB on the tail block by
dragging the knob, then by typing; the window shows the value; the tone file round-trips it; the
game plays the tone at that level.

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

1. ~~Built-in state through the sidecar or inline in the record.~~ Answered 2026-10-02: sidecar,
   with the values state-backed (Phase 1, item 1).
2. ~~One class or a chain sibling sharing the DSP.~~ Answered 2026-10-02: one type, the output
   stage's (Phase 1 ruling); rack admission is the base default.
3. Whether the input gain slider survives beside the input meter once Calibrate is gone.
4. Tile value formatting for a block whose whole state is one number (caption "Gain · −6.0 dB"?).

## Relation to other records

- `docs/plans/completed/tone-active-vs-selected.md` — the active tone the panel binds to;
  unchanged.
- `docs/plans/roadmap/21-game-audio-engine-and-session.md` — the monitor stage; unchanged by this
  plan.
- `docs/tracking/backlog.md` — the float-precision note on the branch level (`6c7d6935`) is retired
  by Phase 2, which deletes that store.
