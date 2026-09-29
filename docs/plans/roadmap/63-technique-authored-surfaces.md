# Plan 63 — The Bend Chip as Its Own Authored Surface

## 1. Status

**Phases 1–2 BUILT 2026-09-29, awaiting sighting (Phase 3).** The user pulled the plan forward the
day it was ruled. Where this document and the build differ, the build follows three later rulings:
the caret square stays on the slot (§7.6), `Enter` opens the bend picker (§7.7), and every arrival
lands on the mark (63-Q1, §8). The inventory (§5) was stamped against `master @ 4c7184ef`.

History, in one paragraph: the user sighted that `Delete` on a keyframe stating a fret AND a bend
removed both ("each individual technique is really its own authored surface"). This plan first
proposed that `Delete` peel whichever statement the point's mark prints. The user replaced that the
same day with a narrower ruling, SHIPPED in `4c7184ef`: **`Delete` on a keyframe takes only its
fret**, `B` → "No bend" clears the bend, `V` clears vibrato, and the point goes whole where nothing
new is left on it or it states no fret; the bare digit on a lone fret-less point CUTS the ring there
while `Alt`+digit states its fret. What remains, and what this plan now is, is the user's follow-up
direction: **the bend chip becomes a surface the caret stands on, so `Delete` there takes the
bend.** Its keyboard design was reviewed by a Fable UI review and ruled by the user (§7).

It is the first editor item after the first releasable editor (G0): the bend's clear is already
authorable (`B`, `Return`, the picker opening on "No bend"), so it adds no editable fact.

## 2. Goal

Every technique a keyframe states has a surface of its own that the caret can stand on and
`Delete` can withdraw. After `4c7184ef` the MARK is the fret's surface (`Delete` takes the fret).
The bend's surface is the CHIP printing it, which today is only a picture: clicking it selects the
point, and the caret cannot stand on it. This plan makes the chip a caret face.

The vibrato change is not in scope: it has no chip, and on a fret-less point its rest head is
already the mark (`Delete` on a fret-less point takes the point whole).

## 3. Non-goals

- **No channel-scoped selection.** A `ChartKeyframeKey` stays `{note, offset}`. The face lives on
  the CARET, as the held stop's already does; the selection still names moments.
- **No caret dissolve.** The marker chips (sections, tones, hand markers) are separate objects on
  rows of their own, and the caret dissolves onto them. A bend chip is a face OF the point, like
  the held-stop satellite, so the caret stands on it (§7.1).
- **No chip-to-chip `Tab` route.** `Tab` and `Ctrl+Tab` keep their meanings; a "chips only" route
  would be a mode.
- **No picker on double-click, no drag.** Nothing in the lane drags today.
- **No format change.**

## 4. Constraints

- **One face field.** `ChartCaret::channel` becomes a face enum and the stop channel digits read is
  DERIVED from it, never a second field that must agree with it.
- **One withdraw of the bend.** `Delete` on the chip and `planSetBend` with no amount call the same
  arm; neither restates `bend.reset()`.
- **One answer to "can the caret stand on that face".** `chartFaceShown` answers for every face,
  asked when the caret arms and again when the face is read. For the chip it asks the CHART —
  whether an object on the slot states a bend (`chartObjectStatesBend`) — not the layout: the
  keyboard has no geometry, and a stated bend whose chip the lane crops is still the face (built
  2026-09-29 after a Fable simplicity review; a projection-based first draft is what it replaced).
- **Layering.** The face and its handlers live in `rock-hero-editor/core`; the chip's box comes
  from the common/ui layout manifest the lane already paints and hit-tests with; the square is the
  tab view's.
- **Builds** through `.agents/rockhero-build.ps1`, as separate invocations.

## 5. Current-state inventory (verified 2026-09-29 @ `4c7184ef`)

- **The caret's face today.** `ChartCaret::channel` is a `common::core::ChartStopChannel`
  (`Sounding` / `Held`). `armChartCaret(position, string, channel)` and `armChartHeldStopHandle`
  (`rock-hero-editor/core/src/chart/chart_handlers.cpp`, ~741) are its two writers;
  `chartCaretChannel()` reads it back through `chartSlotShowsHeldStop` (~623), which demotes a
  held caret whose satellite is no longer drawn.
- **Stepping onto the held stop.** The plain Left/Right step visits a slot's head and then its
  satellite in display order before moving along the grid (`chart_handlers.cpp`, ~1600–1620).
- **Clicking the held stop.** The press handler (~1180–1195): a satellite on an unselected note
  arms the caret on it; on an already-selected note, `armChartHeldStopHandle` arms it without
  collapsing a chord's selection.
- **`Delete` dispatch.** `deleteChartSelection` (~2216): a verb scope on the `Held` channel runs
  `planClearHeldStops`; otherwise `planDeleteSelection`, and the points it only took the fret from
  stay selected.
- **The bend's clear.** `planSetBend` with no amount resets `bend` at each keyed point
  (`chart_edits.cpp`).
- **Hit targets.** `ChartHitTarget = std::variant<ChartNoteHit, ChartHeldStopHit, ChartKeyframeHit>`
  (`chart_hit_testing.h`, ~101). A chip hit today resolves to the note or keyframe it prints for.
- **The chip's box.** `TabNoteLayout::bend_chip` (the onset's pre-bend chip) and
  `TabKeyframeLayout::bend_chip` (`rock-hero-common/ui/.../tab_layout_manifest.h`, ~214 and ~346).
- **The caret square.** `TabView::caretSquare` (`rock-hero-editor/ui/src/tab/tab_view.cpp`, ~1082)
  has two arms: the head square and, for `Held`, the satellite column. It is stroked at ~680,
  AFTER the selection overlay re-paints a selected chip's plate (~551, ~649), so where the chip
  overlaps the head square the caret's outline crosses the chip's text (sighted 2026-09-29: "the
  bend chip may need to display over the caret when selected because currently it is under it").

## 6. Dependencies

None blocking. The refusal flash (`docs/plans/in-progress/refusal-flash.md`) covers a chip `Delete`
the rules refuse, like any refused verb.

## 7. Decisions (ruled by the user 2026-09-29)

1. **The held-stop precedent, not the marker chips.** The caret stands on the chip; it does not
   dissolve. Consistency is kept by the distinction itself: faces of a point keep the caret,
   objects on rows of their own take it away.
2. **One face enum on the caret**: `{Mark, HeldStop, BendChip}`, with the digit channel derived
   (`HeldStop` → `Held`, else `Sounding`).
3. **Arrows.** Left/Right walk keyframes and heads as today; from the chip they drop back into the
   string lane's walk. `Up` from a mark whose point shows a chip goes onto the chip; `Down` from the
   chip returns to its mark; `Up` from the chip goes to the string above. `Ctrl+Up/Down`, `Tab` and
   `Ctrl+Tab` skip faces.
4. **`Delete` on the chip takes the bend** — through the one withdraw arm `planSetBend`'s clear
   also calls. On a NOTE's pre-bend chip it sets the onset's bend to rest, since an onset always
   states its bend (accepted asymmetry). The face changes what `Delete` does and nothing else.
5. **Pointer.** Clicking a chip arms the caret on it; clicking the chip of an already-selected
   note or point arms it without collapsing the selection, exactly as `armChartHeldStopHandle` does.
6. **Drawing (the user's model A).** The caret square stays on the SLOT on every face but the held
   stop's, which is a column of its own; on the `BendChip` face the chip, not the head, wears the
   selection ring. The selection's chips draw OVER the caret square on every face, so the amount
   stays readable.
7. **`Enter` and `B` on the chip open the bend picker.** `B` already acts on the chip's object;
   `Enter` restates the selection by kind, and a bend is restated by choosing its amount.
   `Ctrl+↑/↓` from the chip jump between groups exactly as from any string.
8. **No picker on double-click** for now.
9. **Delete on the chip IS the picker's "No bend"** — one planner (`planSetBend` with no amount),
   which now leaves a named onset at rest and takes a named point left saying nothing, so the two
   verbs cannot part.

## 8. Settled question

- **63-Q1 — Where does a DOWNWARD arrival land? RULED 2026-09-29: on the mark.** The UI review recommended that `Down` from the
  string above land on a chip first (the chip sits above the mark, so display order says chip, then
  mark). The user suspects it may feel off. **R: land on the MARK.** Every arrival from another
  string, in either direction, lands on the mark, and the chip is entered only by `Up` from its own
  mark. It keeps one arrival rule instead of a direction-dependent one, and a vertical walk that
  passes a bent note stops on its fret, which is what a charter walking strings is reading. The
  cost: walking UP visits a chip that walking DOWN does not, which the user should confirm in the
  sighting rather than on paper. Does not gate the start; it is one branch in the vertical step.

## 9. Phased implementation

### Phase 1 — The face on the caret

- `ChartCaret::channel` → a face enum (working name `ChartCaretFace { Mark, HeldStop, BendChip }`);
  `chartCaretChannel()` derives the stop channel from it. Every writer of `channel` moves to the
  face.
- `chartFaceShown(slot, face, objects)` beside `chartSlotShowsHeldStop`, the chip answered from the
  chart. The read-side demotion (a face no longer shown reads as `Mark`) covers both faces in one
  place.
- The vertical step: `Up` from a mark with a drawn chip → `BendChip`; `Down` from `BendChip` →
  `Mark`; `Up` from `BendChip` → the string above; arrivals land per 63-Q1. The horizontal step
  from `BendChip` resolves to the ordinary walk from the mark's slot.
- `deleteChartSelection`: a verb scope on the `BendChip` face runs the bend's withdraw over the
  scope's anchors. `planSetBend`'s no-amount path and this share one arm.
- Tests (editor core): arming, the vertical and horizontal steps, the demotion when the chip stops
  drawing, `Delete` on a point's chip (bend gone, fret kept, point selected; a bend-only point goes
  whole) and on a note's pre-bend chip (onset at rest), undo of each.

### Phase 2 — The pointer and the square

- `ChartHitTarget` gains a `ChartBendChipHit` beside `ChartHeldStopHit` (its own alternative, not a
  flag on `ChartKeyframeHit`, which would make keyframe-plus-held-stop spellable), resolved from the
  manifest's `bend_chip` boxes; the press handler arms or hand-arms per §7.5.
- `TabView::caretSquare` gains the `BendChip` arm (the chip plate's box), and the paint order moves
  the chip overlay above the caret square (§7.6) for every face.
- Tests: the hit test for onset and keyframe chips, the press paths, and a pixel probe that the
  chip's plate is not crossed by the square (probe the plate, not the glyph; see the CI table in
  `CLAUDE.md`).

### Phase 3 — Sighting

Sight on real charts: vertical walks across bent notes (rule 63-Q1 from it), chip `Delete` on
fret+bend points and on pre-bends, a chord's chip clicked while the chord is selected.

### Acceptance

- The caret stands on a chip; `Delete` there removes the bend and keeps the fret; `Delete` on the
  mark removes the fret and keeps the bend (unchanged since `4c7184ef`).
- One face field on the caret; `rg` finds no second stop-channel field and no second `bend.reset()`
  outside the shared withdraw.
- The chip reads cleanly under the caret on every face.
- Docs: the keymap matrix's arrow, `Delete` and pointer rows; the developer guide's caret section
  wherever it names `ChartStopChannel` on the caret.

## 10. Cost

**M**, about eight to ten files: the caret type and its writers, the vertical step, the `Delete`
dispatch and the shared withdraw, the hit test and press handler, the caret square and paint order,
and the tests. The subtle part is the press handler's already-selected / handle / collapse logic,
which the held stop already solved and this reuses.

## 11. Rejected alternatives

- **Peel the headline** (this plan's first design): `Delete` withdrew whichever statement the mark
  prints, so a fret+bend point took two presses and the first took the fret — correct, but it
  surprised where the charter aimed at the chip. Replaced by the shipped ruling plus this plan.
- **Channel-scoped selection keys.** Every keyframe-key site changes and one point selected twice
  under two channels becomes representable, for a distinction only `Delete` needs.
- **The caret dissolving onto the chip**, as onto marker chips. The chip is a face of the point, not
  an object of its own; dissolving would drop the caret's slot, and the next digit would have no
  place to land.
- **`Tab` between chips.** A second route for one verb's convenience.

## 12. References

- `docs/plans/in-progress/keymap-matrix.md` — the arrow, `Delete`, `B` and pointer rows.
- `docs/plans/in-progress/first-releasable-editor.md` — G0's release bar.
- `docs/tracking/watch-items.md` — "A bend chip is not a surface of its own".
