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

- `deriveChartShapes` walks the stored stream once, forward, and publishes every span with the
  posture table (rules 1 to 4). It reads every fret channel through `statedStopFrom` (rule 4), the
  publication thresholds through `foundsSpan` (rule 1), and every pull-off source through
  `gripStatement` (rule 7).
- `chartShapeArrivals` classifies each span as an arpeggio or a box (rule 5). The box each onset
  group wears is `HighwayChordBoxTreatment` in `highway_view_state.h`.
- `makeChartViewState` (`chart_projection.h`) draws each span short of its close through
  `drawnSpanExtent` (rule 6) and decides the column of each posture digit (rule 7).
- `chartPlantedStops` and `chartHeldStops` (`chart_legato.h`) derive the held stops (rule 8).
- `deriveChartShapes` obeys the shape holds, and `normalizeShapeHolds` (`chart_legato.h`) compares
  each hold with the span the walk published for it (`shapeHoldPublishedEnd`), so a hold fits
  exactly when the normal form keeps it as stated (rule 9), after merging the none holds that meet
  (`mergedNoneHolds`); `validateShapeHolds` (`chart_rules.h`) checks the hold stream's structure.

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
- **Shape hold** — the charter's statement that the hand holds one shape over a stated extent
  (`ShapeHold`, rule 9). Not hand-position rule 6's hold, which is one stop kept pressed.
- **Front** — the instant a span opened (rule 1). **Reach** — how far its members' statements run.
  **Close** — where it ended, the musical close (rule 3).

# The numbered rules

1. **A span is grip tenure: it opens at a statement and runs forward from it.** Every other rule is
   bookkeeping about that tenure. The walk reads the stream once, instant by instant, and never
   asks what the hand was doing before the instant it stands at; nothing is dated back.
   - **The open.** Where no span stands, a fretting-hand statement opens one at its own instant,
     fronted there: the strike's grip statements are its stops, however many there are, and a lone
     note opens a span exactly as a chord does. Two other things open a span: a landing (rule 4)
     and an unnamed shape hold's start (rule 9). Nothing else does: a right-hand onset states no
     grip (rule 7), a natural harmonic's node is no grip, and ring-out opens nothing.
   - **Carried members.** What sounds under a bracket's opening is what it prints: every ring
     already sounding at the front that outlives it by the minimum sustain distance there (tail
     rule 1) is a **carried member**, fretted or open alike. It states what its strike stated, it
     prints in the posture, it bounds the span like any member, and as a member the hand did not
     strike into this span it makes the span sound in parts (rule 5). It founds nothing: a span
     exists on its own onsets. A ring outliving the front by less than the minimum distance is a
     tail under the opening and joins nothing; a ring the opening slot strikes or sounds on its
     string is the slot's own business, never a carry; a ring caught mid-travel states no stop and
     joins nothing (rule 4); a natural harmonic's chime joins nothing. **A carried member's death
     is a split, not a close**: the piece up to the death is published with the carried member in
     its grip, and the span runs on from the death with the members still standing, fronted there,
     each piece classified on its own members (rule 5). The remainder exists only where it holds a
     stop of the span's own; a span of carried rings alone, which only a hold can state (rule 9),
     closes at the death. A fretting-hand restrike of a carried member inside the span makes it the
     span's own, and its death is then a quit (rule 3).
   - **Own members and publication.** A span's own members are the stops stated at or after its
     front: the statements it opened with, the strikes that grew it, a landing's survivors and a
     carried member restruck inside it. A span is published only once its own members have met a
     threshold at some instant: **two stops stated at one slot**, or **three own members sounding at
     one instant** — a struck one, or one whose ring still reaches strictly past that instant. A
     span that never meets one is dropped unseen when it closes; its rings are tails for whatever
     opens next, and its ground belongs to no span. Carried members count toward neither threshold.
     The thresholds gate publication and nothing else: a span grows, breaks and splits below them
     exactly as above, except that a two-stop span not yet published turns to parts where a
     published pair would break (rule 3). Both numbers are named once, in `chart_shapes.cpp`, and
     asked through `foundsSpan`, which the importer's let-ring fragment donation shares. A hold's
     span is published regardless (rule 9), and a landing successor is published as far as its
     predecessor was (rule 4).
   - **The posture** is the stop each member string holds, deduplicated by stop vector across the
     chart. It carries no name and no fingering, because nothing authors either.
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
     bracket, and the arriving head restates it as it lands. An accumulation is this and nothing
     more: a pedal, then a note, then a third opens at the pedal, grows twice and is published when
     the third sounds, fronted at the pedal — and a whole stroke struck over a ringing pedal that
     opened the span grows it the same way, so the bracket runs from the pedal. No disjoint-grip
     guard exists, because a ring that dies breaks the grip first (rule 3), so growth only ever
     unions grips whose sounds overlap. A stroke striking every stop a published span states and a
     string never stated is not growth but a new chord statement (rule 3).
   - **A partial sounding** — a lone re-pick of a held stop, or a partial restrike — is judged on
     its stops alone, however it is articulated. It continues a span already sounding in parts, and
     a whole stroke it follows directly under that stroke's own rings is absorbed so that the span
     flows through both (rule 3). Continuation is adjacency-scoped: a re-pick after a member's
     genuine gap meets a grip that has already broken. The re-picked string's own ring is then that
     string's newest bound, so a re-pick ringing short lets the span end at its own gap.
   - **A mixed onset** — fretting-hand notes struck under simultaneous right-hand onsets — counts
     its fretting-hand members alone: one is a single onset, two or more a chord. Right-hand onsets
     are transparent to the grouping (rule 7), so a chord ringing under taps on other strings keeps
     its span. They are not transparent to the ring: a tap is a real onset on its own string, so the
     same-string clamp ends any ring there.
3. **A span runs until its grip breaks.** Three things break it, and nothing else does.
   - **A member quits.** A member quits where its sound goes out with nothing renewing that string
     at that instant. Continuous means ringing through, or ending exactly at the next onset sounding
     that string, the strike-into-strike shape of a stored chug chain; so the first genuine stored
     gap on any member breaks the grip at that ring's end — a carried member's excepted, whose
     death splits the span instead (rule 1). Renewal is a grip's: a span stating one stop of its
     own has no grip to renew — the rings carried into it are no statement of the hand's — so it
     ends at that member's ring end however the slot there re-sounds it, and the restrike opens the
     figure anew: a lone note before a chord is its own gesture, whatever rings beneath it. Any onset
     sounding the string at that instant renews a grip of two or more stops, whichever hand made it,
     so a two-hand run over a held shape is one statement through its own re-picks. After that
     instant a right-hand onset keeps the string sounding only where it plants no stop other than
     the one the fretting hand holds there. Renewing a string and bounding the span are different
     acts: only a member's own strike writes how far the span reaches, because a reach a tap wrote
     would let a tapped sixteenth decide a chord's extent.
   - **A statement contradicts the grip.** A strike naming a different stop on a string the grip
     states, or on a string still audibly sounding another stop, breaks it; a same-stop restatement
     never does. A ring the same-string clamp ends exactly at the displacing strike is displaced by
     it: the displacement is that relation between the two notes, the ring abutting the strike, not
     the ring sounding at its end. A ringing open string is evidence too: a fret or a node struck
     over it displaces it. A slide-out ring witnesses nothing, its finger being off the board by its
     end. **The join break:** a span asserts its whole grip from its front, so a strike on a string
     the grip does not state, whose last foreign sound — the last instant it audibly sounded a stop
     other than the one now struck, whether a displacing strike ended that sound or it died into
     silence — ended after the span's front, breaks the span rather than growing it. A right-hand
     stop is the other hand's and writes no foreign sound; what it proves about the fretting finger
     is the tap's floor, below. A pull-off source riding above a stop the grip holds, and its
     release back onto that stop, contradict nothing (rule 7). The one contradiction a right-hand
     onset makes is **the tap's floor**: a picking-hand stop whose path reaches the standing grip's
     fret on its string, or goes below it, says that finger is gone. Over an open-string entry it
     proves nothing. After every break the statement that broke the span opens a span of its own,
     with the survivors still sounding carried into it (rule 1).
   - **The statement changes character.** A span's statements keep one character, whole or in
     parts, so its class is a fact of how its members arrived rather than a verdict on everything it
     later contains.
     - **The absorption rule.** A stroke striking the whole grip is a span boundary only where it
       stands alone. Where the very next slot's fretting-hand onsets restate a non-empty proper
       subset of the stroke's own stops, and every stroke member they do not restate still sounds
       its own stop strictly past that slot, the stroke is absorbed: the span flows through it and
       sounds in parts from there. A member whose ring ends at the parts, or who is mid-travel
       there, leaves the stroke standing alone, since the span could not flow through the parts —
       the quit arm closes it there, as the chord it was. The box law is display and unaffected:
       the stroke still wears its box (rule 5).
     - **Parts to chord: the unison restatement.** A stroke standing alone and stating every own
       stop the span states is the whole grip said in unison, a new chord statement. It closes a
       span the hand's own strokes sounded in parts, and it closes any span of two or more own stops
       when it also strikes a string never stated, since a superset states the whole chord and more.
       A whole stroke on a span never in parts that strikes nothing new is the chug chain, and
       continues it. A carried member is no stop the hand stated, so a stroke restating the span's
       own stops beside a carried ring is the chug, and a superset of a lone own stop beside one is
       growth.
     - **Chord to parts.** A stroke restating some but not all of the own stops a published span
       has stated is the statement coming apart where the span does not yet sound in parts (rule 5,
       its own strokes and its carried rings alike — the class that prints): the span closes there,
       and the parts open their own span at that strike, the chord's rings carried into it (rule
       1). A span already sounding in parts wears the bracket that covers partial sounding, so the
       stroke is a restatement that continues it. A stroke only
       on strings the span never stated is growth instead. Neither a pull-off source riding above a
       stated stop nor its landing back on it counts as part.
     - **The partial slide.** A slot whose first held member to fall silent does so while a
       co-struck glide is in flight — strictly after it departs and strictly before it arrives —
       sounds in parts from that very slot. Against a never-in-parts span it closes the span even
       where it restates the whole grip, and the span it opens is born in parts at its own onset. A
       voicing-shift slide whose held strings ring the whole transit stays one statement, a chord
       released exactly where one finger departs on a glide sounded whole for its whole tenure, and
       a travel of the whole grip is a chord slide (rule 4).
     - **Guards.** Neither split applies to a landing successor no event has stated yet: its first
       sounding turns its class in place. A member mid-travel blocks both splits and the class
       turn, because a glide is not the figure coming apart. A span already in parts rides its
       partials unchanged.
     - **A span too small for parts turns at a partial stroke like any other, and publication
       judges it** (rule 5): one that never grew to three members is published as the box its
       stroke stated — closing at the strike that began picking it apart and stated through the
       instant before it, the parts after that unpublished, where such a strike fell inside it;
       whole to its close where the parts that absorbed its stroke never did. A WHOLE stroke restating such a
       grip is a new statement of the box, never a turn of the one before: where the parts under
       its rings absorb it, the chain breaks there and the stroke opens its own span, born in parts.
   - **The close is the earlier of the reach and the event that ended the span**: the closing
     event's own instant, or the span's reach where its statement simply ran out. It is stored as
     the musical close (`ChartShape::sustain`), with no display margin in it (rule 6). The reach is
     the minimum of the members' reaches, and every member bounds, carried or own. A member's reach
     is its ring, capped at its fret channel's next landing (rule 4). Two strings of one strum with
     unequal rings end their span together at the shorter, a run of strums ringing into each other
     is one span through the last ring, and a stored gap between two strums makes two spans. **The
     closing head** (`ChartShape::closing_onset_place`) is the fretting-hand strike standing at the
     close, whichever way the span closed there — a break, a wall, a landing or a carried member's
     death at that slot — and only where the statement was still live at the close; a span whose
     statement ran out before the strike, and a close at a slot of held fingers or bare taps, has
     none.
   - **The posture truth criterion: no span claims a stop the hand abandoned while it ran.** A
     posture only ever grows, the first member to stop bounds the whole span, and the join break
     keeps the front honest on every string, so every fret a bracket prints was held for every
     instant it covers. A long accumulation bracket is true by construction and needs no length
     ceiling.
   - **The evidence outlives spans.** The walk keeps one per-string table, the hand, that no span's
     lifetime bounds. It holds the note whose statement the string carries, its current stop read
     through the channel and empty mid-travel; `covers`, the fretting hand's own reach, written
     only by a member strike and capped at a landing, the one input to a span's reach and close;
     `sounds`, renewed by a sounding onset of either hand, the one input to renewal;
     `foreign_until`, the end of the string's last foreign sound, the join break's state; and the
     stop its strike stated beneath the fret it sounds (rule 7). Splitting the reach in two lets the
     table outlive spans, which is why a ring can contradict a grip that no longer records it.
   - **A tail is `[start, end)`; abutment is a relation.** Membership and the carried join read
     sounding strictly, because a ring ending at a slot crosses no slot and an inclusive read would
     found zero-length spans. The contradiction and displacement witnesses also take a ring ending
     exactly at a strike, as the relation between the two notes: the ring abuts the strike.
   - **Every span with a sounding member is strictly positive.** Every member reaches strictly past
     its span's start, and the close is the earlier of two instants at or after the start. A span
     closing at its own front is not published.
   - **What the tenure holds.** A span shortens and hides no ring. The 3D board pins each covered
     fretting-hand member whose tail rests for the rest of its span or until its string is struck
     again, whichever comes first, floored at its own stored ring; a dead member is choked rather
     than held, and an uncovered resting note holds to its ink end (`chartNoteLengths`). The cut
     at the next strike is the sound's, not the hand's: the span's rails state the hold, a pinned
     head states what its string sounds, so a pull-off's source ends where its destination lands
     and a stop planted under a higher fret ends as that fret is struck. An onset at a seam
     stands in the grip that arrived (`SpanCover::reaching`).
4. **A member's fret travel breaks the span at the landing, and the grip it lands in re-opens
   there.**
   - **The channel has one reader** (`statedStopFrom`). Every question about where a finger is — at
     a strike, at a member's reach, at a landing, at a carried ring's join — asks which stop the
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
     truncates the travelling span there: fingers travelling together carry the statement, and a
     glide is not the figure growing.
   - **The landing opens.** A landing begins a statement exactly as a strike does. Where a travel
     lands — at least one member arriving exactly there — every member of the closing span whose
     own ring runs strictly past the landing is a survivor, and the survivors open a span at the
     landing with the stops their channels state there, open strings included as part of the chord
     landed on; a member that stayed put keeps its stop. A ring abutting the landing, ending exactly
     there, was held through the slide and belonged to the predecessor. The successor is published
     as its predecessor was where two or more survivors carried the grip over — two, not the
     accumulation minimum, because a landing's members were already established members of the span
     that just closed; a single survivor opens a span that growth may yet found (rule 1). A landing
     is judged once, before the slot at its instant, so a restrike struck there is never a survivor.
     The successor runs by rule 3 over those rings, which bound it. It opens exactly where its
     predecessor closes, so the two tile with no gap and no zero-length span, and the chain
     terminates: each successor starts strictly later than its predecessor and holds no more
     members than the rings that reached its start. A lone re-pick or a full restatement of the
     landed grip rides the successor like any other restatement (rule 2).
   - **A landing reached after the grip broke opens nothing of the broken span.** Where a
     contradiction ends a span mid-glide, the landing hands nothing over from it: the survivors
     ring out as tails, carried into the next span only where its own onsets open one over them
     (rule 1). One span stands at a time, so nothing remembers a grip it has not arrived at.
   - **Staggered landings refuse themselves.** Members landing at different instants carry over
     only the members stating a stop at the earliest landing, because a finger mid-glide states
     none. Where a ring that is not travelling survives beside a landed one, the two hold a shape
     and it opens. Travels of unequal distance landing together open like any landing.
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
     `ChartShape::bracket_place` the anchor, under one write rule: a span a statement opens seeds it
     at its front, a hold's span at the hold's start, and a landing successor's first sounding fills
     it.
5. **A span is an arpeggio when its members sound separately, and a box while every sounding of it
   is the shape whole.** The class decides the span's own furniture: an arpeggio's brackets, or
   nothing of its own for a box span, which its strums' own boxes state. The projection resolves
   the bracket's anchor (`ShapeViewState::bracket_seconds`) for an arpeggio span alone. A landing
   successor that never sounds inside draws no furniture at all: no bracket, and no box, since
   nothing strikes it.
   - **A sounding that is only part of the shape** makes the span an arpeggio: its own start where
     a member is carried in rather than struck (rule 1), a partial restrike, a lone re-pick, growth
     by a string the span never stated, a partial slide, or a whole stroke absorbed by its parts
     (rule 3).
     These are one comparison — a slot striking fewer strings than the shape sounds — and the walk
     records the answer (`ChartShape::sounds_in_parts`), because only the walk knows which slots
     the statement covers. Every accumulation is an arpeggio by construction. A landing is no
     sounding, so it is no partial one. The posture only grows within a span, so the comparison's
     denominator is well defined. A carried member's death splits the span (rule 1), and each
     piece is classed on its own members: the piece holding the carried member sounds in parts,
     and the remainder is an arpeggio if the hand's own strokes sounded it in parts and a box if
     they struck it whole — a bracket ending where the carried ring died, the chord's own tails
     ringing past it.
   - **A right-hand onset sounding within the span**, from its front up to its musical close, makes
     it an arpeggio: the fretting hand holds the shape while the other hand sounds above it. This is
     the one trigger `chartShapeArrivals` derives itself, because it asks about the span's extent.
   - **A derived span of fewer than three members is never an arpeggio**: a bracket over two
     strings states nothing its boxes do not. The rule is asked at publication, because a pair
     picked apart may yet grow to three — a dyad struck and then picked while its other note
     rings is born in parts like any absorbed stroke, and is the arpeggio from its stroke once a
     third member joins. One that never grew is published as the box its stroke stated, closing
     where the picking began, and its parts publish nothing (rule 3); it is no arpeggio under the
     other hand either. **A span an unnamed hold states is an arpeggio whenever it sounds in parts, at any
     member count**, and a box only while every sounding of it is the shape struck whole, since a
     chord span strictly means notes struck together and ringing together: the hold itself says one
     shape is held there, so the bracket states what the boxes cannot. The held exception is
     decided where the class is, in `chartShapeIsArpeggio`.
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
     which the span covers (rule 4), so its rails ride the window with it. A span a shape hold
     states is exempt: the hold is the author's word that the hand holds this grip through the
     hold's end, so a window changing inside it — widening for a finger added above a held stop —
     is not the hand leaving, and the furniture runs to the close under the closing head's margin
     alone (rule 9).
   - **The close stays reachable.** The view state publishes both instants,
     `ShapeViewState::drawn_end_seconds`, which draws everywhere, and
     `ShapeViewState::close_seconds`. The editor's 2D lane draws a span to its close while the
     reveal modifier is held, while the span is itself selected, while the selection holds a note
     the span covers, or while the caret stands inside its tenure (`chartSpanRevealed`). Where no
     trim was owed the two instants coincide and the reveal moves nothing. The 3D board draws the
     trimmed extent alone.
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
   none: no arpeggio shape is held there, so the arpeggio spans fronted inside it are hidden,
   though a chord struck inside still grips its own and stands. None holds that meet are one
   stretch: two that abut or overlap merge into one, comparing their edges alone
   (`mergedNoneHolds`). A none hold is never cropped around a chord.
   - **The walk is the one authority.** A hold changes only the edges. Everything inside is the
     span walk's own judgement by rules 1 to 5 and hand-position rule 6: grip statements and
     plants, membership, the landing instant, the class and the posture. Inside an unnamed hold
     the walk keeps exactly one span, the hold's, fronted at the hold's start.
   - **The hold is the statement.** An unnamed hold is the charter's statement that one grip is
     held there, so its span is published whatever the notes inside come to (rule 1's thresholds
     gate derived spans alone). The hold's span opens at the hold's start, every ring sounding there
     joining as a carried member by rule 1's carried-member law judged at that front, and every
     fretting-hand strike inside grows it (rule 2: growth is accumulation, each strike's grip
     statement as elsewhere). The first strike inside is the span's first statement, whole unless a
     partial slide divides it: parts sounding under it inside the hold turn the span themselves when
     they arrive, and parts at the hold's end lie outside the span, which is then the stroke whole.
     A carried member beside it makes the span sound in parts without changing the hand's own
     verdict. Every strike after it is growth or restatement, and the class follows rule 5.
   - **Inside, only a hand conflict breaks the span.** A hand conflict is hand-position rule 6's
     release of a stop the span states, the tap's floor (rule 3), or a member's glide leaving the
     stop the span states, at its landing (rule 4). Every other break is a notation split — a
     contradiction no release makes, the join break, the character splits (rule 3), the drop of a
     crowded landing (rule 4) — and the hold suppresses it, as it suppresses a member's quit: rests
     inside a hold are legal. The span's class is rule 5's: a sounding that is only part of the
     shape makes it an arpeggio, at any member count, since the hold keeps whole what a derived
     span would break.
   - **The close.** An unnamed hold's span closes at its first hand conflict inside it, or at the
     first instant at or after its stated end where the walk would close a span standing there:
     any break the walk recognises, hand conflict or notation split; its statement no longer in
     force, a rest included; the next hold's start; or the song's end. From the end on the span
     reaches as its members do, never short of the end, so a ring a tap renews ends where the
     fretting hand's ring ends (rule 3). The end is judged over the hold alone, `[start, end)`: a
     member whose statement ends exactly at the stated end closes the span there however the slot
     at the end re-sounds it, because an onset at the end belongs to what follows and renews
     nothing inside the hold.
   - **A hold fits its notes where its published extent equals its stated extent.** One ending
     where the walk closes nothing — in the middle of a sustain, nothing happening — does not fit.
     A hold exactly over a derived span fits and leaves every span as derived. One close rule
     serves every unnamed hold, whatever its span came to hold: a hold over silence publishes an
     empty-grip span that closes at its end and fits; a hold over a single note's whole ring fits;
     one ending in the middle of that ring does not, its span running on to the ring's end; a
     hold whose members all still sound past its end runs on, while one member whose statement
     ended inside it closes the span at the end. A carried member dying at the end closes the
     hold's span there, and the remainder runs on as derived only where it holds a stop of the
     span's own (rule 1): a span exists on its own onsets, and a hold's span, which exists on the
     hold, may hold nothing but the rings carried into it. Its span is always published; one
     closing at its own start holds nothing and is not. A none hold always fits. Whether the hold
     stream is well formed is a structural check of its own.
   - **Every other edge is a wall.** Any span but an unnamed hold's own at that hold's end closes
     where it stands across a hold's edge, and the statement struck there opens a span of its own
     (rule 1). A hold's wall is its published end: once its span has closed short of its stated
     end, that stated end walls nothing. Outside holds the derivation is unchanged, and at a
     hold's close the walk resumes as after any span closing with rings still sounding, a
     landing's hand-off included (rule 4).
     (`deriveChartShapes`, `normalizeShapeHolds`)
