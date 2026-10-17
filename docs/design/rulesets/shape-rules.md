\page design_rulesets_shapes Shape Rules

# Scope

These rules derive the chart's hand-posture spans from its notes: where the fretting hand holds a
grip, from when to when, which grip it is, and whether the notation marks it with a chord box or an
arpeggio bracket. No span is stored. A span is a statement about the notes under it, so every reader
derives the spans from the settled note stream, once per chart revision inside `chartResolutions`.
The one authored statement about a hand shape is the charter's shape hold (rule 9), which states
where the hand holds one shape and is judged against the notes like any other claim about them.
The stream is read as stored: a note's ring is the duration its string truly sounds, and a new
onset on a string ends the ring before it.

Code cites a rule as **shape rule N**. Rules 1 to 4, which decide when a span opens, runs and ends,
are also called **the grip-tenure law**. The rules are implemented in
`rock-hero-common/core/src/chart/chart_shapes.cpp`, declared in `chart_shapes.h`:

- `deriveChartShapes` walks the stored stream once and publishes every span with the posture table
  (rules 1 to 4). It reads every fret channel through `statedStopFrom` (rule 4), the founding
  thresholds through `foundsSpan` (rule 1), and every pull-off source through `gripStatement`
  (rule 7).
- `chartShapeArrivals` classifies each span as an arpeggio or a box (rule 5). The box each onset
  group wears is `HighwayChordBoxTreatment` in `highway_view_state.h`.
- `makeChartViewState` (`chart_projection.h`) draws each span short of its close through
  `drawnSpanExtent` (rule 6) and decides the column of each posture digit (rule 7).
- `chartPlantedStops` and `chartHeldStops` (`chart_legato.h`) derive the held stops (rule 8).
- `shapeHoldNormalEnd` (`chart_shape_holds.h`) judges a shape hold against its notes (rule 9), and
  `validateShapeHolds` (`chart_rules.h`) the hold stream's structure.

Whether a tail rests is tail rule 4 (\ref design_rulesets_tails), which reads no span.

Numbers are stable: a rule is never renumbered, a retired rule keeps its number, and a new rule
takes the next one.

# Vocabulary

- **Span** — the statement "the hand holds this grip, from here to here" (`ChartShape`).
- **Stop** — where a string sounds under the fretting hand: a fret pressed, the open string, or a
  harmonic node touched (`ChartStop`). Node 5 is neither fret 5 nor the open string.
- **Grip**, **posture** — the stop a span holds on each string (`ChartPosture`).
- **Slot** — every onset at one grid position.
- **Right-hand onset** — a note the picking hand sounds: a tap or a pick slide.
- **Grip statement** — the stop a fretting-hand strike says the hand holds: the stop it sounds,
  except a pull-off source riding a held stop, which states that stop (rule 7).
- **Member** — a stop a span counts: a fretting-hand strike in the span, or a ring still sounding at
  a stated stop.
- **Restatement** — a strike stating a stop the grip already holds. **Growth** — a strike stating a
  stop on a string the grip does not hold. **Contradiction** — a strike stating a different stop
  on a string the grip holds or that still sounds.
- **Travel**, **landing** — a fret channel moving between stops, and the instant it comes to rest or
  turns back (rule 4).
- **Coverage frontier** — the close of the last span emitted.
- **Shape hold** — the charter's statement that the hand holds one shape over a stated extent
  (`ShapeHold`, rule 9). Not hand-position rule 6's hold, which is one stop kept pressed.
- **Front** — where a span dates from (rule 1). **Reach** — how far its members' statements run.
  **Close** — where it ended, the musical close (rule 3).

# The numbered rules

1. **A span is grip tenure, and three things open one.** Every other rule is bookkeeping about
   that tenure.
   - **Openings.** An onset stating a grip opens a span: two or more stops the fretting hand states
     at one slot. Sound alone may accumulate one: three or more members sounding at one instant.
     Overlap is asked at that instant, never pairwise, so every member is sounding when the newest
     arrives and no bracket claims a conjunction that never held. A landed travel opens the one
     span no onset states, at two survivors (rule 4). Nothing else opens a span: a string ringing
     on past a break is a tail, and ring-out opens nothing. The accumulation minimum gates founding
     by sound alone and nothing else, so growing a standing span has no minimum. Both thresholds
     are named once, in `chart_shapes.cpp`, and asked through `foundsSpan`, which the importer's
     let-ring fragment donation shares.
   - **Members.** Struck and carried members are counted in one count, and a lone member of either
     kind opens nothing. An open string is a member exactly as a fretted one is: its `0` claims no
     finger, only the ring the chart stores, so no member's claim can be false. A natural harmonic
     states no grip: the finger lifts at the chime, so its node founds, grows and joins nothing,
     though its strike still sounds and can contradict (rule 3). A right-hand onset is no member
     (rule 7), and a ring caught mid-travel states no stop and joins nothing (rule 4).
   - **A ring belongs only to the span it was struck in.** Every member's onset lies inside its
     span. A ring struck before the coverage frontier founds nothing, joins no posture and prints in
     no later bracket — fretted, open or chimed alike — until it is restruck. A ring struck at or
     after the frontier is fresh evidence and folds in like any other member, which is what lets an
     open-position arpeggio found a span from its first open string. A ring also folds into no new
     span on a string the slot strikes at another stop. The landing is the one seam a member
     crosses (rule 4). A bracket states the onsets inside its span and nothing else: a refused ring
     is a plain tail whatever its stop, so it prints in no later bracket and classifies no span.
   - **The posture** is the stop each member string holds, deduplicated by stop vector across the
     chart. It carries no name and no fingering, because nothing authors either.
   - **The dating rule: a span dates from its front, the earliest member statement not covered by a
     preceding span.** A broken chord's bracket starts at its first note, not at the arrival that
     reached the threshold. The date is where a member's statement began, not its onset.
     **Re-articulation:** a same-stop restrike whose predecessor's ring reaches it, abutting or
     overlapping it, continues its string's earlier statement for dating, transitively, only where
     another member of the figure is sounding at the restrike; a restrike with nothing else sounding
     is the figure's first articulation, and the lone note before it is its own gesture. Nothing
     inherits earlier than the coverage frontier, so a restrike of a stop a closed span held dates
     the next span from that close. Continuity (rule 3's renewal) is untouched. A slid finger's
     statement begins at the landing where its new stop is established (rule 4). A whole-grip stroke
     standing alone (rule 3) reads none of this: the span it founds dates from the stroke.
   - **The dating floor.** A front never dates behind the later of the coverage frontier and, on
     every stated string, the end of its last foreign sound: the last instant it audibly sounded a
     stop other than the one its grip states, whether a displacing strike ended that sound or it
     died into silence. A member statement that began behind the floor dates the span from the floor
     itself, where it is already in force: a front is the later of the floor and the earliest member
     statement, so extending a member's statement backward never moves a front later. The floor is
     an invariant, not a founding step: growth cannot move a front, so a strike on a string whose
     foreign sound ended after the standing span's front breaks the span rather than joining it
     (rule 3). The frontier is a dating floor only and never enters a reach. A slid string's foreign
     sound ends at its landing, which the channel knows (rule 4), so no record is written for it.
   - **There are no founding modes.** How a span was born is not a fact anything reads, so the span
     publishes no such field.
2. **Restatements of one grip share one span.** Consecutive onsets stating the same stops are the
   same grip restated, so they continue one span for as long as its tenure runs (rule 3). A chug
   chain ringing strike into strike is one span from the first strum through the last strum's stored
   ring, whatever the tails draw.
   - **Articulation never splits a span.** The comparison is the stop alone, never the whole note:
     palm mute is the picking hand, dead is pressure, accent and ghost are dynamics, and none of
     them moves the grip. A chord, the dead chugs played on it and the chord again are one span,
     with articulation varying inside it as per-onset display data.
   - **An arriving stop the grip lacks grows the span in place**, because growth is accumulation.
     The posture gains the string and the figure stays one span: the new digit prints in the opening
     bracket, and the arriving head restates it as it lands. No disjoint-grip guard exists, because
     a ring that dies breaks the grip first (rule 3), so growth only ever unions grips whose sounds
     overlap. A stroke striking every stated stop and a string never stated is not growth but a new
     chord statement (rule 3).
   - **A partial sounding** — a lone re-pick of a held stop, or a partial restrike — is judged on
     its stops alone, however it is articulated. It continues a span already sounding in parts, and
     a whole stroke it follows directly under that stroke's own rings is absorbed so that the span
     flows through both (rule 3). Continuation is adjacency-scoped: a re-pick after a member's
     genuine gap meets a grip that has already broken. The re-picked string's own ring is then that
     string's newest bound, so a re-pick ringing short lets the span end at its own gap.
   - **A restatement continues a span and never opens one.** What opens a span at a lone onset is
     rule 1's opening law over the rings that onset overlaps.
   - **A mixed onset** — fretting-hand notes struck under simultaneous right-hand onsets — counts
     its fretting-hand members alone: one is a single onset, two or more a chord. Right-hand onsets
     are transparent to the grouping (rule 7), so a chord ringing under taps on other strings keeps
     its span. They are not transparent to the ring: a tap is a real onset on its own string, so the
     same-string clamp ends any ring there.
3. **A span runs until its grip breaks.** Three things break it, and nothing else does.
   - **A member quits.** A member quits where its sound goes out with nothing renewing that string
     at that instant. Continuous means ringing through, or ending exactly at the next onset sounding
     that string, the strike-into-strike shape of a stored chug chain; so the first genuine stored
     gap on any member breaks the grip at that ring's end. The quit is decided in one place. Any
     onset sounding the string at that instant renews it, whichever hand made it, so a two-hand run
     over a held shape is one statement through its own re-picks. After that instant a right-hand
     onset keeps the string sounding only where it plants no stop other than the one the fretting
     hand holds there. Renewing a string and bounding the span are different acts: only a member's
     own strike writes how far the span reaches, because a reach a tap wrote would let a tapped
     sixteenth decide a chord's extent.
   - **A statement contradicts the grip.** A strike naming a different stop on a string the grip
     states, or on a string still audibly sounding another stop, breaks it; a same-stop restatement
     never does (rule 1's re-articulation). A ring the same-string clamp ends exactly at the
     displacing strike is displaced by it: the displacement is that relation between the two notes,
     the ring abutting the strike, not the ring sounding at its end. A ringing open string is
     evidence too: a fret or a node struck over it displaces it. A slide-out ring witnesses nothing,
     its finger being off the board by its end. A span opening over a ring that sounds another stop
     contradicts nothing until it states that string, and a foreign ring never joins a posture: it
     contradicts or it is a tail. A strike on a string the grip does not state, whose last foreign
     sound ended after the span's front, breaks the span rather than growing it (rule 1's dating
     floor). A pull-off source riding above a stop the grip holds, and its release back onto that
     stop, contradict nothing (rule 7). The one contradiction a right-hand onset makes is **the
     tap's floor**: a picking-hand stop whose path reaches the standing grip's fret on its string,
     or goes below it, says that finger is gone. Over an open-string entry it proves nothing.
   - **The statement changes character.** A span's statements keep one character, whole or in
     parts, so its class is a fact of how it was founded rather than a verdict on everything it
     later contains.
     - **The absorption rule.** A stroke striking the whole grip is a span boundary only where it
       stands alone. Where the very next slot's fretting-hand onsets restate a non-empty proper
       subset of the stroke's own stops, and a stroke member they do not restate still sounds its
       own stop strictly past that slot, the stroke is absorbed: the span flows through it and
       sounds in parts from there. A member mid-travel holds no stop, so a chord slide with picks
       in its transit is never absorbed. The box law is display and unaffected: the stroke still
       wears its box (rule 5).
     - **Parts to chord: the unison restatement.** A stroke standing alone and stating every stop
       the span states is the whole grip said in unison, a new chord statement. It closes a span
       sounding in parts, and it closes any span of two or more stated stops when it also strikes
       a string never stated, since a superset states the whole chord and more. A whole stroke on a
       span never in parts that strikes nothing new is the chug chain, and continues it.
     - **Chord to parts.** A stroke restating some but not all of the stops a never-in-parts span
       has stated is the statement coming apart: the span closes there, and the parts found a span
       only by the ordinary opening law over what they strike, the chord's rings now lying behind
       the frontier. A stroke only on strings the span never stated is growth instead. Neither a
       pull-off source riding above a stated stop nor its landing back on it counts as part.
     - **The partial slide.** A slot whose first held member to fall silent does so while a
       co-struck glide is in flight — strictly after it departs and strictly before it arrives —
       sounds in parts from that very slot. Against a never-in-parts span it closes the span even
       where it restates the whole grip, and the span it founds is born in parts at its own onset.
       A voicing-shift slide whose held strings ring the whole transit stays one statement, a chord
       released exactly where one finger departs on a glide sounded whole for its whole tenure, and
       a travel of the whole grip is a chord slide (rule 4).
     - **Guards.** Neither split applies to a landing successor no event has stated yet: its first
       sounding turns its class in place. A member mid-travel blocks both splits and the class
       turn, because a glide is not the figure coming apart. A span already in parts rides its
       partials unchanged.
   - **The close is the earlier of the reach and the event that ended the span**: the closing
     event's own onset, or the span's reach where its statement simply ran out. It is stored as the
     musical close (`ChartShape::sustain`), with no display margin in it (rule 6). The reach is the
     minimum of the members' reaches, and every sounded member bounds, carried or struck. A
     member's reach is its ring, capped at its fret channel's next landing (rule 4). Two strings of
     one strum with unequal rings end their span together at the shorter, a run of strums ringing
     into each other is one span through the last ring, and a stored gap between two strums makes
     two spans.
   - **The posture truth criterion: no span claims a stop the hand abandoned while it ran.** A
     posture only ever grows, and the first member to stop bounds the whole span, so every fret a
     bracket prints was held for every instant it covers. A long accumulation bracket is true by
     construction and needs no length ceiling.
   - **The evidence outlives spans.** The walk keeps one per-string table, the hand, that no span's
     lifetime bounds. It holds the note whose statement the string carries, its current stop read
     through the channel and empty mid-travel; `covers`, the fretting hand's own reach, written
     only by a member strike and capped at a landing, the one input to a span's reach and close;
     `sounds`, renewed by a sounding onset of either hand, the one input to renewal; `stated_since`,
     when the current stop's statement began (rule 1); and `foreign_until`, the end of the string's
     last foreign sound. Splitting the reach in two lets the table outlive spans, which is why a
     ring can contradict a grip that no longer records it.
   - **A tail is `[start, end)`; abutment is a relation.** Membership, the fold-in, the opening
     count and re-articulation's witness read sounding strictly, because a ring ending at a slot
     crosses no slot and an inclusive read would found zero-length spans. The contradiction and
     displacement witnesses also take a ring ending exactly at a strike, as the relation between the
     two notes: the ring abuts the strike.
   - **Every span with a sounding member is strictly positive.** Every member reaches strictly past
     its span's start, and the close is the earlier of two instants at or after the start.
   - **What the tenure holds.** A span shortens and hides no ring. The 3D board pins each covered
     fretting-hand member whose tail rests for the rest of its span, floored at its own stored ring;
     a handed-over member holds exactly its stored ring, a dead member is choked rather than held,
     and an uncovered resting note holds to its ink end (`chartNoteLengths`). An onset at a seam
     stands in the grip that arrived (`SpanCover::reaching`).
4. **A member's fret travel breaks the span at the landing, and the grip it lands in re-opens
   there.**
   - **The channel has one reader** (`statedStopFrom`). Every question about where a finger is — at
     a strike, at a member's reach, at a landing, at a carried ring's fold-in — asks which stop the
     note's fret channel states at that offset. A travel goes on while each fret it states lies
     further in its direction, so a fret a glide passes is a point on its path. A restated fret is
     a rest and a fret back the way it came is a turn, and either one lands the travel: the chord
     the glide turns on is the chord the listener hears it land on. Between departure and landing
     the answer is no stop, so a finger mid-glide joins no other posture and cannot be restated. A
     slide-out says nothing about where the finger is: the channel goes on stating the stop it
     slides from, while an arrival into the next head lands like any travel
     (`arrivesIntoNextHead`). A natural harmonic states its node and never travels.
   - **The span covers the glide.** Fingers travelling together with the grip held carry the
     statement: the span states the departing grip, covers the glide, and ends at the arrival,
     where the new grip is established. A member's reach is capped at its landing, so the earliest
     landing ends the span. A glide from 5 to 7 and back to 5 is three spans: the departing grip
     over the glide up, the 7 from the turn over the glide back, and the 5 again from the return. A
     travelling finger lets go of nothing, so travel is no quit. Where a member's first
     fret-stating keyframe already differs, the hand departs at the onset and the whole glide is the
     departing grip's span. Every slot inside a glide is inside a span, so a tap mid-glide makes the
     span an arpeggio (rule 5). Nothing publishes that a span covers a glide: tail rule 4 never
     rests a ring that is still stating.
   - **Restatement mid-travel is judged per member.** A slot restates the shape while everything it
     sounds agrees with what the shape states and contradicts nothing the shape still covers. An
     open member restruck mid-slide therefore rides the span, makes it an arpeggio and leaves the
     split at the landing. A different fret on a stated string, or a string the span never held,
     truncates the travelling span there.
   - **The landing open.** Where a travel lands with the grip held, at least one member arriving
     exactly there and two or more of the closing span's own members sounding strictly past it at a
     stop, a span opens at the landing. Two, not the accumulation minimum, because a landing's
     members were already established members of the span that just closed, and nothing arrives at a
     landing. A ring abutting the landing, ending exactly there, was held through the slide and
     belonged to the predecessor; only a ring sounding strictly past the landing carries into the
     successor. The survivors carry the stops their channels state at the landing, open strings
     included as part of the chord landed on; a member that stayed put keeps its stop. The successor
     runs by rule 3 over those rings, which bound it. It opens exactly where its predecessor closes,
     so the two tile with no gap and no zero-length span, and the chain terminates: each successor
     starts strictly later than its predecessor and holds strictly fewer members than the rings that
     reached its start. A lone re-pick or a full restatement of the landed grip rides the successor
     like any other restatement (rule 2).
   - **A landing reached after the grip broke opens nothing.** Where a contradiction ends a span
     mid-glide, its survivors ring out as tails and the landed grip lives in their tails alone. One
     span stands at a time, so nothing remembers a grip it has not arrived at.
   - **Staggered landings refuse themselves.** Members landing at different instants open nothing
     at the earliest landing unless two members state a stop there, because a finger mid-glide
     states none. Where a ring that is not travelling survives beside a landed one, the two hold a
     shape and it opens. Travels of unequal distance landing together open like any landing.
   - **A landing is not a sounding.** Nothing is struck at a landing, so a successor is classified
     only by what sounds inside it (rule 5): a chord sliding into chords is a box at both ends,
     joined by its members' sliding tails.
   - **Emission is musical.** A landed span no event has stated — no restrike, no growth — is
     emitted only where its tenure strictly exceeds the minimum sustain distance read at the head
     that closed it; at or under that distance it states nothing its neighbours do not, so it is
     dropped and the chord name never flickers for a sliver. The comparison is strict because the
     chart encodes a glide into a restrike by placing the arrival exactly one such distance before
     the replacing onset. A landed span closed with no sounding head drops only at zero tenure, and
     one an event has stated is always emitted. No derivation question measures drawable room.
   - **The opening mark follows the sound.** A landing successor draws no opening mark at the
     landing: its mark anchors at its first interior fretting-hand sounding, and one that never
     sounds inside draws none. `ChartShape::landing_opened` publishes that one cause, and
     `ChartShape::bracket_place` the anchor, under one write rule: a span an event states seeds it
     at its front, an accumulation's included, and a landing successor's first sounding fills it.
5. **A span is an arpeggio when its members sound separately, and a box while every sounding of it
   is the shape whole.** The class decides the span's own furniture: an arpeggio's brackets, or
   nothing of its own for a box span, which its strums' own boxes state. The projection resolves
   the bracket's anchor (`ShapeViewState::bracket_seconds`) for an arpeggio span alone. A landing
   successor that never sounds inside draws no furniture at all: no bracket, and no box, since
   nothing strikes it.
   - **A sounding that is only part of the shape** makes the span an arpeggio: its own start where
     a member is carried in rather than struck, a partial restrike, a lone re-pick, growth by a
     string the span never stated, a partial slide, or a whole stroke absorbed by its parts
     (rule 3). These are one comparison — a slot striking fewer strings than the shape sounds —
     and the walk records the answer (`ChartShape::sounds_in_parts`), because only the walk knows
     which slots the statement covers. Every accumulation is an arpeggio by construction. A
     landing is no sounding, so it is no partial one. The posture only grows within a span, so the
     comparison's denominator is well defined.
   - **A right-hand onset sounding within the span**, from its front up to its musical close, makes
     it an arpeggio: the fretting hand holds the shape while the other hand sounds above it. This is
     the one trigger `chartShapeArrivals` derives itself, because it asks about the span's extent.
   - **The class reads the stored stream.** A dead string's carry classifies like any other ring,
     because where the fingers are is a fact about the hands; technique rule 25 is a display rule.
   - **A box marks simultaneity.** Every onset striking two or more strings with the fretting hand
     wears the standard chord box, inside a span or out of one, a partial restrike inside an
     arpeggio span included; a single note wears none. The box is the headless repeat box when the
     onset immediately before it, in the same span with no onset of any kind between, struck the
     same strings at the same sounding places (`ChartStop`). Every count and comparison here is
     asked of the fretting hand's members alone, and the mute profile and emphasis are free to
     change. A repeat box renders only the profiles it wears a mark for — plain, palm-muted, dead,
     or both — and any other profile, or any drawn tail, falls back to the full box. So silence
     re-heads, a fresh grip re-heads, any interleaved onset re-heads, and a partial strike after a
     full chord re-heads. Only the 3D board draws boxes; the 2D lane states the same with the
     span's rails and name.
6. **A drawn span keeps the minimum sustain distance before the head that closed it and ends where
   the hand next moves, and only a drawn one.** The derivation stores the musical close (rule 3);
   the trim is applied once, where the view state is built, so no margin sits inside the seams
   spans abut at. The margin's half of the rule also draws a tapped chord's hold.
   - **The margin** is tail rule 1's minimum sustain distance, read at the closing head. Three
     published facts feed it. The closing head (`ChartShape::closing_onset_place`) is not the
     close: a span whose statement ran out before the event keeps its own length, and a close at a
     slot of held fingers or bare taps has no head, so the replaced span ends exactly where its
     successor starts. The last statement (`ChartShape::stated_extent`) floors the trim, so rails
     never retreat behind the strum they are drawn over. Where even that leaves nothing, the span
     draws to its musical close, the protection a crowded sustain keeps.
   - **The hand's next move.** A drawn span never passes the start of the fretting hand's first
     margin morph that begins after the span's front: there the hand leaves the grip, so the
     furniture ends where the morph begins instead of riding it. It is a second limit beside the
     closing head's, the earlier of the two binding, under the same floor and protection. It is
     what bounds a span whose rings outlive the start of the move into the next position with no
     head to cut them. A glide's ramp is not such a move: it is the span's own member travelling,
     which the span covers (rule 4), so its rails ride the window with it.
   - **The close stays reachable.** The view state publishes both instants,
     `ShapeViewState::drawn_end_seconds`, which draws everywhere, and
     `ShapeViewState::close_seconds`. The editor's 2D lane draws a span to its close while the
     reveal modifier is held, while the selection holds a note the span covers, or while the caret
     stands inside its tenure (`chartSpanRevealed`). Where no trim was owed the two instants
     coincide and the reveal moves nothing. The 3D board draws the trimmed extent alone.
7. **A right-hand onset states no grip.** A finger resting on a fret makes no sound and no onset,
   so a hand holding a six-string shape and picking four of it streams exactly like a hand holding
   four. The derivation notates what the fretting hand sounds and nothing else: a tap or a pick
   slide founds, grows and joins no span, and a tapped chord derives none. A right-hand onset renews
   its string's sound and makes the one contradiction of the tap's floor (rule 3). What it holds is
   rule 8's derived fact, which reads the spans and never feeds them.
   - **The hold-under law.** A pull-off proves a finger on its landing stop at the release and at
     no earlier instant, so every source states the fret it sounds and its release is a new
     statement. The one exception is a source added above a stop its string is demonstrably already
     at: it states that stop, the fret it sounds being an ornament riding above, so the grip neither
     breaks nor is rewritten (`gripStatement`). The exception never holds where the source's own
     path sweeps that stop, and never under a harmonic played over a pressed stop, which states the
     pressed stop, so a pull-off from it is a new statement and closes the span at the release even
     while the chord around it rings on. The landing stop stays in the derived table as the
     source's held stop, and the pull's own head prints it.
   - **A held stop has no mark of its own.** The hand windows and the 3D fret-hand light read it
     (`NoteViewState::held_fret`), and other ink already prints every one: a plant by the pull's
     destination head, a default by its span's bracket.
   - **The digit window is the bracket's own instant.** A posture string's fret prints centred in
     the bracket where nothing heads the string at that instant; in the satellite column outboard of
     the closing bar where a head there, of either hand, sounds at another place; and nowhere where
     a head there sounds at this one. The note pass paints after the brackets, so the satellite is
     the one column a head sounding elsewhere cannot cover. Places compare as stops, never as
     printed numbers: a head printing node 17 over a grip holding 5 puts the 5 in the satellite.
     A head later in the span suppresses nothing, because the opening bracket is the span's chord
     frame and states the whole membership as the reader meets it; an accumulation's later members
     print there and their own heads restate them. The projection publishes the column
     (`ShapeStringViewState::digit`), so the painter and the hit test read one answer.
8. **A held stop is derived, never stored.** No pull-off lands on a fret unless a finger was already
   waiting there, so the connection the chart records is the statement that the hand held that
   stop.
   - **The plant.** A note whose same-string successor claims legato, where the claim resolves to a
     pull against this very onset and the successor stops the string at a lower fret, holds the stop
     the successor lands on, whichever hand made the note. Every fret plants alike, the open
     string's 0 included; only a destination the chart never defines plants nothing. The pull
     carries strict adjacency with it, so a released string hands nothing over
     (`chartPlantedStops`).
   - **The default.** A right-hand onset with no plant holds the stop the covering span's posture
     holds on its string. Coverage is half-open, so a tap at the instant a span closes stands in no
     grip.
   - **An open string is no finger.** A held stop of 0 — a plant onto the open string, or a default
     with no grip beneath it — holds nothing, though the spans still read the raw plant.
   - Nothing stores a held stop, feeds one to the spans or draws one on a note: `chartHeldStops` is
     the one table the hand windows and the fret-hand light read, re-derived whenever the spans
     reflow.
9. **A shape hold states where the hand holds one shape.** The charter authors it over
   `[start, end)`, both edges stated, and holds never overlap, though one may begin where the last
   ends. Its grip is unnamed — the hand holds one shape there, read from the notes inside — or
   none: no arpeggio shape is held there, though a chord struck inside still grips its own.
   - **What a hold states.** On each string, an unnamed hold states the pressed stop of the first
     fretting-hand note struck on it inside the hold. Open strings, natural harmonics and the
     picking hand state nothing, and neither does a ring sounding in from before the start. A none
     hold states no stop.
   - **No conflict inside.** A conflict is a later note releasing a stated stop under
     hand-position rule 6's release: a lower fret on the string, the open string, a node, the
     stating finger's own glide leaving it, a tap at or below it, or another note arriving at it.
     A higher fret is another finger and fits, and other strings, rests and silence never
     conflict. A conflict lies at its release instant: a struck note's onset, or a glide's landing.
   - **A stated stop has stopped sounding at the end.** The end fails where every stated stop
     still sounds across it, from a note struck inside the hold that grips it, so a higher fret
     struck on the string has ended the stated stop's sound. Once any stated stop has stopped,
     notes may ring on past the end, as they do past a derived span's close. A hold stating no
     pressed stop always passes.
   - **A hold fits its notes where it is its own normal end.** It fits where no conflict lies
     inside it and its end passes. Its normal end is its first conflict where one lies inside it,
     its own end where that passes, and otherwise the earliest later end that does, judged over the
     stops the longer hold states, a string first struck inside the extension included, and never
     past the song's end, past which nothing sounds. No conflict can lie inside the extension: a
     note releasing a stop there would have ended that stop's sound first.
     (`shapeHoldNormalEnd`, `shapeHoldFitsNotes`)
