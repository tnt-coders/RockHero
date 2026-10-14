# Plan 60b — Holds: what the hand holds

## Status

**Sub-plan of `docs/plans/roadmap/60-the-hand.md`, split out 2026-10-04. NOT STARTED.** It begins
after `60a-fret-hand-positions.md` is finished. Nothing here is built: no hold record, no `grips`
dictionary, no edge verb. Re-verify every anchor below against the code before execution. The
design section is the 2026-10-13 form and wins over anything below it.

Section numbers are the family's (the master's §0 has the map): §§1–6, the G60 gate record of §9
and §§10–12 are in the master.

## The hold — the design (RULED 2026-10-13; supersedes the 2026-10-04 form)

Saved as `holds[]`; new. A hold is the charter's statement about one stretch of the song: where a
grip begins, optionally where it ends, and optionally which grip it is. It replaces the authored
span marker, the span-free zone and the bare boundary marker of the earlier records. The grip
reference rides the hold because a derived span has no record of its own, and a stored front the
derivation OBEYS is the one anchor that restates nothing: it is a pin, never a copy kept in sync.

### The records

- **`holds[] {start, end?, grip?}`**, starts strictly ascending and unique (the rule
  `fret_hand_positions` already carries), no start inside another hold's authored `[start, end)`.
  - `start` is a PIN the derivation obeys: any standing span closes there, and the span of the
    notes from there fronts there. Always an onset. The derived front at a pin IS the pin.
  - `end` absent: the close follows the notes, as every derived close does. Present: the close is
    stated, covers rests, and is the one edge that does not follow note edits.
  - `grip` absent: ONE GRIP IS HELD, its content whatever the notes inside sound. The id of a
    `grips` entry: that grip, including the stops nothing sounds. `"none"`: NOTHING IS HELD, which
    requires `end` (nothing inside derives a close) and blocks arpeggio founding.
  - No fret is ever stored on a hold (2026-09-29: a held stop is never typed).
- **`grips[] {id, stops, fingers?, chord?}`**, song-scoped (Phase 5, below).

A hold-authored span takes every note inside its extent that fits, ringing or not: a muted or
staccato arpeggio out of one shape is charted by stating the hold, since the sound never proves it.
Derived yields, authored blocks: no derived span overlaps a hold, a hold's edge takes notes from a
derived neighbour and walls at another hold. Derived spans are unchanged: they never bridge a gap,
and their ends follow the tails.

**What a pin founds.** A `{start}` with no `end` founds a span at the hold's floor of two members
— two stops stated at its onset, or one there and a second that joins while the first still rings
— where sound alone would need three; its close then follows the continuity law like any derived
close. A pin therefore states a two-note grip and a front, and nothing more: over notes that never
ring together it founds no span, so the extent of such a figure is stated with `end`.

### What is derived

One function, `resolvedGrip(span)`: the entry of the hold pinning the span's front, when its
`grip` is an id; otherwise nothing — the anonymous digits, no name, no fingering, no guess. The
order is notes → spans (holds obeyed as pins, walls and statements) → `resolvedGrip` → positions
(the fingering pins the anchor, unsounded stated stops join coverage) → Phase 6's clip at derived
shifts. Nothing before positions reads them.

### The verbs

| Verb | Writes |
|---|---|
| `Ctrl+H` over selected notes | `{start}` at the first onset, two or more notes, selected; real at once where it states something (a two-note grip, a new front), else silent until its end is walked or a grip assigned |
| Assign a grip to a derived span | `{start: its front, grip}`; nothing changes on screen |
| Assign a grip to a hold | its `grip` |
| Clear a grip | unsets `grip`, nothing else; a hold left stating nothing is silent (the commit law, below) |
| `Alt+←/→` on a hold | its `start`, one onset per step: a start between onsets states nothing new |
| `Alt+←/→` on a derived span | `{start}` at the new onset: a pin, its end and content derived |
| `Shift+Alt+←/→` on a hold | its `end`, one note per step: growing, to the next fitting note's ring end, the wall (a misfit's onset, another hold, a stored position) the last stop; shrinking, back to the previous member's ring end, walled at two members or a struck chord. On a pin that founds nothing, the first step lands on the ring end that gives the hold its two members |
| `Shift+Alt+←/→` on a derived span | `{start: its front, end}`, stepped as on a hold |
| Delete a derived arpeggio span | `{start, end, grip: "none"}` over its musical close |
| Delete a hold | nothing: the derivation takes the stretch back |
| `Ctrl+Shift+H` | selects the hold under the caret; a hold is also selected by its brackets |

**The commit law governs holds** (`keymap-matrix.md`, the keyframe commit law; user,
2026-10-13). A hold is SILENT where it changes nothing the notes derive: a `{start}` at a front
the notes date anyway, an `end` at the derived close. A silent `end` is never written; a hold
silent whole pushes no undo entry, lives while selected, dissolves when the selection leaves it,
and is never written. So `Ctrl+H` over one ringing span plants a provisional pin and nothing is
saved until the charter walks its end or names it — the user's "save only the start" — while over
a two-note accumulation the same press is a real statement at once. An end walked onto the
derived close is silent, so a held run passes straight through that instant; a start walked back
onto the front the notes date is the same. A held run is one undo entry through
`commitChartGestureStep`, retired where it changed nothing. Acceptance case for
the start key (`first-releasable-editor.md`, sighted): the Torn intro's span fronts at `3:3+1/2`;
stepping its start back to `3:1` swallows the G-string 7 at `3:3` (above the grip's 5) and
restores the front. A held stretch emptied of its notes stays, drawn empty; only Delete removes a
statement.

### Walls and floors

Every wall is a hard stop with the refusal flash (the 2026-09-05 dissolve ruling: removal is
Delete, and two verbs do not produce one outcome). Growing a held stretch walls at the first note
that does not FIT — the extend law, master §9 item 9, rewritten without `held`: a note fits where
it sounds on a string the grip leaves free, restates the grip's stop, or sounds above the stop the
grip holds on its string; never an open string, a fret-hand harmonic over a gripped stop, or
through the travelled range. A swallowed note's held stop is what `chartHeldStops` derives from
the covering grip, so no per-note field exists. Growing a not-held stretch walls at any struck
chord: a box is a grip held. Shrinking walls at a struck chord or two members — the held
stretch's floor is TWO notes, not the accumulation minimum of three (one stop is a note, two held
at once is a grip; the authored two-note span is the escape the signed minimum owes), and a
not-held stretch's floor is a non-empty extent. Both directions wall at another hold.

**Edges never change a tail** (user, 2026-10-13: lengthening tails would make a span with rests
inside impossible to author). A fretted note may ring past its bracket's end.

**Edges step by the notes, never the grid** (RULED, user, 2026-10-14): starts by onsets, ends by
ring ends. An end between a member's ring end and the next onset covers rest and nothing else, so
it states nothing the next ring end does not; the one between-events stop is a wall, where the end
lands at the misfit's onset. A hand released in a rest before a shift is a wall's answer (a
stored position walls the hold). There is no fine tier: off-grid placement is the snap mode, and
the edge keys do not read the grid. The derived close is a member's ring end, so walking back onto
it is a stop by construction, and silent.

**A hold across a position shift** (RULED 2026-10-13): refused across a STORED position change,
since the charter said the hand moved; across a DERIVED shift the hold wins and pins the position,
as a fingering will. Phase 6 still clips derived spans at derived shifts.

### The hold normalizer

Never refuse a note. One normalizer, run by every producer as `normalizeSustainOverlaps` is —
typing, re-fretting, moving, pasting, load, a moved edge, a re-pointed grip, an edited entry —
keeps every hold its own normal form:

- An authored `end` is TRIMMED to the first onset inside that does not fit; for `"none"`, the
  first struck chord. The notes after it are derived, never a second held piece.
- A `grip` whose stops no longer fit the hold's notes is CLEARED. Fixing the note back does not
  restore it; undo does, and the "?" chip, the unresolved count and the save walk catch the span.
- A hold with a derived end needs no clause: a misfit note closes it as it closes any span.

The rewrite lands in the note edit's own undo entry, and each one writes a log line naming the
hold, the note and the grip. GP import writes notes and never a hold, so it meets none.

### Silent holds

Holds need no Prune command: the commit law above sheds what states nothing (RULED, user,
2026-10-13, over keeping an authored end until an explicit Prune). A note edit that
makes a stored hold silent — tails grown out to its authored `end`, or a figure that now founds
at its pin by itself — sheds it in that edit's own commit, as a point the path now passes through
anyway dissolves; a selected hold lives until the selection leaves it. The builder's one cost:
"states nothing" is answered by deriving without the hold, which holds are few enough to afford.

### Export

Every span's `resolvedGrip` is non-empty (user, 2026-10-06: a valid chart's every span should have
a template to be fully complete). Save WARNS on unresolved spans, export is BLOCKED until none
remain, and the chrome shows a standing count.

### The 2D marks

Draw what is stored and nothing for what is derived; all editor-only, through EditorTheme roles,
and the highway draws the derived result. The label chip at every span's start (built during 60a
Phase 2) is the START's tell: FILLED in the span color for a hold, HOLLOW for a derived span,
printing the grip's name or "?". An end tick in the span color draws only where `end` is stored.
A silent hold draws as a hold for as long as it lives, so the charter sees what the next key will
give meaning to.
A not-held stretch is no span and has no grip to name, so it gets NO chip: its own rails over its
whole duration are the tell for the record entire, with its end tick. PROPOSED (user,
2026-10-13, open to options): it reads by PATTERN, not by hue — hatched or dashed rails, or a
hatched band, in a neutral EditorTheme color — because a theme may use red for its own chord or
arpeggio ink, so a red "no span" style would clash. To be chosen from rendered options at a
sighting.

### The grip dictionary (`grips`, Phase 5)

Song-scoped; a hold's `grip` points into it. An entry states the stops, with optional fingers, and
an optional `chord` name. PROPOSED, not yet ruled: export requires `chord` on an entry of three or
more strings, while the file accepts its absence, on the plan's own save-warns / export-blocks
tiers. The entry's fields are Phase 5's design.

**Where a grip's name shows (user, 2026-10-06).** The 2D tab lane already draws a LABEL CHIP at
every span's start (built ahead of this plan, during 60a Phase 2): a rectangle with rounded top
corners in the span's rail color, standing on the ruler's bottom edge between the measure numbers
and the fret-hand position chips, left-aligned to the span's start and held at the visible left
edge while its span is scrolled. It prints "?" for a span with NO TEMPLATE (user, 2026-10-06:
"while the chart is VALID every span SHOULD have an associated template to be fully complete"),
reversing an earlier blank-chip choice; abutting spans are separated by a 1 px trim of the
earlier span's rails. Phase 5 feeds the chip's name, and must decide how a template WITHOUT a
`chord` name reads, which is neither "?" nor a name. Owed here: (1) the grip name on the 3D
highway, NOT as a trapezoid, in the conventional in-lane style the user has in mind; (2) a span-start mark on the highway, world-sized, since a pixel gap cannot
read through the rails' glow there. Until (1) lands, the 2D name is a surface divergence this plan
closes.

### Carried from the older records

`docs/plans/todo/span-marker-redesign.md` and `first-releasable-editor.md` hold rulings this
plan's split did not bring across and the design above does not restate:

- **Grip entry content** (2026-09-05, fingering conversation; Phase 5 re-confirms): `{name,
  fingering, stops}`, fingering keyed by string, stacked fingers first-class (the 1-3-4 power
  chord), a barre one finger across strings. An entry's frets are authoritative only for stops
  nothing sounds. A grip may name either stop of a note that has two (the sounding fret or the
  held one beneath it).
- **The picker's ranking cascade** (user, 2026-09-05): entries this chart already uses for the
  same grip, then (later) the user's global usage, then a shipped commonness rank measured from
  the corpus. A future global library copies entries INTO the song on first use, collapsing
  identical definitions; nothing in the format references it.
- **The FHP coupling's arithmetic**: the fingering pins the anchor, `anchor = floor − (floor's
  finger − 1)`, a barre at the barre fret; unsounded stated stops join the window's coverage.
- **Purge-on-save candidates** (Phase 3 item 6 there): purge project-local only; never purge and
  report unused entries at export; purge at export.
- **Read first at the Phase 5 session**: `docs/plans/todo/arpeggio-authoring.md`'s storage F, the
  fret-optional hold marker shipped 2026-08-26 and replaced the next day. The hold is that idea
  returning, and the survey that killed it is the risk list.

### Superseded today (2026-10-13)

Each earlier ruling the design above reverses, with its old wording:

- **"The END steps one grid quantum, since an end is tail-valued and sits anywhere"** (2026-09-05,
  restated 2026-10-13; reversed 2026-10-14): that reason belonged to the gesture that lengthened
  tails; with tails untouched, the end steps by ring ends.

- **"`start` … always comes with an `end`"** (2026-10-04): `end` is optional; `start` still names
  a range's start, and the range's close is derived where no `end` is stored.
- **"The grip rides the hold at its current extent"** and the tension "resolving a span freezes
  it" (2026-10-04): a grip now rides a pinned start whose end follows the notes, so naming a span
  freezes nothing.
- **"A conflicting note inside a held stretch is REFUSED"** (2026-10-04), then **"a contradiction
  SPLITS the hold"** (read-time, earlier today) and **"a misfitting grip resolves to nothing"**:
  the hold normalizer trims an authored end and clears a misfitting grip instead. A record the
  derivation silently overrides is not its own normal form; a refusal reserves the flash for
  states that cannot exist, and a clashing note is a changed mind.
- **"Struck chords inside a not-held stretch stand as chord spans"** (2026-10-04): the not-held
  edge walls at a struck chord and the normalizer trims at one, so no box ever stands inside a
  not-held stretch. A box inside one could never have been named: the stretch's one field was
  `"none"`, holds do not overlap, and export requires every span named.
- **The 2026-09-01 authored start line** behind the heads: the label chip's filled style is the
  start's tell; no second mark states the same fact.
- **"Once a span is authored, NEITHER edge follows later note edits"** (2026-10-04): only an
  authored `end` is fixed; a pinned start is remedied by `Alt+←/→`.
- **"The bare boundary marker is replaced"** (2026-10-04): `{start}` alone returns as the
  smallest hold statement, which the start key must write anyway.

### Still OPEN

- The not-held stretch's look (no chip; a pattern rather than a hue): options to sight.
- By eye, from rendered options at a sighting (the user will choose among them): whether a filled
  chip reads as "authored" against a hollow "?"; the end tick's strength; whether a trimmed end's
  tick should flash.
- The fit test's exact clauses, checked against the span derivation before the hold is built;
  no user input expected unless a real question surfaces.
- 60-Q3 (the fingering model and the GP parser's fingering scope), purge-on-save, and the
  derived-default question 60-Q5 (ruled NO, reopenable there) — the Phase 5 session's.

## 7. Phases — 4 and 5

### From the former Phase 3 — its items 3 to 5

Carried here because they are the span's: how a span or a hold is selected, the boundary
gestures the redesign closed, and the seam's census row. Phase 3's REVISED note in
`60a-fret-hand-positions.md` says how each reads now.

3. **Rail selection and the span's selected ground**: a click on a span's rails selects its hand
   event (`tabShapeRailBounds` exported from the paint core beside `tabFhpChipBounds`);
   `chartSpanRevealed` gains a fourth ground, the selected event, so a selected span draws to its
   musical close; the game-shared paint core stays selection-free.
4. **The edge keys** (RULED; user, 2026-09-05, restated 2026-10-13): with a span or a hold
   selected, `Alt+←/→` moves its START and the note-tail keys (`Shift+Alt+←/→`) move its END —
   each key one edge, never both. The verbs table, the walls and floors and the tail ruling in the
   design section are the whole rule: the start steps by onsets and the end by ring ends, one
   note per press; on a derived span the first step authors the hold the
   table names, which is the one way to state a span across rests. A position marker's
   `Alt+←/→` (built) is unchanged: one placement quantum, refused at a duplicate front or off the
   chart's start. The content-editing front move of 2026-09-05 stays STRUCK (§9 item 8), and the
   2026-09-05 end form that lengthened tails is superseded by the tail ruling. `Delete` on a
   DERIVED span waits for Phase 4's hold.
5. **Riding the seam**: publish the OPENING SLOT on `ChartShape` (Q-D) so the strike-less census
   row is exact — NEEDS VERIFICATION whether ~~`bracket_position`~~ `bracket_place` (renamed;
   `std::optional<Fraction>`, `chart_shapes.h:284`, verified 2026-10-04) / `landing_opened` (still
   present) already answer it; `bracket_place` probably does.

**Tests owed with item 3** (moved from `60a-fret-hand-positions.md` §8 on 2026-10-08, since they
are the span's): pointer selection from rails; `chartSpanRevealed`'s selected ground (today it
answers to the lane reveal, a selected note and the caret, and spans are not selectable);
`test_tab_paint_core.cpp`'s exported rail bounds (`tabShapeRailBounds`, not yet built) matching
the drawn rails.

**Sighting with item 3** (moved from the same brief): clicking rails; the rail-highlight
strength; a span ending near the cursor; a landing successor abutting its predecessor.

### Phase 4 — span-free zones and the settlement census (needs Phase 3)

**This phase builds THE HOLD** as the design section states it: the record `holds[] {start,
end?, grip?}` without the grip id (Phase 5's), the pin, the verbs, the walls and floors, the hold
normalizer, the commit law's silent holds, the 2D marks, and the not-held polarity that was plan
61's span-free zone (one kind with polarity; it blocks accumulation-founded openings only,
statement openings are immune, a derived span closes at its edge, `Delete` on a derived arpeggio
span authors it, its ink is editor-only). The background sighting triplet is judged here. The
settlement edge (the 2-note statement-founded bracket of the sighting reel's measure 3) was
CLAIM-founded, and claims are gone, so it is most likely moot — re-verify on the regenerated reel
(`span-marker-redesign.md`, "THE SIGHTING FIXTURE NEEDS REBUILDING": the reel's
`"attack": "none"` record no longer reads). The ≥3 minimum is already signed; what lands is the
census re-sign of the accumulation checklist's section G and the FHP counter rows left unsigned
for this bundle.

### Phase 5 — templates, dictionary, editor and fingering coupling (gated on 60-Q3, 60-Q5)

The dictionary is saved as `grips`, its entries carry an optional `chord` name, and the
reference is the hold's `grip`: assigning a grip to a derived span writes `{start, grip}` at its
front, a pin with a derived end, so naming freezes nothing. A grip's stops are stated ONLY in the
entry, never on a hold.

**Prerequisite, unlisted until now:** the GP parser carries no fingering and no chord diagrams
(`gp_score.h`), so the import wiring this phase assumes ("GP chord-diagram fingering assembles
into template entries") needs the parser extended first — its own step, sized before the phase.

Plan 61's Phase 3 and the fingering half of the former plan 60 Phase 3, one arc: the entry save
format and the template editor (the home of span-wide fret editing, and the only place a stop
NOTHING sounds can be stated since `NoteAttack::None` and the stored `held` left — the two lapsed
figures of `span-marker-redesign.md` "THE GAP WIDENED" return here); holds reference entries in
the song-scoped dictionary (chord and arpeggio spans share ONE list; the dictionary's stamp model
is superseded by its own 2026-09-05 section); save WARNS on unresolved spans with EXPORT blocked
until resolved and a standing unresolved count in the chrome; the save-walk resolver with the
grip-matching suggestion sort (never an assignment), whose apply is the assign verb of the design
section; the normalizer's grip clearing; the purge-on-save question; the free-note finger, landing
WITH its writer; the template coupling (`resolvedGrip` feeds positions: the fingering pins the
anchor, unsounded stated stops join coverage, a hold over silence feeds nothing); a template that
reintroduces a stop nothing sounds brings its own justification rule (LAW II's machinery was
deleted as dead, not parked); the derived-default suggestion ships only if 60-Q5 re-confirms.
**The sub-4 width truth amendment** (60-Q1(b)) is partly DISCHARGED by the authored end fret: a
stated window is exactly `[fret, end_fret]`, however narrow, built and
sighted 2026-09-29 on a 3-fret window, so the "does a sub-4 window read oddly" gate has an
answer for the authored case; what this phase adds is the fingering-DERIVED form (both ends
pinned by a template's fingers), which inherits that sighting. The width-consumers audit (highway
window drawer, camera framing, census counters) is largely done with the end fret; re-verify the
camera framing and the census counters only.

