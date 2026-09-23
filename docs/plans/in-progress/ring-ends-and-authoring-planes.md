# Ring ends at their true instant, and the chart lane's two authoring planes

Status: DESIGN PROPOSED 2026-09-22, still being decided; not built. Merges and replaces
`chart-lane-authoring-planes.md` and `ring-end-display.md`. Baseline: HEAD `a4bfed7b` (the
arrow-and-reveal fix has landed). Re-verify every code claim below against the tree before a
phase starts. Starts after the tick-lattice follow-up lands (every editor verb producing positions
on the lattice, which validation now requires), since phases 3 and 4 add verbs on that footing.

The two halves are one design because they meet at the ring's end: the display decides where a
ring's end statement is DRAWN, the keys decide what reaches it, and each assumes the other.

## Where we are (HEAD a4bfed7b)

- **Display.** Presentation rule 1 (`chart_presentation.cpp`) trims every tail one minimum sustain
  distance (`g_minimum_sustain_distance_seconds`) before the next onset on ANY string, and
  rule 2 CARRIES the end statement back to the trimmed end, falling back to halfway along its last
  leg where the margin would crowd it (`lastStatementClearance`). Both surfaces draw the presented
  note, so the end statement is drawn early on both, and the hand window completes with the rail
  at that drawn instant.
- **Sighting.** 2D read right; 3D read wrong (the rail and hand window complete short of where the
  sound goes). Trials at 50 and 75 ms sighted better than 100 for the pulled-back keyframes, but
  any statement drawn early will read out of sync once practice mode slows a song down.
- **The margin's value is settled at 75 ms** (2026-09-22), inside the 70–85 ms band rhythm-game
  charting converged on for the gap before a following note, and at or above the early half of
  plan 24's hit window target. 100 ms erased sixteenth-note ring detail above 150 BPM; 50 ms left
  about 3 px between a tail and the next head at the 2D lane's default zoom. It is confirmed, not
  re-chosen, in the display phase's sighting once the fade is built, since a fade makes a gap read
  larger than the margin itself. The fret-hand window morphs over the same margin, deliberately:
  it starts moving exactly where the old tail's ink stops, so the hand never leaves while a note it
  holds is still drawn. A separate, longer approach lead was tried and reverted for that reason
  (2026-09-23);
  a morph START derived from where the ink under the old window ends is the recorded refinement if
  a margin-long morph reads as a jump (`docs/tracking/watch-items.md`).
- **Keys.** A bare digit strictly inside a ring makes a POINT; at a ring's end or on an empty slot
  it makes a head (`chartCaretDigitTarget`, `chart_handlers.cpp`). `Alt+Insert`
  (`InsertChartStatement`) states the fret in force on a tail. Bare `Insert` is
  `InsertLanePoint`, which does nothing on a string row. `Alt`+digit was deleted this morning; its
  command values (`0x180B`–`0x1814`) stay spent. Only `Alt` reveals the stored form.

## Where we are going

### The display: keyframes at their true instant, ink cropped

**THE ENDING ZONE.** Where a head binds a tail, the stretch of one margin before that head is the
ring's ending. The ink draws the stored curve toward the zone's FIRST keyframe, fading out as it
reaches the crop, and one label there names that keyframe's value where the leg changes something.
Later keyframes in the zone stay stored and are neither drawn nor scored. A free tail has no zone
and draws in full.

**Which head binds is today's rule 1, unchanged (decided 2026-09-23):** the first later onset on ANY
string that the ring does not run strictly past. A ring ending exactly at another string's head is
bound by it; a ring stated to ring on past that head is not. On the ring's own string the head
supersedes the zone's later statements outright, the string being struck again; on another string
nothing re-strikes, and those statements are simply the ring's ending, closer to it than the
display can show or scoring judge.

What that sentence commits to:

1. **Nothing moves.** Every keyframe keeps its stored instant; no statement is ever drawn at an
   instant the chart does not store. The ink is cropped, never compressed: the leg crossing the
   crop is drawn on its TRUE path and simply stops, having travelled part of the way.
2. **Before the zone, everything is inked.** The ink reaches every keyframe that stands before the
   zone, as the trim's interior floor does today; the zone is the only place ink stops early.
3. **The label names the destination of the leg the ink ends on**, and is drawn only where that
   leg changes something. The chip (a slide-out's fret, a bend amount, an arrival) is a destination
   label, as a tab bend arrow's "full" or a slide's target fret is: the line carries the timing,
   the label names where it goes. Why the FIRST keyframe in the zone and not the last: the first is
   where the visible leg is heading, while the last is the state the string is in as the head
   strikes, which a head on the same string already shows (its fret, its own bend state).
4. **One rule for every case.** A free tail's ink ends on its last statement, labelled at its true
   instant. A bound tail with one end statement has first and last the same. A bound tail with
   several keyframes in the zone labels its cropped leg with the first. A flat leg crossing the
   crop (a held bend whose release is written inside the zone) draws no label: the ink shows the
   hold to the crop, and the next head, drawn unbent, shows the release at the strike.
5. **The fade.** The leg crossing the crop fades out as it reaches the crop. In 2D a fade is the
   lane's lean toward the background, never alpha.
6. **3D is the same rule.** The rail and the bend curve run on their true path to the crop, and the
   hand window completes at the TRUE instant — the picture that sighted well.
7. **A reveal adds ink, never moves a mark.** `Alt` extends every tail's ink, in the lane's quieted
   form, to its stored end, showing every stored keyframe at its true instant; the label stays at
   the crop. The caret peek and the selection return as reveals under this rule (decided
   2026-09-23): they were withdrawn because revealing moved a clicked end chip, and once no reveal
   moves a mark that reason is gone.

What is lost, deliberately: a gesture that begins and ends inside the zone (a flick up and back in
the last 50–75 ms) has no net change on the visible leg, so it is neither drawn nor scored; and a
note shorter than the margin draws no ink at all.

**Scoring agrees with the display, exactly** — judging what the player cannot see is a critical
bug. A keyframe before the zone is judged at its stored instant, which is where it is drawn; a
sustain is held to the crop, where its ink finishes fading; nothing inside the zone is judged — a
same-string head is judged as its own onset, and on another string the zone is the ring's ending.
(No scoring code reads the chart yet; this is the contract it will be built against.)

**At most one keyframe in the zone is NOT a chart rule.** The zone relates a ring, the head that
binds it and the tempo, so a tempo edit, a move of the next head, a cut or an import would each
break such a rule on notes they never touched, and each would need a repair that deletes stored
keyframes or a refusal of an unrelated edit. The display rule already makes every later zone
keyframe inert, so nothing needs enforcing. Nor do the keys redirect a zone slot to the zone's
first keyframe: under `Alt` the charter sees the distinct slot they typed on.

**The ring touches the next head in the data.** A ring may end exactly on the next onset on its
string (`sustainBoundOf`), and an end statement may stand exactly there. No buffer is kept: Guitar
Pro rings touch (a 64th followed by a 64th imports with the first ring ending exactly on the second
onset), a bend whose destination sits at 100% of the note — the parser's default — lands its
statement exactly on the head, and two derivations read the touch itself: the legato hold test
(a hammer-on justified by a ring reaching the onset) and the shift slide's arrival
(`arrivesIntoNextHead`). Spacing is display's alone.

**The margin stays one constant.** With nothing moved, it only decides how early ink stops; its
other readers (span drawn ends, the hand window's margin morph, the landed-span tenure in shape
derivation) keep reading it. A screen-space clearance was weighed and not taken: engraving measures
the gap in space (MuseScore stops a slide line 0.25 staff spaces before its target), and a time gap
varies with zoom and playback rate, but those costs bite only when a statement MOVES. A keyframe
ban inside the margin was rejected too: the arrival must stand at the head, Guitar Pro imports
bends that land there, and a tempo edit would push untouched keyframes into a zone measured in
seconds.

### The keys: two planes

**The rule.** A key first finds its OPERAND: the selection if there is one, else the caret's slot.
Its PLANE then decides what it does there. A bare key acts on the operand itself. An `Alt` key
reveals the stored ring and acts on the ring that covers or ends at the operand's instant on its
string; where none does, it acts on the operand exactly as the bare key would. So at a slot holding
both a head and a previous ring's end, the bare key is the head and the `Alt` key is the ring, with
no Esc in between, whether the head is selected or only under the caret. The walk never stops on an
EMPTY ring end, so this redirect is the only way to state a new end statement at a shared instant.

With no selection, at the caret:

| Key | Says | Empty slot | On a head | Inside a ring | At a ring's end |
|---|---|---|---|---|---|
| **digit** | a note here, at the typed fret | places the head | retypes it | cuts the ring | places the head |
| **`Insert`** | a note here, at the fret in force | places the head | selects it | cuts the ring | places the head |
| **`Alt`+digit** | a point on the ring here, at the typed fret | = digit | = digit, unless a ring ends there | states an interior point | states the end statement |
| **`Alt+Insert`** | a point on the ring here, at the fret in force | = `Insert` | = `Insert`, unless a ring ends there | a silent point, to be given a bend or shake | the end statement at the fret in force, silent |

A head and a ring's end can share a slot; there the `Alt` chords take the "At a ring's end" column.
With a selection, a bare digit retypes the selection, notes or keyframes alike, as today; an `Alt`
digit retypes a selected keyframe, states the end statement of a ring ending at a selected head,
and retypes any other selected head.

**`Insert` on a head selects it**, the precedent the shipped `Alt+Insert` already sets: `Insert`
creates, or selects what already stands there. If retyping at the head's own fret gives the same
result — head selected, no undo entry — with no new branch, take that path.

**The fret in force** on the string at a slot, one definition for both `Insert` chords: inside a
ring, the ring's path at that offset (`ringStateAt`, the stated fret, never interpolated travel);
past a ring's end, the fret that ring released or arrived at; with no note before it, 0.

**Multi-digit entry** is unchanged: `Alt+1` opens a pending entry that authors a keyframe, a `2`
inside the 750 ms window widens it to 12, and after the window settles the keyframe is selected and
a later `2` retypes it. The first key decides the plane.

**The cut is the split walk with a picked head.** `splitNoteIntoProducts` (`chart_edits.cpp`)
already cuts a ring losslessly for `Shift+L`: the keyframes to the right ride onto the new head,
rebased; the channel states in force at the cut open it (a bend in progress becomes a pre-bend);
the old end statement stays at the end of the whole ring, now the new note's end. The cut differs
only in the new head, which takes the typed fret (or the fret in force) and a picked attack where
the split's claims `Legato`. So a cut deletes nothing, needs no refusal, and moves no statement;
the new note's ring is the old ring's remainder. A scrape refuses, as the split does, through the
pending entry's red box. `Shift+L` keeps its own job, and splitting at a typed fret is `Alt`+digit
then `Shift+L`.

**The silent end statement.** `Alt+Insert` at a ring's end states the fret the ring already holds,
which says nothing. It survives in focus under the keyframe commit law, like an interior silent
point: no undo entry, dissolved when its note leaves focus, never written. An `Alt` digit typing
that fret there is the same press. This retires the 2026-09-21 flash consumer in `refusal-flash.md`.

**`Alt`+digit on an open string's tail** keeps the `OpenStringSlide` refusal and its red box.

**Why the morning's grammar lost:** with a bare digit making a point wherever a ring covered the
slot, what a key did depended on geometry the presented lane may not show — a slot that looked
blank inside a cropped tail took an invisible point, and a ring stretched past its grid step
turned sequential entry into points. This supersedes, in `keymap-matrix.md`, the digit, `Alt`+digit,
`Insert` and retired `Alt`+`Insert` rows, the header amendments of 2026-09-11 and 2026-09-22 where
they rule on these keys, and the surface summary; and it reverses the 2026-09-11 retirement of the
fret in force as a head's value and its fret-0 default.

## Phases

Each phase ends built, with touched tests passing, sighted where it changes the picture, and
committed. The display comes first: it changes what the lane shows, and the keys are then sighted
against the lane they will ship with.

### Phase 1 — The display

The sighting phase that preceded this one is retired: the question it was to settle — which head
binds a tail — was decided without one (rule 1 unchanged, see *The display*).

- **Presentation keeps the stored ring and adds an ink end.** The presented note keeps the stored
  sustain and every keyframe at its stored offset, and carries the ink's end beside it — the pair
  spans already publish (`ShapeViewState::close_seconds` and `drawn_end_seconds`). Rules 3 and 4
  keep ending a tail outright; the tail law keeps its verdict, re-read against the ink end.
- **Deleted:** rule 2's ride of the end statement, `lastStatementClearance` and its halfway
  fallback, and whatever of the drawn-to-stored keyframe identity mapping existed only to undo the
  ride.
- **The ending zone** in presentation: the ink end is the crop where a head binds (rule 1's binding
  onset), with the zone's first keyframe published as the label's value where the leg changes
  something.
- **The caret peek and the selection reveal return**, each adding the stored tail's ink the way
  `Alt` does and moving no mark.
- **Painters crop at the ink end**, fade the leg crossing it, and draw the label there, the 2D lane
  and the highway alike.
- **The hand window's slide ramp** ends at the true end (`chart_projection.cpp`, which reads the
  drawn ring today).
- **Readers of the presented stream to re-verify:** the view-state projection, the highway
  projection, `chart_legato.cpp` (`presentedChartNotes` at :387) and the Guitar Pro importer's
  slide-out exit pass (`gp_chart_builder.cpp` :3861, which reads presented LENGTHS).
- **The margin's value confirmed** against the built fade (the decision test: a sixteenth-note
  slide or bend passage at 160–180 BPM keeps its ink, and a long sustain into a same-string head
  reads as ending clearly short of it).
- **Docs:** `chart_presentation.h`'s rule text, and the documents that describe the ride:
  `derived-shift-slide.md`, `keyframe-and-burst-ground-up.md`, `note-sustain-model.md`,
  `keymap-matrix.md`, `docs/developer/musical-time.md`, `docs/developer/the-project-lifecycle.md`.
- **Tests:** presentation for a free tail, a bound tail with one end statement, several keyframes
  in the zone (the first labels), a flat leg crossing the crop (no label), keyframes before the
  zone (all inked), a note shorter than the margin, a ring ending exactly at another string's head
  (bound) and one ringing past it (not bound); projection for the ink end and the ramp. Sight My
  Sacrifice measure 9 (the chord shift slide with open strings ringing through), a slide-out and
  an end bend abutting a same-string head, a free-ending slide-out, a dense sixteenth passage and
  a Guitar Pro bend-release written at 98% and 99% — each in 2D, in 3D and under `Alt`.

### Phase 2 — The cut's authority

Parameterize `splitNoteIntoProducts` by the new head's attack and fret, so the split and the cut
are one walk. Tests: the cut's lossless inheritance (keyframes, channel states, end statement), a
point exactly at the cut whose fret differs from the typed one, and a scrape's refusal. No key
reaches the cut yet; the split's behaviour is unchanged.

### Phase 3 — The keys

- The digit dispatch (`chartCaretDigitTarget` and the pending entry's targets) as operand then
  plane, with the cut for a bare digit inside a ring.
- `Alt`+digit as new commands (the old values stay spent), "Type Ring Digit 0–9", with the
  fallback and the redirect.
- Bare `Insert` gains its string-row half: a head at the fret in force, or the head selected. It
  becomes one command, "Insert at Caret", creating whatever the caret's row holds — a head on a
  string row, a point on an automation lane — and replaces "Insert Lane Point"
  (`InsertLanePoint`).
- `Alt+Insert` is renamed "Insert Ring Point", replacing "Insert Chart Statement"
  (`InsertChartStatement`), after the plan's own word for what `Alt` acts on. The three names
  outlive the change, so the naming expert checks them before they ship.
- An `Alt` key over a multi-selection acts on each selected element as it would alone (decided
  2026-09-23), so one press may retype some heads and state end statements at others.
- `Alt+Insert` at a ring's end leaves the silent statement standing in focus.
- **Tests** at controller level for every cell of the table, the selection plane, the multi-digit
  window across planes, the silent end statement, and the shared-instant addressing.
- **Docs:** `keymap-matrix.md` (rows, header amendments, surface summary), `refusal-flash.md`,
  `docs/developer/keyboard-input.md`, `docs/developer/the-editor-2d-views.md`.
- Sight the entry flow: sequential entry, a cut into a long ring, an end statement at a shared
  instant.

### Alongside, any time

- **The move verb's possible collision.** `moveErasesStatement` keeps a statement standing on a
  landing, and the end statement rides back to it, which may put two statements at one offset — a
  shape the chart may not hold. One controller test settles whether it is a live bug.
- **AltGr.** JUCE's Windows peer strips `Ctrl` from AltGr, so AltGr+digit reaches the editor as
  `Alt`+digit and would state a point where a German, Polish or Brazilian charter typed `{`, `[`,
  `]`, `}`, `²` or `³`. The composed-character filter cannot catch it without a Windows-only seam.
  Proposed as a `watch-items.md` entry with a reporting trigger, not a ship gate.

## Open decisions

None. Decided 2026-09-23 and recorded where they apply: the tail binds on rule 1's onset on any
string (*The display*); the caret peek and the selection return as reveals (display item 7); an
`Alt` key over a multi-selection acts per element (phase 3); and the command names — "Insert at
Caret", "Insert Ring Point", "Type Ring Digit 0–9" (phase 3).
