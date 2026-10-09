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
`validateFretHandCoverage` in `chart_rules.h`.

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
> move it.

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
5. **One-fret runs stay.** A run the standing window already covers, all on one fret, moves nothing.
6. **Holds pin the hand.** A pressed stop is held from where the note's fret channel states it until
   the channel's next statement or the end of the ring; a chord member struck after its chord's
   front is held from the front. Open strings and natural harmonics hold nothing. A run the
   standing window already covers moves nothing while any hold begun before it still holds.
   (`fretHandHolds`)
7. **The floor.** A run's window sits on the run's lowest demanded fret: the index finger.
8. **The width.** A window spans the run's hull, never fewer than four frets. The same
   `max(4, hull)` is the capacity rule 3 tests a run against. (`runWidth`)
9. **On the board.** A window is lowered until it ends on the last fret, then lifted to the first
   playable fret; the lift wins. (`fittedWindowFret`)
10. **Where a window takes over.** A derived window fronts at the earliest note onset — open strings
    and taps included — that comes after the previous instant and the previous event, and not
    before the latest end of the holds begun before its run, nor before its bar's downbeat when the
    run is the first in its bar. So a window arrives with the leading open strings of its figure,
    never later than the run's own head. A slide arrival never reaches back.
11. **Only changes are emitted.** A run whose window equals the standing one emits nothing, so every
    event is a real move.
12. **Pull-off plants.** The stop a pull-off plants beneath its source note is demanded from the
    source's onset until the pull-off: the finger waits there the whole time. (`chartPlantedStops`)
13. **Behind-the-nut bends.** An open string bent anywhere along its ring is bent behind the nut, so
    it demands the first playable fret: fret 1, or the fret above a capo.

# Named rules

## The slide rule

A fretting finger sliding from one pressed stop to another proves the hand moved with it and kept
the finger's place in it, so the window at the arrival is the standing one shifted by the slide's
distance. The shift stands only where the shifted window covers everything demanded and held at the
arrival, and only where every finger sliding into that instant travels the same distance; otherwise
rules 3 and 7 to 9 place the window there as usual. A slide arrival is a forced boundary: no run
crosses it, and rules 5, 6 and 10 neither hold the old window over it nor reach back across it.

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
stated. An edit that breaks this is refused. Notes after its place never make a position invalid;
the derivation takes over where they do not fit. (`validateFretHandCoverage`, which reads the same
`forEachGrippedStop` query the authored window does.)

## Slide-out rides

A slide-out's exit is motion, not a position. The window rides the slide: it shifts the least
distance that covers the exit frets ending at that instant, keeping its width, and returns to the
window the hand events state at the next instant the hand covers. A slide-out with no room — the
next demand at or before its end, or a hand event fronting during it — does not ride, and neither
does a tap's slide-out, which the picking hand makes. Rides are not hand events and change none.
(`deriveFretHandRides`)
