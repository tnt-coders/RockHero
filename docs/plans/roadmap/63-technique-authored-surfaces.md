# Plan 63 — Each Technique Its Own Authored Surface

## 1. Status

**Roadmap — decision shape settled 2026-09-29, not started.** Written the day the user sighted that
selecting a keyframe stating both a fret and a bend, then pressing `Delete`, removes BOTH ("It may be
desirable to just delete one … each individual technique is really its own authored surface"). The
design below was reviewed the same day by a Fable design review whose brief was to find a simpler
solution than a channel-scoped selection model; it found one, and this plan is that design.

Placed AFTER the first releasable editor (G0): everything it touches is already authorable today —
a point's fret can be removed by deleting the point and restating what else it carried — so it
adds no editable fact and sits outside G0's release bar. **The watch item "Deleting a keyframe
removes every technique it states" (`docs/tracking/watch-items.md`) pulls Phase 1 into G0 Phase 2**
the moment the sighting repeats; Phase 1 is small (S–M, §10) and ungated once 63-Q1 is signed, so
pulling it forward costs no restructuring.

Re-verify the inventory (§5) before executing: it was stamped against `master @ c2d5e097`.

## 2. Goal

Every technique a moment states is withdrawn on its own. A keyframe is ONE MOMENT carrying any
subset of three statements — a fret, a bend, a vibrato change — and a charter must be able to take
back one of them without losing the others: the bend but not the fret, the fret but not the bend.

What already holds, and stays: every technique has its own SET verb (digits for the fret, `B` for
the bend, `V` / `Shift+V` for vibrato, and on notes `H`, `L`, `G`, the toggles), and every
technique but ONE has its own CLEAR — the bend picker's "No bend" row, `V`, `H`'s "No harmonic",
each toggle's second press. **The fret has no clear at all.** The only verb that touches it is
`Delete`, and on a keyframe `Delete` erases the whole moment. That one verb is the outlier this plan
fixes.

## 3. Non-goals

- **No channel-scoped selection.** A `ChartKeyframeKey` stays `{note, offset}`. Every verb but
  `Delete` acts on the moment (move, retype, the bend and vibrato verbs), and a key naming a
  channel would make "one point selected twice under two channels" representable (§11).
- **No change to `Delete` on a NOTE.** A strike has an identity beyond its techniques — the onset,
  the string, the attack — so deleting a note deletes the note, as today. Its techniques already
  withdraw one by one through their own verbs.
- **No format change.** A keyframe's stored shape is untouched; nothing migrates.
- **No "No fret" key.** A picker-less channel has no natural chord in the keymap grammar (`Alt` is
  the ring plane, `Shift` magnitude, `Ctrl` precision), and a new verb would add a concept where
  this plan removes one (§11).
- **No change to `Move`.** It stays the one verb that treats the moment as one object — which is
  what the moment is stored once for (`chart.h`, the `Keyframe` doc: moving it moves every technique
  authored there).

## 4. Constraints

- **One authority per rule.** The rule for which statement a point's MARK prints lives today only
  inside the projection. `Delete` must ask it, so it is extracted into common/core FIRST (Phase 1);
  a planner restating it is the project's recurring defect class (a rule stated twice).
- **The commit law is reused, never restated.** A point left saying nothing new is swept by the
  existing authority (`keyframeSaysNothingNew`), and only the point `Delete` touched is judged — a
  charter's freshly planted, not-yet-landed point elsewhere on the note must not dissolve because
  of someone else's `Delete`.
- **Layering.** The mark rule and the withdraw are chart facts and live in
  `rock-hero-common/core`; the planner lives in `rock-hero-editor/core`; nothing enters the UI
  layers in Phase 1 (docs/design/architectural-principles.md).
- **Both surfaces agree.** The mark is what both the 2D lane and the 3D board draw for a point, so
  "what `Delete` withdraws" and "what the charter sees there" are the same statement by
  construction.
- **Builds** through `.agents/rockhero-build.ps1`, as separate invocations, only where a change
  determinately warrants the check.

## 5. Current-state inventory (verified 2026-09-29 @ `c2d5e097`)

- **The moment.** `common::core::Keyframe` (`rock-hero-common/core/include/rock_hero/common/core/chart/chart.h`,
  the `Keyframe` struct): `offset` plus optional `fret`, optional `bend`, and a `vibrato` width.
  Vibrato has no "absent" value: `None` is itself a statement (an unvibrated leg), and a point's
  vibrato CHANGE is a difference from `vibratoBefore(note, offset)`.
- **The precedent: rules already shed channels.** `chart.h` (the `stripKeyframeChannels` doc,
  around line 1161): "Such a rule clears CHANNELS rather than whole keyframes — a capo floor takes
  the fret, not the bend authored at the same instant." The held channel already withdraws a
  statement rather than an object: `planClearHeldStops`, reached by `Delete` on the held caret.
- **The mark rule — stated once, but in the wrong layer.** `makeChartViewState`
  (`rock-hero-common/core/src/chart/chart_projection.cpp`, lines ~608–624): a stated fret wears
  `KeyframeStopMark`; else a vibrato change (`ring.vibrato != was`) wears `KeyframeRestMark`; else
  `KeyframeCurveMark` (the bend's dot).
- **Every face is a statement's face.** `KeyframeStopMark` is the stop's mark, `KeyframeRestMark`
  the vibrato change's, `KeyframeCurveMark` the bend's, and the bend chip is the bend's second face
  (`chart_view_state.h`). A keyframe has no face of its own.
- **`Delete` on a keyframe erases the moment.** `planDeleteSelection`
  (`rock-hero-editor/core/src/chart/chart_edits.cpp`, around line 757): `std::erase_if` over the keyed
  offsets, with the comment "Delete takes the keyframe ITSELF … it is an erase and never a strip:
  a stripped keyframe would stay as a bare boundary, and where the leg before it vibrates that
  boundary would still end the vibrato." Labels: "Delete Keyframe(s)", "Delete Note(s)",
  "Delete Selection".
- **After `Delete`, no selection.** `EditorController::Impl::deleteChartSelection`
  (`chart_handlers.cpp`, around line 2242) applies the plan with an empty selection.
- **The per-channel clears.** Bend: `planSetBend` with no amount resets `bend` at each keyed point
  (`chart_edits.cpp`, around line 1934). Vibrato: `endVibratoAt` (`chart.h`) STATES an unvibrated
  leg. Fret: none.
- **The commit law.** `keyframeSaysNothingNew` / `keyframeStatesNothing` (`chart.h`), swept at
  focus leave (`dissolveSilentKeyframes`) and by the writer.
- **The keymap row.** `docs/plans/in-progress/keymap-matrix.md`, `Delete` / `Backspace`: "delete
  note(s) | delete point | delete region (merges)".
- **Tests pinning today's `Delete`.** `rock-hero-editor/core/tests/test_chart_edits.cpp` (the
  keyframe-delete sections around line 6717, including "a deleted vibrato ending lets the vibrato
  before it run on").
- **No 3D selection surface.** The highway renderer draws no selection; Phase 1 changes nothing in
  `rock-hero-common/ui/src/highway`.

## 6. Dependencies

None blocking. Coordinates with:

- **The refusal flash** (`docs/plans/in-progress/refusal-flash.md`, G0 Phase 2): a `Delete` whose
  withdraw the finalize gate refuses (a pick slide's required terminal) flashes like any refused
  verb; nothing here designs its own feedback.
- **Plan 60 Phase 3** (hand markers): the hand row's `Delete` is a marker delete and is untouched;
  if the hand marker ever carries more than one statement, this plan's rule is the precedent.

## 7. Decisions already made (the settled shape)

1. **`Delete` on a keyframe withdraws the statement its mark prints** — the HEADLINE — and the point
   is swept only if that leaves it saying nothing new. Whole-point deletion is not a second rule: it
   EMERGES when the last statement goes (the Fable review, 2026-09-29).
2. **The headline is the mark rule, extracted.** Fret stated → `Fret`; else a vibrato change →
   `Vibrato`; else `Bend` — exactly the projection's order today, moved into one named core
   function that the projection then switches on.
3. **Withdrawing vibrato restores the leg before, it does not state `None`.** `V`'s clear states an
   unvibrated leg (a statement); `Delete` withdraws the CHANGE (`vibrato = vibratoBefore(...)`), so
   the two are two functions by right. This is also why the erase-not-strip fence in
   `planDeleteSelection` can go: a withdrawn vibrato change leaves a boundary that says nothing new,
   which the targeted sweep removes, so "a deleted vibrato ending lets the vibrato before it run on"
   keeps its exact result.
4. **The bend's clear is spelled once.** `planSetBend(no amount)` calls the same withdraw arm
   rather than keeping its own `bend.reset()`.
5. **A peeled point stays selected.** After a `Delete` that leaves a point standing, the surviving
   keyframe keys stay the selection (ringed, like `B` and `V` leave their anchors); keys whose point
   was swept are dropped, or the next digit would fall into a retype with no operand.
6. **Notes and faceless selections.** A note in the selection is deleted whole, taking its
   keyframes. A marquee or keyboard selection names no face, so each boxed keyframe withdraws its
   own headline — the same rule, no second path.

## 8. Open questions (63-Q1 must be signed before Phase 2)

- **63-Q1 — Peel, confirmed?** `Delete` on a multi-statement point peels one statement per press
  (the headline first), so a point stating a fret, a vibrato change and a bend needs up to three
  presses, and a marquee over junctions peels rather than empties. Recommended: YES — it is the
  user's principle applied literally; `Move` stays the one whole-moment verb.
- **63-Q2 — Phase 4 now or after sighting?** Clicking a fret+bend point's BEND CHIP selects the
  point, and Phase 2's `Delete` then withdraws the FRET (the headline), not the bend the charter
  clicked. Not a lie — the selection ring traces both faces — but a surprise. Recommended: sight
  Phase 2 first; build Phase 4 only if charters reach for chip-then-`Delete` (the bend's
  one-keystroke clear is `B`, `Return`, the picker opening on "No bend").
- **63-Q3 — Undo labels.** Recommended: "Remove Fret" / "Remove Bend" / "Remove Vibrato Change"
  when the point survives, "Delete Keyframe(s)" when it went, "Delete Selection" when mixed — the
  count rule `planDeleteSelection` already uses.
- **63-Q4 — The note-level chip (Phase 4 only).** An onset always states its bend, so `Delete` on a
  NOTE's pre-bend chip would SET the bend to rest rather than withdraw it. Recommended: accept the
  asymmetry; it is the onset's nature, not a rule.

## 9. Phased implementation

### Phase 1 — Extract the mark rule (no behaviour change)

- `enum class KeyframeChannel : std::uint8_t { Fret, Bend, Vibrato };` and
  `keyframeHeadline(const ChartNote&, const Keyframe&) -> KeyframeChannel` in common/core
  `chart.h`, documented as THE statement of which channel a point's mark prints.
- `makeChartViewState` builds `KeyframeMark` by switching on `keyframeHeadline` — the projection
  keeps its per-mark payload (the stop index, the fret in force) and loses its private copy of the
  order.
- Tests: the headline for each of the seven non-empty channel subsets, and the projection's marks
  unchanged over the existing fixtures (the whole common-core suite passes untouched).

### Phase 2 — `Delete` withdraws the headline (gated on 63-Q1)

- `withdrawKeyframeChannel(ChartNote&, Fraction offset, KeyframeChannel)` beside `keyframeAt` and
  `endVibratoAt`: `Fret` → `fret.reset()`, `Bend` → `bend.reset()`, `Vibrato` →
  `vibrato = vibratoBefore(note, offset)`. `planSetBend`'s clear calls the `Bend` arm.
- `planDeleteSelection`'s keyframe half: per keyed point, withdraw its headline, then sweep THAT
  point alone if `keyframeSaysNothingNew` says it is silent. The erase-not-strip comment goes with
  the erase. Labels per 63-Q3.
- `deleteChartSelection`: the next selection is the keyed points that survived.
- Tests: re-pin the keyframe-delete sections of `test_chart_edits.cpp` (the fret-only and
  vibrato-ending cases keep their exact results), and add fret+bend (a bend dot remains, selected),
  fret+vibrato (a rest mark remains), a slide-out carrying a bend (a bend-only end statement
  remains; a slide-out alone is swept), a marquee mixing notes and points, and a silent planted
  sibling point left untouched by a `Delete` on its neighbour.
- Docs: the keymap `Delete` row ("withdraw the point's headline; the point goes when nothing is
  left"), the `Keyframe` doc paragraph, and the developer guide's editing tour wherever it names
  `planDeleteSelection`.

### Phase 3 — Sighting

Sight on real charts: fret+bend points, a bend riding a slide-out, vibrato changes on a vibrating
leg, and a marquee over a chord slide's junctions. Rule 63-Q2 from what the sighting shows.

### Phase 4 — The caret stands on a face (only if 63-Q2 calls for it)

The held-stop precedent generalized. `ChartCaret::channel` becomes a FACE enum
`{Mark, HeldStop, BendChip}` and the stop channel digits read is DERIVED from it (`HeldStop` →
`Held`, else `Sounding`), never a second field that must agree. `ChartHitTarget` gains a bend-chip
alternative beside `ChartHeldStopHit`, so a press on a chip arms the caret on it while preserving a
wider selection exactly as `armChartHeldStopHandle` does; `Delete` dispatches on the face
(`BendChip` → withdraw the bend); a bend twin of `chartSlotShowsHeldStop` answers "is that face
drawn" at the moment it is spent; the tab view draws the caret box on the chip. Keyboard reach of
the chip is optional — the picker is the keyboard route. A multi-selection never carries a face
(the verb scope already dissolves the caret), which is what keeps the marquee path single. Size M;
the subtle part is the press handler's already-selected / handle / collapse logic.

### Acceptance

- A point stating a fret and a bend: `Delete` leaves a bend-only point, selected; a second
  `Delete` removes it. Undo restores each step exactly.
- A slide's junction stating only its fret: one `Delete` removes it exactly as today.
- A deleted vibrato ending lets the vibrato before it run on (unchanged).
- No planner restates the mark rule (`rg` for the projection's old `if (fret.has_value()) …
  else if (ring.vibrato != was)` finds nothing outside `keyframeHeadline`).

## 10. Cost, worth and cleanliness (the review's answers, 2026-09-29)

- **Cost.** Phase 1 + 2: **S–M.** The projection's mark block becomes a switch (net about zero),
  ~50 lines in `chart.h` / `chart.cpp` for the enum and two functions with their docs, ~40 in `chart_edits.cpp` for the
  planner half, the label rule and the bend reuse, ~5 in `chart_handlers.cpp` for the surviving
  selection; four existing test sections re-pinned (two keep identical results) and about eight
  new ones. Untouched: the hit test, the marquee and keyboard selection, the pending digit entry,
  the undo mechanism, both surfaces' selection rings. Phase 4: **M**, eight to ten files.
- **Worth.** Phase 1 + 2: yes, unconditionally, at the frequency the user expects — it removes the
  model's one outlier (every rule sheds channels; only `Delete` erased moments), it deletes a fence
  comment instead of adding a branch, and it is the only shape that gives the fret a clear.
  Phase 4 saves one keystroke over `B`, `Return` and removes a surprise; worth it only if sighting
  shows the reach.
- **Cleanliness.** Phase 1 + 2 are clean with one precondition, which is Phase 1 itself: the mark
  rule must leave the projection before `Delete` asks it. No flag, no mode, no special case — the
  whole-point delete emerges, the vibrato withdraw is a genuinely different rule from `V`'s clear,
  and the ring's-end case is already inside `keyframeSaysNothingNew`. Phase 4 is clean if the face
  is ONE enum on the caret with the stop channel derived from it, and the chip hit is its own
  variant alternative rather than a flag on `ChartKeyframeHit` (which would make
  keyframe-plus-held-stop spellable).

## 11. Rejected alternatives

- **Channel-scoped selection keys** (`ChartKeyframeKey{note, offset, channel}`). It touches every
  keyframe-key site in the chart handlers, planners and selection (well over a hundred), every
  planner that resolves keys, the onset-group expansion, and the marquee (which channel does a box
  name?), and it makes one point selected twice under two channels representable. Every verb but
  `Delete` wants the moment, so the complexity buys no correctness.
- **A "No fret" verb, `Delete` unchanged.** No natural chord for a picker-less channel, and it adds
  a verb where this plan deletes a concept.
- **Do nothing.** Removing a fret from a fret+bend point costs `Delete`, then `B` and the amount at
  the caret — three or four actions — at the frequency the user expects.
- **Making vibrato optional so every withdraw is a reset.** It would make the vibrato arm read like
  the other two, but it re-opens the "nothing carries" law and the format for a symmetry nobody
  needs. Recorded, not pursued.

## 12. References

- Sighting and ruling context: this session's `Alt`+digit fix (`e09ecd0a`, a digit STATES a fret on
  a fret-less point) and the fret-less keyframe ruling (2026-09-27).
- `docs/plans/in-progress/keymap-matrix.md` — the `Delete`, `B` and `V` rows.
- `docs/plans/in-progress/first-releasable-editor.md` — G0's release bar and Phase 2.
- `docs/tracking/watch-items.md` — "Deleting a keyframe removes every technique it states".
