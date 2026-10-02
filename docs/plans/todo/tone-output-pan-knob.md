# The tone's output stage: level and a pan knob

Status: **DEFERRED. Designed 2026-10-02 with the user, and the layout was sighted on a render
(the knob above the fader, the panel at its tall height). Nothing is built.** Re-verify every
`file:line` below against the code before executing; they were read on 2026-10-02.

The Volume and Pan automation lanes live in `tone-parallel-paths-and-mix-lanes.md`. This plan
builds the static values those lanes anchor to, and the control that sets pan.

## Rulings

- **The output stage is one value per tone:** `ToneOutput { Gain level; Pan pan; }`. It has one
  setter, one undo edit and one preview/commit pair: the existing output-gain pair
  (`signal_chain_view.h:94-100`, `signal_chain_handlers.cpp` ~148-197), generalized, never copied
  for pan.
- **Pan is a balance with unity at centre** (`juce::dsp::PannerRule::balanced`).
  - The rig is stereo throughout, and the chain can turn stereo after a cab or reverb. A balance
    places a dual-mono signal exactly like a pan, and it keeps a stereo signal's width, where a
    true panner would collapse it.
  - Unity at centre means adding a Pan lane never changes the level.
- **One value format,** `C` / `L35` / `R100`, from a formatter and parser pair beside `gain.h` in
  `rock-hero-common/audio/shared/`. The knob and the lane both use it.
- **The controls show the stored value,** which is the lane's anchor (the lane-anchor law plugin
  knobs already follow), and never the value at the playhead.
- **The layout: the pan knob goes directly above the Output fader,** in the fader's 32 px lane.
  - Its top aligns with the meter's top. The "Output" title, the meter and the dB box do not move,
    so both meters stay full height and aligned.
  - The fader loses about 30 px of travel: ~104 → ~72 px at the 1280×720 minimum, and ~148 →
    ~117 px at the tall panel. The typed dB box and Ctrl-drag cover precision.
- **Set aside the same day:**
  - level and pan in the panel's title band;
  - a Tone menu replacing the header's file buttons;
  - one open document with merged File verbs;
  - a separate tone-designer app;
  - widening the side columns.

  The user prefers the header's context-switching file buttons as they are, and this is the
  smallest change that adds pan.

## The knob

- `juce::Slider`, `RotaryHorizontalVerticalDrag`, 28 px, −100..100 step 1, double-click → C.
- Scroll wheel off, so an idle wheel over the panel never writes an undo entry.
- **No text box: the lane has no room for one.** The value shows in JUCE's popup while dragging
  (`setPopupDisplayEnabled`) and in the tooltip, through the shared formatter. Pan cannot be typed;
  the user accepted that.
- **It needs custom drawing.** Stock `LookAndFeel_V4::drawRotarySlider` insets its bounds by
  10 px (a 28 px knob draws as an 8 px disc) and fills from the start angle, so C would paint
  half-full. Draw a dark body, a muted track over the ±135° sweep, and an `accent` arc from
  12 o'clock to the value, with a white pointer. The sighting render used exactly this.
  - Extend the existing `OutputGainSliderLookAndFeel` (`signal_chain_view.cpp:100-129`) rather
    than adding a second one.
  - It also needs to know the fader now starts below the knob. Today it aligns the fader's track
    to the meter's full height (`:115-120`).
- The knob shares the Output fader's enabled flag.

## Phases

1. **The tone output stage (audio and model).**
   - `ToneOutput` replaces `output_gain` wherever the gain travels: the live-rig snapshot, the
     load result and the audible state (`i_live_rig.h:76,183,250`), plus `ILiveRig::setOutputGain`,
     the tone document's `outputGainDb` (`tone_document.cpp:32,229`), `OutputGainEdit`
     (`signal_chain_edits.h:236`) and the view state.
   - Split a `ToneOutputPlugin` (level + pan) out of `ToneBranchGainPlugin`
     (`rock-hero-common/audio/src/tracktion/tone_branch_gain_plugin.h`), which keeps only the
     tone-switch crossfade. A Volume lane then can never reach the crossfade. Gains in series
     multiply, so a switch still follows the tone's level. Wire it in `multi_tone_rack.cpp`
     (~198, ~266).
   - Make level and pan real `AutomatableParameter`s, persisted from their explicit value and not
     an attached `CachedValue`: a playing curve writes back into an attached value
     (`tracktion_AutomatableParameter.cpp` ~1404-1408).
   - Not `VolumeAndPanPlugin`: it takes a lock on the audio thread
     (`tracktion_VolumeAndPan.cpp` ~200), and its volume tops out at +6 dB.
   - **FORMAT:** the tone document gains pan.
   - **NEEDS VERIFICATION FIRST:** does the mono guitar input reach the stereo rack as L = R, or
     left only? If left only, hard right would be silent under any pan law.
2. **The knob.** As above, plus tests for the preview/commit pair, double-click to centre, and the
   formatter round trip.

## Small fixes to take along

- The muted "disabled" sub-caption under "Input" (`signal_chain_view.cpp:403-422`) pushes the
  title off centre against "Output" whenever live input is off. Keep the word, but stop it moving
  the title.
- The output meter is a single bar, so a hard pan shows no visible change in the panel. Accept
  that for now; a stereo meter is a separate decision.

## Open

1. An Audio menu with "Audio Device Settings…" and "Calibrate Input…". It is not needed while the
   Calibrate button stays, but it would make both reachable from the keyboard.
2. The Ctrl+S gap: in the Tone Designer, Ctrl+S belongs to the disabled Save Project, and Save Tone
   has no shortcut. This already exists today; it belongs in the backlog unless taken up here.

## Sighting checks

- At the 1280×720 minimum, the shortened fader still reads as a fader, and ±0.5 dB is settable by
  drag.
- C is distinguishable from L10 at 28 px, and ten consecutive drags land without a miss.
- With a stereo reverb panned to L100, the tail stays stereo on the left side.
