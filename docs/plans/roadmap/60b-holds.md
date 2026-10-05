# Plan 60b — Holds: what the hand holds

## Status

**Sub-plan of `docs/plans/roadmap/60-the-hand.md`, split out 2026-10-04. NOT STARTED.** It begins
after `60a-fret-hand-positions.md` is finished. Nothing here is built: no hold record, no `grips`
dictionary, no edge verb. Re-verify every anchor below against the code before execution; the
phase texts were written for the merged hand marker and the span-free zone, and read through the
design section and their REVISED notes.

Section numbers are the family's (the master's §0 has the map): §§1–6, the G60 gate record of §9
and §§10–12 are in the master.

## The hold — the design (RULED 2026-10-04; from the master's §1a)

Saved as `holds[]`; new.

A stretch of the song with a start and an end, in one of two polarities. It replaces three things
this plan carried: the authored span marker, the span-free zone, and the bare boundary marker.

- **The record:** `{start, end, grip?}`, ascending and non-overlapping. `start` names the start of
  a range and so always comes with an `end` (user, 2026-10-04, over `at` / `until`). `grip` absent
  means ONE GRIP IS HELD, its content whatever the notes inside sound; `"none"` means NOTHING IS
  HELD; from Phase 5 it may instead be the id of a `grips` entry, which then states the content.
  One field, so "none plus a template" cannot be written. No fret is ever stored on a hold (the 2026-09-29 ruling
  stands: a held stop is never typed).
- **A held stretch takes every note inside it that fits, ringing or not.** That is how a muted or
  staccato arpeggio out of one shape is charted: the sound never proves that hold, so the
  derivation never asserts it, and the charter states it with its extent.
- **A conflicting note inside a held stretch is REFUSED,** by chart validation, with ONE fit test —
  the test that walls an edge (§9 item 9's, rewritten without `held`). Entering, re-fretting,
  moving and pasting are refused alike, and a file holding the state does not read.
- **A not-held stretch blocks arpeggio founding only.** Struck chords inside it stand as chord
  spans: a chord span is the minimum proven truth of a grip and can be neither shrunk nor deleted.
- **Derived yields, authored blocks.** No derived span overlaps a hold; a hold's edge takes notes
  from a derived neighbour and is refused at another hold.
- **Derived spans are unchanged:** they never bridge a gap, and their ends follow the note tails.

The verbs, and what each saves:

| Verb | Saved |
|---|---|
| `Ctrl+H` over selected notes | A held stretch, first onset to the last ring's end |
| Move either edge of a derived span | A held stretch at the new extent — the span is now authored |
| Delete a derived arpeggio span | A not-held stretch over it |
| Move an edge of a hold | That edge |
| Delete a hold | Nothing: the derivation takes the stretch back |

- **Edges move outward** to a conflicting note or another hold, and **inward** to a struck chord
  or the founding minimum, a HARD STOP (the 2026-09-05 dissolve ruling, for its own reason: removal
  is Delete, and two verbs do not produce one outcome).
- **Resizing never changes a tail.** A fretted note may therefore ring past its bracket's end, the
  picture the zone design already accepted.
- **The cost accepted:** once a span is authored, NEITHER edge follows later note edits.
- **A held stretch emptied of its notes stays,** drawn empty; only Delete removes a statement.
- **`Ctrl+Shift+H`** jumps to the hold under the caret; a hold is also selected by its brackets.

### The grip dictionary (`grips`, Phase 5)

Song-scoped; a hold's `grip` points into it. An entry states the stops, with optional fingers, and
an optional `chord` name. PROPOSED, not yet ruled: export requires `chord` on an entry of three or
more strings, while the file accepts its absence, on the plan's own save-warns / export-blocks
tiers. The entry's fields are Phase 5's design.

### Still OPEN

- **How a not-held stretch draws.** The 2026-09-01 zone rulings carry (editor-only, its own span
  style, red-leaning, the authored start line). PROPOSED: its bracket breaks around each chord
  span inside it and is still one object. A sighting, with the UI design review.
- **A held stretch across a position shift.** Phase 6 clips a derived span at a shift; whether an
  authored hold is refused across one, or pins the position the way a fingering will, is ruled
  with Phase 6.
- **The fit test's exact clauses**, re-read against the span derivation before the hold is built.

## 7. Phases — 4 and 5

### From the former Phase 3 — its items 3 to 5

Carried here because they are the span's: how a span or a hold is selected, the boundary
gestures the redesign closed, and the seam's census row. Phase 3's REVISED note in
`60a-fret-hand-positions.md` says how each reads now.

3. **Rail selection and the span's selected ground**: a click on a span's rails selects its hand
   event (`tabShapeRailBounds` exported from the paint core beside `tabFhpChipBounds`);
   `chartSpanRevealed` gains a fourth ground, the selected event, so a selected span draws to its
   musical close; the game-shared paint core stays selection-free.
4. **The boundary gestures, as ruled (§9 item 8)**: `Alt+←/→` on an event WITH a marker moves
   the marker one placement quantum (built — the marker owns the boundary and the derivation
   answers), refused with the refusal flash at a duplicate front or off the chart's start; a
   DERIVED span front has NO move verb — the signed content-editing front move (tail rewriting at
   the boundary populations) is STRUCK, and a derived span changes only through the sound (the
   tails' own verb) or a marker. The END gesture (the tail keys move the co-terminating end
   through the content, admitting a note only where it FITS — the extend law of
   `first-releasable-editor.md`, signed as §9 item 9 and rewritten without `held`: a note fits
   where it sounds on a string the grip leaves free, restates the grip's stop, or sounds above the
   stop the grip holds on its string — never an open string, a fret-hand harmonic over a gripped
   stop, or through the travelled range; a swallowed note's held stop is then what
   `chartHeldStops` already derives from the covering grip, so no per-note field exists or is
   needed) is bulk tail editing, not a boundary authority, and stays an OPEN question for this
   phase's session: build it, or let the selected members' own tail keys serve. If built: hard-stop
   at a contradiction or the founding minimum with the loud refusal style, one undo entry per
   gesture through `commitChartGestureStep`. `Delete` on a DERIVED span waits for Phase 4's zone.
5. **Riding the seam**: publish the OPENING SLOT on `ChartShape` (Q-D) so the strike-less census
   row is exact — NEEDS VERIFICATION whether ~~`bracket_position`~~ `bracket_place` (renamed;
   `std::optional<Fraction>`, `chart_shapes.h:284`, verified 2026-10-04) / `landing_opened` (still
   present) already answer it; `bracket_place` probably does.
### Phase 4 — span-free zones and the settlement census (needs Phase 3)

**REVISED 2026-10-04 (§1a) — this phase builds THE HOLD.** The zone is the hold's not-held
polarity and is no longer a record of its own: one kind, `holds[] {start, end, grip?}`, answers the
"one kind with polarity vs two kinds" question below. What carries from the zone text: it blocks
accumulation-founded openings only, statement openings are immune, a derived span closes at its
edge, `Delete` on a derived span authors it, and its ink is editor-only. What changes: BOTH
polarities are resizable from either edge, "a marker inside a zone is refused" becomes "holds do
not overlap", and the held polarity (the charter's own span, with its refusal of a conflicting
note) is built here beside it. The census re-sign stands.

Plan 61's Phase 2, unchanged in substance: the zone record (location-anchored range; one kind
with polarity vs two kinds is the naming question — nothing is built, and the record's
`SpanFounding` discriminator is a planned name, not a symbol) and its derivation wall (no
accumulation-founded opening inside, spans close at its edge, statement openings immune);
`Delete` on a derived span AUTHORS a zone over its musical close; zone resize via the note-tail
keys, BLOCKED at an authored marker (the precedence law: derived yields to the zone, authored
blocks it, only explicit deletion changes an authored statement); zone ink editor-only (red
leaning, EditorTheme roles); a marker inside a zone refused at authoring; the background sighting
triplet judged here. The settlement edge (the 2-note statement-founded bracket of the sighting
reel's measure 3) was CLAIM-founded, and claims are gone, so it is most likely moot — re-verify
on the regenerated reel (`span-marker-redesign.md`, "THE SIGHTING FIXTURE NEEDS REBUILDING": the
reel's `"attack": "none"` record no longer reads and claims cannot replace it). The ≥3 minimum is
already signed; what lands is the census re-sign of the accumulation checklist's section G and
the FHP counter rows left unsigned for this bundle. (Verified 2026-10-04: nothing here has been
renamed, and the span record's "sweep STILL OPEN" is moot — the sweep is deleted.)

### Phase 5 — templates, dictionary, editor and fingering coupling (gated on 60-Q3, 60-Q5)

**REVISED 2026-10-04 (§1a).** The dictionary is saved as `grips`, its entries carry an optional
`chord` name, and the reference rides the HOLD (`holds[].grip`), not a hand marker: read "markers
reference entries" and "authors the reference on the marker at the span's front" below as the
hold's `grip`. A grip's stops are stated ONLY here, never on a hold. Everything else in this phase
stands, re-verified at its own session.

**Prerequisite, unlisted until now:** the GP parser carries no fingering and no chord diagrams
(`gp_score.h`), so the import wiring this phase assumes ("GP chord-diagram fingering assembles
into template entries") needs the parser extended first — its own step, sized before the phase.

Plan 61's Phase 3 and the fingering half of the former plan 60 Phase 3, one arc: the template
save format and the template editor (the home of span-wide fret editing, and the only place a
stop NOTHING sounds can be stated since `NoteAttack::None` and the stored `held` left — the two
lapsed figures of `span-marker-redesign.md` "THE GAP WIDENED" return here); markers reference
entries in the song-scoped dictionary (chord and arpeggio spans share ONE list; the dictionary's
stamp model is superseded by its own 2026-09-05 section); save WARNS on template-less spans with
EXPORT blocked until resolved and a standing unresolved count in the chrome; the save-walk
resolver with the grip-matching suggestion sort (never an assignment) — its apply authors the
reference on the marker at the span's front, which is also what gives a span a durable identity;
the invalidation rule (contradicted-tell vs reference removal — the record flags the user's
confirming word as owed); the purge-on-save question; the free-note finger, landing WITH its
writer; the template coupling per the scoping law (the fingering pins the run its marker heads,
unsounded stated stops join that run's coverage, contradiction is a per-onset test that drops the
reference to notes-only fallback, references over silence feed nothing, no span extent
consulted); a template that reintroduces a stop nothing sounds brings its own justification rule
(LAW II's machinery was deleted as dead, not parked); the derived-default suggestion ships only
if 60-Q5 re-confirms. **The sub-4 width truth amendment** (60-Q1(b)) is partly DISCHARGED by the
authored end fret: a stated window is exactly `[fret, end_fret]`, however narrow, built and
sighted 2026-09-29 on a 3-fret window, so the "does a sub-4 window read oddly" gate has an
answer for the authored case; what this phase adds is the fingering-DERIVED form (both ends
pinned by a template's fingers), which inherits that sighting. The width-consumers audit (highway
window drawer, camera framing, census counters) is largely done with the end fret; re-verify the
camera framing and the census counters only.

