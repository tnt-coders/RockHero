# The shift slide as a derived fact

Status: SIGNED 2026-09-21 (the user: "I think that is the one. Implement that"), with the two amendments below; building. Follows the store-the-truth work (`9dc24935`,
`0ee88bd3`, `e7c42407`, `181d3e27`) and is built on a read-only study of the code at `e7c42407`;
its findings and the two rulings taken so far are recorded in `keyframe-and-burst-ground-up.md`.
Every code claim below carries that date.

## The claim

A shift slide — a glide into a note that is then struck again on the same string — is stored as
the fret statement AT the ring's end, landing exactly on the next head. That the statement is an
ARRIVAL and not a FALL is a fact the chart proves: the statement names the same stop the next head
on that string is struck at, at the same instant. Nothing is stored to say so.

Today the importer synthesizes a pitched arrival one margin BEFORE the next head, because a fret
exactly at a ring's end reads as the unpitched release. The synthesized offset is the last place
the store still says something the hands did not do.

## What is ruled

- **The store holds the truth; presentation spaces it** (user, 2026-09-21). The arrival sits on the
  head and is drawn one minimum sustain distance early, exactly as today.
- **A bend and the arrival coexist** at the end moment, one keyframe stating both, both drawn
  retreated together.
- **A next head struck by the picking hand is not arrived into**, except a tapped harmonic, whose
  fretting-hand stop is the fret slid into (refused by validation today; recorded for its return).

## Amendments from the first build attempt (2026-09-21)

The first build stopped on two contradictions in the page as first written. Both are resolved
here and the text below is amended to match.

- **The predicate has FIVE clauses, not six.** "The next head is re-struck, not legato-claimed"
  is deleted: it read the successor's claim while the legato resolver reads the arrival, giving
  the pair two self-consistent readings, and it broke `Shift+L`'s split-then-join. The fact is
  what the claim's own sentence says — the end statement names the stop the next head takes, at
  the same instant. An equal-fret claim can never be justified, so after a settle the deleted
  clause was always true whenever the other five held.
- **The strip sheds only the SHAKE.** "A release states its fret and nothing else" was enforced
  eagerly in the note-local writers, which cannot see the relation. The bend half of that law was
  wrong on its own terms: by the channel table a bend at the end is the curve's LAST value, which
  shapes the final leg before the end, and that is as true of a fall as of an arrival. So the strip
  is note-local, applies to any end statement, and sheds the shake alone (a shake at the end has no
  ring to shake in). A bend on a fall becomes legal; no corpus note has one, so nothing re-imports
  differently. The `ReleasePayload` repair narrows to the shake likewise.

## The interaction half (signed the same day): BUILT 2026-09-22

- **`Insert` on a tail types the fret in force at the caret** — the digit route with the digit
  supplied, through the same pending entry. Inside a ring that is what typing the note's own fret
  already does; at the ring's end the product is the end statement at the fret in force. Digits
  retype it into a fall, `B` gives it a bend, `Tab` finds it, Delete removes it. At the end of a
  ring abutting a head at that fret the product names the head's stop: a shift slide, one key.
  `Insert`'s chart half, retired 2026-09-11 because "every note is typed", returns with the fret
  it supplies being the one already stated.
- **`Alt`+digit is deleted** (its one creating cell, the ten `TypePathDigit` commands, the
  both-key-code registration, and with them the AltGr collision). `Alt+B` is never built. `Alt`
  on the chart lane is the ring reveal and the move modifier, nothing else. Supersedes the
  2026-09-11 ruling that made `Alt`+digit "the ONE thing `Alt` creates on this lane".
- **The object walk carries KEYS, and at a shared instant steps the end statement before the
  head** (time order). `Shift+Tab` from a head selects the previous ring's end; every
  selection-addressed verb then reaches it. An empty end is never a stop and never armed.
- **A silent statement lingers while its note is in focus and dissolves when focus leaves, the
  end included.** The painter's skip of a same-fret segment before the chip push is deleted, so a
  fall to the fret in force draws its chip as an interior same-fret point draws its head; with a
  face, the end's silent statement needs no same-edit dissolve, and `dissolveSilentRelease`
  (8318f371) goes.
- **The selected object draws last**, so a selected arrival shows over the picked head with the
  accent ring on it, and a selected head over the arrival.
- **The band conditional at a shared instant** (`keyframe-and-burst-ground-up.md`, "The revealed
  form at a shared instant"): built once, keyed on the stored relation; its direction — the
  ending ring's chips below, or the head's marks higher — chosen at the sighting with both drawn.

Built as signed, with six things worth recording:

- `Insert` is the command the registry already had, spelled `InsertLanePoint` ("Insert Lane Point",
  `0x1707`) rather than `NeutralInsert` as `keymap-matrix.md` named it; the matrix row now names the
  code. The chart lane IS a lane and its keyframe IS a point, so the name stayed and the doc moved.
- The entry settles in the arming keystroke rather than arming the window: the value arrives WHOLE,
  so no digit could widen it, and a refused one shows no red box — there is no provisional digit for
  a box to display.
- `chartObjectAt` narrowed to what a slot HOLDS: the end statement belongs to the ring that ends
  there, so no landing re-derives it and the walk and the pointer carry its key into
  `armChartCaret` instead. That is the deleted "arming selects a slide-out's chip" clause, and it is
  what keeps a bare digit at a ring's end always the next note. The undo/redo repair
  (`dropChartSelectionKeysNamingNothing`) stopped borrowing that answer with it: it asks whether the
  chart holds what each KEY names — a note at its slot, a keyframe at its offset — because an object
  exists whether or not a landing reaches it, and a selected fall chip lost to an unrelated undo
  would be a loss the screen never explains. The linger law needed nothing from the old test: it is
  scoped to a live verb window, which the transition has already ended.
- The walk carries a `RowObjectStop` — the slot AND the object — because a lane point is named by
  its slot alone while a chart object is not. At a shared instant the order is (instant, then
  statement before head).
- The band conditional needed one new pair fact, `ChartConnections::ends_on_next_head`, resolved in
  the walk that resolves the arrival (it is that predicate's clause 2 on its own) and carried to the
  surfaces on `NoteViewState`. An arrival and an abutting fall both set it: what collides is
  geometry, not gesture.
- The duration verb's floor (`ringEndMayLandOnLastKeyframe`) still refuses to land a ring's end on a
  keyframe that says nothing. Its old reason was the dissolve; the reason now is that the landing
  would trade the charter's own point for a ring that simply ends, since a silent statement does not
  survive its note leaving focus. Whether the floor should lift with the dissolve is a duration-verb
  question this change set deliberately did not open.

## The design

**One predicate, resolved once.** "Is this end statement an arrival?" is a fact about a PAIR of
notes, so it lives where the pair relation is already established: the connections pass
(`chart_legato.cpp`, the forward walk that fills `hands_over`), as one more per-note vector in
`ChartConnections`, resolved once per revision and read everywhere. It is not threaded into the
note-local inline helpers; `hands_over`'s doc already says why: "two producers of one relation is
how a chart comes to be described two ways."

For a predecessor `p` and the successor `s` the walk reaches on the same string, `p` ARRIVES INTO
`s` when all of: `p` ends in a statement naming a fret; `p`'s ring ends exactly at `s`'s onset;
neither is a scrape (a scrape's travel is the pick's, and its terminal is required at its end);
`s` is not stopped by the picking hand (a tapped harmonic excepted); and the fret named is the
stop `s` is struck at, node-aware. Whether `s` is picked or claimed is `s`'s own business (see the
amendment above). A same-string head at a DIFFERENT fret is a fall that abuts — 463 corpus slide-outs
are exactly that — which is why the test is the fret and not adjacency alone. An open-string next
head can never match (a keyframe at fret 0 is refused). A tied next note was merged away before
any pair exists.

**The release follows.** `releaseKeyframe` — and with it `slideOutFretOrNull`, the strip
`stripReleaseChannels`, `dissolveSilentRelease`, `ringEndMayLandOnLastKeyframe`,
`moveErasesStatement`'s exemption, `releasedFret`, `statedStopFrom` and the FHP generator's skip —
means "an end statement whose fret does not travel into the next head". Each consumer reads the
resolved answer instead of asking position alone. This is the whole cost of the change: about
twenty-five call sites move from a local helper to a resolved read.

**The strip needs no relation.** It sheds the shake from any end statement, note-local, and never
the bend (see the amendment above). The coexistence ruling holds for arrivals and falls alike.

**No display change.** One flag, `KeyframeViewState::release`, isolates every surface: false draws
a linked arrival head at the presented end, true draws a floating fall chip. The projection fills
it from the resolved answer instead of from position, and the presented instant is already the
trim's. Both surfaces keep drawing what they draw; the signed P8 look is reproduced pixel for
pixel, and the sighting concern recorded against it ("it MOVES an authored instant") retires,
because nothing moves an authored instant any more.

## What it deletes

- The importer's arrival placement: the `clearSlideOut`-then-arrive dance, the folded-point
  retreat, the "leg" requirement, the margin / leg-start / window computation. The shift branch
  collapses to "state the next head's fret at the end and grow the ring to it".
- The `Shift+L` split's retreat and the join's "did this point retreat?" equality test. The join
  stays an exact inverse through the merge path it already has, and the fragile spelling-based
  distinction between a retreated arrival and a charter's own point at the clearance goes with it.
- `latestStatementBeforeStrike` is left with one caller, presentation's `lastStatementClearance`;
  it folds into `chart_presentation.cpp` as a file-local and leaves the shared header.
- Two presentation cases: the rest landmark's slide-out branch (an arrival is a finished
  statement, like a handover) and the trim's interior-arrival case (the arrival is the end).

## What it re-keys

**Span rule 6.** Its emit test suppresses a landing-born span with no interior statement whose
tenure is not more than a quantum, and is calibrated to the synthesized arrival sitting exactly one
quantum before the closing head. With the arrival on the head that tenure is zero for every shift
slide. The rule keys on the relation instead: a landing span closed by an arrival into its closing
head is suppressed outright; the tenure test survives for landing spans closed by anything else.
The census pins do not move; three discriminating tests (`test_chart_shapes.cpp` ~2685,
~3347-3390, ~3863-3875) are rewritten to the relation rather than re-spelled.

## The model half: BUILT 2026-09-22

Built, green, and cleaned: the predicate `arrivesIntoNextHead` beside `resolveLegato` with its
`ChartConnections::arrives_into` vector; the release family split into a NOTE-LOCAL pair
(`endFretStatement`, `endStatedFretOrNull`) and a RESOLVED pair (`releaseKeyframe(note,
arrives_into)`, `slideOutFretOrNull(note, arrives_into)`, the second spelled over the first so the
relational clause stands in one function); the strip narrowed to `shedEndStatementShake` /
`endStatementWouldShedShake` (note-local, any end statement, the shake alone,
`ChartRepair::EndStatementShake`); `releasedFret` and `dissolveSilentRelease` reading the fact; the
importer's shift branch collapsed to "grow the ring to the gap and state the landing's fret at the
end"; the `Shift+L` split's retreat and the join's equality test deleted, with a scrape now refused
outright; `latestStatementBeforeStrike` folded into `chart_presentation.cpp` as
`lastStatementClearance`; the projection's `release` flag read from the fact; `deriveChartShapes`
taking the connections.

### The four findings, and how each was settled

**1. Rule 6's re-key is unrepresentable, so the tenure test stands unchanged — ACCEPTED.** This
page predicted "that tenure is zero for every shift slide". The truth is stronger: a landing opens
a successor only where the ring runs STRICTLY PAST the boundary (`settle_landings`), and an arrival
stands at its ring's END, so a shift slide opens no landing span at all. A branch keyed on the
relation could never fire, so none was added; the strict tenure test now governs a charter's own
crowded landing and nothing else.

**2. The landing hand-off disappears corpus-wide — RULED (user, 2026-09-22): the EXACT END OF A
TAIL never founds a span.** Where a tail's end lands on the same instant as an onset, only the
ONSET is a member of the span that results. The "held through" clause therefore stands as built,
and the new census counts are the truth: the old ones came from the synthesized arrival standing a
margin early, which made a ring ending exactly at the strike look held through it. Re-pinned in
`test_corpus_census.cpp`, with the ruling stated in the two pin comments and in
`chart-ruleset.md`'s span section:

| Census row | Old pin | New pin |
|---|---|---|
| spans total | 22398 | 22386 |
| arpeggio spans | 1164 | 1154 |
| lone re-pick spans | 2778 | 2769 |
| spans opened by a LANDING | 1243 | 988 |
| ... landing successors classified BOX | 1149 | 904 |
| spans holding a stop outside the window | 163 | 159 |
| ... those out-of-reach stops | 219 | 211 |

The five cross-check rows that were already FLAGGING at `1216def0` (the FHP-shift and pinned-finger
rows) keep their signed figures untouched: they are standing findings, not stale pins, and two of
them moved only within their band (pinned finger 530 to 538, pinned rings 1058 to 1090).

What the ruling costs, and it is accepted: a ring held THROUGH a shift-slide landing no longer
crosses the seam into the shape that replaces it, so in the Periphery fixture the beat-3 chord's
grip states the two stops it strikes and not the tied fret-3 still down under it, and that shape is
a BOX rather than an arpeggio.

**3. A source-stated FALL may never name the next head's stop — FIXED.** The importer INVENTS a
trail-off's exit fret (four frets out, or the hand's next move), and a fret at a ring's end naming
the stop the next head is struck at, at that same instant, is what the chart reads as an ARRIVAL.
`fallExitClearOfNextHead` now keeps both writers clear of that stop — the synthesis placeholder and
the resolved exit in `resolveSlideOutExits` — by moving one fret further in the gesture's own
direction, asked of `arrivesIntoNextHead` itself rather than by restating its clauses.

The presented corpus is therefore NOT byte-identical to `1216def0`, and cannot be: 175 of 245 866
notes differ, across 25 of 113 packages, in exactly two ways.

- **168 exit frets moved by one**, each a source-stated fall whose invented exit named the next
  head's own stop. At `1216def0` those 168 gestures were stored in the one spelling the derived
  model reads as a shift slide, so the store was wrong about them; keeping them clear is what makes
  it right, and the price is one fret of an exit nobody authored.
- **7 end statements keep a BEND they previously lost**, which is the signed amendment working: a
  bend at a ring's end is the curve's last value on a fall exactly as on an arrival.

**Stored end statements sitting exactly on the next head of their own string: 583 to 1927** (530 to
1874 of them stating a fret), so **1344 stored arrivals moved onto the head**.

**4. The fret-hand window moves with the arrival — ACCEPTED.** Rule 9's drag had to be re-homed:
the drag event WAS the synthesized arrival, so with the arrival on the head the coverage event
vanished and the window fell back to minimal-shift placement. It is now stated at the landing's own
onset — a head some glide ARRIVED into inherits that glide's fret delta — and the same-instant merge
folds it with the head's own demand, which restores rule 9 exactly. What does not survive is the
hull-exact reshape for a shift slide: at the arrival's instant a chord partner's ring has just
ended, so nothing pins an edge and the window translates at the four-fret width. Reading the
relation inside `coverageEventsOf` was tried and reverted: there the landing head itself counts as
the planted finger, and the reshape collapses to a one-fret window. One coupling is open and
accepted: an FHP authored at the arrival resolves to the STORED instant while the drawn rail
arrives one margin earlier, so the hand marker lands a margin after the rail completes.

## Order

After the user has sighted the store-the-truth work. Then as one change set: the predicate and its
vector; the release family re-homed onto it; the importer and split deletions; rule 6; the
presentation cases; tests and the developer guide (`the-project-lifecycle.md` import rule 13,
`the-editor-2d-views.md`, `file-formats.md`'s keyframe row). Re-import the corpus afterwards so the
files hold the arrival on the head; the presented corpus must be unchanged, which is the
acceptance test.
