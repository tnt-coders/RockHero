# The Chart Ruleset — the five laws and every rule under them

This is the standing law of the chart model: what the chart stores, what the derivation makes of it,
what each surface draws, and what a verb may act on. It is FIVE laws, and everything under them is a
consequence.

THE CODE IS THE AUTHORITY. Where this document and the code disagree, this document is wrong:
`chart_shapes.cpp` for the span walk, `chart_presentation.cpp` for the presentation rules and the
tail law, `chart_projection.cpp` for what a view state publishes, `chart_legato.cpp` for the
connection and stop resolutions, `span_cover.h` for span coverage, `grid_arithmetic.h` for the
shared bounds, and `gp_chart_builder.cpp` for the import translations. The developer guide's rules
10 to 12b (`docs/developer/the-project-lifecycle.md`) are the same derivation written as a
maintained plain-English spec; this document is the law behind them.

---

## LAW I — TRUTH. The chart stores performance facts and nothing else.

What sounds: notes — onset facts plus the ACTUAL ring plus per-channel keyframe statements. What the
hand holds unsounded: claims — the stop the fretting hand holds under an onset the OTHER hand makes,
read through `claimedStop`. Sound truth is never bent for display; nothing derivable is stored.

- **The ring is the actual sustain**, strictly positive, bounded only by the next SOUNDING onset on
  its own string. This is a deliberate divergence from all published notation, which stores the
  NOTATED value and needs `let ring` / l.v. marks precisely because that value is not the true one:
  storing the truth needs no mark, and a mark would be a second authority over the same fact.
- **Keyframes** are per-channel-optional statements `{offset, fret?, bend?, vibrato?}`; fret and
  bend interpolate between their own statements, vibrato holds. `slideOut` is fret-only, because the
  release IS the ring's end by definition — physically forced, not stylistic. Each channel's
  semantics is separately convention-backed: MusicXML models a bend release as a SEQUENCE of bend
  elements, vibrato is an interval state, and a slide is relational and re-expressed locally per the
  no-note-references law.
- **`fret` is the stop the string SPEAKS from**, on every note that sounds and whichever hand
  stopped it: the fretting hand's press, a plain tap's landing, and the PRESSED stop under a
  harmonic of either hand — 0 for a natural, the fret held under the node for an artificial or a
  tapped one. The tapped harmonic is ONE record
  `{pressed fret, tap, node}` — the machine shape of MusicXML's harmonic triple
  (base/touching/sounding), with the touching node stored absolutely and the sounding pitch derived.
- **`held` is the finger the fretting hand plants BEHIND that stop**, legal only where the PICKING
  hand is what stops the string (`pickingHandStopsString`: a plain tap or a pick slide, carrying no
  node). **Fret 0 in a `held` stop is a VOICING member** — the chord frame's "o",
  not a finger — and is described that way. `claimedStop` is the one reading of a claim: `held`
  where the picking hand stops the string, the note's own `fret` under a tapped harmonic sounded
  over a PRESSED stop, and nothing at all under one sounded over the open string — that form is a
  natural harmonic whose node the picking hand touches, so it states no fretting-hand stop any more
  than a natural does (`harmonicOverPressedStop`, RULED 2026-09-18).
- **Vibrato has two tiers and an off state** (`VibratoState{Off, Narrow, Wide}`, saved as `narrow`,
  `wide` or `off`). Ordinary guitar vibrato IS physically narrow — a fraction of a semitone of
  excursion — while wide is the deliberate exaggeration, so `narrow` is an accurate intrinsic
  description of the ordinary act and never an instruction to hold back. `Off` is listed first so
  value-initialization lands on not-shaking; `"vibrato": "off"` at an ONSET is a read error (the
  writer can never produce it) while the keyframe channel accepts all three; consumers classify
  through `isShaking()` so nothing open-codes `== Narrow` and drops wide notes.
- **Internal consistency**: one record may not state contradictory facts about its string. The held
  stop is refused anywhere inside the note's traveled hull (`travelsThroughFret` — the closed hull
  of its own fret, every keyframe fret and its slide-out terminal);
  the node is judged against the note's own physical stop (`physicalStopFret` — its `fret`, or the
  capo when that is 0). A claim on a string the sound
  already states publishes no reach and is swept at settle — the physically impossible
  claim-under-ring cannot persist.
- **No chord entity is stored.** Names and fingerings are the future dictionary's decoration,
  matched by shape at read time. Convention agrees outright: chord symbols are a derived editorial
  layer.

### Import is this law applied to sources

Parse what the source states about SOUND and translate its spellings into these records, counting
every drop.

**THE PLAYBACK-TRUTH PRINCIPLE.** Where a Guitar Pro mark's duration or timing is not stated
explicitly in the score, GP's OWN PLAYBACK RENDERING is the default translation — never an invented
rule — because the chart's author tuned by ear against that playback. Check the reference
implementation before writing any custom duration rule.

- **Let ring — THE FIGURE LAW** (`letRingFigureEnds` in `gp_chart_builder.cpp`, and the developer
  guide's let-ring section). Each voice accumulates a GRIP: the stop last stated on each string
  since the figure began, read through the one statement authority (`statedStopAt`, and
  `gripStatementAt` where a pull-off plants a stop beneath the fret it sounds). A first-time string
  GROWS the grip; a same-stop statement CONFIRMS it. A figure SEAMS two ways and no other: an onset
  stating a DIFFERENT stop on a gripped string seams at itself (a GRIP seam), and an onset arriving
  past the AUDIBILITY HORIZON of the figure's most recent member seams at itself (a HORIZON seam —
  real silence, and the reason a continuous texture of any length stays one figure while a 33-bar
  rest does not). The horizon is one ORIGIN-BAR metric length past the beat it is measured from,
  which is GP's own let-ring audibility rule, stated once (`audibilityHorizonFrom`) for both its
  readers.
  - Every marked ring is `max(written, min(same-string clamp, figure end))`. Written length is
    AUTHORED TRUTH and therefore the floor; the same-string clamp is physics and the strongest
    bound; the pass itself lengthens only. A slide-out is exempt — the gesture IS the ring's end.
  - The FIGURE END is the first onset the figure's own voice states after its LAST marked note
    (material past the marked run never asked to ring), else the first onset anywhere in the track,
    else the figure's latest written end — capped at that last mark's audibility horizon. Anchor and
    cap are ONE rule at one scope (`scope_end`), never a per-ring bound: a per-ring cap stops each
    member of a stack separately and leaves it ragged, which is the disease the one-end law exists
    to cure.
  - **THE FRAGMENT DONATION.** A figure closed by a GRIP seam while holding too few notes to ever
    found a span — fewer than three, no two co-struck, the span machine's own founding law — hands
    its non-contradicting notes to the figure that closed it, so a new figure's marked head is never
    stranded in a dying remnant. Never across a HORIZON seam, which mis-groups nothing.
  - **THE OPEN STRING'S LIFT.** A figure ends where the HAND stops asking, and an open string is not
    held by the hand: nothing about a grip moving away stops a string no finger is on. So a grip
    seam is no answer to an open mark — its ring runs to the PHRASE's anchor instead, the phrase
    being the chain the seams already draw (figures joined by GRIP seams are one asking continued
    under a moving hand; only a HORIZON seam ends it). Capped by the phrase's own last mark, by the
    same one rule asked at the wider scope. Lengthen-only; what still stops an open ring is a DIRECT
    CONTRADICTION on its own string, which is the same-string clamp and needs no code of its own.
    Deliberately the open string alone — a natural harmonic's ring is hand-free by the same physics,
    but a marked harmonic ring is rare enough to sight before lifting.
  - Grammar is per VOICE; only the same-string clamp — physics — is cross-voice. One line saying its
    hand has moved says nothing about what another line's hand is holding, and an event no hand
    stated is not an event. The walk reads onsets and statements only, never a ring, so it is a pure
    function of the written stream: one forward pass per voice, no fixpoint, span-blind.
  - Tie-merged notes extend like any other marked note: ties combine into a single note at its true
    written duration, and the mark then extends that note normally.
- **Staccato** imports at HALF the stated duration, which is what GP's playback sounds. No staccato
  field ever exists — the short ring IS the record. Staccato and legato marked together degrade the
  legato claim to a pick through the resolver's existing counted path.
- **Trills** spell out at import as legato alternation (`expandTrilledEvents`): GP's trill IS
  alternation with hammer-ons and pull-offs, so its faithful translation is the existing legato
  vocabulary and no trill field ever exists. The run spells out at sixteenths — a knowing estimate
  the format forces, since gpif stores only the auxiliary's absolute pitch and no speed. The first
  note keeps the onset's marks, continuations claim legato carrying only the hand-truth mutes, the
  last note absorbs the remainder and the onward tie, and hammer/pull derive from the frets alone.
  Unexpandable trills stay single notes and are counted: a ring within one step, or an auxiliary the
  hand cannot reach. The capo'd open is a LEGAL auxiliary.
- **Rolls** (GP's beat-level "Arpeggio" property, engraving's ROLLED CHORD — the vertical wavy line,
  not our derived arpeggio-span sense) import as exactly the sound: each member struck at its turn
  over the stored spread, every one ringing to the end its beat gave it. The derivation reads one
  arpeggio span off those rings by the ordinary opening law, with no rule of its own. The second
  slider ("Start time") is honoured — at 0 the roll ANTICIPATES, the last member landing on the beat
  and the figure opening a spread early; no reference implementation exists, so the recorded
  semantic is the linear reading of GP's two labelled endpoints. Degenerate rolls (spread
  unfittable, lone-note beats) stay simultaneous and are counted. **IMPORTS AUTHOR ZERO CLAIMS.**
  The statement model is sound states and AUTHORED states; the claim machinery keeps exactly the
  residue it was always for — the never-sounded stop and the deliberately short-rung one.
- **The playback rings are read as imported**, with no fold-in filter: a fretted ring crossing a
  chord's onset physically PROVES the finger stayed, and an open-string carry is a voicing member,
  so the flips such rings produce are overwhelmingly TRUE statements.
- **Strum direction is deliberately out of the chart record.** It changes neither the pitches nor
  the posture and detection cannot verify it from audio, so it would be display-only advice riding
  in the truth record. GP's `Brush`/`PickStroke` marks drop WITH A COUNT. If it ever returns it
  enters as a pedagogy-surface datum, not a chart-truth field
  (`docs/plans/todo/strum-direction-support.md`).

## LAW II — NO EMPTY STATEMENTS. Every stored record asserts something; what asserts nothing is refused at write or swept at settle.

- An empty keyframe is refused; a pending statement dissolves at settle iff it changes neither path
  nor state; a keyframe that says nothing the path does not already say is authoring state — no
  undo entry, gone when its note leaves focus, shed by the writer and by the load repair
  (`keyframeSaysNothingNew`).
- **The inert sweep asks ONE question** — is the claim's derived face absent (joins no span, past
  its span, restates a stated stop)? It runs in the same undo entry, clearing the FIELD and never
  the note, which is a sound the charter wrote. **It is
  ONE PASS** (`sweepInertClaimedStops`): what it takes is a claim that reached NO span, so it was a
  member of nothing and no span's membership moves when it goes — the cascade a fixpoint would
  iterate for cannot arise.
- **A claim that reaches a span is never inert**, and the sweep needs no rule to say so: reaching is
  publishing, so the derivation records the span each claim reached (`ChartShapes::claim_shapes`)
  and the sweep reads that same record.
- **A harmonic needs no exemption, and the sweep carries none** (2026-09-17). A tapped harmonic's
  claim is its own `fret` where it presses one — the stop its pitch is measured from — while the
  sweep clears `held` and nothing else, and a node-bearing note carries no `held` at all, so a stop
  the record's own pitch depends on is out of the sweep's reach by construction rather than by a
  clause. A PLAIN
  tap's lone held always sweeps, and correctly so — a lone member opens no span, so the claim
  reached nothing.
- **A claim is a member that does not SOUND on its own, and its CARRIER is a note that sounds at
  that slot.** A claim comes from one of two fields and `claimedStop` is the one place that says
  which: `held` under a plain tap or a pick slide, where the picking hand stops the string and the
  fretting hand's planted finger is the claim; and the note's own `fret` under a TAPPED HARMONIC
  SOUNDED OVER A PRESSED STOP, where the picking hand only touches the node and the string speaks
  from the stop the fretting hand presses — over the OPEN string that same record claims nothing,
  being a natural harmonic the other hand touches. Either way the claim is answered in the slot that
  founds it — the tapped harmonic sounds
  FROM its claimed stop, and a plain tap's plant is the stop its string falls back to when the
  tapping finger lifts. A
  shape the hand alone states therefore needs nothing later to earn it: it publishes at its own
  instant with zero sustain, and a sounding arrival at a claimed stop GROWS it in place rather than
  replacing it. There is no justification test.
  **2026-09-17**: the law's justification half — a shape the hand alone states must be JUSTIFIED by
  a later sound at a claimed stop or it DISSOLVES — left with `NoteAttack::None`, the silent hold.
  Without that attack value the test has no population: every claim is carried by an onset that
  sounds it, confirmed by an exhaustive probe (46,655 valid charts, 82,212 claims, zero exceptions),
  so the machinery was deleted from `chart_shapes.cpp` as dead code with the corpus census
  bit-identical. A later span-template design that reintroduces a stop nothing sounds must bring its
  own justification rule; this one is gone.
- **The mid-chain "still held" claim is unstatable, and DELETE is the record.** The repeated-chord
  member figure has three intents with three records: *string simply not played* — Delete, because
  the smaller strum IS the record, which is published notation's own economy (no published
  persistence construct exists; the only persistence ink there is — duration lines, classical barre
  and guide-finger marks — is fingering-layer CONTINUATION anchored at the original statement, never
  a fresh mid-figure record); *previous note keeps ringing* — the tie; *finger stays down, string
  unpicked* — unstatable mid-chain, because the claim is the default stated aloud (physically true
  in damped context and of every chug member between strokes, hence informationless). Span-START
  claims are untouched: the bracket remains the licensed posture statement at its own statement.
- Deletion cascades ride the entry. Engraving's own discipline — no redundant marks — agrees; the
  courtesy-accidental class of reader-aid redundancy belongs to projection, never storage.

## LAW III — SHAPES ARE STATED, NEVER GUESSED.

**THE GRIP-TENURE LAW.** A span is the statement "the hand holds this grip, from here to here", and
everything else is bookkeeping about that tenure. The design record is
`docs/plans/in-progress/span-derivation-ground-up.md`; the machine is `chart_shapes.cpp`.

**THE EVIDENCE OUTLIVES SPANS.** The walk owns ONE per-string table — THE HAND — that no span's
lifetime bounds, holding per string: the current finger and the stop its channel states (empty
MID-TRAVEL, because a finger between stops is on none, which is how staggered slides refuse
themselves); TWO reach columns, `covers` — the fretting hand's own reach, written only by a member
strike and capped at a travel's landing, and the ONLY input to a span's reach and close — and
`sounds`, renewed by any sounding onset of either hand and the ONLY input to renewal and continuity;
WHEN the current stop's statement began; and the end of the last FOREIGN sound on the string.
Splitting the old single reach in two is what lets the table outlive spans at all, and it is why a
ring can contradict a grip that no longer records it.

**TWO QUERY WINDOWS over the one stop**, named so they cannot collapse into each other. The
CONTRADICTION and DISPLACEMENT witnesses read end-INCLUSIVELY, because the same-string clamp puts a
displaced ring's end exactly ON the displacing strike and a strict read would make the junction
invisible. MEMBERSHIP, the fold-in and the opening count read STRICTLY, because a ring ending at a
slot crosses no slot and the inclusive reading births zero-length spans.

**A span OPENS three ways and no other.** An ONSET stating a grip — two or more stops struck or
claimed at one slot, the statement threshold of **2**. SOUND ALONE accumulating — three or more
members' rings overlapping at stated stops, the minimum of **3**. And a LANDED TRAVEL, with two or
more members ringing strictly past the landing. **Ring-out opens nothing.** Both numbers are named
once, beside each other, in `chart_shapes.cpp` (`g_span_member_threshold`,
`g_accumulation_member_minimum`), and appear nowhere else.

- **The three-minimum gates founding by SOUND and nothing else.** A STATEMENT is untouched —
  two-note strums included — so chord boxes and dyads are unaffected, and growing a standing span
  has no minimum at all. Two-member accumulations read as noise beside the figures three members
  find. The escape from the higher number is an AUTHORED two-note span, owed by
  `docs/plans/todo/span-marker-redesign.md`.
- **The landing's TWO is about THE SAME GRIP IN MOTION, and does not generalize.** Fingers
  travelling together carry the statement and the close belongs to the landing, so a landing's
  survivors were already ESTABLISHED members of the span that just closed and nothing ARRIVES there.
  A lift-and-replant is a NEW statement — which is exactly why the law calls it a contradiction —
  and a new statement is judged fresh: two stops struck together, or three accumulating. Counting
  the old grip's leftover fingers toward a new grip is the mixing of two statements the break exists
  to keep apart.
- Overlap is asked at ONE INSTANT, which is what makes the accumulation form STRONG rather than
  pairwise: every member is sounding at the moment the newest one arrives, so no bracket ever claims
  a conjunction that never held. A member whose ring dies does not go on being one — its QUITTING
  breaks the grip, which is what keeps a posture from outliving a finger.

**Membership — ONE COUNT over three kinds**: sounding fretting-hand onsets, carried rings still
sounding at stated stops, and claims (the RESOLVED claimed stop under a right-hand
onset — `chartClaimedStops` over `claimedStop`). They are three ways of stating the one thing a shape is made of: where a finger is. A LONE
member of any kind opens nothing — convention agrees, a chord is two-plus noteheads. Claims stay
OUTSIDE the overlap test and INSIDE the count, because a claim has no ring. The carried fold-in is
ungated from the strike count, so one claim beside one carried ring opens a two-member span and the
carried ring is a member of it. Right-hand onsets are evidence, never members: the posture is the
fretting hand's, and a tap says nothing about it.

**AN OPEN STRING IS A MEMBER exactly as a fretted one is, at the strike.** The bracket's claims are
PER MEMBER and both kinds are true — a fretted member's digit asserts a held finger, which its own
ring proves, while an open member's `0` asserts no finger at all, only the ring the chart already
stores — so no member's claim can be false, and the fretted-only branch the opening test would
otherwise need is deleted rather than argued.

**A RING NO HAND HOLDS BELONGS ONLY TO THE SPAN IT WAS STRUCK IN.** A HAND-FREE stop is one the
fretting hand presses nothing for (`handFree`, `ChartStop::fret == 0`): the open string, and the
node a natural or open-string tap harmonic touches, the finger lifting the instant the chime sounds.
An artificial harmonic presses a fret and is hand-bound. Struck, a hand-free note is a member of the
span standing or founded at its strike, exactly as any other. Once the span it was struck in has
ENDED, its ring is TEXTURE: it founds no accumulation, folds into no posture, survives into no
landing and displaces no finger — until it is RESTRUCK, which is an ordinary statement joining the
span standing then. True is not the same as evidence of a grip, and a bracket is a statement about
the hand.

- **THE WITNESS is the coverage frontier** — the end of the last emitted span (`covered`) — and it
  is exact rather than a proxy: a hand-free ring still sounding when any span founds is folded into
  it by the very loop that asks the question, and one struck while a span stands is a statement that
  grows it, so "struck before the last emitted span ended" IS "was a member of an earlier span".
  Strict, because a ring struck AT the frontier belongs to the figure arriving there. This is what
  separates the law from a literal "never carried" reading, which would strip the first open string
  off every open-position arpeggio: E0 then A2 then D2 still founds at the D2 on its own carried
  rings, dated at the E0, because nothing had closed since the E0 was struck. A per-string "the span
  this ring was struck in" record was rejected because it cannot express founding — the span a fresh
  open helps found does not exist yet when the open is struck; the frontier can.
- **NOT at the displacement witness.** Law A reads the SOUND: a fret or node struck on a string
  still ringing open changes what that string sounds and breaks the span, whether or not a finger
  held it — otherwise the bracket would print the new stop from a front before which the string
  audibly rang open. A hand-free ring is no member, but its sound is still evidence.
- **TEXTURE IN THE BRACKET.** The two halves of a posture are the GRIP — what the walk reasons
  about, every rule reading `OpenSpan::stops` and nothing else — and the TEXTURE under it
  (`OpenSpan::texture`, published as `ChartPosture::texture`): the hand-free rings sounding through
  a span's open that belong to an earlier span, recorded at exactly the two sites that skip them
  (the slot open's fold-in and the landing's survivors). Disjoint from the grip by construction — a
  claim's stop and a grip's both outrank it, and a fret struck on a texture string GROWS the grip —
  so every display unions the two blindly while every rule and the census read the grip alone.
  Published apart rather than merged, because merged it was read two ways.
- **TEXTURE IS THE OPEN STRING ALONE** (`textureStop`). An open string's 0 is true for as long as it
  rings, because no hand was ever on it; a harmonic's node was true at the strike and false a moment
  later, so printing it in a later bracket would claim a finger the hand has long since moved. A
  harmonic's stale ring is a plain tail: no digit, no class, founding nothing — and still breaking a
  span if struck over.

**THE CARRIER SOUNDS AT THE CLAIM'S SLOT.** A claim rides a right-hand onset on its own string, and
that onset is a note that sounds there: a TAPPED HARMONIC sounds FROM its claimed stop — the
overtone divides the pressed length, so the claim is the pitch — while a plain tap sounds where its
finger lands over a planted stop the string falls back to on lift, and a scrape dragged over a
planted finger is the same picture. A zero-sound span is
therefore complete where it is stated — it publishes at its instant with zero sustain — and a
sounding arrival at a claimed stop GROWS it in place, one statement with no furniture overlap. No
justification test stands between the two (LAW II).

**THE FRONT — a span dates from WHEN EACH MEMBER'S STATEMENT BEGAN**, never from the onset of the
note the member happens to ride, and never across ground a preceding span covered. One floor: the
coverage frontier, raised by the end of the last FOREIGN sound on every stated string. The earliest
member statement at or after that floor dates the span; members behind it state their stops and date
nothing.

- **The tie doctrine.** A same-stop restrike whose predecessor's ring reaches it — read
  END-INCLUSIVELY, the window the contradiction witness already uses — is ONE statement said twice,
  so it inherits the beginning rather than starting its own. **TRANSITIVE**: what it inherits may
  itself have been inherited, because one statement has one beginning however many times it is said.
  A single-lookback reading was rejected as an arbitrary rider on the contract's own words. The tie
  is tested POSITIVELY — the string's channel must state EXACTLY the struck stop at that instant —
  and never as a negated displacement, so a mid-glide finger inherits nothing; a slide-out
  predecessor is exempt exactly as LAW I's junction exemption has it, since a ring whose finger is
  leaving the board asserts no grip to inherit. Identity is judged on GRIP STATEMENTS, so a source
  planting the still-held stop states that stop and its ornament rides above.
- **The landing.** A glide's arrival establishes a new stop, so a slid finger's statement begins AT
  THE LANDING and not at the note it rides — the mirror case, and the reason the column cannot be a
  stored onset in either direction. It needs no record: the fret channel already knows where a
  stop's statement began, and the same re-ask caps that string's coverage there (`coverage_at`,
  asked at the three moments a statement restarts — a strike, a landing, and a carry folding into a
  new span).
- **Carried rings never backdate**, but they are members like any other and they BOUND the span: the
  coverage frontier survives ONLY as the dating floor and is never an input to the reach.

**EXTENT — THE BREAK LAW.** A span RUNS until its grip BREAKS, and only two things break it: a
MEMBER QUITS, or a STATEMENT CONTRADICTS the grip.

- **A member quits** where its sound goes out with nothing renewing that string at that instant.
  Continuous therefore means ringing through, or ending exactly at the next onset that SOUNDS that
  string — the strike-into-strike shape a stored chug chain has, which is a REPLACEMENT and no quit
  at all, and needs no clause of its own. The first genuine stored gap on any sounding member ends
  the span at that ring's end, because a ring that simply stops with nothing sounding after it is
  the chart stating DETACHMENT of sound rather than mere silence; the reader's inference about the
  finger is their own. Survivors ring out as tails and open nothing.
- **RENEWING a string and BOUNDING the span are different acts, and only the fretting hand does the
  second.** A sounding onset of either hand renews that string's evidence and keeps the tenure
  running across it — a tap on a member string ends that member's ring underneath with no hand
  lifting anywhere, so the sound was REPLACED, not silenced — while how far the span REACHES is
  written only by a MEMBER's own strike. A reach a tap wrote would let a tapped sixteenth decide a
  chord's extent. One number can only ever be right about one of those, which is why the hand keeps
  two columns.
- **A span's reach is the MINIMUM of its sounded members' coverage** (`span_reach`), landing-capped.
  Claims never bound — a claim has no ring — so a span whose members are all claims reaches its own
  start, which is the honest zero. Min-extent is this law's box case rather than a rule beside it:
  two strings of one strum with unequal rings end their box together at the shorter.
- **The CONTRADICTION arm is GRADED, because EVIDENCE OUTRANKS ASSERTION.** The hand's SOUNDING stop
  and the span's SOUNDED stops always witness — sound is evidence — and a same-stop restatement is
  the tie doctrine and witnesses nothing. A differing CLAIM against a carried claim always witnesses
  too: assertion against assertion is the charter re-authoring the hand. But a differing STRIKE
  against a carried claim witnesses only where the grip is ESTABLISHED (the span has sounded
  members) and the string is silent; against a still-ASSEMBLING silent statement a strike is
  evidence arriving rather than a contradiction, so it joins the assembly in place.
- **The close** is the breaking event's onset or the instant the statement ran out, whichever is
  earlier. The closing HEAD belongs to the close only where the event actually cut a live statement
  — a statement that had already run out was reach-closed and owes no distance to a head that
  arrived after it ended — and a slot of held fingers or bare taps publishes no head at all.
- **THE INVARIANT: every span with a SOUNDING member is strictly positive**, by arithmetic rather
  than by a case, since the close is the earlier of two instants both at or after the start.

**GROWTH IS ACCUMULATION.** A stop the grip lacks — struck or CLAIMED — grows the span IN PLACE; the
posture set gains a string and the figure stays ONE span, whatever opened it. No disjoint-grip guard
is needed and none exists: absorption can only ever union grips whose sounds genuinely overlap,
because a dead ring fires the quit arm first. Nothing ever removes a stop — a stop's silence is what
BREAKS the grip, never what shrinks it. **Founding modes are gone**: they existed only to decide
what an arriving new stop did, and growth answers that once for every span.

**A CHANGE IN ARTICULATION DOES NOT SPLIT THE SPAN.** The span is a FRETTING-HAND statement —
palm-mute is the picking hand, dead is pressure, accent and ghost are dynamics; none of them move
the grip — so a chord, its dead chugs, and the chord again are ONE hand fact and derive as ONE span.
Continuation and merging compare POSITION (strings + stops) only, while articulation varies freely
within a span as per-onset display data. A lone RE-PICK needs no exception of its own: a same-grip
restatement CONTINUES the span at any width, and the rule is ADJACENCY-SCOPED, so a re-pick after a
genuine gap meets a grip the quit arm has already broken and opens fresh.

**THE STATEMENT-CHARACTER SPLITS.** A span's statements keep ONE character — whole or in parts — and
the walk splits where that character turns, so the class is a fact of the span's founding rather
than a retroactive verdict on everything it contained.

- **Parts → chord (the unison restatement).** A stroke striking EVERY stop the span states is the
  whole grip said in unison. It closes a sounds-in-parts span, and it closes ANY span when it also
  strikes a string never stated — a strict superset states the whole chord AND MORE, a new statement
  and never growth.
- **Chord → parts.** A stroke sounding PART of what a never-in-parts span STATED — some of its own
  stops, not all — is the statement coming apart, so the chord span closes and the partial founds
  the parts span through the ordinary slot open, taking the still-ringing members in as carried
  texture. A stroke on strings the span never stated is not this direction at all: it is the
  statement still ASSEMBLING, which is growth.
- **THE PARTIAL SLIDE.** A slot whose statement is divided by its own notated rings — a held
  member's ring ending STRICTLY BEFORE a co-struck glide arrives — necessarily sounds in parts from
  that slot, so it splits and the span it founds is born in parts. A voicing-shift slide whose held
  strings ring the whole transit stays one statement, and a whole-grip travel is the ruled chord
  slide with no holder to divide it.
- **THE ABSORPTION RULE — box and boundary are two laws and they part company.** A whole-grip stroke
  is a span BOUNDARY, and a box-class statement, **only where it STANDS ALONE**. Where same-hold
  material sounds IN PARTS within the stroke's own ring extent the stroke is ABSORBED: the standing
  span flows through it, its members fold in as ordinary same-stop restatements, the class turns in
  place, and it wears its box INSIDE the span. The evidence is never the ring-divergence of the
  stroke's OWN members; it is what FOLLOWS within the rings, so the judgment is pending at the
  stroke and the next slot settles it — the house shape for a verdict the current instant cannot
  answer. It CANCELS where the next slot SOUNDS a PROPER SUBSET of the stroke's stops, at those
  same stops — a fretting-hand strike sounding the stop it presses, or a right-hand onset sounding
  the stop it holds under it (its claim), read exactly as the slot open reads a tap (sighted
  2026-09-10 on Periphery, "It's Only Smiles" measure 82: a chord followed by tap-and-pull-off
  runs on its own strings is one arpeggio from the strum, not a box the first pull-off breaks) —
  and some member the subset does not restate is still sounding ITS OWN STOP strictly past that
  slot; that last clause is the hold underneath, and it is what makes the parts sound UNDER the
  stroke rather than after it. A MID-TRAVEL channel holds nothing, so a chord slide with
  transit picks is never absorbed. It COMMITS on anything else — another unison, a contradiction,
  growth, or a stroke whose rings all end before the parts.
- **Mid-travel soundings are judged PER MEMBER, never per slot.** A sounding at an in-force member
  stop rides; only a contradiction or a NEW stop truncates. An open channel never departs, so open
  members restruck mid-slide are an interior subset sounding that chains through: ONE span covering
  slide and restrikes alike, splitting only at the landing.
- One predicate and one arithmetic answer "is this stroke a chord statement?" for all three of its
  askers — the break arm, the founding class, and the dispose arm's in-place class turn — which is
  why the rule removes a divergence rather than adding a branch. A stroke says a STOP, not a string:
  a strike at a DIFFERENT fret is not a restatement of this one.
- The BOX LAW is display and UNCONDITIONAL, and nothing here touches it.

**TRAVEL — the split is AT THE LANDING, and the landed grip re-opens there.** A note's fret channel
states where its finger is along the ring, so it bounds that member the same way the ring does, and
the bound is the ARRIVAL. A chord slide keeps the fingers planted and the rings run continuously, so
fingers travelling together CARRY the statement: the span states the departing grip, COVERS the
glide, and the break lands where the new grip ESTABLISHES.

- **THE CHANNEL HAS ONE READER** (`statedStopFrom`), asked "what stop does this note state at this
  offset". Every question about a finger's whereabouts is that one question at a different moment: a
  strike at the onset, a member's reach wherever the shape's start falls inside the ring, a landing
  at the arrival, and the ring-through fold-in at the slot the ring crosses. Between a departure and
  its landing the answer is NOTHING, and it is that silence — not a second stored bound — that keeps
  a mid-glide member from being restated or folded into any other posture. A fret the channel LEAVES
  AGAIN is a point on the path and never a landing ("equal frets are a hold, different frets are
  travel"), which keeps a continuous multi-fret glide one travel while a glide with a held grip
  between its legs states each grip exactly once.
- **The landing open is its OWN one-line law.** Where a travel lands with the grip held, at least
  one finger arriving and TWO or more members ringing STRICTLY past the landing, a span opens there.
  "Held through the slide" is END-INCLUSIVE at the landing instant — a ring dying exactly AT the
  landing belonged to the predecessor, which is the seam ownership. The two spans TILE, with no gap
  and no zero-length span between them, and the transit underneath draws as the members' sliding
  tails: the published chord-slide picture, two fret stacks joined by parallel lines. A fretted ring
  the closing span never HELD is not its survivor; a hand-free survivor is not a finger that slid,
  so it counts toward no survivor threshold and a one-string slide over a struck open drone lands
  into no bracket. Travels of unequal distance landing together — voice-leading slides — are
  included, because nothing here asks how far a finger moved. A staggered landing whose every other
  surviving member is itself mid-glide opens nothing, and the truth stays in the sliding tails.
- **THE LANDING CLASSIFIES EVERY STRING THE HAND TABLE KNOWS, ONCE** — grip survivor, texture, or
  nothing — exactly as the slot open classifies its carried rings, so the two seams cannot disagree
  about what a string is. An open string sounding strictly past the boundary is texture under the
  successor WHICHEVER span struck it; a drone that died before the boundary is left out on both
  paths by the same sounding test. The smallest bracket this can produce is two grip digits over a
  ringing 0 — what the slot path already prints for a two-finger chord over a drone.
- **The landing restarts each survivor's coverage at the landed grip**, re-read from the channel, so
  a multi-leg glide's NEXT departure still caps it.
- **Whether the new grip gets a moment of its own is a MUSICAL test, not a drawable-room one.** A
  landed span is EMITTED if an event ever stated it, or if its TENURE STRICTLY EXCEEDS the
  notated-distinguishability quantum read at the CLOSING onset's own measure. The importer
  synthesizes every glide-into-restrike arrival exactly one quantum before the replacing onset, so
  the equality case IS the suppressed population and strict is the whole ruling; the strike's own
  full box states the new chord instead. A held-but-never-restruck landed span IS emitted.

**THE NODE GRIP — a grip statement is a PLACE, not a fret number.** A grip statement is a place on
the fret axis: a fret pressed, the open string, or a node touched (`ChartStop`, the `(fret, node)`
pair the chart already spells, built only by `frettedStop` and `nodeStop`). A fretting-hand harmonic
states its NODE (`frettingStopAt`), which differs from the same-numbered fret AND from the open
string, because the finger is on the string pressing nothing. Two harmonics at one node restate one
statement; a co-struck node chord founds a span like any co-struck grip; a node landing on a string
the grip neither states nor holds GROWS the span as any new stop does. NO HARMONIC CLAUSE EXISTS IN
THE WALK: the split falls out of the ordinary contradiction law reading a stop that can no longer
say a node is fret 0. Claims and plants stay pressed frets — a tap's held stop is pressed, and a
pull-off never lands on a node, since the resolver refuses a fret-hand harmonic on either end. The
picking hand's nodes are not grips: a two-hand tap harmonic states the stop it claims, which is the
`fret` it presses under the touched node, an artificial harmonic the same, a pinch its fret. A
harmonic standing over a PRESSED stop — tapped or artificial alike
(`harmonicOverPressedStop`) — PRINTS that stop beside its head, standing and read-only, since the
head itself prints the node (RULED 2026-09-18). **It also STATES that pressed stop as its grip**
(RULED 2026-09-18): where a pull-off derives a finger planted beneath such a harmonic, the span's
grip statement is the pressed fret and never the plant — the node is measured from that fret, so
that is the grip the figure needs, and satellite and bracket then say one number. The plant stays
true in the wide table because it is real: it is the hand window's to reach, not the bracket's to
print. Consequences that follow: a lone natural harmonic
mid-span closes the span and, unless two more stops or three carried rings are present, opens
nothing; and stop-identical postures deduplicate, so a node grip and a fret grip printing the same
number are two postures because they are two grips.

**THE HOLD-UNDER LAW.** You cannot pull off onto a fret unless a finger is already waiting on it. So
a note that is pulled off FROM holds two stops: the one it sounds, and — planted beneath it for the
whole of its ring — the one the pull-off lands on, WHICHEVER hand made the onset. The chart writes
that second stop nowhere: the notation already states it, in the pull-off itself
(`chartPlantedStops`). Where a figure holds a grip and a finger is added above it, the added note
sounds a fret foreign to the grip and yet lifts nothing — the gripped stop was down before it, under
it, and after it — so the bracket does not seam, neither when the finger arrives above the grip nor
when the release returns to it. **The bridge asks what the source STATES** (`gripStatement`), the
one authority the grip column asks, because a bridge is the claim that two stops are one statement
of one hand: so a pull-off from a harmonic over a pressed stop never rides beneath it — that
harmonic states its pressed fret, so the landing is a NEW statement and the span closes at the
release with the pressed fret in its bracket.

- **Every fret derives alike, ZERO included**: what the pull-off states beneath its source is the
  STOP the string falls to when the finger lifts, and for a pull onto the open string that stop is
  the open string — always waiting, no finger needed. Only a destination the chart never defines
  derives nothing, and the TRAVELED RANGE refuses a plant through the very predicate that refuses an
  authored held stop (`travelsThroughFret`).
- **A strike that PLANTS a stop states THAT stop as its grip**, the fret it sounds being the
  ornament riding above it; every other strike states the fret it sounds. So a source striking over
  a grip that never held its plant states a DIFFERENT grip — an ordinary contradiction, the span
  breaking at the planting strike and the successor's bracket wearing the plant — while a source
  over its own gripped stop is a plain restatement. Every identity question in the walk reads grip
  statements: the tie doctrine, the absorption's parts test, the whole-grip test and the touched
  count.
- **THE NARROW FORM (the law's own bound).** The planted stop is a seam VERDICT: it is never written
  into the grip column by anything but the statements above, never extends a span's reach past its
  sounding evidence, and never reaches the held FIELD's scope. The claim column and the writer's
  residue sweep take the tap-scoped narrowing (`chartDerivedStops`), which is bit-identical to its
  pre-law output by construction. ONE LEVEL ONLY: a chain (9p7p5) plants one stop per source. A
  source that FOUNDS its own span still states its sounding fret into the posture — the founding
  asymmetry, pinned by the machine's shape.

**CLASS — a span is an ARPEGGIO when its members sound separately, and a BOX span while every
sounding of it is the shape whole.** The law classifies the SPAN and nothing else; which box an
individual onset wears is the display law. Four triggers, all of them that one question:

(a) a posture string still ringing at the span start with no onset there; (b) a CLAIMED member
— the bracket is the only mark with anywhere to print a fret nothing struck; (c) any SOUNDING of the
span that is only PART of the shape, which is (a) asked at any interior slot; and (d) a tapped note
sounding anywhere within the span. (a) and (c) are ONE comparison recorded BY the walk, because
answering it means knowing which slots the statement covers; (d) is the one trigger the projection
still derives, because it asks about the span's EXTENT.

- **The whole class law reads the STORED ring.** Where the fingers are, and which of them the pick
  reached, is a fact about the HANDS; E25 stays a DISPLAY rule about what a surface draws of a ring
  nobody hears.
- **A LANDING IS NOT A SOUNDING**, so it fires no trigger. A successor is classified by whatever
  (b), (c) and (d) find INSIDE it, and a full restrike of its grip trips none of them — a chord
  sliding into chords is therefore BOX class at both ends, joined by its members' sliding tails.
- **An ACCUMULATION is an arpeggio by construction**, needing no clause: its opening slot strikes
  fewer strings than the shape sounds by definition, so trigger (a) answers it at the opening. So is
  every span a lone re-pick continues, a lone re-pick being an interior subset sounding.
- **The denominator is well defined** because within a span the posture is a PER-SPAN SET that only
  ever GROWS, and growing is not leaving; the quit arm is what ends the span the moment a stop would
  have to leave.
- **TEXTURE CLASSIFIES, but ONLY WHERE ITS BRACKET DRAWS.** A shape with hand-free rings sounding
  under it has members sounding separately from them by definition, so the bracket that prints the
  texture draws and the span is published in parts. But the classification exists so that bracket
  prints the texture: a span with no `bracket_position` — a landing successor nothing has sounded
  inside, whose mark is deferred because nothing is stated at a boundary — has nowhere to print it,
  and classing it in parts would colour the rails arpeggio over a figure that reads as a chord.
  Which is also the landing law's own class rule, applied to texture: a successor is classified by
  what sounds INSIDE it. Until a first interior sounding it is a chord span with the drone carried
  in its posture and printed nowhere; at that sounding the bracket draws, prints the 0, and the
  rails turn arpeggio in one act. The walk's own in-parts flag never sees texture, so a chug over a
  drone stays ONE span — one bracket with its boxes inside — and nothing structural moves.
- **Raw pairwise overlap outside spans never creates or classifies a span**: that is let-ring,
  texture and not statement. The strong mutual accumulation is not this — it is the opening law.
- **Vocabulary note.** "Arpeggio" here is the project's own THIRD sense — narrower than theory's
  broken chord, different from engraving's rolled-chord wavy line, whose bracket partner means *not*
  rolled. The marks do not collide (ours are horizontal spans, not pre-chord signs), and this
  document is where the project sense is defined, once.

**THE POSTURE TRUTH CRITERION, and the break laws are what enforce it: no span claims a stop the
hand abandoned while it ran.** A posture only ever grows, so the one way it could come to lie is by
outliving a member, and the quit arm forbids exactly that. Every fret a bracket prints was held for
every instant that bracket covers, so a long accumulation bracket is true BY CONSTRUCTION rather
than by measurement — which is why no length ceiling is needed on one, and why the long
Travis-picked and washed figures are honest at any scale.

**FHP AND SPANS NEVER MERGE.** The fret-hand position stream is the POSITION story and the span is
the GRIP story; they agree in kind and stay two derivations, with every span's fretted stops
expected inside the covering FHP window.

## LAW IV — INK HAS ONE OWNER. Every displayed fact draws exactly once, owned by the most specific furniture that states it.

**BRACKETS state MEMBERSHIP at statement boundaries.** A bracket is ARPEGGIO furniture: a box-class
span states itself with its strums' own boxes and publishes no bracket at all. Where a span's one
opening mark draws is published by the walk (`ChartShape::bracket_position`) rather than re-scanned
downstream, with one write rule: every span an EVENT states carries its own FRONT — or the founding
slot where the tie doctrine dated that front to a landing, since nothing sounds at a landing and a
mark there would frame the chord a quantum ahead of its own heads — and a landing-opened successor
carries its first interior SOUNDING, or nothing at all where it never sounds interiorly. The
projection resolves it into `ShapeViewState::bracket_seconds`: where both surfaces draw the mark,
where the posture digits are decided, and what a claim's own face rides.

**THE DIGIT WINDOW is the bracket's own instant and nothing besides.** The opening bracket is the
span's CHORD FRAME — it states the whole membership at the moment the reader meets it — so a member
that ACCUMULATES IN LATER prints its digit there and its own head restates it on arrival, and a head
LATER in the span suppresses nothing. Three answers to one question about the one head that instant
can carry: NOTHING heads the string, so the bracket's centre is the whole of what states the stop; a
head SOUNDING AT THIS PLACE states it already, so the bracket prints nothing beside it; a head
sounding at ANOTHER place, whichever hand made it, owns the string's centre, so the grip it does not
state takes the SATELLITE column beside the bracket — the centre carries what SOUNDS and the
satellite what the fretting hand HOLDS. THE PLACE IS PART OF THE TEST on every arm, compared as a
stop and never as a printed number: a node head over a node grip suppresses because both are that
node, and a fretted head printing the same digit over a node grip does not.

**WHO PRINTS A DISPLACED POSTURE DIGIT is the hand's question.** A RIGHT-hand head shows nothing
about the left hand, so under a tap the bracket prints the CLAIMED stop itself in the satellite
column — the planted `held` under a plain tap, the pressed `fret` under a tapped harmonic sounded
over one — standing whatever its authorship, and the note's face defers to it
(`StopMarkFace::Posture`) — the
bracket's number is the one statement that the left hand is on that string at all, and a bracket's
fret number is important information. A FRETTING-hand head already states the hand's presence with
its own number, so the stop a pull-off PLANTS beneath it is the refinement the notation already
prints in the pull-off: the NOTE wears it as its own reveal-only satellite and the bracket prints
nothing on that string — one ink states it. A fretting-hand head whose own number is a NODE — an
artificial harmonic, pressing a fret its head does not print — HAS a face: the pressed fret itself,
on the note's own satellite, standing and read-only, so the bracket prints nothing on that string
either — the posture states that same pressed fret there (THE NODE GRIP), so the two agree and the
digit falls away, while the plant a pull-off leaves beneath it reaches the hand window alone.

**THE SATELLITE REVEAL.** A satellite is the note's claimed FACE, note-scoped, at the note's own
slot, and its visibility is keyed to AUTHORSHIP: a stop THE CHART ITSELF STATES stands everywhere —
an authored `held`, and the pressed `fret` of a harmonic standing over it, tapped or artificial
alike, which is pitch-critical and stands even where a pull-off plants another stop beneath it —
the plant reaching the hand window, never the bracket, so nothing contradicts it there; a
tap FRONTING a bracket stands regardless of authorship, because the bracket owes the statement there; anything the
chart did not state — a pull-off derivation, a plant, the default fact — is REVEALED on the note's
truth channel, shown exactly while the note's full ring is, through the existing
selection-and-reveal pick, and read-only. Revealing a note shows the whole truth about it at once.
Lone span-less claims follow the same two rules. The bracket's membership DIGIT is a separate fact
and INDEPENDENT: an authored mid-span held has BOTH its bracket digit (grip membership) and its
standing satellite (the note's own face) — two facts, two inks. Print and click are ONE decision:
the mark is published from the very record that decides the digit prints, so a drawn digit is
clickable by construction and an undrawn one is not.

**THE HELD PRECEDENCE, complete**: THE PRESSED STOP of a harmonic standing over one (standing face,
READ-ONLY) > AUTHORED (standing face, typeable) > PULL-OFF-DERIVED (revealed face, typing REFUSED —
it retypes via the pull-off target) > THE DEFAULT FACT (revealed face, typing AUTHORS a real held
stop). The pressed stop leads because it is what the note's own pitch is measured from, so it
outranks a plant beneath it as much as a default beneath it — and the SPAN agrees rather than
disagreeing, its grip statement under such a harmonic being that same pressed stop (THE NODE GRIP),
so the two inks can never state two numbers in one column; it is read-only because the stop is the
note's own `fret`, the Held channel refusing to state one on a note carrying a node and the fret
itself being retyped through the head. Tapped and artificial harmonics reach that tier alike
(`harmonicOverPressedStop`, RULED 2026-09-18). A harmonic over the OPEN string reaches NO tier: it
presses no stop, so it holds none, wears no satellite, and takes the same tiers an artificial one
takes — the pressed stop, then the plant — under which it simply answers nothing
(RULED 2026-09-18).

**THE DEFAULT HELD FACT.** An onset the PICKING HAND STOPS THE STRING FOR whose held stop is
UNDEFINED still HAS one, because a tap says nothing about the other hand and the other hand is
holding whatever it is holding. A tapped harmonic never reaches this tier — over a pressed stop its
claim is the `fret` it is pressed at, which is defined, and over the open string it states nothing
and is asked nothing. It is a
FACT of the tap, not presentation decoration, which is why it resolves in core and every surface
copies it. Inside a span the release lands on WHATEVER STOP THE COVERING SPAN'S POSTURE HOLDS on the
tap's own string (the pressed fret, which a node grip states as 0 by construction, since a node
presses nothing); span-less, or where the posture states nothing on that string, it is 0 — the open
string, nothing held. It is LIVE-DERIVED: an edit that reflows the spans re-derives it, which falls
out of per-revision recomputation because there is no stored value to go stale. **THE LAYERING is
half the ruling**: the default READS the derived posture, so it computes AFTER `deriveChartShapes`,
as its own table (`chartHeldStops` → `ChartResolutions::held_stops`). It must not enter the claim
column or anything the span derivation reads — claims feed the span-opening count, so a claim-tier
default would be circular and would move spans corpus-wide. A default can never take the Posture
face, by construction: that face is owed by the span a note's CLAIM joined, and a tap that states
nothing joins none.

**SATELLITES ARE NOTE-SCOPED, ALWAYS.** A press on one addresses that note's held stop whatever the
selection was. What a
SELECTION adds is a handle alone: a selected note's satellite is hit-tested as PART of that
selection, so pressing it moves the caret onto that note's held stop and leaves a wider selection
standing — naming a stop inside a selection must not be what takes the selection away. Bracket
column digits are not hit targets at all: a digit an accumulating member prints in the opening
bracket is READ-ONLY notation, reached through that member's own head.

**SPAN-WIDE FRET EDITING IS DEFERRED TO THE TEMPLATE EDITOR**, and it is the reason satellite scope
collapsed to note scope: typing a number over a bracket ALREADY means INSERT A NOTE at the caret, so
a bracket-digit write-through would have to steal that keystroke. Re-queued for the template editor,
where a span's grip is edited as a grip and nothing competes for the digits — also the natural home
for editing tap-held values in bulk (`docs/plans/todo/span-marker-redesign.md`).

**A BOX MARKS SIMULTANEITY, and a REPEAT box marks the identical onset before it.** Any
two-or-more-string strike wears a box, inside a span and outside one alike, and it is THE STANDARD
CHORD BOX every time — a partial restrike inside an arpeggio span included. A box scoped to just the
strings that restrike was rejected: it would look ugly and would restate context the figure already
carries, the span's own borders and the brackets on the fretboard being what say this is an
arpeggio. A single note wears no box. Whether a box is FULL or the headless REPEAT is one
comparison: **the onset immediately before it, within the same span, with no onset of any kind
between, striking the same strings at the same SOUNDING PLACES** (`ChartStop`, where each head
sounds — a node grip and an open string are two places however the fret column reads, and a fretted
5 damped at node 17 is not a plain 5 — because the box stands in for the heads it suppresses). Every
question in that comparison is asked of the FRETTING HAND's members alone. The PROFILE is free, so a
plain chord's first dead chug is an X'd repeat box wearing its own mark; what a repeat box may not
do is drop information it cannot draw, so it renders only the mute profiles it wears a mark for —
plain, palm-muted, dead, or both, each composed with its emphasis — and any other profile, or any
presented tail, falls back to the full box that keeps its heads. Only the board draws boxes; the 2D
lane says the same thing with the span's rails and its name.

**THE COINCIDENCE RULE.** Where a chord box and an arpeggio box would coincide, the chord box is
SUPPRESSED and the arpeggio box shows — a bracket draws its own box with additional information, and
two overlapping translucent boxes hurt readability. It keys on the instant the bracket actually
draws, not on the span's start.

**RULE 12A — A DRAWN span keeps the minimum sustain distance, like every other element, and only a
drawn one.** What the derivation stores is THE MUSICAL CLOSE (`ChartShape::sustain`): the instant
the statement ended, which is what spans are measured against and what seams must abut at. A margin
inside that number put a margin inside every seam. The trim is applied ONCE, in `chart_projection`
(`drawnShapeExtent`), from three published facts, each answering a case the others cannot: the
CLOSING HEAD (`ChartShape::closing_onset`), which is not the close — a span whose rings died a full
margin early ends where they died and is not pulled back from a head it never reached, and a close
that sounds nothing publishes no head, which is what keeps a landing successor tiled onto its
predecessor; the LAST STATEMENT (`ChartShape::stated_extent`), which floors the trim, because
furniture may not retreat behind the strum it is drawn over; and PROTECTED ADJACENCY, where even
that leaves nothing, falling back to the musical close itself. Because the margin is display and not
truth the view state publishes BOTH instants (`drawn_end_seconds` and `close_seconds`) and the
editor's 2D lane reveals the second while the reveal modifier is held or the selection holds a note
the span covers (`core::chartSpanRevealed`, beside the note's own `core::chartNoteRevealed`),
snapping back on release. Where no margin was owed the two coincide and the reveal moves nothing.
The board reveals nothing and reads the drawn extent.

**THE TAIL LAW — SPAN FURNITURE MAY HIDE A TAIL, NEVER SHORTEN ONE. THE CURTAIN IS UNIVERSAL.**
EVERY fretting-hand tail RESTS, from its own last always-visible landmark, unless it is still
stating at its end — over open board exactly as under a bracket. It runs LAST inside
`presentedChartNotes`, is VERDICT-ONLY (it reads the STORED rings, judges, and MARKS where each tail
rests, inventing and erasing no length), and it computes nothing: no length, no endpoint, no
threshold, no constant of its own, and no span CLASS. That is what ends the argument a
neighbour-dependent length kept having — once one ribbon's length is a function of a neighbour's
position, every question about which neighbours count becomes a new ruling — and it is what makes
span authorship reversible, since deleting a span restores every ribbon at its exact original
length.

- **THE ATOM IS THE MEMBER.** Each member is judged on its own: a plain member rests, a member still
  stating at its end draws. So a chord CAN show a full ribbon on one string and a curtained one
  beside it, and that is the ruled picture — the ribbon carrying information is the one that stays.
  Rule 3's per-group atom is untouched; it decides whether a group presents tails at all, before any
  can rest.
- **THE THREE LANDMARK CASES, stated once** (`ChartPresentation::rested_from`, `restedOffsetOf`):
  zero for a plain ring; the last keyframe's end for a statement that FINISHES, the stated
  portion staying always visible; and the ribbon's own end — an empty remainder — for a HANDOVER.
- **PRESENCE — nothing of its own.** A note still STATING at its ring's end — a bend held out, a
  shake that never stops, tremolo, a slide-out — and not handed over never rests. Zero further
  exceptions, because an exception is a place where exception number two attaches.
- **A HANDOVER FINISHES.** A note whose string a later strike takes over
  (`ChartConnections::hands_over`) is a TRANSFER of the sound, which the span has no vocabulary for
  either — but the transfer COMPLETES at the takeover, so the whole ribbon is stated portion and the
  note keeps every pixel of it. Asked FIRST, deliberately: the takeover terminates whatever the ring
  was still stating, so a shake or a bend into a pull-off finishes there too and the handover
  outranks the never-rests disjunction. The relation is read off the SUCCESSOR's stored claim, never
  the resolved direction — an equal-fret tie resolves `Unjustified` and still hands the string over.
  Duration cannot tell a transfer from a close, which is why the relation is stated rather than
  probed.
- **SCOPE, on both sides of the judgment**: right-hand onsets are neither members
  nor witnesses. That fixes a live defect — a tap must not cut the fretting hand's ring underneath
  it — and it is the one place this law moves ink UP.
- **THE EXECUTION FORM.** The presented stream keeps every member's rules-1-to-4 tail; the 2D lane
  draws it always, and the 3D board rests hidden ribbons at distance, drawing each only inside the
  sliding reveal window rising from the hit line (`g_tail_reveal_lead_whole_note`, whose initializer
  is the one statement of its value, fully lit at the line and fading to nothing at the window's
  outer edge). Rule-3 and rule-4 emptiness never enters the resting set. The verdict is PUBLISHED
  rather than inferred (`ChartResolutions::rested_from` → `NoteViewState::rested`), says only WHERE
  a ribbon rests and never how long one is, and is never set in the ACTUAL reveal.
- **WHAT IT COSTS**, and it is the headline visual change: plain sustained chords, quarter-note chug
  chains, dry arpeggios and co-terminating let-ring figures go RIBBONLESS, and so does every lone
  plain note over open board. Rails, repeat boxes and 3D hold-pinning are what state the tenure
  where furniture exists; Alt, the selection and the caret reveal the close.

**THE HOLD IS THE TENURE.** A LIVE fretting-hand member with no drawn tail, covered by a span, is
held to the span's reach — while the grip is held, the board pins what is held. One rule, no
strum-size gate: under grip tenure a covered member's un-renewed death would have BROKEN the span,
so coverage past a member's ring IS the record that the finger never lifted. Dead members (a dead
chug is choked, not held), the other hand's onsets, and members whose tails stand and never rest
state their own hold. Resting is keyed by the VERDICT and not by tail emptiness, since the execution
form restored hidden members' presented tails and keying on the tail again would release the very
pins this fixed. **The floor is keyed on SPAN COVERAGE**, not on the verdict: a COVERED resting
member raises to its own stored ring — which may exceed the span's reach, the honest hold, because
the string genuinely rings there — and then to the span's reach; a LONE resting note is never
reached and holds for the tail it presents. A HANDED-OVER member is excluded whole and pins for
exactly its stored ring, where the next strike takes the string. Coverage is positional only, from
the one authority both span-scoped display rules ask (`SpanCover::reaching`, the furthest-reaching
span already started when the instant arrives, an onset at a seam standing in the grip that
ARRIVED).

**THE KEPT-SUSTAIN BOUND, and it is a DURATION.** A ring earns a drawn tail by running LONGER THAN
the bound in SECONDS: rule 3 reads each note's actual ring through the tempo map, onset time to
ring-end time. Its value is stated ONCE, at `g_minimum_kept_sustain_seconds` in
`chart/grid_arithmetic.h` — every comment, guide entry and rule text names "the kept-sustain bound"
and points there, and describes a fixture's ring by where it sits relative to the bound rather than
by what it is, because the bound is headed for a user-tunable option. TIME, not note value: the
player experiences the highway in time, and a note-value bound draws tails too often in a fast song
and too rarely in a slow one, so the same written value earns a tail below a crossover tempo and
drops it above. The meter never enters — seconds do not care about the denominator — and the tempo
map is piecewise constant between anchors, so the verdict can only change AT an anchor and never
inside a run. A song-scoped note value derived from the dominant tempo is refused with cause: it
would be wrong for a slow intro under a fast body. What did NOT move with it, each on purpose: the
minimum sustain distance is ink spacing rather than an earning threshold and the two answer
different questions; the 2D lane has no tail-length threshold of its own and simply follows;
the board rests the new tails like any other plain tail; and the legato hold test still reads the
STORED ring and asks strict adjacency, so nothing about tails moves a hammer-on or a pull-off.

**Other ink under this law:**

- Outside spans, crossing tails are simply TRUE — truth-first let-ring, denser than the published
  dashed span and more truthful; same-lane overlap is structurally impossible because of the ring
  bound.
- The tap-harmonic tail extends from the TOUCH position; the satellite carries the stop.
- A node grip wears the same bracket the fretted members wear on both surfaces: on the 2D lane the
  bracket prints its label (`chartStopText`, the one label authority), and in 3D the bracket carries
  no text at all — the node states itself by SITTING ON ITS OWN WIRE (`highwayStopX`), with the same
  label authority reaching that board through the floor numbers. Node text, no diamond.
- Pending statements are visibly pending (the ghost head through the honesty gate); the Alt reveal
  is the universal actual-truth escape, and it shows the ACTUAL STORED ring rather than the
  presented tail.
- **THE LANE'S HIT MODEL: HEADS ARE TARGETS; TAILS ARE TESTIMONY.** Clicks move the caret; heads
  select; tails never select, visible and hidden alike. If the clicked slot lies inside ink that is
  not drawn, that ink REVEALS while the caret sits within the ring — a deterministic peek keyed on
  the edit position, no timer and no selection mutation — and the peek reveals a tail absent FOR ANY
  REASON, one condition: the caret sits inside the note's ACTUAL ring beyond its drawn ink. The
  warrant is authoring: techniques can be authored on presentation-hidden tails, so authoring there
  must function exactly as it would on any tail, and the reveal must not care why the ink is absent.
- **DRAWN = SCORED.** The presented tail is the surface and the scorer reads it; there is ONE end
  per note and both surfaces draw to it. Scoring for arpeggio spans is to be revisited definitively
  in the note-detection plan — possibly awarding extra points for HOLDING THE HANDSHAPE, diverging
  somewhat from display because the bracket displays that everything is held — and until that plan
  rules, drawn = scored stands.
- **Surface parity is an obligation this law creates**, not an optional cleanup: the claim's face
  has no signed 3D story, and the game-side box-chain walk must realign to the interior
  classification.

## LAW V — VERBS ACT ON THE SCOPE'S STATEMENT.

One scope authority: the selection, else the armed caret's slot, else inert (`chartVerbSlots`). One
meaning per verb per occupant. Bare
digits retype the record's own fret; prefixed digits state the named channel; one pending-entry
state with two roads in (keyboard stop, satellite click). Keyboard stops mirror displayed
digits in display order. Plans are atomic over their product, and the sweep rides the entry (LAW II
at edit time).

- **Authoring a pull-off off a right-hand onset CLEARS that onset's stored `held` unconditionally**
  — agreeing or contradicting, since an agreeing value is duplication and a contradicting one is a
  lie — inside the SAME undo entry; authoring `held` on an onset that already has a pull-off
  successor is REFUSED rather than silently dropped; the writer never emits residue and the load
  normalizer sweeps it (`sweepDerivedHeldStops`).
- **The retype verb refuses a derived stop and a plant**, read off the one ownership table
  (`ChartResolutions::planted_stops`): a fretting-hand note can never be handed a held FIELD its
  attack forbids, and neither can a note carrying a NODE — a harmonic has no planted finger, its
  stop being the `fret` it speaks from.
- **The Held-channel DELETE has a clearing planner of its own** (`planClearHeldStops`), because a
  default satellite must never be authored as a real `0`. Clearing withdraws the charter's statement
  and nothing else: an authored stop goes, a default clears nothing, and a derived tap stop or a
  plant refuses off the same ownership table the retype reads. Refusals are as silent as every
  refused plan today.
- A typed digit STATES a stop and a transpose SHIFTS one, both reaching a claim through the note
  that carries it exactly as they reach that note's own head.

---

## Expressibility — the musical completeness verdict

A sweep of 41 standard guitar and bass figures — strums, broken chords in every form, two-hand
tapping, all four harmonic families, the full bend/slide/vibrato space, legato, palm-mute, dead,
ghost, slap/pop, scrapes, rakes, drones, position statements, whammy — against store/derive/display
on both surfaces found the model COMPLETE, with several rules judged *more* precise than print: two
independent mute flags where tab conflates the X; ghost-vs-dead disambiguated; the natural
harmonic's node snapping being the physics itself; derived legato direction beating MusicXML's
storable-stale pairs.

Deliberate and honest exclusions: semi-harmonic (counted at the nearest technique), feedback
(counted unsupported), whammy (planned, with the vocabulary firewall held everywhere read), and the
non-string acts — volume swells, fades and golpe — which have no `(position, string)` home and may
be supported later.

**Recorded so nobody "fixes" them.** Barre-under-legato continuity is correctly UNSTATABLE: the span
splits at the run's first new fret, as print practice also restates; FHP carries position
continuity; and the resolver's Pull clause already knows the released fret. No new field. The
rejected simpler notations stay rejected: let-ring as a stored mark (the ring IS the record; a mark
would be a second authority), the ring-through region, stored legato direction, and a third mute
state.

## The dead list, carried forward (do not resurrect silently)

- **F1** a separate hold-marker array.
- **F2** stored hold durations on silent notes.
- **F3** unjustified zero-sound spans — RETIRED 2026-09-17, see the note below.
- **F4** the plant-offset convention.
- **F5** the ring-through region — independently confirmed dead from the physical layer.
- **F6** classification from raw PAIRWISE overlap, the pure ring-discriminator, and the pure
  interior-kill. The strong MUTUAL accumulation is not this: it is LAW III's opening law, and its
  per-member claims are proven or stated.
- **F7** an arpeggio display flag on the chord dictionary.
- **F8** auto-extend on `N`.
- **F9** the template-as-source substrate — independently confirmed dead from the physical layer.
- **F10** the old "singles and chugs don't break the chain" display rule; the game-side walk
  realignment is the obligation that replaces it.

F1, F2 and F8 name the silent-hold design that has itself left the model (RULED 2026-09-17: the `N`
verb and `NoteAttack::None` are gone; `N` is unbound and free for reuse). A claim is read through
one query (`claimedStop`): `held` under a plain tap or a pick slide, the pressed `fret` under a
tapped harmonic. **F3 left with them**: a zero-sound span is now sounded by
its own carriers, so it stands at its instant with nothing to justify, and the entry named a refusal
that has no population left (LAW II).

Also dead, and for stated reasons: the death successor (ring-out opens nothing); founding modes;
extent-inertness (there is one kind of member); the tail law's CROSSING conjunct (the closer's tail
is not special); C3's ink ownership and the bracket law's staircase clip (a ribbon's length must not
be a function of a neighbour's position); the fronted-claims import machinery (imports author zero
claims); and section marks anywhere in the let-ring law (organizational, not a hand fact).

## Open, carried forward

- The claim's face has no signed 3D story, and the game-side box-chain walk must realign to the
  interior classification — LAW IV's surface-parity obligation.
- The converter's default ring for tail-less source notes. The frame is the TWO-LAYER principle:
  STORED sustain chosen for semantic truth (span derivation, chains, adjacency; real source sustains
  as-is, bare short notes short), while the tail-less DISPLAY question is answered by the
  presentation rules that already exist, never by shortening storage. A blanket same-string-bound
  default is refused with cause: it would let-ring-ify sweeps and dense cross-string runs, stating
  false texture. Tap-harmonic canonization at import rides the same re-export window.
- An AUTHORED two-note span, and span-scoped grip editing, both owed by
  `docs/plans/todo/span-marker-redesign.md` — whose founding premise is recorded there rather than
  as law here: **sound founds, claims attach, markers define.** Several rules above have the shape
  they have because claims can found spans today.
- The chord dictionary: names and fingerings keyed by posture, with the constraint that a
  slide-opened template inherits its predecessor's fingering, which makes the lie unrepresentable.
- Scoring for arpeggio spans, definitively, in the note-detection plan.
- The tap-harmonic head's touch-primary emphasis against published tab's stop-primary habit, and the
  span-start bracket digit's polysemy (silent versus carried member, disambiguated only by the
  incoming tail) — both flagged for UI design judgment.
- Open verbs recorded, not judged: the move gesture, the tie verb and the disconnect's unstruck-tie
  default, the keyframe ruling bundle, and rebase.
- Accepted-for-now derivation residues, each with its own trigger, live in
  `docs/tracking/watch-items.md` rather than here — the let-ring clip under a moving same-string
  melody, staggered landings opening nothing, and two-member figure noise among them.
