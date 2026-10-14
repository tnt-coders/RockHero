\page design_rulesets_gp_import Guitar Pro Import Rules

# Scope

These rules decide what the Guitar Pro importer does to a score beyond literal conversion: how it
plays what Guitar Pro leaves to playback, places ornaments in the time they take, resolves the
score's slide flags into ordinary notes, frames frets and harmonic nodes against the capo, reduces
what the chart cannot hold, and decides how long each string rings. They bind the import alone. What
they produce is ordinary notes, so every rule that reads notes afterwards — the tail, hand-position
and shape rules — reads an imported chart exactly as it reads a loaded one.

Code cites a rule as **GP import rule N**, or by its name for the two rules below the numbered list:
**the GP import ring policy** and **the GP import figure law** (also called the let-ring figure
law). The rules are implemented in `rock-hero-editor/core/src/project/gp_chart_builder.cpp`, whose
entry point is `buildGpSong`, and `gp_score_parser.cpp` reads the marks they consume:

- `collectEvents` places a track's timed events. It plays each bar's feel (`swungBeats`, rule 11)
  and splits tremolo beats into strokes (`expandTremoloBeats`, rule 8) before reading any duration,
  places grace runs (rules 5 and 6) and halves staccato rings (rule 13) as each beat's events are
  emitted, spreads rolled beats (`expandRolledBeat`, rule 10), and spells trills out last
  (`expandTrilledEvents`, rule 9).
- `buildChart` merges ties and maps each note onto the chart — the capo frame, dynamics, harmonics,
  reductions and attacks (rules 12 to 17) — then converts pick-slide carriers (rule 7) and resolves
  the slide chains (rules 1 to 3). `resolveSlideIns` turns bare slide-ins into scoops (rule 4), and
  `resolveSlideOutExits` chooses each slide-out's exit fret (rule 1).
- `letRingFigureEnds` is the figure law, and `clampSameStringOverlaps` applies the ring policy's
  same-string clamp (`normalizeSustainOverlaps`) and trims each payload to its ring.

The finished chart then goes through the load normalizer (\ref design_rulesets_techniques), the
same `normalizeChart` every load path runs, and the import log counts its repairs by rule. Nothing
leaves the import invalid, and every guess or loss it makes is counted in its log.

Numbers are stable: a rule is never renumbered, a retired rule keeps its number, and a new rule
takes the next one.

# Vocabulary

- **Voice** — one line of the transcription: a voice slot of its bar, running across bar lines.
  The chart stores no voices; the import reads them only while it builds.
- **Ring** — `ChartNote::sustain`, the duration the string actually rings.
- **Written** — the ring a note's notation states once these rules have spelled it out, a merged
  tie chain's included.
- **Principal** — the next sounding beat in its voice, which a grace run attaches to.
- **Lead** — the time one grace takes before or on its principal: a thirty-second note, or less
  where it shrinks (rule 6).
- **Slide flags** — Guitar Pro's `Slide` bitset: 1 a shift slide, 2 a legato slide, 4 and 8 a
  slide-out down and up, 16 and 32 a slide-in from below and above, 64 and 128 a pick slide down
  and up.
- **Natural stream** — the fret-hand events derived over the built notes before their rings
  settle, slide-ins still plain notes at their notated frets; it chooses the frets rules 1 and 4
  synthesize.
- **Same-string clamp** — no stored ring crosses the next onset on its own string
  (`normalizeSustainOverlaps`); exact adjacency stays legal.
- **Audibility horizon** — one bar past an onset, the bar measured in the meter the onset sits in
  (`audibilityHorizonFrom`): how long Guitar Pro's playback lets a struck note still ring.

# The numbered rules

1. **A shift slide re-picks its landing, and the chart proves it.** The origin states the landing's
   fret at its ring's end, exactly on the landing's onset, and nothing marks the statement as an
   arrival: an end statement naming the stop the next head on its string is struck at, at that
   instant, is a glide into position and a pick (`arrivesIntoNextHead`, five clauses). Guitar Pro
   states no arrival time and none is synthesized; the arrival's instant is the head's own. The
   origin's ring grows to reach the landing — the string sounds while it travels — and the
   same-string clamp ends it there, while its ink stops one margin before the landing (tail rule 1),
   so the arrival stands in the ring's ending zone and draws only under a reveal. The landing keeps
   its own onset and head. The gesture degrades to a slide-out down where the landing is an open
   string or no landing note exists.
   - **A slide-out is the slide-out keyframe**: the keyframe at the ring's end stating the gestured
     fret. A ring ending at or before the last fret its chain states grows to one minimum slide
     window past it. The exit fret is four frets out in the flag's direction, held on the playable
     board. Where the natural stream's next hand event after the slide-out departs — the instant
     its last fret is stated, not the note's onset — moves in that direction and arrives by the
     next onset after that departure, the slide-out is the departure and its exit rides that
     travel, widened to rule 4's two-fret minimum (`resolveSlideOutExits`); a slide-out ringing
     past the next onset, or with no onset after it, keeps the four frets. An exit that would name the stop the next head on
     its string is struck at, and so read as an arrival, moves one fret further in its direction, or
     back at the board's edge (`slideOutExitClearOfNextHead`).
2. **A legato slide is the same note continuing.** The landing is not re-picked, so it never becomes
   a note: it folds into the origin as a pitched keyframe at the junction. The ring extends to the
   landing's ring end, the landing's vibrato, tremolo and bend fold in, and its own onward slide
   continues the chain until a shift slide, a slide-out or the chain's end stops it. The junction
   draws as the linked continuation head that `.rock` chains use. The notated slide kind is never
   second-guessed, chord landings included: a source charted with the wrong kind is fixed in Guitar
   Pro.
3. **A slide notated on a tied continuation belongs to the merged note, and leaves from the
   junction.** Tie merging folds the continuation's slide flags into the origin, so a held note that
   slides away at its end keeps its glide. A hold keyframe at the continuation's own onset pins the
   pitch until then, so the glide starts where the sliding segment was notated rather than at the
   merged onset. The tab draws the hold as the linked continuation head, and the glide's diagonal
   leaves from there.
4. **A bare slide-in imports as an on-beat scoop, an ordinary slide in the note's own slot.** The
   ornament is the manner of the note's attack, not a moved head: Guitar Pro's manual defines it as
   attacking the note from an adjacent fret, faithful open-source players pluck on the notated tick
   and resolve to the target pitch a quarter of the duration in, and notation practice treats such
   approaches as zero-duration articulations of their note. The head keeps its notated position at
   a derived approach fret, and a pitched keyframe rises to the notated fret over the scoop window:
   the minimum slide window measured from the onset (`minimumSlideWindow`), halved to stay strictly
   before the note's first chain keyframe or its slide-out where it would reach either. Bend curves
   order only against the ring, so the scoop leaves them untouched. The scoop is synthesis, so it
   may lengthen a short ring to fit.
   - **The approach fret** is two frets out in the flag's direction. Where the natural stream's hand
     moves onto the note in that direction, the approach rides the move instead, widened to the same
     two-fret minimum; a still hand or a contradicting move keeps the default. The approach floors
     at the first playable fret. An open-string landing, or one whose approach clamps onto its own
     fret, stays a plain note, counted.
   - **Anticipation is not a slide-in.** An approach sounding before the beat with the target on it
     is a before-beat grace with a slide, which resolves through the ordinary chain; a bare slide-in
     never fabricates an early onset.
   - The stored scoop is struck at its approach fret and states the notated fret at its arrival, so
     the positions a reader derives cover both (hand-position rule 2), and every rule that reads the
     note reads an ordinary slide.
5. **A grace beat takes no bar time and attaches to the next sounding beat in its voice.** A
   before-beat grace sounds one lead ahead of its principal, crossing the bar line backward where
   the principal is a downbeat, and **steals that lead from the beat before it**: Guitar Pro plays
   the ornament in the preceding note's time, so every note of the voice's previous sounding beat
   still ringing at the run's first onset ends there. A tie origin shortened this way still merges,
   since the merge keys on the tie flag. An on-beat grace sounds on the principal's own position and
   delays the principal's notes on its strings by the same lead, their ends unchanged. Runs of
   graces stack lead by lead. A grace keeps its techniques, so a hammer or a slide into the
   principal resolves through the ordinary paths once the grace is a real note.
6. **Leads shrink rather than collide.** Where a before-beat run's leads would reach the voice's
   previous sounding onset, or an on-beat run's would fill the principal's whole duration, the run
   shrinks to half the available gap, each lead taking an equal share of that half. A grace with no
   room at all — its principal at the song's start — or no principal to attach to — a rest follows,
   or the track ends — is dropped, counted.
7. **Pick-slide flags convert their carrier into a pick-slide note.** Guitar Pro notates a pick
   scrape as a dead note carrying slide flag 64 (down) or 128 (up). The carrier is only the
   encoding, so it becomes a `PickSlide` note in saved form (`savedChartNote`, which sheds its mute)
   with the default path, down 17 to 3 or up the mirror, across its notated span, or one minimum
   slide window where it notates none. The path is the required unpitched terminal at exactly the
   ring's end; turnaround keyframes are never synthesized. The start floors above the capo.
   Simultaneous same-direction carriers are one scrape sounding on every string the pick crosses, so
   each becomes its own note on its own string, all sharing the gesture's longest span. An opposed
   direction at the same onset is a notation error rather than a chord: the first direction wins
   and the opposed carrier is dropped, counted. A dead note carrying slide-out flags (4 or 8) is a
   fretting-hand figure and never converts. As ordinary notes, scrapes take part in every distance
   rule, and the fret-hand machinery ignores them as it ignores taps (`rightHandOnset`).
8. **Tremolo picking spells out as its strokes.** Guitar Pro's tremolo is measured — the beat's
   `Tremolo` element states the stroke length in quarter notes ("1/2", "1/4", "1/8") — while the
   chart reserves `tremolo` for unmeasured noise, so a tremolo beat splits into its strokes before
   events are collected, and they flow through graces, ties and every later rule as hand-notated
   notes. Strokes re-pick: a tie into the beat releases its origin, only the last stroke keeps a
   notated onward tie, and only the first keeps the emphasis and a legato arrival. The last stroke
   takes the remainder, so the beat's total survives an indivisible span. A bent tremolo spells out
   too: each stroke samples the curve at its own onset and carries the value as a flat prebend. A
   beat holding fewer than two whole strokes is a single plain note. Only a beat carrying slide
   flags keeps the mark, counted: per-stroke frets along a glide would be fabricated data, and the
   flags include the pick-slide carriers.
9. **A trill spells out as its legato alternation.** Guitar Pro's trill states only the auxiliary,
   as an absolute pitch, and no speed, so the run alternates at sixteenths — the one estimate the
   format forces — principal, auxiliary, principal, for as long as the note rings. The first note is
   the source note: it keeps the onset's marks and remembers the run's take as stolen lead. Every
   continuation claims legato and carries only the mutes; the resolver derives hammer or pull from
   the frets, so no direction is stored. The last note absorbs the remainder and takes the onward
   tie and the let-ring mark. A ring shorter than two steps, or an auxiliary the hand cannot reach —
   below the capo'd open string, off the board, or the note's own stop — leaves the note single,
   counted; the capo'd open string itself is a legal auxiliary.
10. **A roll spreads its chord over a held grip.** Guitar Pro's roll (the beat's `Arpeggio`
    element; `Down` strikes the lowest pitch first) states its spread in the beat property
    `687931393`, in ticks at 480 to the quarter note — not in the brush's sibling `687935489`, which
    alphaTab reads for both and so imports every roll with no spread. The beat emits as plain
    staggered notes: its members are struck one at a time over the spread, every ring ending at the
    beat's stated end. A tick is two quanta of the chart's lattice, so every offset lands on the
    grid. Tied continuations are not members. The roll is an accumulation figure played fast, and
    it derives: the members' rings overlap, so shape rule 1 opens one span dated from the roll's
    first member, and the import writes nothing else.
    - **The start time** (`687931394`) places the figure against its beat: the figure opens
      `1 - start_time` of its written span before the beat, so at 0 the last member lands on the
      beat, and at 1, or with the property absent, the first does. The ends do not move, so an early
      member rings longer. No reference implementation reads this property, so the linear reading of
      the tool's two labelled endpoints is the semantic; a partial value rounds onto the lattice. An
      anticipation with nowhere to open — before the song's start, or onto a slot an earlier note
      already holds on the same string — starts on the beat instead, counted.
    - **What stays simultaneous**, counted: a beat with fewer than two members, a spread the beat
      cannot hold, a stagger that rounds to nothing, or one that would leave a member no ring. A
      tremolo-split beat drops the roll, counted: the mark spreads the grip once, and a stroke
      carrying it would state a hand re-taking stops it never left.
11. **A bar's feel plays its written pairs.** Guitar Pro leaves a swung bar's rhythm straight on the
    page and swings it on playback, so a chart storing what sounds swings it at import. The master
    bar's `TripletFeel` names a unit, the eighth for the `8th` values and the sixteenth for the
    `16th` ones, and exactly the pairs move: two consecutive time-taking beats of one voice, each
    lasting exactly one unit, the first opening a whole number of pairs after the bar's downbeat.
    The move is purely one of durations: the first beat plays the pair's first share and the second
    the remainder, so the second onset lands at the first's new end and the beat after the pair sits
    where the page has it. The shares are two thirds and one third for `Triplet`, three quarters and
    one quarter for `Dotted`, and one quarter and three quarters for `Scottish`, the short value
    first. A rest is a beat like any other. Everything else keeps its written time: a unit whose
    partner is not one, a unit on an off slot, and any dotted or tupleted value. Grace beats are
    transparent, neither breaking a pair nor moving with it, and a grace's lead comes off the swung
    ring afterwards (rule 5). The feel is per bar, and it plays before the tremolo split (rule 8),
    so a tremolo-picked unit swings as a slot. The reading is alphaTab's
    `MidiFileGenerator._calculateTripletFeelInfo`, which matches Guitar Pro's playback on the
    corpus.
12. **A fretted note imports as source fret plus capo; an open string stays 0.** Guitar Pro's frets
    are capo-relative — with a capo at 3, an entered 1 sounds absolute fret 4 — and the chart stores
    absolute frets with 0 meaning the open string, capo'd or not, so the capo never appears as a
    fret number and frets 1 to the capo are invalid. Every pressed position the import generates
    floors at the capo plus one: a slide-in's approach, a slide-out's exit and a scrape's start, as
    every derived fret-hand window does. The capo itself is clamped to `0..g_max_capo`, counted.
13. **The score's two dynamics marks resolve onto one emphasis axis, and staccato is duration.**
    Guitar Pro writes loudness as the `Accent` bitset (1 staccato, 4 heavy accent, 8 accent) and
    quietness as a sibling `AntiAccent` element whose presence is the claim. Both loud bits import
    as `Accent` and the quiet mark as `Ghost`. A note claiming both is contradictory data, and the
    louder claim wins, since a hit drawn quiet invites under-playing it while the reverse merely
    over-plays. Staccato is no dynamic at all: Guitar Pro sounds the note for exactly half its
    stated duration, so it imports with half the ring, per note, and nothing stores the mark.
14. **A natural's label is its node; every other harmonic's is a partial label against its stop.**
    Guitar Pro's `HarmonicFret` means two things. For `Natural`, the label — or, absent one, the
    source fret, which for a natural is the touched position in the capo-relative frame — resolves
    against the open string and lands at the capo plus that offset, with the note's fret set to 0.
    For the fretted family — `Pinch`, `Semi`, `Artificial`, `Tap` — the label is a partial spelled
    as the familiar open-string position, so the node is the stop plus that offset, the stop being
    the note's absolute fret or the capo on an open string (`physicalStopFret`). `Pinch` and `Semi`
    take the `Pinch` attack over any tap or legato mark beside them, the pinch being the onset the
    squeal makes audible — a semi-harmonic is a pinch whose fundamental keeps ringing, the nearest
    technique until the format distinguishes them, counted — `Tap` takes the `Tap` attack, and
    `Artificial` keeps the note's attack. The artificial and tapped forms then meet the load
    normalizer's reduction of the disabled harmonic forms (\ref design_rulesets_techniques).
    `Feedback` and unknown types are unsupported — feedback needs a real amp in the room — so the
    note imports without its harmonic, counted.
15. **Labels snap to the node they name, inside a half-fret window.** Notation writes rounded
    labels, and a touch slightly off a node chokes the harmonic, so each label snaps to the nearest
    true node, up to the 8th partial (`g_max_snapped_partial`, taken from Guitar Pro's own output).
    Real labels land within 0.331 of a node, while the integer frets with no harmonic near them (1,
    11, 13, ...) miss by 0.669 or more, so a label farther than half a fret from every node names
    nothing: a natural then imports as an ordinary note, and the fretted family takes the octave —
    the lowest-order harmonic at any stop, and so the easiest to ring — as it does with no label at
    all. A node past what the note can reach (`harmonicNodeCeiling`: the neck where the fretting
    finger stands on the node, the string otherwise) also falls back to the octave. Each case is
    counted.
16. **An out-of-range value is reduced and reported, never carried to validation**, where it would
    refuse the whole song over one field. A track declaring more than eight strings loses the extra
    courses; a note naming a string the tuning lacks is dropped; a fret past the last one, once
    shifted by the capo, is pulled back to `g_max_fret`; a beat overflowing its bar is dropped with
    its grace run; and of two notes struck on one string at one instant, the first is kept. A
    whammy-bar beat imports without its dive. Each reduction is counted.
17. **An attack mark imports as the intent it states, and nothing more.** Guitar Pro's *Tapped*
    mark, the two-hand tap, imports as the `Tap` attack, and *LeftHandTapped*, the fretting hand
    striking from nowhere, as `LeftTap`; a note carrying both is the left-hand tap, the more
    specific articulation. A hammer-on or pull-off destination imports as the `Legato` claim: the
    score says the notes connect but not which way, which is exactly what the claim says, so the
    import neither needs a direction nor invents one. The junk marks real scores carry — a claim
    with nothing before it, or with a predecessor at the same stop — are what the load normalizer's
    relational settle converts, counted. A tap mark on an open string with no node has nothing to
    strike and becomes a pick as soon as its note is built (technique rule 4,
    `flattenStrandedStrike`), because every pass after it reads the attack and must not shape the
    song around a tap that cannot survive.

# Named rules

## The ring policy

`ChartNote::sustain` is the duration the string actually rings, and the import stores exactly that:
the ring is the record, so neither staccato nor let ring is ever a stored field.

- **Lengthened** where the source says one sounding continues — a tie merge, a legato-slide merge
  (rule 2) — where a let-ring mark asks (the figure law), and where a synthesized gesture needs room
  to travel: a shift slide's origin reaching its landing and a slide-out's minimum window (rule 1),
  and a scoop (rule 4).
- **Shortened** by two rules of the import's own: a before-beat grace's steal (rule 5) and
  staccato's halving (rule 13). A bend, whose points Guitar Pro writes as percentages of the
  notated duration, is laid out over that duration and clipped with the stolen part rather than
  squeezed into what is left. A pass that lengthens the same ring later simply out-rings the steal,
  so `max(tie or slide end, let-ring end)` is an order of passes rather than a comparison, and the
  steal needs no exemption for the marks that lengthen.
- **A re-strike stops the ring.** The same-string clamp ends every ring at the next onset on its
  own string. It runs after every pass that can lengthen a ring — the figure law is the last of
  them — and before the one pass that reads the settled rings, the slide-out exits (rule 1).
  Slide-in resolution, and the natural stream it reads, run ahead of it, because the scoop is one
  of the passes that lengthen.
- **Payload is trimmed to the ring once**, straight after the clamp, because that is where the ring
  stops moving. The imported bend is the only payload written past a ring, and trimming it earlier
  would cut a let-ring note's curve against a ring the figure law is about to hand back.
- **Nothing else shortens a ring.** A dead note keeps its notated duration like any other: tail rule
  3 is presentation, and that duration is the timing the legato adjacency test reads. Readability
  is the tail rules' (\ref design_rulesets_tails), drawn and never stored.

## The figure law

Guitar Pro's `LetRing` mark is staccato's mirror: staccato shortens the imported ring, let ring
lengthens it. A marked note's notated duration is the transcriber saying "not this length", and no
charter can state the real one, while Guitar Pro's playback horizon is a default rather than a
statement. So the import derives the end from the music's structure (`letRingFigureEnds`), in three
clauses held together:

- **The anchor.** A marked ring rings to the first onset its own voice states after its figure's
  last mark.
- **The seams.** A figure ends where its grip is contradicted or where its audibility horizon runs
  out.
- **Written is the floor, and the same-string clamp is physics.**

Every marked ring is then one assignment, `max(written, min(same-string clamp, figure end))`.

### Figures

Each voice accumulates a **grip**: the stop last stated on each string since the figure began. Both
halves of every comparison read one statement authority (`statedStopAt`, over `fretHandStopAt`), so
a slid finger carries its statement forward rather than manufacturing a contradiction, and a tap
states the stop its pull-off plants beneath it, or nothing for a bare tap. A pull-off source riding
above a stop the figure already grips states that stop (`gripStatement`, the shape rules' own
authority), so it and the release beneath it close nothing; a source over any other ground states
what it sounds. A first-time string grows the grip and a same-stop statement confirms it, so a chug
can never split a figure and a repetition can never be divided: with no retreat in the law, that
invariant is structural. Co-struck notes are judged against the grip as it stood before their
instant, so they never contradict each other.

- **The grip seam.** An onset stating a different stop on a gripped string closes the figure and
  founds the next at itself.
- **The horizon seam.** An onset lying strictly past the audibility horizon of the figure's most
  recent member closes it too. Measured member to member, never from the figure's opening, so only
  real silence expires the asking and a continuous texture of any length stays one figure.
- **Phrases.** Consecutive marked figures joined by grip seams are one phrase: one asking continued
  under a moving hand. A horizon seam, or a figure carrying no mark, ends it.
- **The fragment donation.** A figure closed by a grip seam while holding too few notes ever to
  found a span — its largest stroke and its whole membership put to shape rule 1's thresholds
  (`foundsSpan`) — is a remnant mis-grouped with the next figure's opening. Each of its notes that
  does not contradict the closing figure's grip joins that figure, and the rest stay. A donation
  never crosses a horizon seam or a voice, and a real figure never donates, which keeps a repetition
  undividable and a closing confirmation with its own stack.

The grip is figure-scoped memory, not sound. Reading the sounding grip would need a staleness guard,
and such a guard fails in opposite directions: late strikes confirming a dying grip escape its
shared clip, while a new figure's own opener is clipped by the contradiction it belongs to. Figure
membership is the one fact that separates the two, so the grip lives and dies with the figure and no
staleness rule exists. The walk reads onsets and statements only, never a ring or a span, so it is a
pure function of the written stream: one forward pass per voice, and no fixpoint. A figure's whole
job for the tails is grouping the marks — which let-ring stack each belongs to. The marked region
segments nothing: segmentation reads every onset, marked or not, and the marks decide only who
extends.

Section marks appear nowhere in the law: a section mark is organizational, not a hand fact, so it
neither seams a figure nor bounds a tail.

### The figure end

- **The anchor** is the first onset the figure's own voice states after its last mark: material
  past the marked run never asked, so a marked drone does not ring into the unmarked chords that
  follow in its own line. Where the voice states nothing more, the first onset anywhere in the track
  answers; where nothing follows at all, the figure's latest written end. A seam always lies at or
  past the anchor, so it never enters the arithmetic.
- **The cap.** The end is never more than one audibility horizon past the figure's last marked
  onset, which bounds a marked drone under a marked same-voice texture that nothing contradicts. The
  anchor and the cap come from the scope that answers the ring, never from one ring's own onset, so
  a stack stops together.
- **The written floor.** The end is never earlier than the figure's latest marked written end. A
  written length is authored truth rather than an estimate, so where one member's tie-merged
  written end outruns the anchor, the whole stack rings to it; the anchor and the cap bound only
  what the law estimates.
- **The open string's lift.** An open string is not held by the hand, so a grip seam is no answer to
  it: a marked open string takes the longer of its figure's end and its phrase's end, the same
  anchor and cap asked of the phrase's last mark. The lift only lengthens, and what still stops an
  open string is the same-string clamp. A natural harmonic rings hand-free by the same physics but
  stays outside the lift until such a ring has been checked by eye.

### What never lengthens

- A **dead**, a **palm-muted** or a **staccato** note. Guitar Pro's playback returns before it
  reads the mark on each (`MidiFileGenerator._getNoteDuration`), so each keeps the ring its own mark
  gives it. Only the pre-emption is taken: the static durations that playback returns there are
  declined, because the notated duration is the timing the chart reads.
- A **grace**, whose ring is its lead.
- A note whose ring ends in a **fret statement** — a slide-out, or a shift slide's arrival — since
  that statement is the ring's end by definition (`endStatedFretOrNull`).

A note that absorbed a same-string merge — a tie continuation, a legato-slide landing — is not among
them: the merge states its true written duration, and the mark then extends the merged note like any
other, as Guitar Pro audibly rings tied let-ring notes past their written duration. The mark that
answers for a merged chain is the struck note's; a tie continuation's own mark states nothing about
when the string was struck.

### One voice

The walk is scoped to one voice end to end: a figure is a statement about one hand's line, so the
grip, the statements judged against it and the tails it bounds all take the same voice. The
same-string clamp stays cross-voice, the difference being physics against grammar: a restrike is one
finger on one string and stops the sound whichever line wrote it, while a grip contradiction is only
the transcription saying a hand moved. The shape rules keep their own grip, shape rule 3's
per-string evidence table, and the two deliberately differ: the import's is figure-scoped grammar
over a transcription with voices, the derivation's is sounding evidence over a chart with none, so
a change to either belongs to its own law. The accepted cost is a registered watch item: a let-ring
drone under a moving melody in its own voice clips at the melody's first fret change, while a drone
in a voice of its own rings on to its anchor.

### What the law reports

The import log counts the rings the figure end lengthened, counted after the clamp so a ring the
clamp took straight back is never announced, and `GpLetRingReport` publishes the pair the census
reads: rings extended, and marks left at exactly their written duration. The figure end is the only
thing that lengthens a marked ring, so a mark that changed nothing is either a texture the clamp
already bounds or a seam landing too early. The fret-hand positions a reader derives read the stored
ring, let-ring extension included.
