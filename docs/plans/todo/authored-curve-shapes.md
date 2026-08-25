# Authored Curve Shapes for Tone Automation

Status: **DEFERRED 2026-08-25.** The `curve_shape` field was deleted from the model, the port,
the view state, and the package format on that date — *because* this feature is wanted later, not
instead of it. This document is the intent record and the restore recipe. It is intent, not
design: the authoring gesture and the drawn curve are open questions, deliberately.

## The user's intent

> "I deliberately INTEND to add curve shapes to the model in the future." — user, 2026-08-25

## Why the field was deleted anyway

It was dead authored storage. `curve_shape` was `0.0F` at every production creation site, no
authoring affordance existed anywhere in the editor, and the only non-zero values in the tree
were test passthroughs asserting that a number survives a copy. Nothing drew a curved segment and
nothing evaluated one. Since ec5c43f1 the Tracktion write seam derives the shape it needs from
the parameter, so the stored field was not even feeding the backend: a stepped parameter got a
`+1.0` hold and a continuous one got the stored `0.0`, which is the linear ramp it would have got
anyway.

Deleting was ruled over keeping precisely because restoration is cheap and routine — a new
package-format field, which the project has a checklist for — and because the empty field bought
the future feature nothing: no schema head start, no persisted user data, no call site that would
not have to be written anyway. A field that only ever holds its default is a promise the code
cannot keep, and it made every reader look like it honored a shape it silently discarded.

## The standing constraint (from ec5c43f1 — do not relitigate)

**A discrete parameter always derives the hold shape, regardless of any authored data.** Segment
shape for a stepped parameter is the plugin's fact, not the chart's: Tracktion's `isDiscrete()`
is false for a hosted VST3, so a ramp reaches the plugin raw and flips it at the plugin's own
threshold — early by a gap-dependent amount, and a multi-state parameter sweeps every state on
the way. `writePluginParameterCurve` therefore writes `+1.0` holds for a stepped parameter and
must keep doing so no matter what a point carries.

So authored curve shapes are a **continuous-parameter feature only**. The authoring affordance
must not offer itself on a discrete lane, and any restored field is an input to the continuous
branch of that one derivation — never a replacement for it.

## What the feature needs

1. **The model field returns.** Follow `docs/developer/changing-the-package-format.md` — both
   readers, the shared validator, the format reference. No `formatVersion` bump; formats change
   in place.
2. **An authoring affordance.** Nothing exists today. The natural shape is a draggable handle on
   the segment between two points in the automation lane (the lane already owns point drag,
   Alt+click insert, and typed value entry), suppressed on a discrete lane.
3. **Curved segment rendering.** The lane draws straight segments for a continuous parameter and
   a staircase for a discrete one; a shape needs the drawn segment to bend with it, matching what
   the backend will actually play.
4. **Projection evaluation.** `tone_automation_projection.cpp` would carry the shape into the
   view state, and whatever evaluates a lane's value between points has to agree with Tracktion's
   bezier so the drawn curve is not a lie about playback.
5. **Undo needs nothing new.** The memento is already the whole point list per gesture
   (`tone_automation_edits.h`), so a shape edit rides the existing entry.

## Restore recipe (what the deletion touched)

- `common/core/tone/tone_automation.h` — the field on `ToneAutomationPoint`, its `operator==`
  clause, and the curve-shape range in the `isValidToneParameterAutomation` rule list;
  `common/core/src/tone/tone_automation.cpp` — the `[-1, 1]` inside-range validation branch
  (written as a range the value must be INSIDE, so NaN is refused rather than accepted).
- `common/core/src/package/rock_song_package_write.cpp` — emit `"shape"` when non-zero;
  `rock_song_package_read.cpp` — read it, and refuse a PRESENT but non-numeric one rather than
  defaulting it to `0` (which would silently straighten an authored curve). Reading it back is
  what turns the currently-ignored stale key into a meaningful one again.
- `common/audio/automation/i_tone_automation.h` — the field on `AutomationCurvePoint` and its
  `operator==` clause, plus the port prose stating that write and read are not inverses for it;
  `src/tracktion/tone_automation_curve.cpp` — the write's continuous branch takes the point's
  shape (`steps ? 1.0F : point.curve_shape`) and the read carries `getPointCurve` back.
- `common/audio/src/automation/tone_automation_rebuild.cpp` — `derivedToneCurvePoints` passes it
  through to the port.
- `editor/core/tone/tone_automation_view_state.h` — the field on `ToneAutomationPointViewState`
  and its `operator==` clause; `tone_automation_projection.cpp` — the passthrough;
  `tone_handlers.cpp` (`toneAutomationDragCommitPoints`) and
  `editor/ui/src/tone/tone_automation_lanes_view.cpp` (`copyLanePoints`, `requestPointReplace`) —
  preserve an existing point's shape across a move, and seed a new point at `0`.
- Tests carried passthrough assertions in `test_rock_song_package.cpp`, `test_tone_track_rules.cpp`,
  `test_tone_automation_rebuild.cpp`, `test_tone_automation_curve.cpp`, `test_tempo_mirror.cpp`,
  `test_tone_automation_projection.cpp`, `test_editor_controller_tone_automation.cpp`,
  `test_tone_automation_lanes_view.cpp`, and `test_gameplay_session.cpp`. Passthrough assertions
  are what made the field look alive; a restored field deserves tests that prove an authored
  shape reaches playback, not that a float survives a copy.

The pre-deletion state is commit `99aa2e7a`. Read a whole file as it stood with
`git show 99aa2e7a:<path>`, or the sweep itself with `git diff 99aa2e7a -- <paths>` — a diff
against that commit is the fastest read of every site. (Bare `git show 99aa2e7a` prints the diff
that commit *introduced*, which is unrelated grid-snapping work.) Do not apply it blind: it
restores the field, not the feature, and it predates whatever the lane looks like when this is
picked up.
