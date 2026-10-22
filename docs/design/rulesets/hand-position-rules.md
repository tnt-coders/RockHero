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
> than four frets unless a slide proves the hand narrower. Two exceptions, both of which only ever
> remove motion: the hand never moves while a stop it holds still rings, and a one-fret figure the
> standing window already covers does not move it — unless a slide leaving it proves where the hand
> was (the slide rule).

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
8. **The width.** A window spans the run's hull, never fewer than four frets — save the hand a
   slide's grip proves (the slide rule), which may be narrower or wider. The same `max(4, hull)` is
   the capacity rule 3 tests a run against, and how far any hand reaches from its floor, a narrower
   one included, up to the board's end (rule 9). (`runWidth`)
9. **On the board.** A window is lowered until it ends on the last fret, then lifted to the first
   playable fret; the lift wins. A window is fitted by its own width: a slide-proven or stated
   window may be narrower than four frets at either edge of the board — 1..1, 1..2 and 1..3 at the
   bottom, 22..24, 23..24 and 24..24 at the top — and stands there as it is. A hand reaches from its
   own floor no further than the board's end, never refitted below that floor. (`fittedWindowFret`,
   `reachOf`)
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

A fretting finger sliding from one pressed stop to another proves where the hand stands at the
arrival, read from every finger pressed across it: each sliding finger, and each stop still held
across the arrival (rule 6), a finger that travels none. The sliding finger keeps its slot in the
hand and the held fingers stay put, so each edge of the window standing before the slide keeps its
distance from the finger nearest it — the one that ended outermost, where several were nearest — and
the hand spans every finger. Fingers all travelling one distance move the whole hand by it. A slide
beside a held finger moves only the edge on the slider's side: with 5 held, a finger sliding 7 to 6
closes 4..7 to 4..6, one sliding 6 to 7 stretches it to 4..8, and 8 sliding to 7 closes 5..8 to
5..7. Held fingers on both sides of the slider move neither edge, so 4..9 with 5 and 8 held stays
4..9 while 7 slides to 6. A finger sliding past a held one moves the edge it stood beside only until
the held finger pins it, and the far edge reaches only as far as the arrival: 4..7 with 6 held and 5
sliding to 8 becomes 6..8, and with 5 held and 6 sliding to 4 becomes 4..5. One arriving exactly at
a held finger's fret crosses nothing: 4..8 with 5 held and 6 sliding to 5 becomes 4..7. The hand
always spans its fingers, and rule 9 fits it on the board by its own width, which never outgrows
the board: a hand proved at 22..24 stands at 22..24. The slide proves this hand only where every
finger pressed before it lies inside the standing window and the hand reaches the arrival — rule
8's four frets from its floor, cut at the board's end; otherwise rules 3 and 7 to 9 place the
landing run as usual.

Where the slide proves the hand, the run it lands in is played in that hand as far as the hand
reaches, and the first instant it does not reach — a finger outside the hand — heads a run of its
own, placed by the ordinary rules. A hand whose fingers all travelled one distance moved as one, and
that proves the slot the fingers kept, not the width the hand stood at: its width is rule 8's over
the run from the moved floor, so a six-fret grip sliding onto a lone note arrives four wide — unless
a finger lands on the moved top, which it then keeps. Any other proven hand keeps the shape the
slide gives it, narrower or wider than four frets, for as long as nothing in the landing run reaches
past its top; a run that does is spanned at rule 8's width from the proven floor, so a later 8 after
the hand closed to 5..7 opens it to four frets again.

Read backward, the proof runs along the whole train of slides the finger makes without lifting — a
note's keyframes, a glide into the next head — each landing heading a run of its own (rule 3), for
as long as each run ends at a slide's arrival; an authored position or a run ending anywhere else
ends the train. A slide proves only the hand at its arrival; nothing proves the finger down past the
arrival, so which later instants share a landing's window is rule 3's call, as for every run. The
hand stands where it is unless the train refutes it: where the slide into every landing, read
forward from the leaving run's window, proves a hand covering that landing's run, nothing is
carried. Otherwise the train is read from its last landing back: each landing stands in its own
window (rules 7 to 9) where the hand stands through the rest of the train from it, else in the next
landing's proven window with that slide reversed — every finger travelling back — placed exactly as
the forward rule places a landing, so the two sides of a slide agree wherever no finger crosses the
one nearest an edge; where the reversed slide proves no hand over the run, the hand moved apart from
that slide, and the train ends there for every run before it. The leaving run is read the same way,
forced from its last instant before the slide; the forward move then reads from the carried window
and plays the train on. So a slide landing in the pinky's slot left from the pinky, however many
stops the finger passed on the way. That proof outranks rule 5, whose one-fret run states nothing,
but not rule 6, whose hold does; a run the forward move placed is never carried. A slide arrival is
a forced boundary: no run crosses it, and rules 5, 6 and 10 neither hold the old window over it nor
reach back across it. A slide-out's end is placed by this same rule (see [Slide-out
rides](#slide-out-rides)).

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

A slide-out's exit is motion, not a position. The hand travels with a slide-out exactly as a slide
arriving at a stop moves it (the slide rule): the fingers pressed across its end are every slide-out
ending there, each from its last stop to its exit fret, and every stop still held across that end,
which travels none, and the slide rule places the hand from them. From where the slide-out leaves
its last stop, the window rides toward that window on the slide-out's own curve and timing. It
follows the slide only as far as the rail is drawn: where the ring's ink is cut short of its end
(tail rule 1), the window leaves the curve there and comes to rest with a continuous slope. A
slide-out lets the string go at speed, so the hand never stops dead on it: the window leaves the
curve no later than one minimum sustain distance before the slide's end, whatever arrives there, and
settles into it, which over that stretch is a rail all but faded. What it comes to rest on is
whatever arrives at the slide's end — the next hand event, the window's return to where it stood,
or, into blank space, the ridden window itself, from which it returns to the standing window at the
next instant the hand covers. A slide-out does not ride where an instant the hand covers, or a hand
event, lies strictly inside it — an event there is the hand moving on — nor where the slide rule
leaves the hand where it stands (rule 11), nor for a tap's slide-out, which the picking hand makes.
A slide-out's end is no stop (rule 2), so after the ride the hand plays on from where it stood:
rides are not hand events and change none. (`deriveFretHandRides`; the drawn track in
`makeChartViewState`)
