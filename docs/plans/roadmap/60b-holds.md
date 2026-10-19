# Plan 60b — Holds: stated extents

## Status

**Sub-plan of `docs/plans/roadmap/60-the-hand.md`. REWRITTEN 2026-10-16; steps 0 to 4 built, step
5 (the marks, and the verb and row rulings of 2026-10-18) built and awaiting the user's sighting.**
The first 60b design (grip-template holds, no required end) was built and then set aside: it lives
on branch `plan-60b-grip-holds` (tip `d9195548`) with its plan and decision log, and serves here
only as a parts bin. Its faults: `Ctrl+H` over a time selection authored holds running far past the
selection, a hold over a derived span did not equal that span, and 604 corpus spans could never be
given a hold. This plan replaces it with the simplest model that is rock solid. Grips (templates,
fingers, the picker, the export gate) move to `docs/plans/roadmap/60c-grips.md`, which starts after
this one is sighted.

## 1. The model

A hold is an authored span with a stated extent: `{start, end, grip}` where both edges are
required and exact (`end > start`), and `grip` is one of:

- **unnamed** — "the hand holds one shape here". Inside it the derivation forms exactly one span,
  `[start, end)`; its class and posture are read from its notes by the existing span rules.
- **`none`** — "no arpeggio shape here". The arpeggio spans fronted inside it are hidden; chord
  boxes are proven by the notes, so they still form inside it and stand. A `none` hold is never
  cropped around a chord (ruled 2026-10-18).

`60c` adds a third alternative, a named template. Holds never overlap, and `none` holds that meet
are one stretch: two that abut or overlap merge into one, judged on their edges alone
(`mergedNoneHolds`). Every hold, `none` included, is a wall at both edges, and spans derive fresh in
every gap between holds. Outside holds, span derivation is exactly what it is on `master` today. At
a hold's end the derivation resumes as it does after any derived span closes with rings still
sounding (shape rule 4's hand-off), including a slide landing exactly at the end.

## 2. Validity: the walk is the one authority

A hold never restates a derivation rule. It changes only the edges; everything inside is the span
walk's own judgement (shape rules 1 to 5, hand-position rule 6).

**What a hold inherits.** Grip statements and plants, membership and founding, hand-position rule
6's release, shape rule 4's landing instant, the span's class and posture. Inside an unnamed hold
the walk founds and accumulates members by its normal rules; the hold's front is its stated start.

**What a hold suppresses.** The walk's notation splits: a higher fret on a stated string, foreign
rings and displacement on strings the span does not state, the join break, the unison-restatement,
partial-sounding and character splits, the whole-stroke boundary, the drop of a crowded landing,
and front dating. These split a derived span where the hand provably keeps its shape. A hold
also suppresses the continuity close: a derived span ends where its statement stops being proven
(a rest, its notes running out), while a hold is the charter's statement, so rests inside it are
legal.

**The break verdict has two named parts.** A *hand conflict* is hand-position rule 6's release
against the open span's own grip (its plants and statements), the tap's floor, or a glide's landing
(shape rule 4). A *notation split* is every other break. The derivation breaks on either; inside a
hold only a hand conflict breaks. "In force" is judged per stop: a string still sounding a
different stop no longer keeps the span's stop in force.

**The one close rule.** An unnamed hold's span closes at the first hand conflict inside it, or at
the first instant at or after its stated end where the walk would close a span standing there: a
break of either kind, its statement no longer in force (a rest included), the next hold's start, or
the song's end. The walk publishes the span there. **A hold is valid when its published extent
equals its stated extent.** So an end in the middle of a sustain, where nothing closes, is invalid;
every derived span is a valid hold by construction, and authoring one exactly changes nothing (the
census gate pins both rows at zero).

The close rule is one for every unnamed hold. A hold whose notes never found a span is a span
standing at its end, its grip what each string states into the end and still reaches it, read
end-inclusively (a ring ending exactly there is in it, a glide is its departure stop up to the
landing, a note dead before the end is not); it stands before the end's landings are judged, and
the walk closes it like any other span. So a hold over silence, or over a single note's whole
ring, is valid, and one ending in the middle of that ring, nothing else happening, is not. Every
hold's span is published, visible and selectable, except one that closes at its own start: it
holds nothing. A `none` hold is always valid.

**Walls at the published end.** A hold's edges are walls for every other span, and its end is a
wall only while its span reaches it: once the walk has closed the span short of the stated end,
that end walls nothing. The spans derived under the stated holds are therefore the spans under the
holds the walk publishes.

## 3. Keeping holds valid after other edits

An edit that is not a hold verb (a note edit, a tempo edit, a load) always lands. Afterwards one
normalizer, run once wherever the edit lands (the plan gate, the timeline funnel, the load) and
logged once per rewrite, writes back each hold's published extent: that one rule is both the crop
(a conflict inside) and the extension (an end where nothing breaks). A hold whose span closes at
its own start is dropped. No rewrite can make two holds overlap, since every span closes at the
next hold's start (touching is legal: holds are half-open). Before the walk derives anything, a
hold a re-address rounded onto no length is dropped, and then `none` holds that abut or overlap are
merged, on their edges alone; in that order, since an emptied hold between two `none` holds would
otherwise keep them apart until a second pass. Each merge is logged like the other repairs, a
`none` hold swallowed whole by another included, so a load that merges marks the document dirty.
Because merging reads no span and the walk walls each hold at its published end, the normalizer is
one derivation and one pass reading it, and the notes layer it returns is that same derivation;
normalizing the result again changes nothing.

Inside an unsettled gesture (a held run of note or edge keys) the normalizer runs on each candidate
replayed from the pre-gesture chart, never on that base, so every extend, crop and drop it makes
reverses as the gesture steps back. Once the gesture settles, the result is written and only undo
restores it.

A hold verb never relies on it: a verb whose result would need cropping or dropping is refused
with the refusal flash.

## 4. Verbs

| Target | Keys | Result |
|---|---|---|
| Span, `none` hold, time selection or note selection (from its first onset to its last tail end) | `Ctrl+H` ("Hold...") | The hold picker over the extent: **Shape** (an unnamed hold), **None** (a `none` hold, merged with any it meets) or **Auto** (every hold carved out, so the notes decide the spans there). A choice that would change nothing is left out, so at least two always show, each with a tooltip; a refused choice flashes |
| Derived span | `Alt+Left` / `Alt+Right` | Moves the start one note onset, converting the span to a hold; refused if invalid |
| Derived span | `Alt+Shift+Left` / `Alt+Shift+Right` | Moves the end one note boundary, converting the span; refused if invalid |
| Derived span, chord or arpeggio | `Delete` | A `none` hold over the span, merged with any it meets and left selected; a chord stands inside it. Over a chord already inside a `none` hold the merge changes nothing |
| Authored hold | the same edge keys | Move its edges; refused if invalid |
| Unnamed hold | `Delete` | Removes it, so the span derives again and stays selected: Delete steps down one level |
| `none` hold | `Delete` | Removes it, so the derivation returns over its whole merged stretch |
| Cursor | `Ctrl+Shift+H` ("Jump to Hold") | Selects the `none` hold holding the cursor, else the span there; nothing in an empty stretch |
| Bare caret | `Ctrl+H` | Refused |

**Overlap.** Every new hold, whatever verb authors it, and Auto's empty stretch, go through the one
carve (`carvedShapeHolds`): the extent comes out of every hold it overlaps — one it straddles is
cropped to its start or end, one it lies inside is split into the parts before and after it, one it
covers entirely is gone, since a selection's `Ctrl+H` is the charter's statement over its whole
range — and a `none` hold carved in beside or over another merges with it. So Auto over the middle
of a merged `none` hold leaves its two outer pieces. Each surviving part must be valid, or the verb
is refused. Shape on a `none` hold restores the shape a `Delete` denied (ruled 2026-10-18). When
`60c` lands, its grip choices replace Shape in the same picker (ruled 2026-10-18).

**One step down, one write.** `Delete` is the hold choice one level below what it is pressed on:
Auto over a held span's hold or a `none` hold, None over a derived span, through the same commit
as the picker's rows. The selection then walks back along the row as on every marker row where the
marker is gone, and otherwise lands on what the carve left at the extent's start, or on nothing
where the notes derive no span there. There is no `Ctrl+Delete`: `Ctrl+H` then None states the
same `none` hold over a held span in one move.

**Rows and clicks.** In the lane a press inside a `none` hold's extent selects the `none` hold,
even where a chord box stands in it; a chord inside a `none` hold is selected by its ruler chip.
The walk up from the strings meets the `none` hold before the span inside it, and `Ctrl+Shift+H`
lands by that same row order. One constant states the order (`g_span_rows`): the lane reports
every mark under a press and the controller picks by it, as the walk and the jump do.

**Pushing.** An edge key that moves an edge into a neighbouring hold pushes that hold's facing
edge along with it. Both holds must stay valid and the pushed one non-empty, or the press is
refused: unlike a selection's `Ctrl+H`, a push is a nudge, and nudging a neighbour out of existence
would be a surprise (ruled 2026-10-18). A held run of edge keys is one gesture that replays from its
base on every press, so stepping back restores the pushed hold, and a run back where it began
changes nothing, leaving a derived span derived; once the gesture settles, the result is written
and only undo restores it. The branch's edge gesture (`f3aed178`, `ChartHoldEdgeGesture`) is the
part to reuse.

There is no `Alt+Delete` for holds: an end is required and is removed only with its hold. `Enter`
and the named grips belong to `60c`. `Delete` on a time selection stays reserved for plan 52's
deletion of the chart content in the range.

## 5. Marks and format

- **2D:** authored holds solid, derived spans faded (the derived fret-hand chip's weight), `none`
  holds as a slashed neutral hatch on the rails (`EditorTheme::none_hold_rail`), drawn beneath the
  spans so a chord box inside one stands over the hatch. No end tick: every end is stated. Spans
  and `none` holds share the one lane; the selected one wears the accent outline.
- **Save format:** `holds[] {at, end, grip?}`; `grip` absent is unnamed, `"none"` is `none`
  (`60c` adds template ids to the same field). The loader rejects empty holds and overlapping
  holds but for `none` holds overlapping one another, then the normalizer runs, merging those; a
  rewrite it makes at load is logged and marks the document dirty, so it is visible and saved.
- The ruleset gains the hold clause (shape rules, plus a pointer from hand-position rule 6); the
  developer guide's file-format and editor-action pages are updated in the same change set.

## 6. Steps

0. **Preserve the independent fixes from the branch** (simplicity review, 2026-10-16): cherry-pick
   `e673e799`; rule 6 grip release `067811a8` with `5b4085d5`'s tap release folded into the one
   channel walk (no `grip_release` module); `6c5bb7fb` (dating-floor clamp) minus its hold
   sentence; `773a390c`; extract `497a1290`'s note sort and `edb34cf3`'s selection-count chip;
   redo `d135eb34`'s renames as a sweep; keep `44c638ca`; squash the none-positions plan. Re-run
   the scorer and census on `relwithdebinfo`.
1. **Record, format, validity.** The record, reader/writer, loader checks, the §2 predicate, unit
   tests.
2. **Derivation.** Holds as spans and walls in the span walk; `none` suppressing arpeggio spans.
   **Census gate:** authoring `Ctrl+H` over every derived span of the corpus must leave the
   resolutions identical and refuse none: both rows are pinned ZERO, the proof that the walk is the
   one authority (§2).
3. **Normalizer** in the timeline funnel, logged once; idempotent over the corpus.
4. **Verbs** of §4 with the refusal flash and undo.
5. **Marks**, then the user's sighting.

## 7. Rulings before step 0

- **A. RULED 2026-10-16 (user): both stand.** Hand-position rule 6's grip release (`067811a8`,
  `5b4085d5`; a sound at or below a stop proves its finger lifted, one above proves nothing) and
  shape rule 1's dating-floor clamp with re-articulation (`6c5bb7fb`).
- **B. RULED 2026-10-16 (user): the retirement carries over, provisionally.** Step 0 brings back
  only the Phase 6 retirement hunks of `55eac2c5` and `afb84d59` in `60-the-hand.md`. If the
  re-run census row "spans holding a stop outside the window" is not zero on `master`, the
  retirement reopens.
