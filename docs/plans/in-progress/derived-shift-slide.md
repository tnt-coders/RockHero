# The shift slide as a derived fact

Status: PROPOSAL for signature, 2026-09-21. Follows the store-the-truth work (`9dc24935`,
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

## The design

**One predicate, resolved once.** "Is this end statement an arrival?" is a fact about a PAIR of
notes, so it lives where the pair relation is already established: the connections pass
(`chart_legato.cpp`, the forward walk that fills `hands_over`), as one more per-note vector in
`ChartConnections`, resolved once per revision and read everywhere. It is not threaded into the
note-local inline helpers; `hands_over`'s doc already says why: "two producers of one relation is
how a chart comes to be described two ways."

For a predecessor `p` and the successor `s` the walk reaches on the same string, `p` ARRIVES INTO
`s` when all of: `p` ends in a statement naming a fret; `p`'s ring ends exactly at `s`'s onset;
`s` is re-struck (not legato-claimed — a claim merges, it does not strike); neither is a scrape (a
scrape's travel is the pick's, and its terminal is required at its end); `s` is not stopped by the
picking hand (a tapped harmonic excepted); and the fret named is the stop `s` is struck at,
node-aware. A same-string head at a DIFFERENT fret is a fall that abuts — 463 corpus slide-outs
are exactly that — which is why the test is the fret and not adjacency alone. An open-string next
head can never match (a keyframe at fret 0 is refused). A tied next note was merged away before
any pair exists.

**The release follows.** `releaseKeyframe` — and with it `slideOutFretOrNull`, the strip
`stripReleaseChannels`, `dissolveSilentRelease`, `ringEndMayLandOnLastKeyframe`,
`moveErasesStatement`'s exemption, `releasedFret`, `statedStopFrom` and the FHP generator's skip —
means "an end statement whose fret does not travel into the next head". Each consumer reads the
resolved answer instead of asking position alone. This is the whole cost of the change: about
twenty-five call sites move from a local helper to a resolved read.

**The strip needs no new clause.** "A release states its fret and nothing else" is defined on
`releaseKeyframe`, so once that excludes an arrival, the strip applies exactly to a fall and an
arrival keeps its bend. The coexistence ruling is obtained for free, and the physical claim it
protects — a bend at the instant the string is let go has no ring to sound in — stands.

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

## Order

After the user has sighted the store-the-truth work. Then as one change set: the predicate and its
vector; the release family re-homed onto it; the importer and split deletions; rule 6; the
presentation cases; tests and the developer guide (`the-project-lifecycle.md` import rule 13,
`the-editor-2d-views.md`, `file-formats.md`'s keyframe row). Re-import the corpus afterwards so the
files hold the arrival on the head; the presented corpus must be unchanged, which is the
acceptance test.
