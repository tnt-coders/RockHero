# The hand-marker stopgap: editable fret-hand positions before plan 60

**Status: BUILT and merged to master 2026-09-26** (`753498f9`, `8b7831f7`, `391c0104`, with the
sighting fixes `ac36e557` and `4298e161`). Plan 60 takes over from here.

*Ruled 2026-09-25. Built on the branch `hand-marker-stopgap`. The user's words: "the BIGGEST pain
with editing charts right now is no ability to insert or delete frethand positions"; plan 60
(derived positions, markers as overrides) is deliberate and stands, and this is the interim form
of its Phase 0 and Phase 3 built early on the stored stream, so that nothing here is thrown away
when the derivation lands.*

## 1. Rulings

- **Width is derived, never stored** (user): `width = max(4, highest stated fret SOUNDING in
  the placement's stretch − fret + 1)`. The stretch runs from the placement up to the next one
  (the last to the chart's end); the stated frets are every fretting-hand stop sounding in it —
  onset frets, pitched mid-ring keyframe frets at their positions, a tap's held stop, and a
  note struck before the stretch whose true ring still sounds at its start (a finger still down
  is a stop the chart proves; the same evidence the floor light reads). Open strings and bare
  taps count for nothing; a note's end statement counts nothing (the hand is leaving). A stated
  fret BELOW the authored fret does not widen: it stays uncovered, honest feedback that the fret
  is wrong. `FretHandPosition` loses its `width` field (a stored copy of a derived fact would be
  code that lies), the importer's width logic is deleted with it, the importer emits a placement
  only where the fret changes (its fabricated same-fret placements only split stretches now), and
  the format drops `"width"` with no back-compat path. A user MAY insert a same-fret placement
  deliberately, to narrow the window after a wide passage; the validator allows it.
- **A tap's held stop is the COMPLETE one** (`chartHeldStops`: its plant, else the covering span's
  default — held stops are derived-only since 2026-09-29), passed to the derivation as a column.
  The one "fretting-hand stop at an instant" authority is `fretHandStopAt(note, held, offset)` in
  `chart.h`, with `fretFor` its offset-0 case; the census rig asks it, and the importer's
  `statedStopAt` asks it with no held stop, the stream being unresolved there. Since the
  2026-09-29 change a bare tap's DEFAULT counts toward the window like any other held finger (it
  used to be invisible to it); with half-open span coverage the default is nearly always 0, so
  this seldom widens anything. The floor light's evidence reads the same table's fret
  (`NoteViewState::stop_mark`).
- **Phase 0 deviation:** the reader does NOT refuse an unsorted `fhps` stream at parse — the
  validator runs on every load and nothing searches the stream in between, so the parse-time
  check was the order rule stated twice. The validator refuses equal positions.
- **Fret stays authored** (defaulting at insert to the lowest stated stop in the stretch).
  Deriving it is plan 60's whole problem — the index-finger position a 7-8-9 run is played from is
  what the notes do not prove — so the marker keeps its one payload.
- **The chords are the ruled hand-marker pair**, `Ctrl+H` (author / restate at the cursor) and
  `Ctrl+Shift+H` (jump onto the hand row): when plan 60 lands, the same chord becomes the merged
  hand object, and the position half of that object is exactly what this stopgap authors.
- **Spans stay derived.** The stopgap does not merge the span into the object (60-H1); it authors
  positions only.

## 2. Steps

1. **Phase 0 hygiene + derived width** (core): the validator and reader refuse equal positions
   ("strictly ascending, unique by position"); `FretHandPosition {position, fret}`; the width
   derived once per projection by one core function into `FhpViewState::width`; tests at the
   rule's breakpoints; the corpus census re-run, its fret-hand-window rows re-pinned with the
   ruling named where they move.
2. **The hand marker kind** (editor core + ui), through the marker grammar's own checklist
   (`marker-verb-grammar.md`, "What a NEW marker kind must do"): the chord target beside the
   section and tone targets; `MarkerRow` arm in `markerStarts` / `markerSelectionAt` /
   `selectedMarker`, the focus-row stack between the time signature and the strings; the letter
   declared once in `editor_command_registry.cpp` and both chords composed from it; `Delete` and
   `Alt+←/→` arms; `commitMarkerModel` snapshot for the `fhps` stream; the lane's FHP chips are
   the row's objects. Insert defaults the fret to the lowest stop the new placement's stretch
   holds, asked of the one held-stop fold the width reads (so a ring still sounding at the cursor
   counts, and a new FIRST placement takes the notes before it, as the window does) — else the
   previous placement's fret, else 1. *Refinements from the step's simplicity pass:* one
   `markerCanStartAt` bound shared by every marker kind (the hand kind had none, and sections and
   tones each carried a private copy); a kind with nothing to prompt for publishes no verb — its
   chord runs its target inside one action; no `Enter` arm until there is a payload to re-enter;
   the lane's chip press is gated by `marker_edits_enabled` like every marker surface.
3. **Fret entry** on a selected placement — the one piece plan 60 may retire (or keep as the
   override path). No new key: the keymap's digit law already says a bare digit finds its operand
   — the selection, else the caret's slot — and retypes a selection, so a selected placement is one
   more operand `Type Digit N` retypes: the digits state its fret through the shared 750 ms
   multi-digit entry, committed through the funnel ("Set Hand Position Fret"), refused with the
   red box on the lane chip where the fret is illegal for the board. No `Enter` arm.

## 3. What survives plan 60

Everything in steps 1-2: the strict stream, the derived width, the marker kind, its row, selection,
chords, funnel snapshot and move/delete arms are Phase 0 and Phase 3 deliverables. When derivation
lands, the marker keeps its position and drops its payload; step 3 becomes the override path or
goes.
