\page guide_musical_time Musical Time and the Tempo Map

*Applies to: Repo-wide — every projection, renderer, and the audio engine consume these types.*

Timing is the heart of a rhythm game, and this codebase keeps two time domains strictly
separated: **musical positions** (measures, beats, exact rational sub-beats) are what charts
author; **seconds** are what renderers and audio consume. Exactly one type converts between them.

# The value types (`rock-hero-common/core`)

- `TimePosition` / `TimeDuration` / `TimeRange` (`timeline/timeline.h`) — seconds as strong
  types. Equality is deliberately *exact* (`std::is_eq(a <=> b)`); tolerance belongs in named
  helpers at call sites, never hidden inside `operator==`.
- `Fraction` (`timeline/fraction.h`) — a gcd-reduced rational, so subdivisions like 1/3 or 3/16
  are exact, not floating-point approximations.
- `GridPosition` (`timeline/grid_position.h`, re-exported by `chart/chart.h`) —
  `{measure, beat, Fraction offset}` with `offset ∈ [0,1)`: the exact musical address every
  authored thing uses (notes, tone boundaries, automation points). Fully ordered, so positions sort and compare exactly.

# The chart model, briefly

The chart itself — "the true tab" of notes, tuning, and authored hand positions — is the
arrangement-owned model in `common/core/chart/chart.h`, addressed entirely in `GridPosition`s and
consumed by the tab and highway projections and by package IO. Chart *editing* is partly built
rather than unbuilt: a family of pure planners lives in editor core
(`editor/core/src/chart/chart_edits.h`) — insertion, selection, retyping, sustain, and the technique
verbs — all funnelling through one shared finalize gate. The model still gets no field-by-field tour
here — see \ref guide_file_formats for the persisted shape and
`docs/plans/roadmap/40-chart-editing.md` for the remaining phases.

# Grid arithmetic (`chart/grid_arithmetic.h`)

Exact arithmetic *on* the grid lives in one header, and it is the shared authority the 2D and 3D
projections, the editor's duration verb, the Guitar Pro import trims, the connection resolver and
the validation gate all resolve through — so a spacing rule cannot mean two things:

- `tickPlaceReached(tempo_map, from, seconds)` — THE one conversion of a stretch of seconds into a
  length on the chart's axis: the place a signed stretch reaches from a lattice place, walked on the
  tempo map's own time axis (a tempo change inside it is therefore exact) and rounded onto the tick
  lattice AWAY from `from` — floored going back, ceiled going forward — so the length never falls
  short of the duration. Going forward it is always at least one tick, because the time axis clamps
  at the terminal anchor. Two one-line bindings read it: `minimumSustainDistance` below, and the
  editor's `minimumSlideWindow` (`g_minimum_slide_window_seconds`, the shortest time a glide,
  slide-out or scrape leg may take, measured forward from where the leg starts).
- `g_minimum_sustain_distance_seconds` (its initializer is the only statement of the value), with
  `minimumSustainDistance(tempo_map, onset)` — the one settled gap drawn ink keeps before the next
  event: where a tail's ink stops, and what spans and hand ramps trim to. Keyframes keep their
  stored instants. It is a **duration**, so it takes the place of the onset being PROTECTED and
  reaches back from it through `tickPlaceReached`. It must stay below the kept-sustain bound below,
  so every tail that earns one keeps some ink; a `static_assert` says so. Every margin a scene build
  keeps before a HEAD is read from `HeadMargins` (`chart/head_margins.h`): `chartResolutions` asks
  the function once per distinct onset and carries the table, so the spans, the ink ends, the drawn
  span extents and each note's `margin_start_seconds` read one answer per head. The function stays
  the authority — the table only stops the build asking it again — and a fret-hand position, which
  is not a head, asks `minimumSustainDistance` directly.
- `g_minimum_kept_sustain_seconds` — the kept-sustain bound tail rule 2 drops a short effect-free
  tail against, a **duration** for the same reason. Tail rule 2 reads each note's actual ring in
  seconds through the tempo map, from onset time to ring-end time, and only a ring running
  *longer* than the bound earns a *drawn* tail. **The value
  itself is stated only at that constant** — it is headed for a user-tunable option, so read it
  there and never repeat it. Time is the reference because the player experiences the highway in
  time: a note-value bound draws tails too often in a fast song and too rarely in a slow one, so the
  same written value earns a tail below a crossover tempo and drops it above. The meter never
  enters — seconds do not care about the denominator — and the tempo map is piecewise constant
  between anchors, so the verdict can only change *at* an anchor and never inside a run. It bounds
  only what is drawn: the legato hold test reads the stored ring and asks strict adjacency, so
  nothing about a missing tail is inferred.
- `sustainBoundOf(notes, note, tempo_map)` — the one bound on a ring: the distance to the next
  onset on the note's **own** string, or nullopt when nothing later sounds there. A re-strike stops
  the ring (the same-string clamp), so a tail may reach that onset exactly and never pass it. Three
  rules need the same answer and are stated once through
  `ringEndWithinBound(notes, note, tempo_map, target)`, which APPLIES the bound — `target`, or the
  bound where `target` would reach or pass it: `normalizeSustainOverlaps` truncates a stored ring
  to it, the editor's duration verb grows a ring toward it, and its move verb steps a slide-out
  toward it, so "a ring's end reaching the next head on its string" has one answer wherever it is
  asked. It bounds the RING and nothing else: the span-implied note length `chartNoteLengths`
  answers deliberately runs past it, because a re-strike stops a string without releasing the shape
  (see that function).
- `predecessorHoldReaches(sustain, onset_gap)` — the connection hold test, and it is plain: true
  when the predecessor's stored ring reaches the onset, both in whole notes. Strict adjacency
  against the stored ring, with no kept-bound assumption and no margin slack, because the stored
  ring is the real one.
- `nearestTick(numerator, denominator)` and `isOnTickLattice(tempo_map, position)` (both in
  `timeline/tick_lattice.h`, beside `isValidGridPosition`, so the tempo map can ask them) — THE one
  rounding rule onto the tick lattice (the nearest whole tick, a tie to the earlier) and the test
  every stored position passes. The lattice is the finest position a chart may store: validation
  refuses an onset, ring end or keyframe between two ticks, and every producer of an exact instant
  — the grid's lines, the editor's drawn grid, the Guitar Pro import's commit point — rounds
  through the one rule. Load validates without repair, so a package saved before the lattice
  existed (exact septuplet offsets, exact bend percentages) is refused with "chart document
  violates chart rules"; there is no migration, by the no-back-compat policy that holds until the
  first release ships.
- `snapGridPosition`, `adjacentGridPosition` — the two questions asked of one note-value lattice
  (nearest line, and the neighbouring line in a direction), exact across beat, measure, and meter
  boundaries. The adjacent line is read off the lattice rather than found by stepping and
  re-snapping, which is what keeps a step back from any line landing on the line it came from in
  every meter.
- `TempoMap::wholeNotePositionAt(position)` and its inverse `TempoMap::gridPositionAtWholeNote`,
  with `wholeNoteDistance` and `advanceGridPositionByWholeNotes` (a ring's end is its onset
  advanced by its sustain) — the WHOLE-NOTE axis, the one axis a meter change does not bend, the axis the tick lattice is ruled on,
  and the axis every stored duration is a note value on. A ring or a keyframe offset means the same
  length on both sides of a signature change, so a whole-note place plus a duration is an exact
  instant anywhere. A pass over a note stream computes every onset's place once
  (`chartNotePlaces`, carried as `ChartConnections::places`) and measures gaps and ends by exact
  addition and subtraction; the distance and advance functions are for a single question about one
  pair. Seconds come from the same axis: `TempoMap::secondsAtWholeNote(place)` is the one route
  every chart instant — onsets included — takes to the clock, so one instant reached two ways (a
  ring ending on the next head) is one double, and `wholeNotePositionAtSeconds` is its inverse.
- `terminalGridPosition(tempo_map)` — the chart's closing barline as a `GridPosition`. Every
  consumer of the chart's end needs the same answer (package read closing the last tone region,
  tone-track normalization, tone-track validation, and the editor's end-of-chart navigation and
  selection bounds), so the terminal-anchor lookup and the position it becomes are spelled once
  here.

# Presentation: stored durations vs drawn ones (`chart/chart_presentation.h`)

The model is simple to state: `ChartNote::sustain` is the **actual** duration the string rings —
Guitar Pro's notated duration at import, what the editor's verbs author — and the stored note is
the only note every surface draws. What presentation adds, once per chart revision, is the **ink
end** (`chartPresentation`): where a surface stops drawing the ring. The ink is cropped, never
compressed, and nothing moves — every keyframe keeps its stored instant, and one standing past the
ink end is in the ring's ENDING ZONE, drawn only under a reveal. The readability policy is a pure
read-side derivation rather than an import-time rewrite that would destroy the notated durations,
so nothing that is drawn is stored and nothing that is stored is a guess. `chartResolutions`
carries it as `ink_end`, and the projection publishes both ends (`NoteViewState::ring_end_seconds`
and `ink_end_seconds`). Scoring's contract is that what is drawn is what is judged.

The arpeggio-vs-box CLASS reads no ink end at all. Whether a posture string is still carried into a
span's start is not a display fact: it asks where the fingers are and which of them the pick
reached, so it reads the **stored** ring, and a dead string's carry classifies exactly like any
other. `chartShapeArrivals` reads the stream only for the attacks it still derives — the right-hand
onsets inside a span — and takes the rest off the spans, where `deriveChartShapes` recorded it
against the stored rings. Technique rule 25, by contrast, takes a dead note's tail off what a
surface **draws**.

- `chartPresentation(connections, margins, tempo_map)` — per saved note, the ink end and the tail law's
  verdict, through four ordered rules: crop to the margin before the binding onset — the first
  sounding onset the ring does not run strictly *past*, so a ring ending exactly on one still binds
  and crops there — floored onto the tick lattice and never before the onset; drop short
  effect-free tails per onset group; draw no tail on a dead note that is neither tremoloed nor
  sliding; and last the tail law below, which reads no spans and so takes none. In the ending zone
  keyframes stay stored and are neither drawn nor judged, and the leg crossing the crop is drawn on
  its true path and stops there.
- `deriveChartShapes(connections, margins, tempo_map)` — the hand-posture spans and the
  posture table the notes imply. The chart stores none: a span is a statement about the notes under
  it, so deriving it is the only way it can never disagree with them. **A span is GRIP TENURE — the
  statement "the hand holds this grip, from here to here"** (the grip-tenure law); everything else
  here is bookkeeping about that tenure. THREE things open one and nothing else does: an onset
  STATING a grip — two or more stops struck at one slot; SOUND ALONE accumulating three
  or more overlapping members — the minimum gates founding by sound and nothing else, since growing
  a standing span has no minimum; and a LANDED TRAVEL, the one onset-less open — the grip held
  through the slide, at least one finger arrived, two members ringing strictly past the landing. A
  string that merely rings on past a break opens nothing: ring-out is a tail. A member is a sounding
  fretting-hand onset or a ring still sounding at a stated stop; a right-hand onset states no grip
  (ruled 2026-09-29). Such a span dates from its FRONT — the earliest member onset no
  preceding span already covers — and then **RUNS UNTIL ITS GRIP BREAKS**, which only two things do:
  a MEMBER QUITS (any posture member's sound out with nothing renewing that string at that instant,
  an onset of either hand renewing), or a CONTRADICTION states a different stop on a string the grip
  states or the hand audibly holds. A restatement of the SAME grip — a restrike, a re-pick, a chug
  chain — CONTINUES the span, so a chord, its dead chugs and the chord again are one statement (a
  change in ARTICULATION never moves the grip), and a stop the grip LACKS grows the span IN PLACE:
  growth IS accumulation, and nothing splits. A still-ringing string joins the posture it crosses,
  tap-only onsets found and grow nothing — except that a tap reaching the grip's own stop on its
  string, or below it, says that finger is gone and splits the span — and the stored close is the breaking event's own
  onset or where the statement ran out, whichever is EARLIER — never a display value, though a span
  closed by a following event still keeps the same minimum sustain distance every other element does
  once the projection trims it. Two facts the finished list cannot re-derive ride on the spans
  themselves: `landing_opened`, true for the one span no EVENT states at its own start — a landed
  travel's successor, whose members are rings struck under the statement BEFORE it, and only that,
  because a landing is not a SOUNDING: nothing is struck at one, so a successor is classified by the
  ordinary triggers found inside it and a chord sliding into chords is a box at both ends, joined by
  its members' sliding tails; and `bracket_place`, the instant that span's one opening mark draws
  at. That last one carries the span's own FRONT wherever an EVENT states it — a strum, an authored
  hold, and an accumulation's earliest uncovered member — carries the first interior sounding of the
  fretting hand where a LANDING opened it instead, and carries nothing at all where such a span
  never sounds interiorly and so draws no mark. The projection reads it only for a span that
  classifies arpeggio, since a box-class span states itself with its strums' own boxes. Neither has
  a proxy that holds, so the walk that read the channels states both. No founding MODE rides a span
  — growth is accumulation in place, so nothing turns on how a span was born — and no travel flag
  either, because the tail law never hides a ring that STATES something and so needs no span-level
  exemption for a travelling member.
- `chartNoteLengths(presentation, connections, shapes)` — how long the hand stays down,
  which is not the same question, and it is ONE RULE: a live fretting-hand member whose tail RESTS
  or was never earned, covered by a span, is held to the span's reach — while the grip is held, the
  board pins what is held. Resting members and those tail rule 2 emptied take the same extension
  because they are the same physical fact: under grip tenure a covered member's un-renewed death
  would have BROKEN the grip, so coverage past a member's ring IS the record that the finger never
  lifted (a re-strike replaces the sound, never the hand). There is no strum-size gate — a lone
  covered chug is a grip member exactly as a strummed one is. A member whose tail STANDS states its
  own hold; the other hand's onsets never inherit the reach; and a DEAD member is
  never held — a dead chug is percussion rather than a grip, which is also what chokes a wholly dead
  group without any unanimity rule stated anywhere. A COVERED resting member's stored ring is the
  floor its extension starts from, and that floor is keyed on COVERAGE rather than on the verdict:
  every plain note rests, so a verdict-keyed floor would run a lone note's pin out to its stored
  ring. An uncovered resting note holds to its ink end. What the walk remembers is the
  **furthest-reaching** span already started, never the latest-STARTING one: an earlier span running
  longer holds the same strum just as well, and keying on the latest start would let a short span
  beginning inside a long one shadow it, silently costing a held chord its extension and making the
  connection that extension justifies read as a plain pick. Scoring is RULED ("detection scores what
  the surface demands"), and the note-length channel is a surface convention that ruling reads, not
  one it waits on.
- **THE TAIL LAW**, tail rule 4 and the last pass inside `chartPresentation`. **A tail that shows
  no technique information RESTS**, whether or not a span stands over it: THE CURTAIN IS UNIVERSAL.
  It is VERDICT-ONLY: it reads the STORED rings, judges, and MARKS the tails it hides, emptying
  nothing — every member keeps the ink end tail rules 1 to 3 gave it, the 2D lane draws to that ink
  end always, and the 3D board rests hidden ribbons at distance, drawing each only inside its
  sliding reveal window (the tunable `g_tail_reveal_lead_whole_note`). It computes no length,
  invents no endpoint and introduces no threshold of its own (the reveal window is the board's, not
  the law's), so it moves no ribbon's length whatever it decides.
  THE VERDICT IS AN OFFSET — where the curtain takes over, and the ring's last always-visible
  landmark: zero for a plain ring, the last statement's offset for a statement that finishes (held
  to the ink end where the statement stands past it), the ribbon's own ink end (an EMPTY remainder)
  for a handover. Past it the curtain owns the ribbon to the ink end, so a chug chain's
  between-strike ribbons go and so does a lone sustained note's.
  COVERAGE IS NO PART OF THE QUESTION: the law asks nothing about spans, so a chart carrying no
  furniture at all rests exactly the same tails — a span was never what made a plain ribbon
  uninformative.
  **SCOPE, on both sides**: right-hand onsets stand outside the judgment
  entirely — a grip states where the fretting hand is, so a tap says nothing about whether that hand
  is still down, and the ring under a tap keeps its whole ribbon.
  **THE ATOM IS THE MEMBER**: each member is judged alone — a plain member rests, one still stating
  at its end draws beside it. Tail rule 2's per-group atom is untouched, and each resting member
  keeps its own landmark.
  **PRESENCE — nothing of its own**: a ring still STATING at its end (a bend held out, vibrato that
  never stops, tremolo, a slide-out) and not handed over never rests. The curtain owns only what the
  ribbon has stopped saying anything with; it has no vocabulary for a statement in progress. There
  are no exceptions beyond that: the disjunction IS the whole law. A ring whose string a later
  strike takes over (`ChartConnections::hands_over`, read off the SUCCESSOR's stored claim and never
  the resolved direction, since an equal-fret tie resolves `Unjustified` and still hands the string
  over) is a TRANSFER of the sound — a statement that FINISHES, at the takeover, which terminates
  whatever the ring was still stating — so it rests from its ribbon's own end with an empty
  remainder: it rests with its stroke and keeps every pixel of its ribbon, and the board publishes
  no window for it.
  **WHAT IT COSTS, the headline visual change**: plain sustained chords, quarter-note chug chains,
  dry arpeggios, co-terminating let-ring figures and lone plain notes over open board go
  RIBBONLESS. The rails, the repeat boxes and the board's hold-pinning are what state the tenure
  where furniture exists, and Alt, the selection and the caret reveal the close. Nothing that
  states anything of its own is touched.
  **THE CLOSER IS NOT SPECIAL**: a span-final tail is hidden like every other, and what makes that
  safe is the rails every span class draws plus the reveal that shows the close on demand, never
  ink ownership. Nor does the law need a conjunct about the STRING or the END a span names: growth
  in place makes every sounding string a posture member, so a covering span always names the
  string, and a member's un-renewed death breaks the grip, so every sounded member bounds.
  THE COVERAGE AUTHORITY IS ASKED NOTHING here. `SpanCover::reaching` — a single O(spans) prefix
  pass over spans that never overlap, keeping the seam ownership an ONSET at a seam needs, since it
  stands in the grip that ARRIVED — is read by the hold walk and the held-stop default alone. Their
  judgment is exact only because `ChartShape::sustain` stores the MUSICAL CLOSE: a close carrying
  shape rule 6's display trim would sit one margin early, and no comparison against it would be the
  musical one.
  **IT ASSIGNS NO LENGTH, WHICH IS THE POINT**, and the two rejected alternatives both did. INK
  OWNERSHIP — the bracket owning its members' ink so their ribbons draw nothing at all, recorded
  per note in a suppression flag each painter tests at its own draw site — cannot show a span-final
  long hold's tail, and hidden ink makes drawn and scored disagree while the published ink end
  still carries the whole ring underneath. CLIPPING a covered ring at its next head — the
  staircase — makes one ribbon's length a function of a neighbour's position, so every question about which neighbours
  count becomes a new ruling. This law can have neither argument: its verdict is PUBLISHED beside
  the length (`ChartResolutions::rested_from` to `NoteViewState::rested`), so there is ONE ink end
  per note and both surfaces draw to it, the verdict never moves `ink_end_seconds`, and drawn =
  judged stays intact.
- `clipPayloadsToSustain` — the one way a stored ring changes length once it carries a payload,
  shared with the Guitar Pro importer so a grown or clamped ring carries its end's statement the
  same way everywhere. It reads the note's ONE interval payload, its `keyframes` array, where each
  entry states any subset of the fret, bend and vibrato channels
  (`docs/plans/todo/unified-waypoint-model.md`). Presentation never calls it: it resizes no ring
  and moves no statement, it only stops the ink (tail rule 1). A statement standing exactly AT the
  ring's end (`endStatement`) keeps its stored instant, drawn only under a reveal where it stands
  in the ending zone. So the stored chart may end a slide-out or a bend exactly on the next head of
  its own string, because the store holds what the hands did (user ruling, 2026-09-21), and the
  importer's shift arrival and the `Shift+L` split both state theirs ON the next head.

Reading those channels is itself one authority, in `chart/chart.h`: a channel opens on the note (its
own fret, its onset bend, its onset vibrato) and every later change lands on a keyframe, so "what is
in force here" is a fold over the two. `RingState` is that state, `ringStateAtOnset` opens it,
`RingState::advance` applies one keyframe's statements (a channel a keyframe says nothing about
passes through), and `ringStateAt(note, offset)` folds to an instant — a statement standing exactly
AT the instant counts. Every reader of a running value reads it there: what a pull-off releases from
(`fretAtRingEnd` = the position channel at the ring's end), what a folded Guitar Pro segment's
vibrato flag has to disagree with before it says anything, the commit law's per-channel silence
test (`keyframeSaysNothingNew`), and the regions the projection hands both surfaces. What it
reports is the
**statement** in force, never the sounding value: between two statements the position channel is
travelling and the bend channel is on its curve, and the surfaces interpolate those.

`chartResolutions` is the whole picture, and the whole picture costs a pass over every note in the
song. A caller that only wants to know what a connection claim resolves to asks `chartConnections`
instead — the saved stream, the resolved motions and each note's same-string predecessor, and
nothing derived from presentation, because `resolveLegato` reads none of it. That is the pass the
editor's `H` verb takes; `chartResolutions` carries its result rather than repeating the walk. The
settle sweep, which runs at every caret move, seek and selection change, asks narrower still:
`unjustifiedLegatoClaims` resolves only the `Legato` claims, over the same predecessor relation
(`chartPredecessors`, the one walk both read), converting only those claims and their predecessors
to the saved form rather than the whole stream. The same walk answers
`chartPlantedStops` — the stop a pull-off lands on beneath its source, whichever hand made the
onset — because you cannot pull off onto a fret unless a finger is waiting there, so what a note
holds is a fact about its NEIGHBOUR. A held stop is DERIVED, never stored (ruled 2026-09-29).

One table is deliberately later than all of that: `chartHeldStops`, carried as `held_stops`, is the
pressed fret under every head that sounds ELSEWHERE: the PLANT a pull-off lands on beneath the
note, else — under an onset the PICKING HAND STOPS THE STRING FOR — **the DEFAULT: the PRESSED fret
the covering span's posture holds on that string**; coverage is half-open, so a tap at the instant a
span closes stands in no grip. **An open string is no finger**, so a stop of 0 — a plant onto the
open string, a default with no grip beneath it, a harmonic node in the posture — holds nothing.
It reads the derived postures, so it computes AFTER `deriveChartShapes` and feeds nothing that runs
before it. `NoteViewState::held_fret` carries this table's fret for the fret-hand light; no note
draws it, because other ink already prints every held stop.

# The TempoMap

`TempoMap` (`timeline/tempo_map.h`) is the **sole durable timing authority**. Its authored data
is small: time-signature changes (`TimeSignatureChange`, each PLACED on the whole-note axis — the
measure it begins is derived, never stored) and sparse *beat anchors* (`BeatAnchor`) pinning
grid positions — on a beat or between beats, anywhere on the tick lattice — to whole
milliseconds (`BeatAnchor::seconds()` is the one conversion back, `nearestAnchorMilliseconds`
the one rounding). Between anchors, time interpolates linearly in **quarter-note (metronome)
time** — a meter change between anchors re-slices beat durations rather than stretching them, so
tempo changes only ever happen at anchors (the "metronome-linear" invariant).

The **reign table** (`TempoMap::reigns`, one `MeterReign` per reign) is the ONE derivation of
every measure's length. Each change opens a reign at its place; where the reign before a change
is not a whole number of its measures, the remainder is one DERIVED short measure whose meter is
the remainder counted in the governing beat (4/4 cut after one beat is 1/4) or, where that is no
whole count, in the finer power of two (a 3/4 cut after seven eighths leaves 1/8). It is never
stored, so changing the meter back makes it vanish. Every address query (`timeSignatureAt`,
`beatsPerMeasureAt`, `gridPositionAtWholeNote`, the grid walker, the metronome mirror) reads the
table, so a short bar is one measure with one address everywhere by construction. The rules
(`validateTempoMapRules`) keep every change on the meter lattice (a place that is a whole number
of 1/256 notes) and every reign at least one whole measure long, so a short bar is always a meter
and never shares its downbeat with the change that made it.

Two performance mechanisms matter when querying it:

- Construction builds **derived index tables** once (`buildDerivedIndices()`), so queries like
  `secondsAtGlobalBeatPosition` binary-search monotonic tables instead of rescanning authored
  lists. (A per-query scan lags a 1/128 grid visibly; do not reintroduce one.)
- For sequential scans (grid lines, projections), `TempoMap::ForwardBeatTimeCursor` resolves
  non-decreasing positions in amortized constant time, bit-identical to the random-access query.

**A meter edit keeps time** (plan 65): inserting, restating, deleting or moving a time signature
leaves every object at its instant and gives it a new address, the measures after the change
renumbering. So does a tempo edit made with the **Note Lock** on (plan 66): the grid moves under
the notes instead of the notes moving with the beat, which is how a song whose grid is wrong under
correctly timed notes is repaired. **Scale Tempo** multiplies every span's tempo by a typed factor
and obeys that lock like every other tempo verb. With the lock ON it RELABELS, keeping time: every
grid address in the song is scaled on the whole-note axis, so x2 doubles the measure count (a meter
at measure m lands on 2m-1) and the music stays where it sounds. With the lock OFF it STRETCHES:
every anchor keeps its address and its distance in time from the first anchor is divided by the
factor, so every object keeps its address and moves in time with its beat — the repair for a chart
that drifts uniformly against its recording. One verb, one prompt, one menu row: the prompt's title
and the undo entry carry the form (`scaleTempoLabel`), so a desync is never a surprise.

Both run ONE transform, `readdressTimeline` (`editor/core/src/timeline/timeline_readdress.h`). A
driver states the new map on the whole-note axis — the placed `TimeSignatureChange`s and
`PlacedAnchor`s, placed by whole note rather than by token, anchors keeping their milliseconds —
plus a content map from each old whole-note place to its new one. The transform builds the new map
from those places, derives the anchors' tokens, and maps every position in every arrangement
through the content map (`readdressedPosition`), rounding onto the tick lattice
(`nearestTickPlace`, common core) only where the mapped place lies off it. Two notes on different
strings can round onto one onset that way, so the notes are then stably re-sorted into slot order.
The drivers:

- **meter edits** — `readdressUnderMeters` (`meter_edits.h`): anchors where they stood, the
  content map `keptPlace`, so nothing rounds (a debug assert holds it to that);
- **Scale Tempo under the Note Lock** — `scaledTimeline` (`tempo_scale.h`): meters, anchors and
  content all at f times their place (`scaledPlaces`); x2 is exact, /2 and free factors such as
  3/2 round. The factor's terms are each at most 16 once reduced (`parseScaleFactor`). Off the
  lock Scale Tempo runs no transform at all: it is a tempo-map edit (below).
- **the Note Lock** — `timeKeptTimeline` (`keep_notes_in_time.h`): the new map's meters and
  anchors, and the content map `timeKeptPlaces`, which sends each place to the tick nearest, under
  the new map, to the instant the old map gave it. Every place rounds once, to within half a tick
  of its old time — and once per LOCK, not once per edit: the lock holds the song's content as it
  stood when it engaged (its origin, `GridSession::note_lock`), and every tempo verb lays its map
  under that one content (`timeKeptSnapshot`), so twenty separate edits round each place exactly as
  one edit to the final map would. Re-addressing the song as it stands instead would round the
  already-rounded places again at every edit; a probe over random songs measured that stacking at a
  median 0.65 ms and a maximum 46.8 ms of drift after forty edits, against 0.22 ms and 1.03 ms for
  one rounding. The origin stays true because nothing else moves content while the lock is on
  (below).

A ring's `sustain` and a keyframe's `offset` are note values — differences on the whole-note axis
— so a meter edit, whose content map keeps every place, leaves them untouched. The locked Scale
Tempo and the Note Lock move places, so each is re-expressed as the difference of its mapped ends,
P(p+d) − P(p), which keeps every ring end and keyframe on the tick lattice. Nothing moves in
time: a later meter keeps its place, so a reign that is no longer a whole number of measures
before it ends in a derived short bar (`TempoMap::reigns`); a section left mid-measure stays
there; a terminal left mid-measure stays too, the song ending on a short final bar. The transform
refuses nothing — a reign left shorter than one of its measures is the tempo-map rules' refusal
(`ReignTooShort`), asked at the commit — and the locked Scale Tempo alone refuses a factor that
would carry a meter off the meter lattice (`isOnMeterLattice`), naming the meter where it stands
now.

The keyboard crosses every landing of a grid model by the model's own rule (`GridCarry`, stated
by each snapshot and applied by `carryKeyboardAcrossGrid`). A song-timeline landing (a meter edit,
the locked Scale Tempo, a tempo edit under the Note Lock) holds every time, so the keyboard keeps
its time. A tempo-map landing (a tempo edit with the lock off, the unlocked Scale Tempo) makes the
grid truth, so the keyboard keeps its address and its time moves with its beat. The verb's commit
(`commitGridModel`) and its undo and redo (`applyPendingEdit`) carry it alike, so an undo puts the
cursor back on what it stood on. The transport follows only where it no longer stands within the
cursor's one-millisecond trust tolerance of the carried place (`carryCursorTo`). No carry is a
keyboard move, so none of them scrolls the window (`EditorViewState::keyboard_move_count`); an
undo's focus on the marker it changed still does.

A time signature RESTATING its reign — the same meter as the authored reign before it, on one of
that reign's own downbeats — changes no measure, so it is the keyframe commit law's meter row: the
history and every file hold the map without it (`withoutRedundantTimeSignatures`, applied by the
marker funnel's written form, the package writer and the package reader), and the editor keeps it
in memory only while its chip is selected, dissolving it with no entry when the selection leaves
(`dissolveRedundantMeters`, at every settle and before every undo and redo). So `Alt+←/→` walks a
repeat across its predecessor's downbeats without the chip vanishing under the key, and `Ctrl+/`
typing the prevailing meter on a downbeat shows a selected chip to retype instead of doing nothing.
A repeat placed off that grid cuts a bar short and is a real change.

Every tempo-map edit (anchor insert, typed tempo, anchor delete, anchor move, and Scale Tempo's
stretch) ends in one step, `commitTempoEdit` (`tempo_handlers.cpp`): the verb hands it how to make
its new map from the session's map, and it commits either that map alone (`TempoMapSnapshot`,
`keptPlace`) or, with the toggle on, the lock's origin laid under it (`SongTimelineSnapshot`; the
cursor carried by `timeKeptPlaces` from where it stands), as one entry either way. A held `Alt+←/→`
run coalesces into one entry like the meter move's, but needs no run origin of its own: a move
states its anchor's time absolutely, and the content's origin is the lock's. Scale Tempo's stretch
is the pure edit `scaleTempoMap` (`tempo_map_edits.h`): each anchor's time becomes
`t0 + (t − t0) / f` in whole milliseconds by exact integer arithmetic, halves away from zero (so
x2 then /2 lands within a millisecond), and the terminal grows to cover the audio exactly as a
typed tempo's does (`terminalCovering`), never shrinking. Nothing is re-placed, so no lattice
refusal applies; a factor that lands two anchors on one millisecond is the tempo-map rules'
refusal at the commit. The verb reaches `commitTempoEdit` only with the lock off: its locked form
is not the stretch laid under the held content but a different map — the meters and anchors
re-placed at f times their place — so the verb chooses its form on the lock itself and the relabel
commits through `commitGridModel` as a meter edit does.

**While the Note Lock is on, the song's content is frozen.** Every verb that would change it is
refused through the one authority `noteLockRefusal` — the marker funnel's judge refuses every
marker model but the two that state the grid (`g_states_grid`: the tempo map and the whole
timeline), the chart funnel `applyChartEditPlan` refuses every planned chart edit, naming the
notes it would rewrite, the pending fret entry's plan refuses so the typed value's box is red, and
the automation points verb refuses — and the plugin removal, which takes a plugin's automation out
of the song, is unavailable (`ActionConditions::note_locked`). Two writes the lock admits move
content, and each re-engages the lock on what it wrote (`engageNoteLock`): the locked Scale Tempo,
whose free factor rounds the content it scales, and the settle sweep, whose flatten is the chart's
normal form. A meter edit keeps every place exactly, so it runs as it always does and the origin stays
true through it; `timeKeptTimeline` reads its meters from the verb's map for that reason.

Whether the history still stands on the origin is the history's own fact. Engaging marks the
cursor as the history's **base** (`EditorUndoHistory::markBase`), and the lock's commits push over
it; `holdsBase` answers true while the cursor is on the base or on an entry pushed over it since,
and false once an undo has gone below the lock-on point, a redo has landed on an entry made before
it, a push below the base has discarded it, or the depth bound has dropped it. The next tempo edit
asks before it reads the origin and, where the base no longer holds, re-engages on the content as
it stands — the undone note stays undone, the redone one stays — so a stale origin is never read.
An arrangement switch resets the history, and since the switch changes no content the controller
marks the base again on the empty history (`resetUndoHistory`); every project boundary resets the
grid session first, which leaves the lock off. Toggling the lock off drops its origin, and on again
engages it on the content as it then stands.

A chip drag, with the toggle on or off, writes nothing per pointer event: the core holds the
move and publishes the map it would make as `EditorViewState::grid_map`, which the grid lines and
the ruler draw from (the cursor rides the chip, `drag_cursor_seconds`), and as
`EditorViewState::content_map` when the toggle is off, so
content re-times with the beat (with it on, content keeps the session's map and its time). The
release (`IEditorController::onTempoAnchorDragReleased`) commits it once, as one move or one
re-address.

The flow every feature follows: charts author `GridPosition` → a projection resolves seconds
through the `TempoMap` once (`chart_projection.cpp` and `highway_projection.cpp` both walk a
`ForwardBeatTimeCursor`) → renderers consume seconds only and never do musical math per frame.
The one time query a 2D surface asks per mark is `TempoMap::secondsRetimedFrom` at its
seconds-to-pixel seam, a span lookup that is the identity unless a tempo-chip drag previews, so a
drag re-times what is drawn instead of re-projecting the song per pointer event.

# Who owns tempo truth (and the one-way mirror)

Tracktion's edit is **tempo-inert**: RockHero never reads tempo back from it. Hosted VST plugins
still need host tempo, so the engine *mirrors* the TempoMap into the edit's tempo sequence —
one-way, write-only (`src/tracktion/tempo_mirror.cpp`, entered via `Engine::mirrorTempoMap`).
Any code that reads `edit.tempoSequence` for timing is a bug by decision, not by style.

# The playback clock

Song time reaches frames through a dedicated read-only port, deliberately separate from the
side-effecting `ITransport`:

- `IPlaybackClock::snapshot()` returns `PlaybackClockSnapshot{position, capture time, rate,
  playing}` — wait-free from any thread (`AtomicPlaybackClock` stores integer nanoseconds and
  parts-per-million so every store is lock-free).
- The engine publishes a boundary at every playhead jump — construction, arrangement load,
  arrangement clear, seek, play, pause, stop (`engine_clock.cpp`, `publishClockBoundary`). A
  boundary also resyncs the loaded tone rig, because the backend evaluates automation only while
  the graph renders blocks, so a jump made with no blocks rendering would leave every tone
  parameter on its pre-jump value.
- The playing flag and the 60 Hz republisher are a separate fact — playback can stop where the
  playhead does not move — so they hang off `updateTransportState()` (`engine_transport.cpp`),
  which sees stops issued outside the transport port too.
- Consumers never use raw snapshots directly for animation: `PlaybackClockExtrapolator` advances
  the last snapshot to "now" with slew-limited correction and never moves backward during
  continuous play. The game's `FrameClock` wraps exactly this; the editor's vblank-driven views
  sample the same way.

# Grid and snapping (editor)

Distinct from the grid arithmetic above, and the split is intentional: `grid_arithmetic.h` in
common/core answers *musical* questions every consumer shares (how far apart, where does this
sustain end, snap to a note value), while `tempo_grid_geometry.h` in editor/core answers the
editor's *timeline* questions — which grid lines are visible, which line is nearest this pixel,
where does one keyboard step land.

The editor's grid note value (a `Fraction` of a whole note, default 1/16) is the *shared
authority* for both drawing and snapping: `tempo_grid_geometry.cpp` computes visible grid lines
and `nearestTempoGridPosition(...)` returns the line's `GridPosition` — the same math, so what you
see is what you snap to. Every line is the tick nearest its exact place (`nearestTick`), so a grid
no tick divides — a septuplet's 1/28, a 1/13 — still names only positions a chart may store, and
common's `snapGridPosition` and the Guitar Pro import round the same instant onto the same tick.

What a verb actually snaps ONTO is the **placement quantum**: `placementQuantumNoteValue(...)`,
the one authority, returns the grid note value while grid snap is on and the tick lattice
(`g_tick_quantum_note_value`, 1/3840 of a whole note — the MIDI PPQ tick) while it is off. Every
verb that quantizes a time POSITION reads it, with no per-verb modifier opt-out; a verb needing a
musical DURATION keeps reading the grid VALUE, because that is the unit the user authors in
(`docs/plans/completed/grid-snap.md`). Pointer gestures must go through
`musicalGridPositionForX` (see \ref guide_2d_views); keyboard stepping has its own single
primitive in the same header — `adjacentTempoGridPosition(...)` (the next line from any position:
common core's `adjacentGridPosition` under the editor's note-value validity policy, exact-rational,
so a coarse step from an off-lattice start lands on the adjacent line, never overshoots, and a step
back returns to the line it came from) — and common core's `TempoMap::secondsAt(...)` turns an
exact position back into seconds for geometry. The ring a placement authors reaches the next line
of the session's grid through the same primitive (`planInsertNote`), which is what keeps a note
placed on a grid no tick divides — a septuplet's — ending on the tick lattice.

The duration verb steps that same primitive, over the ring's END rather than a caret
(`planAdjustSustain`, `chart_edits.h`): a step puts the end on the adjacent line of the quantum's
lattice, so a ring left between lines snaps back onto them. That is why a duration gesture records
its steps in order — each carrying the note value it snapped by — instead of summing them: a step
has no size until you know where the end sits.

The `Alt`+arrow MOVE is a gesture of the same shape (`chartMoveGestureDelta`, `chart_edits.h`): each
press carries the selection's ANCHOR onto the adjacent line from where the run has REACHED, and the
run's delta is the sum of those distances in WHOLE NOTES — the axis a meter change does not bend
(`advanceGridPositionByWholeNotes`, `grid_arithmetic.h`). One delta moves every selected object; a
moved note's ring and keyframes are note values held relative to its onset, so they ride along
unchanged and a note carried across a signature change keeps its real length, every landing on the
tick lattice. One exact step on a grid no tick divides would leave the anchor between ticks. A run
that crosses a signature change is therefore worth a quarter-note beat per press before the boundary
and two eighth-note beats after it, which is why the presses are kept in order rather than summed.

*Design in flux: making the tempo map user-visible/editable shipped
(`docs/plans/completed/tone-track-tempo-map-plan.md`), but tempo-anchor authoring is still an
unbuilt roadmap area (`docs/plans/roadmap/41-tempo-map-authoring.md`) — the model above is
stable; its editing UI is not.*
