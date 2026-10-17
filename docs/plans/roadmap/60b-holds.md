# Plan 60b — Holds: stated extents

## Status

**Sub-plan of `docs/plans/roadmap/60-the-hand.md`. REWRITTEN 2026-10-16; NOT STARTED.** The first
60b design (grip-template holds, no required end) was built and then set aside: it lives on branch
`plan-60b-grip-holds` (tip `d9195548`) with its plan and decision log, and serves here only as a
parts bin. Its faults: `Ctrl+H` over a time selection authored holds running far past the
selection, a hold over a derived span did not equal that span, and 604 corpus spans could never be
given a hold. This plan replaces it with the simplest model that is rock solid. Grips (templates,
fingers, the picker, the export gate) move to `docs/plans/roadmap/60c-grips.md`, which starts after
this one is sighted.

## 1. The model

A hold is an authored span with a stated extent: `{start, end, grip}` where both edges are
required and exact (`end > start`), and `grip` is one of:

- **unnamed** — "the hand holds one shape here". Inside it the derivation forms exactly one span,
  `[start, end)`; its class and posture are read from its notes by the existing span rules.
- **`none`** — "no arpeggio shape here". No arpeggio span is derived inside it; chord boxes still
  are.

`60c` adds a third alternative, a named template. Holds never overlap. Outside holds, span
derivation is exactly what it is on `master` today. At a hold's end the derivation resumes as it
does after any derived span closes with rings still sounding (shape rule 4's hand-off), including
a slide landing exactly at the end.

## 2. Validity: the one predicate

An unnamed hold states, on each string, the stop of the FIRST fretting-hand note struck on that
string inside it. Notes ringing in from before the start state nothing. A hold is valid when:

1. **No conflict inside.** A conflict is a note that DEFINITIVELY contradicts a stated stop: a
   later note that releases it under hand-position rule 6's release predicate (a lower fret on the
   string, the open string, a node, the stating finger's own glide leaving it, a tap reaching it).
   A higher fret on the same string is another finger and fits. Other strings, rests and silence
   never conflict. The conflict's instant is the release instant (a struck note's onset; a glide's
   landing).
2. **A stated stop has stopped sounding at the end.** The end is invalid where EVERY stop the hold
   states is still sounding across it, from a note struck inside the hold on each. If any
   stated stop has stopped ringing, notes may ring on past the end, as they do past a derived
   span's close. A hold that states no fretted stop always passes.

A `none` hold is always valid. A hold with nothing sounding inside is valid and stands.

Rule 1 reuses rule 6's predicate; it is never restated. That predicate is fixed first (§6 step 0).

## 3. Keeping holds valid after other edits

An edit that is not a hold verb (a note edit, a tempo edit, a load) always lands. Afterwards one
normalizer, run once in the timeline funnel and logged once per rewrite:

1. **extends** a hold whose end fails §2.2 to the smallest later end at which §2.2 holds, judged
   over the extended hold's own stated stops (a string first struck inside the extension counts);
2. **crops** a hold whose first conflict lies after its start so it ends at the conflict's instant,
   valid by construction: the conflicting string's stated stop stops sounding there, because no
   stored ring crosses the next onset on its own string (the same-string clamp,
   `normalizeSustainOverlaps`, `docs/design/rulesets/gp-import-rules.md`);
3. **drops** a hold whose result is empty or overlaps another hold (touching is legal: holds are
   half-open).

Each step runs at most once, in that order; a crop never makes an end need extending, so there is
no fixpoint loop. "Sounding" is per stop, not per string: a higher fret on the same string (another
finger) means the stated stop no longer sounds. No stated stop can be contradicted inside an
extension (a note on its string would have ended it sooner), so step 2 after step 1 only meets a
string first struck inside the extension.

Inside an unsettled gesture (a held run of note or edge keys) the normalizer runs on each candidate
replayed from the pre-gesture chart, never on that base, so every extend, crop and drop it makes
reverses as the gesture steps back. Once the gesture settles, the result is written and only undo
restores it.

A hold verb never relies on it: a verb whose result would need cropping or dropping is refused
with the refusal flash.

## 4. Verbs

| Target | Keys | Result |
|---|---|---|
| Derived span | `Ctrl+H` | An unnamed hold exactly over the span; refused if invalid |
| Derived span | `Alt+Left` / `Alt+Right` | Moves the start one note onset, converting the span to a hold; refused if invalid |
| Derived span | `Alt+Shift+Left` / `Alt+Shift+Right` | Moves the end one note boundary, converting the span; refused if invalid |
| Derived arpeggio span | `Delete` | A `none` hold over the span; refused on a chord span |
| Authored hold | the same edge keys | Move its edges; refused if invalid |
| Authored hold | `Delete` | Removes it |
| Time selection | `Ctrl+H` | An unnamed hold exactly over the selection; refused if invalid |
| Note selection | `Ctrl+H` | One unnamed hold from the first onset to the last tail end; refused if invalid |
| Bare caret | `Ctrl+H` | Refused |

**Overlap.** Every NEW hold, whatever verb authors it (a selection's `Ctrl+H`, or a derived span
converted by `Ctrl+H`, an edge key or `Delete`, such as a chord box inside a `none` hold), carves
its range out of every authored hold it overlaps: one it straddles is cropped to the new hold's start or end; one it lies inside is
split into the parts before and after it. Each surviving part must be valid, or the verb is
refused.

**Pushing.** An edge key that moves an edge into a neighbouring hold pushes that hold's facing
edge along with it. Both holds must stay valid and the pushed one non-empty, or the press is
refused. A held run of edge keys is one gesture that replays from its base on every press, so
stepping back restores the pushed hold; once the gesture settles, the result is written and only
undo restores it. The branch's edge gesture (`f3aed178`, `ChartHoldEdgeGesture`) is the part to
reuse.

There is no `Alt+Delete` for holds: an end is required and is removed only with its hold. `Enter`
and the grip picker belong to `60c`.

## 5. Marks and format

- **2D:** authored holds solid, derived spans faded (the derived fret-hand chip's weight), `none`
  holds as dashed neutral rails (a new `EditorTheme` role). No end tick: every end is stated.
  Reuse the branch's `b660bf94` paint core and `248088ab`'s span selection.
- **Save format:** `holds[] {at, end, grip?}`; `grip` absent is unnamed, `"none"` is `none`
  (`60c` adds template ids to the same field). The loader rejects overlapping or empty holds, then
  the normalizer runs; a rewrite it makes at load is logged and marks the document dirty, so it is
  visible and saved.
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
   resolutions identical (zero row), and the count it refuses must be zero or each case explained
   and ruled before step 3.
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
