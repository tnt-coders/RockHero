# Ring ends at their true instant, and the chart lane's two authoring planes

Status: DESIGN DECIDED 2026-09-23, reviewed the same day (three read-only reviewers; every finding
folded in below); not built. Merges and replaces `chart-lane-authoring-planes.md` and
`ring-end-display.md`. Baseline: HEAD `ce0db3a2` (the tick-lattice follow-up has landed: every
editor verb produces lattice positions, restored carets snap onto ticks). Re-verify every code
claim below against the tree before a phase starts; line numbers are as of the baseline.

The two halves are one design because they meet at the ring's end: the display decides where a
ring's ink stops and what is judged, the keys decide what reaches the ring's end, and each assumes
the other.

## Where we are (HEAD ce0db3a2)

- **Display.** Presentation rule 1 (`chartPresentation`, `chart_presentation.cpp`) trims every
  tail one minimum sustain distance before the next onset on ANY string that the ring does not run
  strictly past (`ringPassesHead`), and rule 2 (`trimToMargin`) CARRIES the end statement back to
  the trimmed end through `clipPayloadsToSustain` → `setEndStatement` (`chart_rules.cpp`), with a
  halfway fallback where the margin would crowd it (`lastStatementClearance`), an interior floor
  (`lastInteriorStatementEnd`) and a shake window past a statement that leaves the string shaking
  (`statementsEnd`, `g_minimum_slide_window`). The presented sustain is therefore shorter than the
  stored ring, and the editor carries TWO whole projections to show both (`ChartNoteForm::Actual`,
  `tab_actual` in `editor_controller.cpp`), with `keyframeIdentities` mapping drawn keyframes back
  to stored ones. Both surfaces draw the presented note, so the end statement is drawn early on
  both, and the hand window completes with the rail at that drawn instant (`makeSlideRampStarts`,
  `chart_projection.cpp`).
- **The margin.** `g_minimum_sustain_distance_seconds` is 75 ms (2026-09-22, inside the 70–85 ms
  band rhythm-game charting converged on; 100 ms erased sixteenth-note ring detail above 150 BPM,
  50 ms left about 3 px at the 2D lane's default zoom), floored onto the tick lattice through
  `marginBefore`. Its readers today: rule 1's trim, the span drawn ends (`drawnShapeExtent`), the
  fret-hand window's margin morph (`chart_projection.cpp`), the landed-span tenure in shape
  derivation (`chart_shapes.cpp`), the highway's tap light-rise (`highway_projection.cpp`), and the
  Guitar Pro importer's slide-in scoop cap, which sizes STORED keyframes from it
  (`gp_chart_builder.cpp` ~:2815). The fret-hand morph starts exactly where the old tail's ink
  stops, deliberately (a longer separate lead was tried and reverted 2026-09-23); a morph start
  derived from the ink end is the recorded refinement (`docs/tracking/watch-items.md`).
- **Sighting.** 2D read right; 3D read wrong (the rail and hand window complete short of where the
  sound goes), and any statement drawn early will read out of sync once practice mode slows a
  song down.
- **Reveal.** Only `Alt` reveals a note's stored ring (`chart_reveal.h`); the caret peek and the
  selection reveal were withdrawn at `a4bfed7b` because revealing moved a clicked end chip. Spans
  still reveal on selection and caret (`chartSpanRevealed`).
- **Keys.** A bare digit strictly inside a ring makes a POINT; at a ring's end or on an empty slot
  it places a head, and ON A HEAD it REPLACES the note with a fresh one (`chartCaretDigitTarget`
  → `planInsertNote`, `chart_handlers.cpp`). `Alt+Insert` (`InsertChartStatement`, `0x171C`)
  states the fret in force on a tail and selects a statement already standing there. Bare `Insert`
  is `InsertLanePoint` (`0x1707`), which does nothing on a string row. `Alt`+digit was deleted
  2026-09-22; its command values `0x180B`–`0x1814` stay spent.

## Where we are going

### The display: keyframes at their true instant, ink cropped

**THE ENDING ZONE.** Where a head binds a tail, the zone is the half-open stretch `(crop, head]`:
the crop is `marginBefore(head)`, floored onto the tick lattice as today, clamped to no earlier
than the ring's own onset. The crop itself is outside the zone (a keyframe standing exactly on it
is inked) and the head is inside it (an end statement standing exactly on the head — a bend
written to 100%, a slide-out onto the next strike — is a zone keyframe). A tail is BOUND when its
stored ring ends past the crop of the first
later onset on ANY string that the ring does not run strictly past (rule 1's onset, unchanged:
a ring ending exactly at another string's head is bound by it; a ring stated to ring on past that
head is not). A ring that ends at or before the crop is FREE and draws in full. On the ring's own
string the head supersedes the zone's later statements outright, the string being struck again;
on another string nothing re-strikes, and those statements are simply the ring's ending, closer to
it than the display can show or scoring judge.

What that commits to:

1. **Nothing moves.** Every keyframe keeps its stored instant; no statement is ever drawn at an
   instant the chart does not store. The ink is cropped, never compressed: the leg crossing the
   crop is drawn on its TRUE path toward the zone's first keyframe and simply stops there, having
   travelled part of the way. A bend written to land on the head is drawn rising toward it up to
   the crop; a slide toward the next head is drawn sloping toward it.
2. **Before the crop, everything is inked.** Every keyframe before the crop is reached at its
   instant. The crop is the only place ink stops early: the interior floor and the shake window
   exist to push a trimmed end back out, and go with the trim.
3. **The 2D label** is a destination chip at the crop, drawn only where the leg the ink ends on
   changes something: a slide's or arrival's fret, a bend's amount. It names where the drawn leg
   is heading, which the strike on the same string then shows in its own state. A leg that is
   level across the crop (a bend held into the zone, its release written past the crop) and a
   vibrato-only leg draw no chip. Where the zone's first keyframe states two channels, each
   channel's chip is drawn as it is elsewhere on the lane. The chip is a MARK, not a click
   target: a zone keyframe is selected through a reveal, where it stands at its true instant.
   A fret chip's crop placement is stated once, in `tabKeyframeLayout`
   (`tab_layout_manifest.cpp`), the layout every 2D consumer already shares; a bend chip rides the
   bend line as every bend chip does, bend points never being targets. When the crop is clamped
   to the onset the chip would sit on the head, and is suppressed.
4. **3D has no label.** The rail and the bend curve run their true path to the crop and the hand
   window completes at the TRUE instant — the picture that sighted well. Every hand ramp is filed
   at its keyframe's stored instant, zone keyframes included, so the hand travels with the rail
   and completes where the sound goes; the ramp walk reads the stored notes directly. The
   highway's existing tail tip fade (the last 35% of the drawn extent) applies to the cropped
   extent; "ink" in the scoring contract means the geometric extent, not the fade.
5. **2D is a hard crop.** No fade: the lane records "the bare end is chosen over both a cap and a
   dissolve" (`tab_paint_core.cpp`, the tail painter), and that stands. No new ink primitive.
6. **A reveal adds ink, never moves a mark.** `Alt`, the caret peek and the selection each extend
   a tail's ink, in full ink exactly as the `Alt` reveal draws today (never the ghost-note
   layer, which names a technique), to its stored end, showing every stored keyframe at its true
   instant, hit-testable and ringed there. A keyframe's mark stands in ONE place: the destination
   chip at the crop while the ink cuts the leg toward it, the keyframe's own mark at its instant
   once revealed (built 2026-09-23 this way rather than keeping the chip at the crop under the
   reveal too: the chip is never a target, so no click can select it and see it move, and a
   second chip naming the mark standing beside it said nothing). The
   reveal-only held-stop satellites return with the peek and the selection, as before their
   withdrawal. The peek and the selection were withdrawn because revealing moved a clicked chip;
   once no reveal moves a mark that reason is gone (decided 2026-09-23).

What is lost, deliberately: a gesture that begins and ends inside the zone (a flick up and back
in the last 75 ms) has no visible leg, so it is neither drawn nor scored; a shake switched on
just before the crop shows only the sliver before it (the deleted shake window's job); and a
note whose binding onset lies within a margin of its own onset draws no ink at all. A short FREE
ring is unchanged: it draws in full, and a vibrato end statement cannot stand on it
(`shedEndStatementShake`).

**Scoring is the ink.** Judging what the player cannot see is a critical bug, and so is showing
what is not judged. So the contract is one rule: what is drawn is what is judged. A keyframe before
the crop is judged at its stored instant, where it is drawn; the leg crossing the crop is judged
along its true path as far as the crop, so a bend rising toward the head counts for the rise the
player sees; a sustain is held to the crop; nothing past the crop is judged. A same-string head is
judged as its own onset, and on another string the zone is the ring's ending. (No scoring code
reads the chart yet; this is the contract it will be built against.)

**No presented note, one ink end.** With nothing moved, the "presented note" is a byte copy of
the stored one: `chartPresentation` only ever changed it through the trim and the dropped
tail, and both go. So presentation publishes no note copy at all. `ChartPresentation::notes` and
`ChartResolutions::presented_notes` are deleted, and presentation publishes per note an INK END —
set by rule 1 (the crop) and by rules 3 and 4 (a dropped ring's ink end is its onset), exactly as
the span state carries `drawn_end_seconds` beside `close_seconds` — beside `rested_from`. Every
reader of a presented length then has to choose the stored ring or the ink end, and the compiler
finds every one of them: that is the whole point, since a silent reader of a length that quietly
became the stored ring is the defect class this change would otherwise breed. With it go
`ChartNoteForm::Actual`, the editor's second projection `tab_actual`, `keyframeIdentities`, the
ramp arrival clamp (`chart_projection.cpp` ~:698) and the "two chart forms align by index" watch
item. The one form is what makes per-note reveals possible at all.

The readers, and which length each reads from now on:

| Reader | Reads |
|---|---|
| the tail law's `sustain <= 0` skip and `hasRestingRemainder` (`chart_presentation.cpp`) | the ink end: a tail rules 3 and 4 dropped has no tail |
| `chartHolds` (a lone resting note's hold) and `restedOffsetOf` | the ink end |
| `makeSlideRampStarts` and the hand-window ramps | the stored notes; every ramp filed at its stored instant |
| the crossing leg's value at the crop | ONE helper beside `glideStopAt` (`chart_view_state.h`) returning the leg and its fraction at the ink end, read by the 2D painter, the rail, the tail sample times, the camera and the future scorer |
| `NoteViewState::end_seconds` (2D tail and chips; highway tail, tip fade, slide path, camera framing; `visible_events.h`) | the ink end, published as today's field, with the stored end beside it |
| the 2D cull, the paint core's per-note early-outs, the ghost group bounds, the hit-test candidate range | the STORED end, since any reveal can extend ink to it (`tab_view.cpp` ~:891 already says why) |
| `linkedKeyframe`, `chart_hit_testing.cpp`, selection rings (a `tab_view` overlay, since the controller never learns about `Alt`) | a keyframe past the ink end is neither drawn, hit-testable nor ringed unless revealed |
| `makeHighwayTapOnsets` and chord-group `has_tails` (`highway_view_state.h`) | keyframes before the ink end |
| `nextRowObjectStop` (`chart_handlers.cpp`, the keyboard walk) | every stored keyframe, as the reveal shows it |
| the pending-entry preview and box (`editor_controller.cpp`) | the one projection |
| `chart_legato.cpp` `chartResolutions` | positions only, unchanged |
| the corpus census rows that read a presented length (`test_corpus_census.cpp`) | the ink end, re-pinned in phase 1a |
| Guitar Pro `resolveSlideOutExits` | the STORED ring (it writes exit placements; see below) |

**The importer reads the store.** `resolveSlideOutExits` (`gp_chart_builder.cpp` ~:2912, called
~:3950) places exit fret-hand placements at the presented end and tests for room. It reads the
stored ring from now on, which is what "spacing is display's alone" requires. A slide-out ending
EXACTLY on the next onset — Guitar Pro's default — still counts as having room for the departure
branch, so exit-fret resolution keeps running for those notes and `insertPlacementIfAbsent` keeps
yielding the exit placement to the real one; a slide-out with less room than that stays planted.
Acceptance is a corpus census diff. (The tests at `test_gp_song_importer.cpp` ~:6201-6251 cover
the presentation's halfway floor, not this pass, and go with `lastStatementClearance`.)

**The ring touches the next head in the data.** A ring may end exactly on the next onset on its
string (`sustainBoundOf`), and an end statement may stand exactly there. No buffer is kept: Guitar
Pro rings touch, a bend whose destination sits at 100% of the note — the parser's default —
lands its statement exactly on the head, and two derivations read the touch itself: the legato
hold test (`predecessorHoldReaches`) and the shift slide's arrival (`arrivesIntoNextHead`).

**At most one keyframe in the zone is NOT a chart rule.** The zone relates a ring, the head that
binds it and the tempo, so a tempo edit, a move of the next head, a cut or an import would each
break such a rule on notes they never touched. The display rule makes every later zone keyframe
inert, so nothing needs enforcing. Nor do the keys redirect a zone slot to the zone's first
keyframe: under `Alt` the charter sees the distinct slot they typed on.

**The margin stays one constant, with its readers named.** With nothing moved it decides how early
ink stops, and its other readers (listed under *Where we are*) keep reading it. Because the scoop
cap writes stored keyframes and the landed-span tenure feeds a census-pinned derivation, any retune
of the value carries a corpus census re-pin. The value is confirmed against the built crop; the
decision test is that a long sustain into a same-string head reads as ending clearly short of it,
and a sixteenth-note passage at 160–180 BPM still shows each note's head and chip (its ink is
under a pixel there, which is expected, not a failure). A screen-space clearance was weighed and
not taken: a time gap varies with zoom and playback rate, but with no statement moved the only
mark drawn early is the 2D chip, and the peek shows its true instant. A keyframe ban inside the
margin was rejected: the arrival must stand at the head, and a tempo edit would push untouched
keyframes into a zone measured in seconds.

### The keys: two planes

**The rule.** A key first finds its OPERAND: the selection if there is one, else the caret's slot.
Its PLANE then decides what it does there. A bare key acts on the operand itself. An `Alt` key
reveals the stored ring and acts on the ring that covers or ends at the operand's instant on its
string; where none does, it acts on the operand exactly as the bare key would. So at a slot
holding both a head and a previous ring's end, the bare key is the head and the `Alt` key is the
ring, with no Esc in between, whether the head is selected or only under the caret. The walk never
stops on an EMPTY ring end, so this redirect is the only way to state a new end statement at a
shared instant.

**This amends the uniform-scope law** ("scope is always the selection, never the verb",
`keyframe-and-burst-ground-up.md`, the option B set aside 2026-09-21): an `Alt` key's scope is the
ring at the operand's instant, which may be a ring the selection does not hold. Kept for the
symmetry it gives the keys — every bare key has an `Alt` twin that says "the same, but on the
ring" — and because the alternative (`Alt+Insert` then a digit) makes the commonest keyframe edit
a two-key phrase (decided 2026-09-23). The `Alt+←/→` move and `Alt`+wheel rows are unchanged:
the rule governs the entry keys.

With no selection, at the caret:

| Key | Says | Empty slot | On a head | Inside a ring | At a ring's end |
|---|---|---|---|---|---|
| **digit** | a note here, at the typed fret | places the head | retypes it | cuts the ring | places the head |
| **`Insert`** | a note here, at the fret in force | places the head | selects it | cuts the ring | places the head |
| **`Alt`+digit** | a point on the ring here, at the typed fret | = digit | = digit, unless a ring ends there | states an interior point | states the end statement |
| **`Alt+Insert`** | a point on the ring here, at the fret in force | = `Insert` | = `Insert`, unless a ring ends there | a silent point, to be given a bend or shake | the end statement at the fret in force, silent |

A head and a ring's end can share a slot; there the `Alt` chords take the "At a ring's end" column.
Arming the caret on an object selects it (`chartObjectAt`, which excludes end statements), so "on
a head" and "on a point" with no selection are reached only by a caret walk that lands on the
slot; a slot holding an interior point is then a selection, and the bare digit retypes the point.
**An `Alt` key at a ring's end ADDRESSES the statement already standing there** — retyping it, or
overlaying the channel it states — exactly as `Alt+Insert` does today; it never creates a second
keyframe at that offset, which `planInsertKeyframe` would refuse. **The redirect is a property of
ONE SLOT** — one instant on one string — so it applies when the operand is a single slot: the
caret's, or the one object the caret armed. With a selection of more than one element there is no
single slot, and an `Alt` key acts exactly as the bare key does: it retypes everything selected,
notes or keyframes alike (decided 2026-09-23, replacing the per-element rule of the same day). So
a single selected head where a ring ends is the one case where `Alt` reaches that ring's end
statement, and no planner ever has to compose a retype with a creation.

**A bare digit on a head RETYPES it** — a behaviour change: today it replaces the note, dropping
its techniques and keyframes. The entry routes to `Retype` over that slot.

**`Insert` on a head selects it directly**, through `chartSelectionMutable().replaceWith`, the
way the shipped `Alt+Insert` selects a standing statement. Not through a same-fret retype: that
would log a refusal on a fret-hand harmonic head, which `planRetypeFrets` refuses before any
no-op test, and it needs no settle branch.

**The fret in force** on the string at a slot, one definition for both `Insert` chords: inside a
ring, the fret `ringStateAt` states at that offset (the last stated stop, never the travel between
stops); past a ring's end, `fretBeforeEnd` of the string's latest note (`chart.h`, the existing
helper: the last pitched stop before the end, which is the end statement's own fret unless the end
is a slide-out to an unsounded target), so `Insert` after a slide-out never manufactures a shift
slide; with no note before it, 0. This reverses the 2026-09-11 retirement of the fret in force as
a head's value and of the fret-0 default: `Insert` must place something, and the string's last
sounded stop is what a charter continuing a line expects.

**Multi-digit entry** is unchanged: `Alt+1` opens a pending entry that authors a keyframe, a `2`
inside the 750 ms window widens it to 12, and after the window settles the keyframe is selected
and a later `2` retypes it. The plane is a FIELD on `TypeChartFretDigit`, not a second action id
(a new `EditorAction::Id` breaks every switch over it under `-Wswitch-enum` while MSVC stays
silent); the first key's plane is the entry's, so `chartFretEntryContinuedBy` needs no change.

**The cut is the split walk with a fresh head.** `splitNoteIntoProducts` (`chart_edits.cpp`)
already divides a ring for `Shift+L`: the keyframes to the right ride onto the new head, rebased;
the channel states in force at the cut open it (a bend in progress becomes a pre-bend); the old
end statement stays at the end of the whole ring, now the new note's end. The cut differs in the
new head: it takes the typed fret (or the fret in force), a plain picked attack, and STRIKE
DEFAULTS — no harmonic node, mute, tremolo, emphasis or held stop rides over from the origin,
those being facts of the strike that made the origin (the join and the `Shift+L` row classify them
so). A keyframe standing exactly at the cut becomes the origin's end statement; where its fret
differs from the new head's it resolves as a slide-out onto the new same-string head — a point
that was visible becomes a zone keyframe, which is the test phase 2 pins. A cut mid-glide keeps
the origin's fret to the cut and re-times nothing else. The cut deletes no keyframe itself; a
later keyframe equal to the typed fret becomes silent and dissolves at settle under the commit
law, as any silent point does. A scrape refuses, as the split does: through the pending entry's
red box on a digit, and with the log line alone on `Insert`, which settles in its own keystroke
and has no box. HOW a refusal is shown is `refusal-flash.md`'s question, not this plan's: that
plan is unbuilt, and when it ships the `Insert` cut of a scrape is one more consumer of it. The
walk becomes a public planner (`planCutRing`) so the cut is testable; whether it
parameterizes the walk or overwrites the second product after it is phase 2's first question, the
simpler shape winning. `Shift+L` keeps its own job, and splitting at a typed fret is `Alt`+digit
then `Shift+L`.

**A note here has two ring rules, one per verb.** A head placed on an empty slot or at a ring's
end rings to the next line of the session's grid (`planInsertNote`, since `a6455969`); a head
that cuts a ring takes the old ring's remainder. The first authors a duration, the second divides
one.

**The silent end statement.** `Alt+Insert` at a ring's end states the fret the ring already holds,
which says nothing. It survives in focus under the keyframe commit law, like an interior silent
point: no undo entry, dissolved when its note leaves focus, never written. Already built
(`derived-shift-slide.md`, 2026-09-22). An `Alt` digit typing that fret there is the same press.
`refusal-flash.md`'s second consumer (a silent end statement flashes, since the key does nothing
for a reason the screen does not show) still names the deleted `dissolveSilentRelease`; it is
re-keyed to the commit law's own dissolve when that plan is built, which is outside this one.

**`Alt`+digit on an open string's tail** keeps the `OpenStringSlide` refusal and its red box.

**Why the bare digit cuts (decided 2026-09-23).** `Alt` is the plane that authors keyframes, so
the bare digit's one meaning is "a note here": on an empty slot a head, inside a ring a head that
divides it. This reverses "NOTHING SINGLE-PRESS TRUNCATES A RING" (`editing-interaction-model.md`)
and decision F, "`Insert` never mutates an existing object" (`keymap-matrix.md`), for the chart
lane: the cut deletes nothing and moves no statement, and the returning caret peek shows the ring
the slot lies in before the key is pressed. It also supersedes the morning's grammar (a bare digit
making a point wherever a ring covered the slot), which made a key's effect depend on geometry the
lane may not show. In `keymap-matrix.md` this supersedes the digit, `Alt`+digit, `Insert`, live
`Alt+Insert` and retired `Alt+Insert` rows, the header amendments of 2026-09-11 and 2026-09-22
where they rule on these keys, and the surface summary.

**Commands.** `InsertLanePoint` (`0x1707`) becomes "Insert at Caret" and `InsertChartStatement`
(`0x171C`) becomes "Insert Ring Point", both keeping their ids: keymap persistence keys on the hex
id, so a new id would silently drop a user's custom binding. "Type Ring Digit 0–9" takes the next
free block `0x1815`–`0x181E`; `0x180B`–`0x1814` stay spent, and the spent-values comment in
`editor_command_id.h` (which credits the end statement to `Insert`) is rewritten. The naming
expert checks the three names before they ship. `Alt`+numpad digit never reaches the editor on
Windows (Alt codes, `keyboard-input.md`), which the decoding paragraph records; AltGr+digit does
reach it as `Alt`+digit on layouts where AltGr types `{`, `[`, `]`, `}`, `²` or `³`, blocked while
a text field is being edited and otherwise a `watch-items.md` entry with a reporting trigger.

## Phases

Each phase ends built, with touched tests passing, sighted where it changes the picture, and
committed. The display comes first: it changes what the lane shows, and the keys are then sighted
against the lane they will ship with.

### Phase 1a — Presentation, projection, painters' crop, the `Alt` reveal, the importer

One phase because it cannot be cut smaller and still end sighted: once presentation stops
clipping, the unchanged painters draw every zone keyframe past the ink (2D bend polylines and
slide diagonals, the projection's vibrato regions closing at the ink end, the 3D slide path, tail
sample times and scrape framing), and deleting the second projection takes the `Alt` reveal with
it until the single form carries one.

- `chartPresentation` publishes the ink end per note (a parallel vector in `ChartPresentation`,
  like `rested_from`) and NO note copy; rule 1 sets the ink end to the crop, rules 3 and 4 to the
  onset. **Deleted:** `ChartPresentation::notes`, `ChartResolutions::presented_notes`, the
  importer's `chartPresentation` call, `trimToMargin`'s ride (`clipPayloadsToSustain` stays for
  the store's own clamp), `lastStatementClearance`, `lastInteriorStatementEnd`, `statementsEnd`'s
  shake window, `keyframeIdentities`. Every reader in the table above then chooses its length.
- The crossing-leg helper beside `glideStopAt`.
- `makeChartViewState`: `end_seconds` is the ink end and a stored end travels beside it;
  `NoteViewState` is built by assignment and its hand-written `operator==` gains the field; the
  54 `NoteViewState{` test fixtures default the stored end to 0.0, so the readers are defined so a
  stored end below the ink end reads as "no reveal" rather than a shorter ring, or the fixtures
  are swept; `makeSlideRampStarts` walks the stored notes and files every ramp; the arrival clamp
  is deleted; `ChartNoteForm` and `tab_actual` are deleted with `EditorViewState::tab_actual`,
  the controller's second projection, `TabView::setState`'s second state and its cull table.
- Painters crop at the ink end (2D: `drawNoteTail` and the bend, slide, vibrato, tremolo and
  accent passes in `tab_paint_core.cpp`, marks past the crop suppressed, the `at_ring_end`
  compare reading the stored end; 3D: `highway_renderer.cpp`, `highway_tail.cpp`,
  `highway_slide_path.h`, the camera's scrape framing) — the crop only, no chip yet; the cull and
  early-outs read the stored end.
- The `Alt` reveal on the single form: ink to the stored end in full ink, zone keyframes shown at
  their true instants.
- `resolveSlideOutExits` reads the stored ring with the exact-touch room rule; corpus census
  diff recorded and the presentation-derived census rows re-pinned.
- **Tests:** presentation for a free tail, a bound tail with one end statement, several keyframes
  in the zone, a keyframe exactly on the crop (inked) and one exactly on the head (in the zone),
  a level leg crossing the crop, keyframes before the crop, a note whose binding onset lies
  within a margin of its onset, a ring ending exactly at another string's head (bound) and one
  ringing past it (not bound); projection for the ink end, the ramps, the crossing-leg helper and
  the holds; the painters' crop and the `Alt` reveal; the importer's exit pass.
  `test_chart_presentation.cpp`'s `presentedSustains` helper reads the ink end, and the 27
  `presentedNotesOf` sites in `test_gp_song_importer.cpp` are rewritten against the ink end so
  none passes vacuously.
- **Docs:** `chart_presentation.h`'s rule text, `chart.h` (~:562-578, :1169), `chart_rules.h`,
  `chart_view_state.h` (the `NoteViewState` block and `end_seconds`), `chart_legato.h`,
  `editor_view_state.h` (`tab_actual`), `grid_arithmetic.h` (the shake floor as a producer),
  `note-sustain-model.md`, `derived-shift-slide.md` (also its stale `latestStatementBeforeStrike`),
  `keyframe-and-burst-ground-up.md`, `file-formats.md` (the rule-2 sentence),
  `docs/developer/musical-time.md`, `the-project-lifecycle.md`, `the-3d-highway.md` ("one presented
  end"), and the watch item on two chart forms.

### Phase 1b — The chip, hit testing, rings and the selection reveal

- The 2D destination chip at the crop, placed by `tabKeyframeLayout`; the recorded no-dissolve
  decision stands.
- Hit testing (`chart_hit_testing.cpp`, whose keyframe hits index position keyframes only) and
  selection rings (a `tab_view` overlay): a keyframe past the ink end is neither hit-testable nor
  ringed unless revealed.
- The selection reveal returns here rather than later, so a keyboard walk or a chip-adjacent
  click that selects a zone keyframe always has a ring to show it (`chart_reveal.h`; the code
  deleted at `a4bfed7b` is the starting point).
- **Sight** My Sacrifice measure 9 (the chord shift slide with open strings ringing through), a
  slide-out and an end bend abutting a same-string head, a free-ending slide-out, a dense
  sixteenth passage at 160–180 BPM, and a Guitar Pro bend-release written at 98% and 99% (imported
  points round onto the lattice, so two that round onto one tick stay apart) — each in 2D, in 3D
  and under `Alt`. Confirm the margin against this picture.
- **Docs:** `tab_paint_core.h`, `the-editor-2d-views.md`, `the-3d-highway.md`.

### Phase 1c — The caret peek returns

- The caret peek comes back as a reveal that adds ink, the held-stop satellites with it and with
  the selection; the `tab_view` cull table follows. Sight the peek beside `Alt` and the selection.
- **Docs:** `chart_reveal.h`, `the-editor-2d-views.md`, `keymap-matrix.md`'s reveal rows.

### Phase 2 — The keys

**The rulings of this phase are PROVISIONAL** (2026-09-23): they were taken before the cropped
lane existed, and they are re-read against phase 1's sighted lane before this phase starts.
Nothing in phase 1 depends on any of them.

- `planCutRing` in `chart_edits.h`: the split walk with a fresh head (strike defaults, typed fret,
  picked attack). Tests: lossless inheritance of keyframes, channel states and the end statement;
  a point exactly at the cut with a different fret (a slide-out); a scrape's refusal; the flags
  not inherited; the `Shift+L` byte-exact round trip still green. Comments that call the split the
  only cut (`chart_edits.cpp`, `chart_handlers.cpp`, `keyboard-input.md` step 5, the
  `keymap-matrix.md` `Shift+L` row) are rewritten.
- The digit dispatch (`chartCaretDigitTarget` and the pending entry's targets) as operand then
  plane. `ChartFretEntry` gains a `Cut` target; its silent sites are the two `std::get<Retype>`
  reads (`chart_handlers.cpp` ~:2345, `editor_controller.cpp` ~:2998), the preview's
  `holds_alternative` check, and the settle's `select_exactly`. A bare digit on a head routes to
  `Retype`; `Insert` on a head selects directly.
- "Type Ring Digit 0–9" as new commands carrying the plane field, with the fallback, the
  single-slot redirect, the standing-statement addressing at a ring's end, and the lanes-view
  try-order the bare digits already follow; over a multi-selection the plane is ignored.
- "Insert at Caret" (one action behind `0x1707`, creating whatever the caret's row holds) and
  "Insert Ring Point" (`0x171C`).
- **Tests** at controller level for every cell of the table, the selection plane, the mixed
  selection, the multi-digit window across planes, the silent end statement, the shared-instant
  addressing, the locked command table and the default-chord collision test.
- **Docs:** `keymap-matrix.md` (rows, header amendments, surface summary, the uniform-scope
  amendment), `editing-interaction-model.md` (the retired single-press rule and its key rows),
  `refusal-flash.md`, `chart-span-and-selection-model.md`, `roadmap/40-chart-editing.md`,
  `docs/developer/keyboard-input.md` (step 5 and the Alt-code paragraph),
  `docs/developer/the-editor-2d-views.md`, `derived-shift-slide.md` ~:56-76; the AltGr watch item.
- Sight the entry flow: sequential entry, a cut into a long ring, an end statement at a shared
  instant, a mixed-selection `Alt` press.

### Alongside, any time

- **The move's end-statement overlay.** A move that lands a note on an earlier note's tail
  truncates that ring, and `clipPayloadsToSustain` carries the end statement back onto whatever
  stands at the landing through `overlayKeyframe`, which overwrites every channel the carried
  statement also states: a point at fret 9 on the landing and a slide-out at fret 3 leave one
  keyframe at 3, the authored 9 gone. `moveErasesStatement` exists to refuse exactly that loss
  and misses it; it exempts only the end FRET statement while the clip carries any end
  statement; and it checks only unmoved rings against landings, while a MOVED note whose own
  ring now runs through an unmoved head is truncated by `normalizeSustainOverlaps` with the same
  silent loss. Fix, in one authority: the clip and the normalizer report the authored channels
  they erased or overwrote — a channel both statements state with DIFFERENT values, never an
  equal one — and `finalizePlan` refuses on that report; the restated predicate is deleted. One
  plan-level test per direction in `test_chart_edits.cpp`; every existing move fixture carries a
  single keyframe. This is store law, not display: the ridden end statement lands on the landing
  head and is a zone keyframe from then on.

## Open decisions

None open for phase 1; phase 2's rulings are provisional (see that phase). Decided 2026-09-23
and recorded where they apply: scoring is the ink (*The display*); no 3D label and a hard 2D
crop (display items 4 and 5); the reveals return with their satellites (item 6); `Alt`+digit
stays and the uniform-scope law is amended (*The keys*); the bare digit cuts (*Why the bare
digit cuts*); a placed head and a cut head take different rings (*A note here*); the command
names — "Insert at Caret", "Insert Ring Point", "Type Ring Digit 0–9" — pending the naming
expert's check.
