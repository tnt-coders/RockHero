\page design_rulesets_hand_positions Hand-Position Rules

# Scope

These rules derive where the fretting hand sits from a chart's notes: a stream of hand events, each
a window (the index finger's fret and how many frets the hand spans) and the place it takes over.
Code cites a rule as **hand-position rule N**, or by its name for the rules below the numbered list
(**the slide rule**, **the coverage rule**).

The rules are implemented in `rock-hero-common/core/src/chart/chart_fret_hand.cpp`, declared in
`chart_fret_hand.h`: `fretHandDemands` gathers what the notes demand (rules 1, 2, 6, 12 and 13),
`deriveFretHandEvents` walks those demands into windows (rules 3 and 5 to 11, the slide rule and the
authored positions), and `deriveFretHandRides` adds the slide-out rides. The coverage rule is
`uncoveringFretHandPositions` in `chart_rules.h`, which `dropUncoveringFretHandPositions` and
`validateFretHandCoverage` apply.

Each rule was measured against a corpus of professionally authored hand positions before it was
adopted; the measurements, and the rules tried and refused, are recorded in
`docs/plans/completed/fhp-derivation-algorithm.md`. Numbers are stable: a rule is never renumbered,
a retired rule keeps its number, and a new rule takes the next one.

# The law in three sentences

The numbered list is the implementer's order. A charter is taught only this:

> Cut the song into figures at boundaries you can see — a chord being struck, a bar line, or the
> grip outgrowing the hand. Put the hand at each figure's floor, spanning the grip, never narrower
> than four frets. Two exceptions, both of which only ever remove motion: the hand never moves while
> a stop it holds still rings, and a one-fret figure the standing window already covers does not
> move it — unless a slide leaving it proves where the hand was (the slide rule).

Each figure's window depends only on that figure's own notes, so an edit changes at most the figure
it touches and where the previous one ends.

# Vocabulary

- **Demand** — a fret the hand must cover at an instant.
- **Instant** — a place on the timeline where something is demanded; its *hull* is the lowest and
  highest fret demanded there.
- **Run** (figure) — consecutive instants the hand plays as one shape, closed by rule 3.
- **Hold** — a stop the hand provably keeps pressed over a stretch of time (rule 6).
- **Window** — the hand: its index finger's fret and its width in frets.

# The numbered rules

1. **What demands the hand.** A note demands the fret its fretting finger occupies: the pressed
   fret, or a natural harmonic's node. An open string and a note the picking hand starts (a tap)
   demand nothing, except a bent open string (rule 13). No demand lies below the first playable
   fret; a lower one is lifted to it. (`fretHandDemandAt`)
2. **Slides demand their frets.** Every pitched fret a note's fret channel states along its ring
   demands that fret where it is stated. A slide-out's exit fret is no stop and demands nothing; the
   window rides it instead (see [Slide-out rides](#slide-out-rides)).
3. **Runs.** A run closes at a bar line, at an instant striking two or more fretted notes (a chord),
   or where adding the next instant would widen the hull past `max(4, current hull)`. It also closes
   at an authored position and at a slide arrival. Sections do not close a run. (`runFrom`)
4. **No rest break** (deliberately absent). A rest never closes a run: a rest is where a move lands,
   not what causes one, and every rest threshold measured made the result worse.
5. **One-fret runs stay.** A run the standing window already covers, all on one fret, states
   nothing about where the hand is, so it is played where the hand stands and moves nothing —
   unless a slide leaving it proves where the hand was (the slide rule), which reads the run in that
   standing window.
6. **Holds pin the hand.** A pressed stop is held from where the note's fret channel states it until
   the channel's next statement or the end of the ring; every pressed stop of a span's grip is held
   from the span's front until its string releases it — the finger on it sliding away, or a note of
   either hand sounding the string open, at a node, or at or below the stop — else to the close. A
   note sounding above the stop is another finger and leaves it held, so a tap releases it only
   where it reaches it. Open strings and natural harmonics hold nothing. A run the standing window
   already covers moves nothing while any hold begun before it still holds. The same release decides
   a shape hold's conflicts (shape rule 9). (`fretHandHolds`, `stringReleasesStopAt`)
7. **The floor.** A run's window sits on the run's lowest demanded fret: the index finger.
8. **The width.** A window spans the run's hull, never fewer than four frets. The same
   `max(4, hull)` is the capacity rule 3 tests a run against. (`runWidth`)
9. **On the board.** A window is lowered until it ends on the last fret, then lifted to the first
   playable fret; the lift wins. (`fittedWindowFret`)
10. **Where a window takes over.** A derived window fronts at the earliest note onset — open strings
    and taps included — that comes after the previous instant and the previous event, lies strictly
    inside no hold (the hand never moves while a stop it holds still rings), and is not before its
    bar's downbeat when the run is the first in its bar. So a window arrives with the leading open
    strings of its figure. It is never later than the first instant of its run the standing window
    does not cover: the instants before that one are played where the hand already stands, so the
    last chord of a chain keeps the chain's window and the hand moves for the note that needs it. An
    unforced run's move, which nothing compels, is never later than its head. A slide arrival never
    reaches back.
11. **Only changes are emitted.** A run whose window equals the standing one emits nothing, so every
    event is a real move.
12. **Pull-off plants.** The stop a pull-off plants beneath its source note is demanded from where
    the source states the stop it is pulled from until the pull-off: the finger waits there the
    whole time. That is the source's onset, or, where the source slides, its arrival: the planted
    finger lands behind the sliding one, never waiting beneath a fret the slide has not reached.
    (`chartPlantedStops`)
13. **Behind-the-nut bends.** An open string bent anywhere along its ring is bent behind the nut, so
    it demands the first playable fret: fret 1, or the fret above a capo.

# Named rules

## The slide rule

A fretting finger sliding from one pressed stop to another proves the hand moved with it and kept
the finger's place in it, so the windows on the slide's two sides differ by the slide's distance.
The hand stands where it is, so the window before the slide is placed first, and the run the slide
lands in is played in that window moved by the travel: the moved floor is its floor, and its width
is rule 8's over the run from there, so a six-fret grip sliding onto a lone note arrives four wide.
The move stands only where every finger pressed across the arrival travels the same distance — a
stop held across it (rule 6) travels none, so a slide beside a held finger is a stretch: the hand
did not move with it, and the ordinary rules place both sides, each at its own width — and only
where the moved window covers the arrival; it reaches as far into the landing run as the moved
window covers, and the first instant it does not cover — a finger outside the hand — heads a run of
its own, placed by the ordinary rules. Where the moved window misses the arrival, rules 3 and 7 to 9
place the landing run as usual. Read backward, the proof runs along the whole train of slides the
finger makes without lifting — a note's keyframes, a glide into the next head — each landing heading
a run of its own (rule 3), for as long as each run ends at an arrival whose fingers agree on one
travel; an authored position, an arrival proving no single travel, or a run ending anywhere else
ends the train. A slide proves only the shift between the window at its departure and the window at
its arrival; nothing proves the finger down past the arrival, so which later instants share a
landing's window is rule 3's call, as for every run. The hand stands where it is unless the train
refutes it: where the leaving run's window, moved by each travel in turn, covers every landing run,
nothing is carried. Otherwise the train is read from its last landing back: each landing stands in
its own window (rules 7 to 9) where the hand stands through the rest of the train from it, else in
the next landing's proven window moved back by the travel, on that floor, at rule 8's width over the
run from there, as the forward move places a landing, so the two sides of a slide agree; where that
moved window misses the run, the hand moved apart from that slide, and the train ends there for
every run before it. The leaving run is read the same way, forced from its last instant before the
slide; the forward move then reads from the carried window and plays the train on. So a slide
landing in the pinky's slot left from the pinky, however many stops the finger passed on the way.
That proof outranks rule 5, whose one-fret run states nothing, but not rule 6, whose hold does; a
run the forward move placed is never carried. A slide arrival is a forced boundary: no run crosses
it, and rules 5, 6 and 10 neither hold the old window over it nor reach back across it.

## Authored positions

A position the charter stores is an input to the derivation, never a patch on its output. It is
always an event, fronting exactly where it stands, and no run crosses it.

- **Its window** is `[fret, end fret]` where the end is stated. Otherwise the stated fret is the
  floor, and the top is the highest of every stop gripped at its place and of the run it heads,
  never under four frets. The run is cut at the first later instant reaching below the stated fret,
  because the position never holds that instant.
- **It stands until the chart contradicts it:** it takes its own instant whatever sounds there, and
  every later instant that fits inside its window, up to the next authored position or slide
  arrival. The numbered rules resume at the first instant that does not fit, and their fronts never
  reach back past it.

## The coverage rule

An authored position must cover every stop gripped at its place — every fretted onset struck there
and every hold standing there: each at or above its fret, and at or below its end fret where one is
stated. Notes after its place never make a position invalid; the derivation takes over where they
do not fit. (`uncoveringFretHandPositions`, which reads the same `forEachGrippedStop` query the
authored window does.)

The pipeline runs one way: written notes, then fret-hand positions, then rings, with the holds
feeding the positions and the grid (the tempo map and the time signatures) upstream of all of them.
An authored position is downstream of the grid, the notes and the holds, so:

- **A position is refused where it would not cover the stops gripped at its place.** Adding,
  moving or retyping a position the notes and holds there contradict states something false, and
  is refused with the rule's sentence, as a hold that does not fit its notes is refused
  (`validateFretHandCoverage`). The refusal lights the position's chip and the notes gripping
  every stop it misses, so the charter sees every stop to fix at once.
- **A note, hold, tempo or time-signature edit that leaves a position uncovering drops it.** The
  position never refuses an edit to anything upstream of it. After a note edit, a hold edit, or a
  tempo or time-signature edit that re-addresses the song (a note can round onto a position's
  instant), as on load, the normalizer drops every authored position that no longer covers the
  stops gripped at its place, inside the edit's own undo step, and logs each drop with the
  position and every stop it missed (`dropUncoveringFretHandPositions`).

## Slide-out rides

A slide-out's exit is motion, not a position. From where the slide-out leaves its last stop, the
window rides it on the slide-out's own curve and timing, toward the window that covers the exit
frets ending at that instant by the least shift, keeping its width. It follows the slide only as
far as the rail is drawn: where the ring's ink is cut short of its end (tail rule 1), the window
leaves the curve there and comes to rest with a continuous slope. A slide-out lets the string go at
speed, so the hand never stops dead on it: the window leaves the curve no later than one minimum
sustain distance before the slide's end, whatever arrives there, and settles into it, which over
that stretch is a rail all but faded. What it comes to rest on is whatever arrives at the slide's
end — the next hand event, the window's return to where it stood, or, into blank space, the ridden
window itself, from which it returns to the standing window at the next instant the hand covers.
A slide-out does not ride where an instant the hand covers, or a hand event, lies strictly inside it — an event
there is the hand moving on — nor where the standing window already covers its exits, nor for a
tap's slide-out, which the picking hand makes. Rides are not hand events and change none.
(`deriveFretHandRides`; the drawn track in `makeChartViewState`)
