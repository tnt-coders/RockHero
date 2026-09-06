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

- `presentedChartNotes(connections, shapes, tempo_map)` — one presented note per saved note, through
  four ordered rules (and then the tail law below, which is the fifth pass and the only one the
  spans feed): trim to the margin before the binding onset — the first sounding onset the ring
  does not run strictly *past*, so a ring ending exactly on one still binds and trims there —
  floor the trim on payload that still changes something, drop short effect-free tails per onset
  group, and present no tail on a dead note that is neither tremoloed nor sliding. Payload is
  clipped with the tail, never rescaled.
- `deriveChartShapes(saved_notes, claimed_stops, tempo_map)` — the hand-posture spans and the
  posture table the notes imply. The chart stores none: a span is a statement about the notes under
  it, so deriving it is the only way it can never disagree with them. **A span is GRIP TENURE —
  the statement "the hand holds this grip, from here to here"** (the grip-tenure law, user-signed
  2026-09-04); everything else here is bookkeeping about that tenure. THREE things open one and
  nothing else does: an onset STATING a grip — two or more stops struck or claimed at one slot;
  SOUND ALONE accumulating three or more overlapping members — the minimum gates founding by sound
  and nothing else, since growing a standing span has no minimum; and a LANDED TRAVEL, the one
  onset-less open — the grip held through the slide, at least one finger arrived, two members
  ringing strictly past the landing. A string that merely rings on past a break opens nothing:
  ring-out is a tail. A member is a sounding fretting-hand onset, a ring still sounding at a stated
  stop, or a stop the hand CLAIMS, since they are three ways of stating where a finger is; the
  claims arrive as `claimed_stops`, RESOLVED once for the revision, because a pull-off derives the
  held stop under a right-hand onset and only the connection walk knows that.
  Such a span dates from its FRONT — the earliest member onset no preceding span already covers —
  and then **RUNS UNTIL ITS GRIP BREAKS**, which only two things do: a MEMBER QUITS (any posture
  member's sound out with nothing renewing that string at that instant, an onset of either hand
  renewing), or a CONTRADICTION states a different stop on a string the grip states or the hand
  audibly holds. A restatement of the SAME grip — a restrike, a re-pick, a chug chain — CONTINUES
  the span, so a chord, its dead chugs and the chord again are one statement (a change in
  ARTICULATION never moves the grip, user ruling 2026-08-29), and a stop the grip LACKS grows the
  span IN PLACE: growth IS accumulation, and nothing splits. A still-ringing string joins the
  posture it crosses, tap-only onsets are transparent to the grouping, and the stored close is the
  breaking event's own onset or where the statement ran out, whichever is EARLIER — never a display
  value, though a span closed by a following event still keeps the same minimum sustain distance
  every other element does once the projection trims it.
  Two facts the finished list cannot re-derive ride on the spans themselves:
  `landing_opened`, true for the one span no EVENT states at its own start — a landed travel's
  successor, whose members are rings struck under the statement BEFORE it, and only that, because a
  landing is not a SOUNDING: nothing is struck at one, so a successor is classified by the ordinary
  triggers found inside it and a chord sliding into chords is a box at both ends, joined by its
  members' sliding tails (user ruling 2026-08-30); and `bracket_position`, the
  instant that span's one opening mark draws at. That last one carries the span's own FRONT
  wherever an EVENT states it — a strum, an authored hold, and an
  accumulation's earliest uncovered member — carries the
  first interior sounding of the fretting hand where a LANDING opened it instead, and carries
  nothing at all where such a span never sounds interiorly and so draws no mark. The projection
  reads it only for a span that classifies arpeggio, since a box-class span states itself with its
  strums' own boxes. Neither has a proxy that holds, so the walk that read the channels states
  both. Two fields the older law needed are GONE with it: `founding`, because there are no founding
  MODES left to discriminate — growth is accumulation in place, so nothing turns on how a span was
  born — and `covers_travel`, because the tail law never hides a ring that STATES something, so a
  travelling member needs no span-level exemption and the field had no reader left.
- `chartHolds(presentation, saved_notes, shapes, tempo_map)` — how long the hand stays down, which
  is not the same question, and it is ONE RULE (user sighting 2026-09-03): a live fretting-hand
  member with no DRAWN tail, covered by a span, is held to the span's reach — while the grip is
  held, the board pins what is held. Hidden and rule-3/rule-4-emptied members take the same
  extension because they are the same physical fact: under grip tenure a covered member's
  un-renewed death would have BROKEN the grip, so coverage past a member's ring IS the record that
  the finger never lifted (a re-strike replaces the sound, never the hand). There is no strum-size
  gate — a lone covered chug is a grip member exactly as a strummed one is. A member DRAWING its
  tail states its own hold; a silently-held finger and the other hand's onsets never inherit the
  reach; and a DEAD member is never held — a dead chug is percussion rather than a grip, which is
  also what chokes a wholly dead group without any unanimity rule stated anywhere. A hidden
  member's stored ring survives only as the floor where no span covers the read. What the walk
  remembers is the **furthest-reaching** span already started (an earlier span running longer
  holds the same strum just as well, and tracking the latest start let a short span beginning
  inside a long one shadow it, so a held chord silently lost its extension and the connection that
  extension justified read as a plain pick). Scoring is RULED ("detection scores what the surface
  demands"), and the hold channel is a surface convention that ruling reads, not one it waits on.
- **THE TAIL LAW**, the last pass inside `presentedChartNotes` and the whole of what span furniture
  does to a ribbon (user ruling 2026-09-04). **Span furniture may HIDE a tail, never shorten one.**
  It is VERDICT-ONLY (the execution-form amendment, user ruling 2026-09-03): it reads the STORED
  rings, judges, and MARKS the tails it hides, emptying nothing — the presented stream carries
  every member's rules-1-to-4 tail, the 2D lane draws that form always, and the 3D board rests
  hidden ribbons at distance, drawing each only inside its sliding reveal window (the tunable
  `g_tail_reveal_lead_whole_note`). It computes
  no length, invents no endpoint, reads no span CLASS and introduces no threshold of its own
  (the reveal window is the board's, not the law's), which is why authoring a span is reversible —
  deleting it changes verdicts, never lengths.
  A tail is hidden exactly where **ITS OWN SPAN COVERS THE RING**, and under the grip-tenure law
  that is ONE comparison: the span standing at the tail's OWN ONSET reaches at or past the ring's
  end — the ring dies AT or INSIDE that span's close — and the ring states nothing of its own. Only
  a ring dying PAST the close is LEAVING, and a leaving ring draws whole, the junction survivor
  included. A restrike interior dies inside its own span and IS hidden: same-grip renewal carries
  the span past a replaced ring's death, which is why a chug chain's between-strike ribbons go.
  **SCOPE, on both sides**: right-hand onsets and silently-held stops stand outside the judgment
  entirely — a grip states where the fretting hand is, so a tap says nothing about whether that hand
  is still down. That is the one place this law moves ink UP: the ring under a tap keeps its whole
  ribbon, where the retired rule cut it back to the tap.
  **THE ATOM IS THE STROKE**, matching rule 3's: the verdict is a CONJUNCTION over the stroke's
  tail-standing members, so a chord can never show a ribbon on the string that stopped and none on
  the string still sounding.
  **PRESENCE — nothing of its own**: a ring carrying a sustain technique, or one whose string a
  later strike takes over (`ChartConnections::hands_over`, read off the SUCCESSOR's stored claim and
  never the resolved direction, since an equal-fret tie resolves `Unjustified` and still hands the
  string over), always shows its presence. The span states where the hand IS; it has no vocabulary
  for what the string is DOING nor for a TRANSFER of the sound. There are no exceptions beyond that
  disjunction.
  **WHAT IT COSTS, which is the rebuild's headline visual change**: plain sustained chords,
  quarter-note chug chains, dry arpeggios and co-terminating let-ring figures go RIBBONLESS. The
  rails, the repeat boxes and the board's hold-pinning are what state the tenure there, and Alt, the
  selection and the caret reveal the close. Nothing outside a span is touched, and nothing that
  states anything of its own is either.
  **THREE CONJUNCTS DIED, and not one of them by omission** (2026-09-04). STRING and END are PROOFS
  rather than rulings, each conditional on no non-bounding member class ever returning: growth in
  place makes every sounding string a posture member, so a covering span always names the string,
  and a member's un-renewed death breaks the grip, so every sounded member bounds. CROSSING was
  REVERSED by the user — the closer's tail is not special, and a span hides ALL its tails except the
  explicit exceptions above. And TIME narrowed from a FIGURE (a maximal run of spans abutting at
  their musical closes) to the ONE span standing at the onset, which is why the figure id, the
  cross-span stretch walk and the seam query (`SpanCover::stillReaching`) all deleted with it: a
  question asked of one span has no seam to arbitrate. What survives is `SpanCover::reaching`, a
  single O(spans) prefix pass over spans that never overlap, and the seam ownership it keeps — an
  ONSET at a seam stands in the grip that ARRIVED. The judgment is only exact at all because
  `ChartShape::sustain` stores the MUSICAL CLOSE: while it carried rule 12a's display trim, every
  close sat one margin early and no comparison against it was the musical one.
  WHAT THIS REPLACED, twice over. C3 was an ink-ownership rule: the bracket owned its members' ink
  and their ribbons drew nothing at all, recorded per note in a `tail_suppressed` flag both painters
  tested; it could not show a span-FINAL long hold's tail, and hidden ink made drawn and scored
  disagree since `end_seconds` carried the whole ring underneath. The bracket law that replaced it
  CLIPPED a covered ring at its next head — the staircase — which made one ribbon's length a
  function of a neighbour's position, so every question about which neighbours counted became a new
  ruling and three grew in two days. The law above cannot have that argument, because it assigns
  nothing at all. Its verdict is PUBLISHED instead (`ChartResolutions::rested_from` to
  `NoteViewState::rested`), so there is still ONE end per note and both surfaces read it, and a
  hidden ring's `end_seconds` collapses onto its onset — drawn = scored intact. The span-FINAL tail
  C3 could not show is HIDDEN again, and deliberately: the user reversed the closer's exemption on
  2026-09-04 ("the last note in the span shouldn't get treated special"), and what makes that safe
  is the rails every span class draws plus the reveal that shows the close on demand, never ink
  ownership.
- `informativePayloadEnd`, `clipPayloadsTo`, `keptAfterLastStatedFret` —
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
