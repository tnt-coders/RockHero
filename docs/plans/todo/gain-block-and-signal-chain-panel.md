# The Gain block and the meter-only signal-chain panel

*Phase 2 (the meter-only panel) is superseded 2026-10-02 by
`docs/plans/completed/tone-output-pan-knob.md` (built the same day): the per-tone level stays
beside the output meter and gains a pan knob above it. Phase 1, the Gain block, is delivered by
`docs/plans/todo/rock-hero-plugins-and-gain.md`.*

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

Delivered by `docs/plans/todo/rock-hero-plugins-and-gain.md`: a plain JUCE processor in the
`rock-hero-plugins/gain` scope, hosted compiled-in through Tracktion's `ExternalPlugin` by a
RockHero `juce::AudioPluginFormat`. No built-in type, no Gain-specific code in RockHero, no
format change. The earlier one-type ruling and the eight seams of a Tracktion built-in are
withdrawn (git history, 2026-10-02, keeps the record). Phase 2 depends on that plan's sighting:
Gain at the head and the tail of a chain, −6 dB by drag and by typing, the tone file
round-tripping it, the game playing at that level.

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

1. ~~Built-in state through the sidecar or inline in the record.~~ Answered 2026-10-02: the
   sidecar, as for any hosted plugin (`rock-hero-plugins-and-gain.md`).
2. ~~One class or a chain sibling sharing the DSP.~~ Answered 2026-10-02: neither; a hosted JUCE
   processor, the output stage staying built-in (`rock-hero-plugins-and-gain.md`).
3. Whether the input gain slider survives beside the input meter once Calibrate is gone.
4. Tile value formatting for a block whose whole state is one number (caption "Gain · −6.0 dB"?).

## Relation to other records

- `docs/plans/completed/tone-active-vs-selected.md` — the active tone the panel binds to;
  unchanged.
- `docs/plans/roadmap/21-game-audio-engine-and-session.md` — the monitor stage; unchanged by this
  plan.
- `docs/tracking/backlog.md` — the float-precision note on the branch level (`6c7d6935`) is retired
  by Phase 2, which deletes that store.
