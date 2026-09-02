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
- `GridPosition` (`chart/chart.h`) — `{measure, beat, Fraction offset}` with `offset ∈ [0,1)`:
  the exact musical address every authored thing uses (notes, tone boundaries, automation
  points). Fully ordered, so positions sort and compare exactly.

# The chart model, briefly

The chart itself — "the true tab" of notes, tuning, and hand placements — is the arrangement-owned
model in `common/core/chart/chart.h`, addressed entirely in `GridPosition`s and consumed by the
tab and highway projections and by package IO. Chart *editing* is partly built rather than unbuilt:
seven pure planners live in editor core (`editor/core/src/chart/chart_edits.h`), two of them
technique verbs, all funnelling through one shared finalize gate. The model still gets no
field-by-field tour here — see \ref guide_file_formats for the persisted shape and
`docs/plans/roadmap/40-chart-editing.md` for the remaining phases.

# Grid arithmetic (`chart/grid_arithmetic.h`)

Exact arithmetic *on* the grid lives in one header, and it is the shared authority the 2D and 3D
projections, the editor's duration verb, the Guitar Pro import trims, the connection resolver and
the validation gate all resolve through — so a spacing rule cannot mean two things:

- `g_minimum_sustain_distance_whole_note` (1/16 of a whole note) and
  `minimumSustainDistanceBeats(signature_denominator)` — the one settled gap every element keeps
  before the next event, expressed in signature beats so it scales with the meter.
- `g_minimum_kept_sustain_whole_note` (a quarter note) and
  `minimumKeptSustainBeats(signature_denominator)` — the shortest actual ring that earns a *drawn*
  tail, and the bound presentation rule 3 drops a short effect-free tail against. It bounds only
  what is drawn: the legato hold test reads the stored ring and asks strict adjacency, so nothing
  about a missing tail is inferred any more. Quarter-note-referenced, never
  signature-beat-referenced (user rule 2026-08-14): one signature beat of 12/8 is an eighth, and
  the old one-beat bound handed nearly every note of a 12/8 song a tail. In x/4 meters the two
  references coincide.
- `g_minimum_slide_window` (1/8 **beat**, not a whole-note reference like the two above) — the
  smallest span a glide, slide-out, or scrape leg may occupy. A zero-length gesture has nowhere to
  travel, so import synthesis, the presentation trim's slide-out compression, and the editor's
  scrape defaults all floor on this one window.
- `sustainBoundOf(notes, note, tempo_map)` — the one bound on a ring: the distance to the next
  onset on the note's **own** string, or nullopt when nothing later sounds there. A re-strike stops
  the ring (40-Q2-B), so a tail may reach that onset exactly and never pass it. Two rules need the
  same answer and are stated once through this one: `normalizeSustainOverlaps` truncates to it, and
  the editor's duration verbs grow toward it. It bounds the RING and nothing else: the span-implied
  hold `chartHolds` answers deliberately runs past it, because a re-strike stops a string without
  releasing the shape (see that function).
- `predecessorHoldReaches(...)` — the connection hold test, and now plain: true when the
  predecessor's stored ring reaches the onset. Strict adjacency, no assumptions — the kept-bound
  assumption and the margin slack were both compensations for a trimmed encoding that no longer
  exists.
- `globalBeatPosition`, `advanceGridPosition`, `beatDistance`, `sustainEndPosition`,
  `snapGridPosition`, `adjacentGridPosition` — the exact `GridPosition` ↔ beat conversions, signed
  and inverse-exact, all crossing beat, measure, and meter boundaries without floating-point
  drift. The last two are the two questions asked of one note-value lattice (nearest line, and the
  neighbouring line in a direction); the adjacent line is read off the lattice rather than found by
  stepping and re-snapping, which is what keeps a step back from any line landing on the line it
  came from in every meter.
- `terminalGridPosition(tempo_map)` — the chart's closing barline as a `GridPosition`. Every
  consumer of the chart's end needs the same answer (package read closing the last tone region,
  tone-track normalization, tone-track validation, and the editor's end-of-chart navigation and
  selection bounds), so the terminal-anchor lookup and the position it becomes are spelled once
  here.

# Presentation: stored durations vs drawn ones (`chart/chart_presentation.h`)

The model is simple to state: `ChartNote::sustain` is the **actual** duration the string rings —
Guitar Pro's notated duration at import, what the editor's verbs author — and what a surface
**draws** is derived from it, once per chart revision, by `presentedChartNotes`. The readability
policy that used to run at import time and destroy the notated durations is a pure read-side
derivation, so nothing that is drawn is stored and nothing that is stored is a guess. Every reader
is on the derived form: `chartResolutions` carries it as `presented_notes` and the projection builds
every `NoteViewState` field from it (**scored = presented**, plan ruling 4 — the scorer reads the
same form when it exists).

The one deliberate exception is the arpeggio-vs-box CLASS (user ruling 2026-08-28). Whether a
posture string is still carried into a span's start is not a display fact: it asks where the
fingers are and which of them the pick reached, so it reads the **stored** ring, and a dead
string's carry classifies exactly like any other. `chartShapeArrivals` reads the presented stream
only for the attacks it still derives — the right-hand onsets inside a span — and takes the rest
off the spans, where `deriveChartShapes` recorded it against the stored rings. E25 is untouched by
that: it still takes a dead note's tail off what a surface **draws**.

- `presentedChartNotes(saved_notes, tempo_map)` — one presented note per saved note, through four
  ordered rules: trim to the margin before the binding onset — the first sounding onset the ring
  does not run strictly *past*, so a ring ending exactly on one still binds and trims there —
  floor the trim on payload that still changes something, drop short effect-free tails per onset
  group, and present no tail on a dead note that is neither tremoloed nor sliding. Payload is
  clipped with the tail, never rescaled.
- `deriveChartShapes(saved_notes, claimed_stops, tempo_map)` — the hand-posture spans and the
  posture table the notes imply. The chart stores none: a span is a statement about the notes under
  it, so deriving it is the only way it can never disagree with them. **A span opens where two or
  more MEMBERS meet at an instant** (the one opening law, user ruling 2026-08-31): a simultaneous
  two-or-more-string strike is the case where every member arrives at
  once, a broken chord picked one string at a time is the case where they accumulate, and it is ONE
  law rather than two that agree. ONE COUNT over the three kinds a member takes — a sounding
  fretting-hand onset, a ring still sounding at a stated stop, and a stop the hand CLAIMS — since
  they are three ways of stating where a finger is; the claims arrive as `claimed_stops`, RESOLVED
  once for the revision, because a pull-off derives the held stop under a right-hand onset and only
  the connection walk knows that. Such a span dates from its FRONT — the earliest member onset no
  preceding span already covers — consecutive onsets stating the same STOPS merge,
  a change in articulation does not split the span, so a chord, its dead chugs and the chord again
  are one statement (user ruling 2026-08-29), a still-ringing string joins the posture it crosses,
  tap-only onsets are transparent, and a span
  closed by a following event keeps the same minimum sustain distance every other element does.
  Four facts the finished list cannot re-derive ride on the spans themselves:
  `founding`, which FOLLOWS COMPOSITION (user ruling 2026-08-31): STATEMENT where the opening slot
  stated the WHOLE shape — its own struck and claimed stops reach the threshold with nothing CARRIED
  folded in — and ACCUMULATION wherever it took the rings to get there, re-derived at every EVENT
  open and inherited only by the continuations no event states. It is the whole of what decides an
  arriving new stop — a statement-founded span growth-SPLITS on it, an accumulation ABSORBS it and
  grows in place;
  `carry_opened`, true for the span CARRIED RINGS open at a boundary rather than an event, which a
  LANDING and a member's DEATH both cause — and only that, because neither is a SOUNDING: nothing
  is struck at either, so a successor is classified by the ordinary triggers found
  inside it and a chord sliding into chords is a box at both ends, joined by its members' sliding
  tails (user ruling 2026-08-30); `covers_travel`, true where a member's glide runs inside the
  span's own extent, which suspended the ink-ownership rule that the bracket law retired and today
  has NO reader at all (kept derived, and the field says so); and `bracket_position`, the
  instant that span's one opening mark draws at. That last one carries the span's own FRONT
  wherever an EVENT states it — a strum, an authored hold, a growth split's claim, and an
  accumulation's earliest uncovered member — carries the
  first interior sounding of the fretting hand where CARRIED RINGS opened it instead, and carries
  nothing at all where such a span never sounds interiorly and so draws no mark. The projection
  reads it only for a span that classifies arpeggio, since a box-class span states itself with its
  strums' own boxes. None of the four has a proxy that holds — the spans covering a glide are not
  the spans opening a successor, since a staggered landing with every other member still mid-glide,
  and a landing the close outruns, cover one and re-open nothing while a death-opened successor
  covers no glide at all — so the walk that read the channels states all four.
- `chartHolds(presented_notes, shapes, tempo_map)` — how long the hand stays down, which is not the
  same question: a chug under a hand-shape span presents no tail at all, yet the span is what tells
  the player to keep holding it, so such a member holds for the rest of the span. The span is the
  whole answer and the note's own ring does not cap it — the continuity law already ends a span at
  the first member to stop stating its stop, so a ring shorter than the span's remainder is a ring
  the player's own re-strike cut, and a re-strike does not release the shape. The convention is
  asked of the presented stream so it extends exactly the members presentation emptied; spans may
  overlap, so what it remembers is the **furthest-reaching** span already started (an
  earlier span running longer holds the same strum just as well, and tracking the latest start let
  a short span beginning inside a long one shadow it, so a held chord silently lost its extension
  and the connection that extension justified read as a plain pick).
- `clipArpeggioTails(presented_notes, shapes, arrivals, tempo_map)` — the same span coverage read
  for the OTHER face of one law: where the furniture already states how long the hand stays down, a
  member's own ribbon stops restating it and reads RHYTHM instead. **THE BRACKET IS THE
  HELD-INDICATION; THE TAILS READ RHYTHM** (user ruling 2026-09-01). An arpeggio-span member's
  presented tail is its ring CLIPPED AT THE NEXT ONSET — the first onset at a strictly later
  instant, on any string — and then the ordinary presentation rules run on top of that. A picked run
  draws a staircase, one step per string; an absorbed chord draws a block of parallel tails, since
  co-struck members are at one instant and so never clip each other. This mutates the presented
  stream in place, which is the point: there is ONE end per note and both surfaces can only read it.
  **THE BRACKET CLIPS; THE BOX DOES NOT**, which is why the span's CLASS is a parameter: a bracket
  is drawn across the stretch its members arrive over, so it is what states the hold, while a box is
  drawn at an instant and states a strum — under a box the members' tails are simply their own, and
  "a chord of a quarter note or longer shows its tails" is presentation's kept-sustain earning
  already answering, not a length rule here. ONE DIFFERENCE from rule 1 of `presentedChartNotes`,
  and it is the whole of the rule: outside a bracket a ring may PASS an onset and go on, under one
  it may not. The trim itself is rule 1's own `trimToMargin`, called rather than restated, so the
  margin, the payload floor and the keyframe clip stay one rule for every note in the chart — which
  is also what keeps a technique-bearing tail exactly as long as its statement needs. Two
  exclusions, both MEMBERSHIP rather than exemption: a right-hand onset is a member of nothing (a
  tap over a held shape keeps the tail rule 1 gave it) and a silent hold has no tail to clip.
  Silent holds are skipped when scanning for the next onset too, exactly as rule 1 skips them.
  The coverage question underneath is stated once, as a type — `SpanCover`, the forward cursor that
  answers which span's furniture reaches an onset — and each of the two functions builds its own
  instance and takes its own pass, so what they share is the RULE rather than the traversal; a
  coverage answered in two places is one rule kept in step by hand.
  WHAT THIS REPLACED was C3, an ink-ownership rule: the bracket owned its members' ink and their
  ribbons drew nothing at all, recorded per note in a `tail_suppressed` flag both painters tested.
  It could not show a span-FINAL long hold's tail (the sighting that retired it), and hidden ink
  made drawn and scored disagree since `end_seconds` carried the whole ring underneath. Its
  carve-outs went with it rather than being carried over — each answered ink ownership, and there is
  none left to except from — which is why `ChartShape::covers_travel` ([D2] amendment 1) is now
  derived with no reader at all.
- `hasSustainTechnique`, `informativePayloadEnd`, `clipPayloadsTo`, `keptAfterLastStatedFret` —
  the tail helpers the rules are built from, shared with the Guitar Pro importer so its trim and
  the presentation ask the same questions. They read the note's ONE interval payload, its
  `keyframes` array, where each entry states any subset of the fret, bend and vibrato channels
  (`docs/plans/todo/unified-waypoint-model.md`); `informativePayloadEnd` is where the two shapes
  of information part company, since a bend value and a fret are complete at the instant they are
  reached while a vibrato START needs a minimum window past it to be shown at all.

Reading those channels is itself one authority, in `chart/chart.h`: a channel opens on the note
(its own fret, its onset bend, its onset vibrato) and every later change lands on a keyframe, so
"what is in force here" is a fold over the two. `RingState` is that state, `ringStateAtOnset` opens
it, `RingState::advance` applies one keyframe's statements (a channel a keyframe says nothing about
passes through), and `ringStateAt(note, offset)` folds to an instant — a statement standing
exactly AT the instant counts. Everything that used to carry a running value now reads it: what a
pull-off releases from (`releasedFret` = the position channel at the ring's end), what a folded
Guitar Pro segment's vibrato flag has to disagree with before it says anything, the change
detection in `informativePayloadEnd`, and the regions the projection hands both surfaces. What it
reports is the **statement** in force, never the sounding value: between two statements the
position channel is travelling and the bend channel is on its curve, and the surfaces interpolate
those.

`chartResolutions` is the whole picture, and the whole picture costs a pass over every note in the
song. A caller that only wants to know what a connection claim resolves to asks `chartConnections`
instead — the saved stream, the resolved motions and each note's same-string predecessor, and
nothing derived from presentation, because `resolveLegato` reads none of it. That is the pass the
settle sweep and the editor's `H` verb take, and they run at every caret move, seek and selection
change; `chartResolutions` carries its result rather than repeating the walk. The same walk answers
`chartClaimedStops` — each note's RESOLVED claimed stop, carried on `chartResolutions` as
`claimed_stops` — because a pull-off states the held stop under a right-hand onset, so which stop a
note claims is a fact about its NEIGHBOUR (user ruling 2026-08-31). Every consumer reads that
resolution and never `ChartNote::held`.

One table is deliberately later than all of that: `chartHeldStops`, carried as `held_stops`, is the
COMPLETE held stop under every right-hand onset — the authored value, the one a pull-off derives
over it, or, where the chart states neither, **the DEFAULT: the fret the covering span's posture
holds on that string, else 0** (user ruling 2026-09-02). It reads the derived postures, so it
computes AFTER `deriveChartShapes` and feeds nothing that runs before it — a default folded into
`claimed_stops` would be an input to the very spans it is read out of, and would make every bare tap
a member of the shape above it. `NoteViewState::held` is this table copied across, which is why that
field is present for every right-hand onset and absent everywhere else.

# The TempoMap

`TempoMap` (`timeline/tempo_map.h`) is the **sole durable timing authority**. Its authored data
is small: time-signature changes (per measure) and sparse *beat anchors* pinning specific beats
to absolute seconds. Between anchors, time interpolates linearly in **quarter-note (metronome)
time** — a meter change between anchors re-slices beat durations rather than stretching them, so
tempo changes only ever happen at anchors (the "metronome-linear" invariant).

Two performance mechanisms matter when querying it:

- Construction builds **derived index tables** once (`buildDerivedIndices()`), so queries like
  `secondsAtGlobalBeatPosition` binary-search monotonic tables instead of rescanning authored
  lists. (This is what fixed the 1/128-grid lag; do not reintroduce per-query scans.)
- For sequential scans (grid lines, projections), `TempoMap::ForwardBeatTimeCursor` resolves
  non-decreasing positions in amortized constant time, bit-identical to the random-access query.

The flow every feature follows: charts author `GridPosition` → a projection resolves seconds
through the `TempoMap` once (`chart_projection.cpp` and `highway_projection.cpp` both walk a
`ForwardBeatTimeCursor`) → renderers consume seconds only and never do musical math per frame.

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
and `nearestTempoGridPosition(...)` returns the exact rational `GridPosition` — the same math, so
what you see is what you snap to.

What a verb actually snaps ONTO is the **placement quantum**: `placementQuantumNoteValue(...)`,
the one authority, returns the grid note value while grid snap is on and the tick lattice
(`g_tick_quantum_note_value`, 1/3840 of a whole note — the MIDI PPQ tick) while it is off. Every
verb that quantizes a time POSITION reads it, with no per-verb modifier opt-out; a verb needing a
musical DURATION keeps reading the grid VALUE, because that is the unit the user authors in
(`docs/plans/in-progress/grid-snap.md`). Pointer gestures must go through
`musicalGridPositionForX` (see \ref guide_2d_views); keyboard stepping has its own single
primitives in the same header — `gridStepBeats(...)` (one step's size at a measure) and
`adjacentTempoGridPosition(...)` (the next line from any position: common core's
`adjacentGridPosition` under the editor's note-value validity policy, exact-rational, so a coarse
step from an off-lattice start lands on the adjacent line, never overshoots, and a step back
returns to the line it came from) — and `secondsAtGridPosition(...)` turns an exact position back
into seconds for geometry.

The duration verb steps that same primitive, over the ring's END rather than a caret
(`planAdjustSustain`, `chart_edits.h`): a step puts the end on the adjacent line of the quantum's
lattice, so a ring left between lines snaps back onto them. That is why a duration gesture records
its steps in order — each carrying the note value it snapped by — instead of summing them: a step
has no size until you know where the end sits.

*Design in flux: making the tempo map user-visible/editable is active work
(`docs/plans/in-progress/tone-track-tempo-map-plan.md`), and tempo-anchor authoring is an
unbuilt roadmap area (`docs/plans/roadmap/41-tempo-map-authoring.md`) — the model above is
stable; its editing UI is not.*
