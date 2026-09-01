# The Chart Ruleset — the five laws and every rule under them

Status: **ANALYSIS COMPLETE 2026-08-27, AWAITING USER REVIEW. Nothing below builds until the
review survives the user's eyes.** This is the reconciliation of two parallel deep passes over
every rule signed through 2026-08-27: an architectural collapse (orchestrator) and an independent
musical/convention judgment (music-notation-expert, first assignment). Every code claim below was
verified at HEAD with citations; convention claims carry the expert's named sources (MusicXML 4.0,
SMuFL, Guitar Pro behavior, engraving practice). Markers: **[S]** shipped, **[U]** signed but
unbuilt, **[D#]** a decision the user must make (gathered at the end).

The week's ~40 rulings are not 40 rules. They are **five laws**, and everything else is a
consequence. That collapse is the analysis's central result: where a ruling resisted being
derived from a law, that resistance was itself a finding, and all of them are listed here.

---

## LAW I — TRUTH. The chart stores performance facts and nothing else.

What sounds: notes — onset facts plus the ACTUAL ring plus per-channel keyframe statements.
What the hand holds unsounded: claims — silent notes (`attack: none`) and `held` stops under
right-hand onsets. Sound truth is never bent for display; nothing derivable is stored.

Consequences, all [S] unless marked:

- The ring is the actual sustain, strictly positive, bounded only by the next SOUNDING onset on
  its own string. The expert's verdict: a justified divergence from all published notation (which
  stores the notated value and needs `let ring` / l.v. marks precisely because that value is not
  the true one) — "the strongest single decision in the set."
  - **Knowing-estimate caveat, NARROWED 2026-08-28**: the let-ring horizon was AMENDED by the
    user from the invented measure-end rule to GP's OWN PLAYBACK rule (verified in the
    reference implementation, MidiFileGenerator.ts GuitarPro-mode branch): the ring extends to
    the FIRST of (1) the next same-string strike, (2) the voice's next REST — the transcriber's
    silence statement, and (3) one full measure-duration measured from the note's own onset (a
    sliding cap that crosses barlines — the signed measure-end truncation was wrong both ways:
    it cut late-bar notes short and rang through rests). This upgrades the value from invented
    estimate to what the source audibly states — the author tuned by ear against it. The
    remaining estimate in a truth field is the converter default (D10's same-string bound),
    recorded as such. **OPEN, census-gated (user 2026-08-28)**: GP's one BLIND stop (the
    sliding bar-cap — its other two stops read authored statements) audibly bleeds at section
    ends; two PRECISE divergence candidates are on the census — a SECTION-MARKER stop (the
    parser already reads the markers; targets the complaint literally; the rare deliberate
    ring-over is restorable by hand since our tool states precise lengths) and a REGION-END
    stop (the mark's own extent; over-fire risk on the sail-over figure — a counting question,
    not a debate). Every candidate only ever SHORTENS a ring vs GP, so whichever survives the
    numbers is definitionally never-less-accurate: identical to what the author heard except
    where their own marks say stop. No guessing rule is ever admitted (user constraint).
- Keyframes: per-channel-optional statements `{offset, fret?, bend?, vibrato?}`; fret/bend
  interpolate between their own statements, vibrato holds; `slideOut` is fret-only (the release
  is the ring's end by definition — physically forced, not stylistic). Each channel's semantics
  is separately convention-backed (MusicXML models a bend-release as a SEQUENCE of bend elements;
  vibrato is an interval state; a slide is relational, re-expressed locally per the
  no-note-references law).
- A silent note is a point record: no ring of its own, all techniques refused through the
  saved-form fixpoint. No published mark records "finger down, string never speaks" in a running
  line — this is honest invention, and its nearest ancestors (the chord frame's barre bar over an
  X'd string; classical preparation fingerings) state the same physical fact. **Fret 0 on a
  silent note is a VOICING member** — the frame's "o", not a finger — say it that way.
- `held` is the fretting-hand stop under a right-hand onset; the tapped harmonic is ONE record
  `{touch fret, tap, node, held}` — the machine shape of MusicXML's harmonic triple
  (base/touching/sounding). `claimedStop` is the one reading of both claim carriers.
- **Internal consistency**: one record may not state contradictory facts about its string. The
  held stop is refused anywhere inside the note's traveled hull (`travelsThroughFret` —
  chart_rules.cpp:713); sustain is refused on `none`; the node is judged against the claimed stop
  (`physicalStopFret`). A claim on a string the sound already states publishes no reach and
  sweeps at settle (chart_shapes.cpp:317) — the physically impossible claim-under-ring cannot
  persist.
- No chord entity is stored; names/fingerings are the future dictionary's decoration (#118),
  matched by shape at read time. Convention agrees outright: chord symbols are a derived
  editorial layer (Brandt & Roemer's whole argument).
- Import is this law applied to sources: parse what the source states about sound
  (let-ring -> extended rings [STAGE-1 BUILT 2026-08-28: the voice's next REST and the sliding
  one-measure cap are walked from the source, the same-string stop is delegated to the chart's own
  clamp, and a note that absorbed a same-string merge is pre-empted because its merged ring is
  already GP's answer]; tap-harmonic canonization [U/#78]) and translate its
  spellings into these records, counting every drop (the Feedback precedent, rule 23).
  **THE PLAYBACK-TRUTH PRINCIPLE (user, 2026-08-28)**: where a GP mark's duration or timing is
  not stated explicitly in the score, GP's OWN PLAYBACK RENDERING is the default translation —
  never an invented rule — because the chart's author tuned by ear against that playback.
  Now a five-time pattern: staccato (half), the trill (the readers' sixteenths), the roll's
  spread and direction (the MIDI generator's semantics), anticipation (the slider's playback
  meaning), and let-ring (below). Check the reference implementation before writing any
  custom duration rule.

## LAW II — NO EMPTY STATEMENTS. Every stored record asserts something; what asserts nothing is refused at write or swept at settle.

- An empty keyframe is refused; a pending point dissolves at settle iff it changes neither path
  nor state.
- The inert sweep asks ONE question — is the claim's derived face absent (joins no span, past its
  span, restates a stated stop)? — to fixpoint, in the same undo entry, clearing the FIELD on a
  sounding note and deleting the RECORD of a silent one (never a sound the charter wrote).
- A claim that justifies a span is never inert (the derivation publishes reach). The one
  exemption: a stop the note's own PITCH is measured from — the harmonic — is never inert
  (chart_legato.cpp:195-206), because there the stop is onset data, not posture commentary.
  - **Corrected wording** (finding): the lone-claim bracket rule must be stated as *the
    harmonic-measured held draws its bracket without a span*. A PLAIN tap's lone held always
    sweeps, and correctly so — it restates what its answering arrival records itself. The earlier
    "a lone legal claim (held-carrying tap)" phrasing advertised a branch the settle deletes.
- Deletion cascades ride the entry. Engraving's own discipline (no redundant marks) agrees; the
  courtesy-accidental class of reader-aid redundancy belongs to projection, never storage.

## LAW III — SHAPES ARE STATED, NEVER GUESSED.

A span exists exactly where the chart states >= 2 members at a slot; it lives while its statement
is audibly in force; it splits where the statement changes and merges where statements equalize;
its class is HOW its members sound.

- **Membership** [S]: sounding fretting-hand onsets and claims. A lone member never opens
  (convention agrees: a chord is two-plus noteheads). Right-hand onsets are evidence, never
  members — the posture is the fretting hand's, and a tap says nothing about it.
- **Justification** [S]: a zero-sound span must be justified by a sounding arrival answering a
  claim — same string, same fret, where "sounds the claimed stop" includes a right-hand onset
  whose held equals the claim (the tap-harmonic arm: the overtone divides the STOPPED length, so
  the held fret sonically participates — the expert calls this "the best physics in the set").
  The arrival closes the fronted span and opens its own (chart_shapes.cpp:592) — no furniture
  overlap. Unjustified silent spans dissolve.
- **Extent — THE CONTINUITY LAW** (ruled at [D3] 2026-08-27, superseding the min-extent clause
  and both walkthrough proposals) [U]: the statement is in force while every sounding member's
  STORED ring is continuous — ringing through, or ending exactly at its next same-string onset
  (adjacency, the strike-into-strike shape repeated strums store; the walk already reads the
  stored stream — ring_end_of is saved_notes' sustain, chart_shapes.cpp:245-247). The first
  genuine stored gap on any member ends the span at that ring's end; survivors draw as
  remainder tails; for silent shapes, extent stays justification-driven and claims stay exempt.
  The split records DETACHMENT of sound, never a lift (an open string's gap is right-hand
  damping; the reader's release inference is their own — D1's physics kept). Class plays no
  role in extent, so nothing circular exists to restructure; min-extent is this law's box case
  (corpus stake: 1.6% uneven, median gap half a beat); carried ring-through members are
  EXTENT-INERT (their rings classify, never bound — else let-ring texture bounds span
  structure).
- **Change** [S]: rule 11 splits identical-strum comparisons on any articulation change (print
  practice agrees: publishers restate the frame on any voicing change). `statedStop` is the
  continue/split authority at a claim: absent = growth (splits, inherits, re-merges on
  equalization), different = a moved finger (splits, supersedes), equal = redundant (the sweep's
  territory). The lone re-pick (side ruling (ii)) continues a span only at a member's own stop
  with a witness still ringing.
- **Class** [S start-cases / U interior]: **ARPEGGIO iff the shape's members sound separately** —
  carried-by-ring at the span's own onset, silent members, or any interior onset sounding a
  PROPER SUBSET of the shape [U — flips 188 corpus spans, arpeggios 37 -> 225]. It stays a BOX
  chain only when every sounding is the full shape. Raw overlap outside spans never creates or
  classifies a span — that is let-ring, texture not statement.
  - **Vocabulary note the expert insists on**: "arpeggio" here is the project's own THIRD sense —
    narrower than theory's broken chord, different from engraving's rolled-chord wavy line (whose
    bracket partner means *not* rolled). The marks do not collide (ours are horizontal spans, not
    pre-chord signs), but this document is where the project sense is defined, once.
  - Consequence worth stating plainly: every (ii)-continued span is an arpeggio by definition —
    a lone re-pick IS an interior subset-sounding; the 188 flips are exactly that population.
  - **STAGE 2b BUILT 2026-08-28**: trigger (c) lives in the WALK as one comparison at the one
    chain-update site (the projection home was tried first and measurably failed — 259 of 305
    against the law's own consequence, because rule 12a's trimmed extent is a second window
    free to disagree); every walk-continued re-pick span is arpeggio BY CONSTRUCTION, with the
    corpus residual (38 of 305) proven to be end-slot rig-reading, zero interior misses.
    Corpus: arpeggio spans 727 -> 853 (+126, the ruled intent; the census row deliberately
    FLAGS at +117 against the last independent 736 with all three component deltas named —
    the instrument speaking, not erring). THE CLASSIFICATION-STREAM RULING (user 2026-08-28):
    classification reads STORED truth — trigger (a)'s presented-ring reading was the odd arm
    and unifies onto the stored stream with (c) (a dead carry classifies at the start exactly
    as at an interior slot; E25 stays display-only; "if it sights wrong we can adjust later").
    Interim surface note for stage 3: the highway still boxes interior restrikes inside
    (c)-flipped spans (~126) until the alignment obligation lands; new sighting item — a
    (c)-flipped span with a full-strum start draws empty bracket bars (digits suppressed),
    previously only the tap-flipped look. The four-trigger enumeration is stated once (the
    thin start is the precondition of (a)/(b), not a fifth trigger).
    LANDED same day: the unification is a DELETION, not a flip — the walk's fold-in has always
    read the stored ring, so (a) IS (c) asked at the span's own start, and the projection's
    third reading (its backward per-string look, the `postures` parameter, and the coded
    thin-start clause) is deleted with proof rather than corrected. The two readings agreed on
    every span in the corpus (zero census delta): the divergence class — a dead string's ring
    carried strictly across an otherwise-full-strum onset — is empty in today's imports, so
    the ruling changes what the rule MEANS and what a charter can author into it, not any
    imported chart. Pinned by a both-arms test plus a deliberate old-assertion flip. One
    residue tracked in the backlog: the census rig's own [D4] fold-in still reads the
    presented ring and gets its solo flip-and-re-run.
- **Why the split laws exist** (the collapse's clearest insight): within one span the posture is
  CONSTANT BY CONSTRUCTION — every split rule exists precisely to keep it so. That constancy is
  what makes "the shape" a well-defined denominator for the subset test, the repeat-box identity
  test, and the bracket's claim. The one place the walk currently lets posture drift is [D2].
- **[RULE 11 AMENDED 2026-08-29 (user)] — A CHANGE IN ARTICULATION DOES NOT SPLIT THE SPAN.**
  The user, verbatim: "The bottom line... Change in ARTICULATION does not split the span." The
  span is a FRETTING-HAND statement — palm-mute is the picking hand, dead is pressure, accent
  and ghost are dynamics; none of them move the grip — so a chord, its dead chugs, and the chord
  again are ONE hand fact and derive as ONE span. Continuation and merging compare POSITION
  (strings + frets) only; the constancy law above holds for the POSTURE'S FRETS, while
  articulation varies freely within a span as per-onset display data. WHAT STILL SPLITS, the
  complete list (corollary 1, user-confirmed): position changes — fret travel (the [D2] landing
  split), growth (a new string changes the shape), a same-string different-fret contradiction —
  and GENUINE GAPS (silence breaks spans, the old law, reaffirmed twice this week). Corollary 2
  (confirmed): a full restrike of a landed grip stays INSIDE the successor — "the bracket span
  never strums" dissolves; its full box comes from the display law below. Corollary 3
  (confirmed): accent/ghost ride without splitting. Corollary 4 (confirmed): the class law is
  untouched — arpeggio iff members sound separately, by the signed triggers.
  WHY (the doctrine's own trail, named): the U2 chug-chain machinery, the silent-gap hole, the
  abuts-bit proposal, the span-scoped revert, and review R1's carried-chain bug ALL orbited rule
  11's fragmentation of one grip into many spans; the display kept trying to reassemble what the
  derivation had cut. R1 DISSOLVES under this amendment — when continuation compares position
  for everyone, the carried/struck distinction has nothing left to distinguish.
  THE DISPLAY LAW replacing the five-condition draft (user, final form): **repeat boxes are
  allowed iff no other onset occurs between the chords** — an onset wears a repeat box iff it is
  identical to the IMMEDIATELY PRECEDING onset within the same span with no onset of any kind
  between, gated by the signed display-capability profiles (the four mute profiles x emphasis;
  anything else full). Subsumes "first onset wears the full box" (nothing precedes it); kills
  repeats-after-interleaved-picks, which is where "repeats look odd in arpeggio spans" actually
  lived, so the blanket arpeggio ban dissolves; whole-shape chug runs AND repeated identical
  subset chords inside arpeggio spans both repeat (subset repeats follow boxless plain heads — a
  flagged sighting item, narrow retreat = whole-shape-only). THE IDENTITY, RULED COMPLETE
  2026-08-29 (user: "My U2 ruling should follow here"): **same STRUCK STRINGS + same FRETS,
  profile FREE** — a profile change rides as a repeat wearing its own marks (a plain chord's
  first dead chug is an X'd REPEAT box), capability-gated as always (a non-repeat-capable
  profile falls to a full box). The string-set comparison is EXACT: a partial strike after a
  full chord is a different onset — different notes — and wears its own full own-width box;
  only an identical partial following that partial repeats (user, same day, closing the
  ambiguity explicitly).

## LAW IV — INK HAS ONE OWNER. Every displayed fact draws exactly once, owned by the most specific furniture that states it.

- Brackets state MEMBERSHIP at statement boundaries: span starts only (heads for struck members,
  bracket digits for silent/carried — chord frames print once at the change, not per strum), and
  the harmonic-measured lone claim's own slot (2D satellite [S]; 3D first cut [U], sighting
  pending). **R5's satellite target is RESCOPED by the review-blocker walk (2026-08-31, item 7
  below, final law): a satellite is the note's own held FACE and stands wherever the stop is
  AUTHORED — or wherever a tap FRONTS a bracket, since there the bracket's displaced digit is that
  face.** A DERIVED held stop does not stand: the pull-off notation is already where that fret is
  written, so a second standing mark would be the same statement drawn twice, and the face is
  REVEALED with the note's own ring instead. The bracket's membership digit is a separate fact from
  the note's face, and a mid-span authored tap wears both.
- Repeat boxes mean LITERALLY the same onset struck again identically — the simile mark's idea,
  specialized and stricter — and nothing else. AMENDED 2026-08-29 (user, closing the C2
  overshoot): a BOX marks SIMULTANEITY — any two-or-more-string strike wears one, scoped to its
  struck strings, full vs repeat by the consecutiveness law, inside and outside spans alike. A
  partial restrike inside an arpeggio span wears THE STANDARD CHORD BOX (user Q2 ruling
  2026-08-30, killing the orchestrator's "own-width" invention: a partial box "would probably
  look ugly... they need the full box" — the arpeggio context is already carried by the span's
  borders and the brackets standing on the fretboard, so nothing restates), never a repeat of
  the shape's box. The original C2 defect — repeat boxes claiming a full restatement a partial
  strike never made — stays fixed by the identity law itself: a repeat only ever follows an
  IDENTICAL preceding onset. Single notes stay boxless. THE COINCIDENCE RULE (user Q1/Q6
  rulings, same day): where a chord box and an arpeggio box would coincide, the chord box is
  SUPPRESSED and the arpeggio box shows — "a bracket draws its own box with additional
  information," and two overlapping translucent boxes hurt readability; this is the EXISTING
  suppression rule following its ink, no new rule — under the deferred bracket it keys on the
  instant the bracket actually draws (bracket_seconds), not the span's start. A cleaner
  implementation that removes the suppression may exist ("I can't think of it off the top of my
  head") — revisitable, not now. Q5 stays LITERAL: any onset between identical chords breaks
  the repeat run — a silent hold there either states a new stop (a growth split, a boundary
  anyway) or restates what the span already says and dissolves in the sweep, so the strict
  reading costs nothing.
  **THE ACCUMULATION RULING — RULED 2026-08-30 (user, all seven questions walked one at a
  time; "This design must be SOLID not guessed"). The final missing piece of the derivation.**
  Grown from the user's Creed-intro sighting (let-ring broken chords reading as held chords),
  the rings-together census (2,602 candidate spans, 95.3% let-ring), and a Fable deep analysis;
  the user's own re-derivations twice CORRECTED the analysis (Q2, Q3).
  **RULE A — THE ONE SOUNDED OPENING LAW (Q1/Q2/Q5): a span opens where two or more members'
  rings MUTUALLY overlap at stated stops.** Strong form only (Q5): each new arrival must overlap
  every still-ringing member — pairwise chains would bracket conjunctions that never held; a
  member whose ring dies early stays a member. BROAD founding (Q2, the user overruling the
  analysis's fretted-only boundary with a re-derivation the analysis missed): the bracket's
  claims are PER MEMBER — a fretted member's digit asserts a held finger (proven: a fretted
  ring dies when the finger lifts — on authored charts by authorship, on imports by the
  let-ring mark's own statement), while an OPEN member's 0 asserts no finger at all, only the
  ring itself, which the stored ring states — so no member's claim can be false and open
  founding passes the 100% bar; it also declutters open-string rings and deletes the fretted
  branch from the founding test. The span is DATED FROM THE EARLIEST MEMBER'S ONSET (the
  maximal mutually-ringing set defines membership; its first onset defines the front). Strums
  (simultaneous) and landings (carried) are special cases of this one law — censused at ZERO
  exceptions across 21,809 shipped openings; claims stay correctly outside it (a claim has no
  ring). F6's surviving half is OVERRULED for exactly this form (Q1, "out of the question for
  any of this to work"): the amended line reads "raw overlap never creates a span, except the
  strong-mutual accumulation, whose per-member claims are proven or stated; pairwise overlap
  remains texture." The original kill's grounds never engaged the fretted-ring proof, and the
  drones-under-stabs habitat was censused as already covered.
  **RULE B — THE ELASTIC LET-RING TRANSLATION (Q3, import only): a let-ring-marked note's ring
  becomes the length that makes the LARGEST POSSIBLE span containing it** — elastic in BOTH
  directions (lengthened to the span's end, truncated to it where a split cause cuts) — applying
  ONLY to let-ring-marked notes, whose notated duration is inherently imprecise (the user's
  warrant; GP charters cannot state it). The playback-truth principle's sixth entry; D6's
  staccato halving is the precedent; D4's FULL ACCEPTANCE is amended for the stacked figure
  only. The well-founded order (no circularity): the stacking region derives from RAW rings;
  its end is fixed by EXTERNAL CAUSES ONLY — a closed list: contradicting onset (same string,
  different fret), foreign statement, authored rest, full grip change, section marks (D4's
  cap-at-marks folds in); rings then normalize; the ordinary derivation reads the result.
  Fixed-point stable (a lengthened ring ends AT the boundary; exact adjacency is non-overlap).
  **THE ONE-MEASURE CAP DIES (Q3 follow-up)** — an elastic ring may stretch across measures if
  nothing splits — GATED per the user ("that decision poses some risk"): a census re-run under
  the final rules measuring the uncapped stretch distribution, AND a sighting, both before the
  build ships. Truncation's payoff, censused: the 19- and 39-fragment drone chains collapse to
  one fragment each; the 242-span min-extent tension dissolves; in-span rings end at the span
  end so the suppression's out-ringer case empties for let-ring figures.
  **ROLLS DERIVE (Q7): the roll is an accumulation figure played fast.** D11's fronted-claims
  import machinery DELETES — rolls import as staggered notes with their anticipation offsets
  and real chord-duration rings; the accumulation opens the span, dated from the roll's first
  member; same class, same bracket. VERIFICATION GATE: the derived span must equal the
  claims-produced span byte-for-byte on all three corpus roll beats before the machinery is
  deleted. With rolls derived, IMPORTS AUTHOR ZERO CLAIMS — the statement model collapses to
  sound states and authored states; "fronted span" stops being a derivation concept.
  **THE RESIDUE (Q4, moot-by-construction): N remains** for the never-sounded and the
  deliberately-short-rung stop; claims/justification/D1 machinery unchanged in code, shrunk to
  the residue in use; the authoring coupling follows — extending rings into overlap IS the
  bracket gesture (the shipped sustain gesture).
  **EXTENT (Q6): the continuity law unchanged** — min over member ends, made EXACT by rule B
  (elastic rings end at the region boundary by construction). No new extent machinery.
  BUILD SEQUENCE (user-gated): (1) the final-rule census re-run incl. the uncapped-stretch
  measurement; (2) the build; (3) adversarial review; (4) THE SIGHTING incl. the cap-death
  risk figure, before acceptance.
  **THE FINAL-RULE CENSUS RETURNED 2026-08-30 — THE GATE FIRED. The build is HELD; five open
  questions must be re-ruled first.** What the instrument falsified or exposed:
  (i) FALSIFIED — "truncation collapses the fragment chains": Rule B mostly LENGTHENS (10.6%
  lengthened vs 1.0% truncated, 88.4% unchanged); the collapse is RULE A's doing (one span per
  note by construction), and elastic rings fold into MORE shipped spans (max multiplicity 13 ->
  38). The truncation narrative was written for a world the closed list does not produce.
  (ii) FALSIFIED — "the out-ringer case empties for let-ring figures": under Q6's min-extent
  law, elastic let-ring out-ringers RISE (2,344 -> 2,535); rings-end-at-span-ends holds only if
  extent were the accumulation's END (max member ring), contradicting Q6 — AN INTERNAL TENSION
  between rule B's promise and the extent law, unresolved.
  (iii) OPEN (W-A) — Rule A's strong form as worded ("still-ringing") permits RELAYS: 128-member
  accumulations spanning 87 beats (20+ measures) exist in the RAW world — one bracket whose full
  conjunction never held, the exact disease the strong form was chosen to kill, at larger scale.
  The strict alternative (dead members block) gives 62,577 spans at p75 extent 0.667 beats —
  over-fragmented. AND the census's own extent reading was not Q6's: under min-extent a member's
  death ENDS the span, so the relay becomes a CHAIN of re-heading spans — the membership/extent
  COMPOSITION was never derived. Needs re-derivation before any word.
  (iv) OPEN (W-B) — "foreign statement" in the closed list is ambiguous and decides a law: read
  as growth (Rule A's maximal set) it never cuts; read as a cut it HALVES the median elastic
  ring (1.25 -> 0.5 beats). One sentence, an entire law.
  (v) OPEN (W-C) — THE SCORE-END HOLE: the cap's death is survivable on the distribution (median
  1.0 -> 1.25 beats; 8.6% exceed a measure; p95 under two) but the absurd tail (one ring at 81.5
  beats, +77.5 over the cap) lives almost entirely in the 511 rings (4.6%) bounded by NOTHING
  but the score end. Recommendation on record: the cap stays dead; the score-end population
  gets a DERIVED bound before the build.
  (vi) OPEN (W-D) — Q7's byte-equality gate FAILS on extent on both testable figures (2 of 3
  rolls testable): the shipped claims-produced roll span is as long as the STAGGER (~0.17
  beats); the derived accumulation runs the RING (~2 beats). Rolls still derive on position,
  class, and membership (shipped postures are supersets via ordinary fold-ins, zero roll stops
  missing) — but the roll bracket's DRAWN LENGTH changes, a sighting item and a re-ruled gate
  (position+class+membership equality; extent re-ruled deliberately).
  (vii) CLEANUPS — "full grip change" is subsumed by the contradiction cause (delete the dead
  entry); the same-string clamp does 65% of the bounding work and same-fret restrikes must NOT
  be crossed (the no-overlap invariant); section marks outwork authored rests (1,209 vs 856).
  CONFIRMED CLEAN: 100% arpeggio classification, zero imported claims under the ruling,
  compression +30.5%, open founding 39.1% of accumulation-founded spans, cap-death median
  indistinguishable from today.
  **W-A RULED 2026-08-31 (user, walked to ground) — THE COMPOSITION: ABSORB, THRESHOLD 2,
  STYLING UNIFIED.** The walk exposed that the census probe and the orchestrator's first
  composition disagreed on what an ARRIVAL does (absorb-in-place vs growth-split); the user's
  own deliberate-extension concern surfaced it. RULED (α): **an arpeggio span's arrivals are
  its class** — "members sound separately" — so an overlapping arrival is ABSORBED, growing the
  one span in place; the posture is the PER-SPAN SET (the constancy law amended from per-slot
  to per-span-set, its intent intact); a BOX span's new stop still growth-splits (a strum's
  wholeness broke). The relay dilemma stays dissolved by the death half: min-extent ends a span
  at its first member death; >=2 survivors open a DEATH-SUCCESSOR (the landing-successor arm
  generalized — the landing was always just one cause of carried rings crossing a boundary),
  seamless ink, names evolving. THE FRONT PRINTS NO LIE: heads draw at arrivals, rails from the
  front, no front digit-stack for struck members — REFINED 2026-08-31 by the review-blocker
  walk's chord-frame ruling (item 5 below), which is where the reading of that sentence was
  wrong rather than the sentence: the bracket states MEMBERSHIP where the reader meets it, so a
  member that accumulates in later DOES print its digit in the opening bracket and its own head
  restates it on arrival. What the front may not print is a number a head standing RIGHT THERE is
  already printing — the one thing suppression exists for — and the one residual front-claim is
  the NAME
  (the full template named before late members arrive); the user's deliberate-extension figure
  defaults to one span (the rings' own evidence) with a SPLIT-VERB OVERRIDE recorded for later
  design (their sketch: a hotkey at the caret splits the accumulation, auto-removing soundless
  bracket entries from the first span) — considered before the build, not solved by it.
  THRESHOLD 2 (user, with the unification argument: rule 10 already opens strums at 2; a
  different accumulation threshold would re-fork the one opening law) — WITH THE STYLING RULE
  (user): a 2-member arpeggio box follows the 2-member CHORD box pattern — no top border; 3+
  members draw the top border — the with_top convention unified across boxes and brackets for
  consistency. Sighting items: 2-member accumulation noise (43% of the population; retreat =
  styling differentiation or the one-constant threshold flip), the name-flicker on
  membership-churn figures.
  **W-B + W-C RULED TOGETHER 2026-08-31 (user, "the tail-cap rule") — after four candidates
  died on the walk** (section marks: organizational, not a hand fact; the non-let-ring cut:
  kills the drone-under-melody figure; statement-precedence: a new rule silently changing the
  growth law — the user caught the orchestrator smuggling it, twice; the unscoped justification
  bound: circular or base-case-less). THE RULE: interior members of a let-ring series are fully
  elastic — normalized to the region; **the LAST member (the latest-onset let-ring member of
  the accumulation group) keeps its GP-authored playback length as its MAXIMUM** — the stage-1
  walk (own-voice rest + sliding one-measure cap), which SURVIVES as the tail's translation
  instead of being deleted; every other cut applies everywhere (same-string, contradiction,
  own-voice rest — the cap is a ceiling, never a floor); unmarked lone arrivals absorbed by
  Rule A ride along but never extend the series' bound. THE CAP DID NOT DIE — IT RETREATED to
  the one place it was ever meaningful: the tail of the texture, where nothing else can state
  the end — the playback-truth principle applying by its own words, the default translation
  surviving exactly where the figure provides nothing better. Verified against all six
  constraint figures: the broken chord holds; the 81-beat monster dies both ways (capped as a
  tail, region-bounded as an interior); the lone-drone base case = today's shipped behavior;
  drone-under-melody AND drone-under-stabs ring on (D4's flips preserved, no rewind); the
  next-section bleed bounds at ~a measure; no strum-precedence rule needed (the growth law
  stands untouched); well-founded (the tail length derives from the source walk, independent
  of spans — no circularity, one pass). W-C CLOSES WITH IT: the score-end cause becomes
  unreachable. The final cut list: contradicting onset, same-string cut, own-voice authored
  rest, the tail cap. THE CENSUS RE-RUNS ONCE, on the final ruleset, as the build's gate —
  not per ruling.
  CLARIFIED 2026-08-31 (user): SECTION MARKS APPEAR NOWHERE IN THE FINAL LAW — not in the
  region's cut list (ruled out on the walk: organizational, not a hand fact) and not inside the
  tail cap (stage 1's shipped walk never had a mark stop; D4's "cap-at-marks" refinement was
  sighting-gated, never built, and is now FORMALLY DEAD — the blind-cap world it guarded no
  longer exists). D4's other refinement (the impossibility filter, the 24 nut-stretch fold-ins)
  is orthogonal and survives as designed.
  **W-D RULED 2026-08-31 (user: "legit fixing a bug") — THE ROLL BRACKET RUNS THE RING.** The
  shipped claims-produced roll span's 0.17-beat extent was never a ruling: it was the claims
  scaffolding's justification figure wearing a ruling's clothes — the span ran only as far as
  the roll gesture because that is all the claims stated. Under D3's own semantics the hold is
  the ring (~2 beats on the testable figures), and the accumulation derives it. The roll
  renders like every accumulation: rails from the front, heads through the stagger, member
  tails clipped at the next onset under the bracket (the bracket law, 2026-09-01 — as written
  here it said "suppressed under the bracket (C3)", which is the rule that retired), name at the
  front. THE RE-RULED Q7 GATE: before
  D11's claims machinery deletes, derived roll spans must equal shipped on POSITION, CLASS,
  and MEMBERSHIP (censused: equal, shipped postures superset only via ordinary fold-ins); the
  extent change is deliberate and gets sighted on the corpus rolls when the build lands.
  **HYPOTHESIS ZERO HELD — THE FINAL LAW CONFIRMED 2026-08-31 (user, all seven questions;
  build authorized).** The user's reframe ("are we 100% certain these results are BAD? our
  design should be converging on the TRUE representation of frethand positioning codified into
  spans") was verified by a Fable analysis: the gate census condemned the long spans with DEAD
  LAW — the pre-absorb "conjunction never held" criterion died with W-A; under the ruled law
  the posture is a monotone per-span set and the death law makes every long bracket's claim
  TRUE BY CONSTRUCTION ("no span claims a stop the hand abandoned while it ran"). The 128-beat
  figures are Travis picking / washes — the founding Creed figure at scale. Gates 1 and 2
  DISSOLVED (the interior's end IS stated by the region; the absorb world's numbers were the
  good column: fold-in 11 vs the split world's 108, noise 30% vs 64%); gate 3 CONVERTED and
  passes (a roll during a held figure absorbing is the true story; the re-formed D11 gate:
  coverage 3/3, class, membership, front-where-fronting 1/1 — the claims machinery deletes).
  TWO REAL DEFECTS, one-sentence fixes from owned conventions: THE DATING RULE (a span dates
  from its earliest member onset NOT COVERED by a preceding span; carried rings never backdate
  — 182 to 0 by construction) and THE FOUNDING MODE (the split discriminator is the founding,
  knowable at birth: a simultaneous >=2-string strike founds in STATEMENT mode and
  growth-splits on new stops, shipped law unchanged; a staggered founding is ACCUMULATION mode
  and absorbs — the class stays LAW III's whole-span ink derivation; two questions had shared
  one word). THE FHP CONVERGENCE confirmed at architecture level: FHP = the position story,
  spans = the grip story; never merged, now agreeing in kind; new census invariant (every
  span's fretted stops within the covering FHP window) and #137 reads spans as input.
  THRESHOLD STAYS 2 (a 3-founding touches no gate, re-forks the opening law, and would break
  dyad rolls) — WATCH ITEM registered per the user ("I have a feeling we may be revisiting
  this one"): trigger = the sighting shows too many 2-note arpeggio spans; the flip is one
  constant + expectations; the dyad-roll regression is the cost to weigh. Honest corrections
  folded: score-end reachable in 27 harmless cases (all within the tail cap's measure); Rule
  B's reach is 94.4% of marks (the 5.6% unmatched keep shipped rings, stated not silent);
  let-ring interior rings are AUTHORED DEMAND-LENGTHS per D3's own precedent (the Law I
  annotation). Name de-bounce (name-at-chain-granularity) and every long-bracket figure: to
  the sighting. NO RE-CENSUS BEFORE THE BUILD — the gate census is the build's baseline.
  **THE ACCUMULATION WALK IS CLOSED 2026-08-31** — all five census findings resolved (W-A
  absorb/threshold-2/styling-unified; W-B+W-C the tail-cap rule; W-D the ring extent; the
  cleanups struck as ruled). The sequence: THE FINAL CENSUS (this gate) -> the build ->
  adversarial review -> THE SIGHTING (incl. the tail-cap risk figures, the new roll look, the
  2-member noise question, the name-flicker) -> acceptance.
  **THE REVIEW-BLOCKER WALK — RULED 2026-08-31 (user, every blocker of the adversarial review
  walked to ground; the fix build authorized).** The review above found fifteen; each was ruled
  in turn and the whole set is recorded here rather than scattered through the laws it amends,
  because it is one walk (Debt A will fold it into the laws when the ruleset is consolidated).
  1. **THE EXTENT LAW HAS ONE AUTHORITY** (reviews #1 and #11). Span death is judged on
  FRETTING-HAND stops. A ring ending exactly at its own same-string restrike is a REPLACEMENT and
  no death — and the CONTINUITY LAW already says exactly that, so the replacement clause standing
  beside it (a second reading of the same adjacency, spelled over the slot's rings) is DELETED
  rather than corrected: one fact, one authority. A right-hand onset carrying a RESOLVED held
  fret SOUNDS that stop exactly as a fretting finger does — the one two-hand sounding law, the
  same one justification uses — while a held-less tap neither extends nor closes anything.
  Consequence: a landing into a restrike now never OPENS a successor where it used to open one
  and leave it no room; the emitted spans are identical either way and a test pins that.
  2. **THE ONE-COUNT OPENING LAW** (review #2). ONE member count over three kinds — sounding
  fretting-hand onsets, carried rings still sounding at stated stops, and claims (silent holds
  and resolved-held-carrying right-hand onsets). Count >= threshold opens; a LONE member of any
  kind opens nothing; the threshold is ONE named constant and appears nowhere else. The carried
  fold-in is UNGATED from the strike count, so a strike-less claim-bearing slot folds rings in
  like any other slot: one claim beside one carried ring opens a two-member span, and the carried
  ring is a MEMBER of it. What stood here was two counts in a disjunction, and a shape stated by
  one claim beside one carried ring satisfied neither.
  3. **FOUNDING FOLLOWS COMPOSITION** (review #10). `SpanFounding::Statement` iff the event slot
  stated the WHOLE shape — its own members (struck stops and claimed stops) reach the threshold
  and nothing CARRIED was folded in; ACCUMULATION otherwise. ONE derivation, used at every EVENT
  open including the sound-driven split. Inheritance keeps exactly the scope the header gives it:
  growth splits and carry-opened successors, the continuations no event states. Re-pinned
  consequence: a drone under stabs founds ACCUMULATION where it read Statement.
  4. **DERIVED HELD.** A right-hand onset's `held` is DERIVED wherever a PULL-OFF states it —
  same string, strict-adjacency legato successor, successor fret LOWER than the onset's own and
  greater than zero. You cannot pull off onto a fret unless a finger was already waiting on it,
  so the connection IS the statement. Stored `held` is authoritative only where no such evidence
  exists. ONE resolver in common core is the single reader authority: the span derivation, the
  projection and every verb read the RESOLVED stop and never the raw field. Authoring a pull-off
  off a right-hand onset CLEARS that onset's stored `held` UNCONDITIONALLY (agreeing or
  contradicting — an agreeing value is duplication and a contradicting one is a lie) inside the
  SAME undo entry; authoring `held` on an onset that already has a pull-off successor is REFUSED
  rather than silently dropped; the writer never emits residue and the load normalizer sweeps it.
  5. **THE DIGIT WINDOW.** Bracket-digit suppression asks what heads the string at THE BRACKET'S
  OWN INSTANT and at no other, with the FRET part of the test. A head LATER in the span suppresses
  nothing, because the opening bracket is the span's CHORD FRAME: it states the whole membership
  at the moment the reader meets it, so a member that ACCUMULATES IN LATER prints its digit there
  and its own head restates it on arrival. A resolved-held claim resolving into a span prints in
  that bracket as an ordinary membership digit. The inclusive-end defect the review found (the
  onset that CLOSED a span deciding the digits inside it) dissolves with the window.
  6. **PRINT AND CLICK ARE ONE DECISION.** The claim's mark is published from the very record
  that decides the digit prints, so a drawn digit is clickable BY CONSTRUCTION. The "the span
  starts at this note" proxy is deleted: it answered nothing about what was drawn and missed a
  deferred bracket whole.
  7. **THE SATELLITE REVEAL — the final display law** (signed 2026-08-31 and BUILT the same day;
  it SUPERSEDES the position-based reading first signed here, which keyed standing furniture to
  where in a span a tap sat). A satellite is the note's held FACE, note-scoped, at the note's own
  slot, and its visibility is keyed to AUTHORSHIP:
     - **AUTHORED held → STANDING, everywhere.** No front/mid-span distinction: an authored
     statement earns standing ink wherever it sits.
     - **A tap FRONTING a bracket → STANDING regardless of authorship**, because the bracket owes
     the statement there — its head holds the string's centre, so the posture's digit is displaced
     into the satellite column and IS that tap's face ([D2]).
     - **DERIVED held, not fronting → REVEALED on the note's truth channel**: shown exactly while
     the note's full ring is, through the existing selection-and-reveal pick. Revealing a note
     shows the whole truth about it at once. Read-only — the derivation owns the stop and the
     retype verbs refuse it.
     - **Lone span-less claims follow the same two rules**: authored stands, derived is revealed.
  The bracket's membership DIGIT (rule 5's window) is unchanged and INDEPENDENT: an authored
  mid-span held has BOTH its bracket digit (grip membership) and its standing satellite (the note's
  own face) — two facts, two inks. The projection publishes a face for every resolved-held-carrying
  right-hand onset with its terms (`StopMarkFace`) and stays selection-agnostic; the UI and editor
  layers apply ONE reveal predicate (`core::chartNoteRevealed`) at paint, layout, hit test and caret
  channel alike, so a drawn digit is reachable and an undrawn one is not, by construction. The
  SPAN-MARKER REDESIGN (`docs/plans/todo/span-marker-redesign.md`) may still reshape what a span's
  stops wear.
  8. **SATELLITES ARE NOTE-SCOPED, ALWAYS** — AMENDED the same day, and the amendment is the law
  (the dual-scope reading first signed here is SUPERSEDED and was ripped back out of the tree
  before it shipped). A satellite is its note's held FACE, full stop: a press on one addresses
  that note's held stop whatever the selection was, and the silent hold's bracket face stays the
  record's own note-scoped handle. What survives from the first reading is the SELECTION HANDLE
  alone: a selected note's satellite is hit-tested as PART of that selection, so pressing it moves
  the caret onto that note's held stop and leaves a wider selection standing — naming a stop
  inside a selection must not be what takes the selection away. Bracket column digits are not hit
  targets at all; a digit an accumulating member prints in the opening bracket is READ-ONLY
  notation, reached through that member's own head. The bracket-highlight scope visuals go with
  the scope they signalled.
  9. **SPAN-WIDE FRET EDITING IS DEFERRED TO THE TEMPLATE EDITOR** (amending the span-scoped
  bracket edit signed here, and the reason the scope law collapsed to note scope): typing a number
  over a bracket ALREADY means INSERT A NOTE at the caret, so a bracket-digit write-through has to
  steal that keystroke, and the dual-scope satellite machinery existed only to decide which of the
  two a press had meant. Re-queued for the future TEMPLATE EDITOR, where a span's grip is edited
  as a grip and nothing competes for the digits — also the natural home for editing tap-held
  values in bulk. Design record with the founding principle it belongs to:
  `docs/plans/todo/span-marker-redesign.md`.
  9a. **THE FOUNDING PRINCIPLE, recorded as the redesign's premise** (2026-08-31, not yet a law of
  the shipped model): **Sound founds. Claims attach. Markers define.** A statement comes into
  existence only by something SOUNDING; a claim can join, justify and count inside a standing
  statement but never constitute one; and deliberate span authoring becomes an explicit MARKER
  record rather than a shape conjured out of silent holds. Recorded here because several rulings
  above — the one-count opening law, the inert sweep, the satellite display law — are the shape
  they are because claims can found spans today; the redesign is where that premise is revisited.
  10. **LET-RING: THE STORED RING IS AN ESTIMATE, BOUNDED BY THE STATEMENT STRUCTURE** (LAW I
  amendment, Q4). **SUPERSEDED 2026-09-01 by THE CLEAN LET-RING BASELINE (the dated entry below):
  the span-founded clip described here is DELETED — the cut no longer asks whether a span is
  founded anywhere, and the import pass derives no spans at all.** For let-ring imports the stored
  ring is the best ESTIMATE of the intended ring
  rather than a notated fact — and an estimate yields to what the chart itself STATES. THE SPAN
  CLIP, a one-shot import pass after Rule B: `stored = min(max(written_end, min(region_end,
  foreign_boundary)), same_string_clamp)`. `written_end` is the charter's own notated duration and
  the clip's FLOOR (Guitar Pro's playback estimate is the unreliable half); `foreign_boundary` is
  the founding instant of the first EVENT-founded span after the onset whose founding slot states
  a NEW GRIP against the shape the ring was STRUCK INTO — a fretting-hand statement of a
  DIFFERENT fret on a string that grip already held, the grip read AT THE STRIKE (the ring's
  co-struck members plus what still rang under them). NARROWED in the same walk's fix round: the
  first cut read "any event-founded span the note is not a struck member of", which was the
  W-B-killed cut candidate returning — it clipped drone-under-melody, which W-B preserves. Growth,
  a melody over the drone, and every AGREEING statement are not foreign; a CARRY-OPENED successor
  states nothing at all, since the ring rides through its own statement's continuations; the
  same-string clamp stays strongest. (The A2 gate briefly narrowed "foreign" further on
  2026-09-01 and was REVERTED the same day — the dated entry below.) A
  ring ending exactly at a boundary does not fold into that span. NON-let-ring rings are never
  touched: written durations are authored truth. Rule B's own comparison is honestly
  lengthen-only again, with the structural reason stated where it lives (beats tile, so an
  interior member's notated ring ends at or before the region end; truncation is the clamp's).
  11. **Q2 CONFIRMED, NO CHANGE.** The dating rule's covered/uncovered carry scoping already
  stands as written above: a span dates from its earliest member onset NOT COVERED by a preceding
  span, and carried rings never backdate.
  12. **Q7 — THE LANDING NARROWING.** A staggered landing beside a ring that is NOT travelling
  states a shape with it and re-opens like any other landing; the staggered suppression narrows
  to the case where every OTHER surviving member is itself still mid-glide, which is the only
  reason such a landing has fewer than two members stating a stop. GROUPING opens there; DISPLAY
  stays deferred exactly as [D2] amendment 2 has it — a carry-opened successor's bracket defers
  to its first interior sounding, and one that never sounds interiorly draws no bracket at all.
  Where the developer guide's non-successor list disagreed with the walk, the DOC follows the
  CODE.
  13. **THE INERT-CLAIM SWEEP IS ONE PASS** (review #15 / Q3). The cascade the fixpoint iterated
  for cannot arise: what the sweep takes is a claim that reached NO span, so it was a member of
  nothing and no span's membership moves when it goes. The loop is deleted and the two comments
  that justified it are rewritten.
  14. **THE CENSUS GETS TEETH** (reviews #8 and #9). Signed expectations in the corpus rig are
  real assertions rather than printed markers; rows this build unpinned print under an AWAITING A
  SIGNATURE block and are re-signed after the first corpus run; the landing-era BOX row counts the
  LANDING arm alone again, with the death cause reported beside it rather than folded in; [D4]'s
  fold-in histograms split by arm so the source-hygiene population is measurable again; and new
  counters price the strike-less opening population and the derived-held population.
  **AMENDED in the fix round (review #6): the landing attribution scans the PREDECESSOR's member
  strings and demands the member's ring CROSS the boundary — scanning the whole tuning read a
  death-opened successor as a landing whenever any unrelated string arrived at the instant — and
  the strike-less row is renamed for what it can actually see (spans DATED at a strike-less slot).
  The dating rule backdates a strike-less opening onto its carried member's strike, so that half of
  the population is invisible from published data and the R-B ruling stays HALF-PRICED until the
  walk publishes the slot a span opened at. The derived-held residue row cannot discriminate on an
  import-only corpus either: imports write no `held`, so it guards the editor-authored path alone.**
  **C3 SUPPRESSION IS ALL-OR-NOTHING PER NOTE — RULED 2026-08-30 (user, first sighting bug):
  headless remainder tails are BROKEN.** The original C3 spec's "remainder rings draw from the
  span end" produced ribbons materializing at a bracket's edge with no head ("it looks
  completely whacked") on every member out-ringing its arpeggio. The rule: a member's tail is
  suppressed IFF the span's ink owns the WHOLE ring (ring end at or before the span's end); a
  ring extending past the span draws WHOLE, from its own head, through the rails and out. The
  ternary contract collapses to binary, the remainder arithmetic deletes, and a headless ribbon
  becomes unrepresentable — every drawn tail starts at a head. Compression survives where it
  lives: rings cut by restrikes end within the span and stay suppressed; span-end out-ringers
  draw honestly. If the out-ringing tails read as noise, that is a NEW sighting question — the
  remedy space is the let-ring-texture watch item, never the headless remainder.
  **C3 IS RETIRED — THE BRACKET LAW, RULED 2026-09-01 (user, on the span-final oddity): THE
  BRACKET IS THE HELD-INDICATION; THE TAILS READ RHYTHM.** A bracket is drawn across the stretch
  its members arrive over, so it already states how long the hand stays down — which leaves a
  member's ribbon nothing to add about the hold, and frees it to say the one thing the bracket
  cannot: how long THIS pluck is the sound being heard. THE RULE: an arpeggio-span member's
  presented tail is its ring CLIPPED AT THE NEXT ONSET (the first onset at a strictly later
  instant, on any string), and then the same tail rules every other note in the chart follows.
  Nothing is hidden any more. What that draws is a STAIRCASE — one step per string in a picked
  run, each ending where the next begins — and a BLOCK of parallel tails under an absorbed chord,
  since co-struck members are at one instant and so never clip each other.
  **THE COMPOSE (user sighting 2026-09-01, on the staircase's first look — sub-1/4 stubs).** The
  re-read runs BEFORE the presentation rules, never as a fifth rule after them:
  `chartResolutions` clips a copy of the saved stream (`clipArpeggioTails`) and presents THAT, so
  rules 1 through 4 judge a staircase ring exactly as they judge an equal stored one — rule 1
  binds it at the head it now ends on and trims the margin, rule 2 floors the trim on payload,
  rule 3 drops a sub-quarter step outright, and rule 4 keeps judging dead notes. That is what the
  sighting demanded: tails were showing on sub-1/4 members inside spans where the standard rules
  draw none, because the post-presentation clip assigned lengths rule 3 had never judged. In-span
  and out-of-span cannot disagree about equal rings, because one pipeline draws both. And the clip
  is KEYED ON THE HEAD BEING CROSSED, not on the span over the member's own onset: a real
  let-ring figure opens with a strummed pair under its own small box span, the growth split
  carries those rings into the bracket that follows, and keyed on the onset the founding rings
  drew whole across the bracket's heads. The offending ink is a ribbon crossing a head that
  stands under a bracket, so the head's own coverage is what is asked.
  **THE PAST-SPAN-END EXCEPTION (user ruling 2026-09-01).** A member whose ring extends PAST the
  end of its span ALWAYS shows its tail — the ring outliving the held shape IS the information —
  so the staircase never takes it and only the standard non-staircase rules apply to it. Asked at
  the ring's END against the same coverage authority: a covered end is a ring some span still
  carries (the fold-in laws make every ring under a span a member of it), and an uncovered end
  has outrun the figure entirely. Holds mid-span and span-final alike.
  WHAT KILLED C3: two things it could not answer. A span-FINAL long hold showed no tail at all —
  its ring ended inside the span, which is precisely the ink the bracket owned whole — and that is
  the sighting that opened this. And hidden ink made DRAWN and SCORED disagree, since
  `end_seconds` went on carrying a whole ring under a surface showing none of it; the clip puts
  the answer in the presented tail, so there is ONE end per note, both surfaces read it, and
  drawn = scored survives untouched (#142). C3's carve-outs go with it rather than being carried
  over — each answered INK OWNERSHIP, and there is no ink ownership left to except from. The
  technique exemption is subsumed by the presentation rules' own payload floor (a marked tail
  keeps exactly the length its statement needs, the protection every note already had), and
  [D2] amendment 1's travelling-span carve-out lapses with its premise: a standing mark and a
  travelling ribbon stopped saying the same thing, but a clipped ribbon and a bracket never said
  the same thing to begin with. Two exclusions survive as MEMBERSHIP, not exemption: a right-hand
  onset is a member of nothing (a tap over a held shape keeps its own tail), and a silent hold has
  no tail to clip. CONSEQUENCE FOR THE CODE, worth stating because it is a deletion:
  `chartSuppressedTails`, `ChartResolutions::suppressed_tails` and `NoteViewState::tail_suppressed`
  are GONE, and with them both draw-site tests of a per-note hiding flag — the one shape in which
  the two surfaces could have diverged about a tail. `ChartShape::covers_travel` is left derived
  with NO READER; whether a travelling span should be exempt from the clip too is open, and
  nothing in this ruling decides it.
  **THE A2 CLIP GATE — RULED 2026-09-01 (user, corpus-measured), REVERTED THE SAME DAY (user,
  2026-09-01).** The gate refined Rule B's foreign-statement cut: a contradiction counted only
  when the span FOUNDED at it STATED two or more distinct strings across its own extent —
  "states" being fretting-hand onsets plus resolved claimed stops, the carried texture never
  counting — so a walking melody's lone-string spans stopped clipping the drone beneath them,
  and a disqualified contradiction kept the scan going to the first qualifying front. THE
  REVERT'S TWO HALVES, both measured: A2 is ACQUITTED of the sighted spill — stored rings and
  spans in the sighted window are identical with A2 on or off; the spill was an intermediate
  display state, not the gate's doing — and REVERTED as UNVALIDATED anyway, because the
  population it protected (walking-melody drones under co-struck textures) was never sighted as
  a defect on real material. The law returns to the committed contradiction form above, the
  smallest law matching sighted reality, and the figure A2 would have spared is a WATCH ITEM
  (docs/tracking/watch-items.md: the clip cuts let-ring drones under co-struck walking melodies —
  a reasoned defect, never sighted). THE PRE-MEASURED REMEDY MENU, recorded here so the trigger
  never re-derives candidates from scratch: A2 itself (2,321 -> 1,521 clips, 800 walking-melody
  rings spared ~1,763 beats, 1,262 clips keeping their exact instant, 259 landing at a later
  qualifying front, the motivating figure's clips identical to the beat); the MARK ARM
  (A2-or-marked: 2,028 rings clipped; co-struck-or-marked: 1,979); and the SPAN-CREATION
  candidate, unevaluated. The rival gates stay dead by measurement and are not to be re-derived:
  the sounding-stop narrowing clipped 0 of 2,321 rings (it would have silently repealed the
  clip), and plain member-count died on the arpeggiated new shape, which states one string at a
  time. Q-B REOPENS with the revert: the load-bearing proof for the clip pass's span derivation
  was A2's own extent read, so under the committed law the derivation is back to being a
  deletion candidate carrying its proof obligation (the span-marker redesign plan holds it).
  **THE CLEAN LET-RING BASELINE — RULED 2026-09-01 (user), THE WHOLE LET-RING LAW.** The user's
  three rules, verbatim in substance, replacing everything layered since the checkpoint: (1)
  let-ring notes import at their true WRITTEN duration, ties combined into a single note; (2) all
  let-ring notes in a sequence extend with no upper bound, EXCEPT the last note, which has a hard
  cap where it would no longer be audible in Guitar Pro; (3) the extensions cap (a) when a
  contradiction to the current grip occurs — ALL let-ring tails leading to that contradiction cap
  there — and (b) at the end of the capped tail of the last note in the sequence. ONE FORMULA
  implements all three for every member, the last-note "exception" falling out because its
  extension is simply the one whose cut events have all passed:
  `stored = max(merged_written_end, min(first_cut_event_after_onset, region_end))`, the physical
  same-string clamp still applying on top. Rule B's region end (the tail's rest/one-bar walk) is
  rule 2's audibility cap and rule 3b in one value; the cut pass is rule 3a. TWO PINNED
  CONVENTIONS, each with a measured rival: THE GRIP IS SOUND-SCOPED, END-INCLUSIVE — a string is
  gripped at fret f at instant t while a note on it whose stored ring covers t is sounding, a
  ring ending exactly at t still counting. Sound-scoping is forced by the motivating figure
  itself (a hand-memory grip would read its 5:1+1/2 restatement of a string last fretted in
  measure 3, long silent by then, as a contradiction and wrongly cut the 5:1 ring that audibly
  rings to 6:4), and the end-exclusive reading is measurably VACUOUS (the cut event is detected
  on the statement's own string, where the clamp guarantees no ring outlives the next onset —
  zero cuts corpus-wide). A CUT EVENT is a fretting statement stating a DIFFERENT fret on a
  gripped string, both halves read through the one `statedStopAt` reader; a same-fret
  restatement never cuts, same-instant statements are judged against the PRE-instant state
  (co-struck notes never cut each other), and a ring struck AT the event's instant is never cut
  by it — the cutting note cannot cut itself. One forward pass in time order over the rings as
  cut so far; cuts only shorten and shortening only removes later events, so there is no
  fixpoint (probe-verified). SUPERSEDED BY THIS ENTRY: rule 10's span-founded clip (the clip
  pass, its event-founded-span narrowing, and the span derivation inside the import pass are all
  DELETED — Q-B closes as DELETE: the import pass no longer derives spans at all); and the
  absorbed_merge exemption on Rule B, with the divergence finding that killed it: the reference
  caps a tied let-ring note at exactly its tie end ONLY because its let-ring walk breaks on
  `Beat.hasNoteOnString` (MidiFileGenerator.ts `_getNoteDuration`), a tie-BLIND lookup that
  counts the note's own silent continuation as a string re-strike (Beat.ts populates
  `noteStringLookup` for every note, tied or not), while Guitar Pro itself audibly rings tied
  let-ring notes past the written duration (user-verified by ear 2026-09-01) — and the old
  comment's "the region collapses to exactly the merged end" claim measured FALSE for 674 of 697
  exempt rings corpus-wide. Merged notes therefore extend like any other marked note (the
  slide-out exemption stands — LAW I). The A2 entry's remedy-menu note above STAYS as the drone
  remedy. THE ACCEPTED COST, user's words: "I understand that this still has an issue with drone
  notes... I want a CLEAN baseline" — lone drones under moving same-string melodies now clip too
  (the melody's own restrikes are cut events), the watch item standing unchanged with its
  real-sighting trigger. CORPUS PRICE vs the superseded committed law, measured before the build
  and reproduced by it digit-for-digit: 828 rings shorter (−1,421.4 beats), 610 longer (+790.0),
  9,740 unchanged, across 39 files; the target figure's measures 1–8 reproduce the remembered
  picture row for row, and its second instance (measures 11–13, a different voicing) moves both
  ways — the first sighting target after the build.
  **THE CUT IS VOICE-SCOPED — RULED 2026-09-01 (user), A BUG FIX TO THE BASELINE ABOVE.** The
  user's words: "events should not cut rings in another voice." PINNED END TO END: a voice is a
  LINE of the transcription, and all three halves of the cut take the same one — the sounding
  GRIP is built from that voice's own rings (marked or not, as before), a STATEMENT is judged
  only against its own voice's grip, and a cut event caps only its own voice's marked extensions.
  The HALF-MEASURE — a global grip with voice-scoped victims — is REJECTED as incoherent: it lets
  one line's contradiction manufacture an event out of another line's sound, and an event no hand
  stated is not an event. Voice identity is the bar's voice SLOT, exactly the identity Rule B's
  region walk already chains by, so the two passes speak about the same lines instead of each
  inventing a grouping. THE PHYSICAL SAME-STRING CLAMP IS UNCHANGED AND STAYS CROSS-VOICE,
  deliberately: physics vs grammar — a restrike is one finger on one string and the sound stops
  whichever line wrote it, while a grip contradiction is not a physical event at all but the
  transcription saying a hand has moved, and one line saying that about its own strings says
  nothing about what another line's hand is holding. Everything else about the law above is
  untouched. CORPUS FOOTPRINT vs the clean baseline, identity-matched per ring: 48 rings LONGER
  (+226.0 beats), 3 SHORTER (−1.5 beats, second-order — a saved ring's extra coverage states a
  grip that a later same-voice statement then contradicts), net +224.5 beats, across 3 files,
  every one of them multi-voice; all 106 single-voice files in the corpus show ZERO change, the
  invariant this ruling predicts. ATTRIBUTION, measured by a shadow build of the old global law
  that reproduced the baseline dump byte for byte: of the 3,123 rings the baseline cut, 3,055 were
  cut wholly within their own voice, 66 by a statement in ANOTHER voice, 2 by a statement whose
  grip was also another voice's — and ZERO by a statement contradicting only a foreign grip while
  cutting its own voice's ring. So the half-measure is CORPUS-INDISTINGUISHABLE from the full
  scoping here: the pinning was decided on coherence, not on measurement, and the distinguishing
  figure exists only as a test. The target figure is unmoved — its let-ring bars are single-voice
  throughout, so measures 1–8 and 11–13 reproduce the baseline row for row. The census gains no
  red row: the four standing reds are unchanged except arpeggio spans (2,168 → 2,179), with spans
  total 23,338 → 23,314 and lone re-pick spans 1,857 → 1,833 both still inside tolerance.
  RESIDUE, accepted for now: a drone under a moving same-string melody IN ITS OWN VOICE still
  clips at the melody's first fret change, because those restrikes are cut events in the drone's
  own line; the watch item is rescoped to exactly that case and the remedy-menu re-evaluation is
  queued behind a real sighting.
  **THE SCORING RIDER (recorded 2026-09-01, DEFERRED — not implemented here).** Scoring for
  arpeggio spans is to be revisited DEFINITIVELY in the note-detection plan. The user anticipates
  possibly awarding extra points for HOLDING THE HANDSHAPE — scoring diverging somewhat from
  display, justifiable because the bracket displays that everything is held — while still clipping
  scored durations at the minimum note distance and keeping the standard short-note duration rules
  (notes shorter than a quarter scored on one duration metric). Until that plan rules, DRAWN =
  SCORED stands: the presented tail is the surface and the scorer reads it.
  **THE LANE'S HIT MODEL, RULED 2026-08-30 (user, closing review N1/W1): HEADS ARE TARGETS;
  TAILS ARE TESTIMONY.** Clicks move the caret; heads select; tails never select — clicking any
  mid-tail spot used to select a note whose onset is elsewhere ("that selection is not under the
  caret"), and the rule is UNIFORM: visible tails stop selecting too, not only absorbed ones.
  If the clicked slot lies inside hidden (absorbed) ink, that ink REVEALS while the caret sits
  within the ring — a deterministic peek keyed on the edit position, no timer, no selection
  mutation; the caret leaves and the ink hides. WIDENED 2026-08-30 (user): the peek reveals a
  tail hidden FOR ANY REASON — span suppression, the presentation earning rule's short-tail
  hiding, the margin trim — one condition: the caret sits inside the note's ACTUAL ring beyond
  its drawn ink. The warrant is authoring: techniques can be authored on presentation-hidden
  tails (which would force them visible), so "authoring here should function the same way it
  would on any tail" — the reveal must not care why the ink is absent. Honest answer to "is something here?" with the
  click doing exactly what clicks always do. The one affordance lost — selecting a long sustain
  whose head is off-screen by clicking its tail — is a SIGHTING ITEM (marquee/keyboard still
  cover it). The layout manifest's invariant is rewritten to the new truth in the same change.
  **THE SUCCESSOR CLASSIFIES LIKE EVERY SPAN, RULED 2026-08-30 (user sighting: chord slides
  into chords were becoming arpeggio on landing — "that should not be an arpeggio").** The
  constant-true class (F11-1's "honest constant") was honest only while successors never
  strummed; corollary 2 ended that world and the constant became a lie. A LANDING IS NOT A
  SOUNDING — nothing sounds there, the rings continue — so trigger (a) never applies to a
  landing-opened span (it survives untouched at its birthplace: a STRUM carrying an extra
  ringing string). A successor classifies by the ordinary triggers: interior partial soundings
  (c), inherited claims (b), right-hand onsets (d); a full restrike fires nothing. The
  chord-slide-into-chord figure is therefore BOX class end to end — boxes joined by sliding
  tails, the published picture. A NEVER-SOUNDING successor: no soundings, no trigger, box class
  — no furniture draws at all (no strum, no box; box class does not absorb, so the carried
  tails draw): amendment 2's seamless name-plus-tails picture FALLS OUT of the class law
  instead of being enforced, and the carried-tails guarantee stops being emergent. The census's
  "successors NOT arpeggio: 0" equality row inverts into a real classification census.
  **N5 RULED (a) — THE RECORD UNIFICATION, NOW (user: "I want (a) now if it is correct").** One
  record of SOUNDED membership (Fable amendment A3: claims remain the separate AUTHORED record —
  provenance-bearing, keyed by justification/sweep/supersession/faces — and the posture build
  stays two-phase, sounded stops from chains + claimed stops at emit; folding claims in would
  erase the authored/sounded distinction and is refused): the chain carries {stop, member_end,
  sound_end, extent-inert}; fold-ins write INERT chains; `stops` becomes derivable from the
  chains and is DELETED; `spanReach`
  skips inert entries (2a's "carried members are extent-inert" preserved as a flag instead of
  an absence); `covers_travel` reads the one record and finally sees a fold-in's glide (the
  absorption carve-out fires); the SPLIT and SUCCESSOR arms still ignore inert chains — 2c's
  judgment call (e) STANDS: a carried ring-through member's glide never splits the span, else
  let-ring texture would bound span structure. Two defects in two days from the two-record seam
  (R1's carried inference, N5's blind covers_travel) were the seam telling us it was the flaw.
  CONDITIONAL per the user's word: built only if it verifies correct across every consumer —
  the builder stops and reports rather than bending any ruling to make it fit. MANDATE SHARPENED
  same day (user): the verdict must identify the FULL CORRECT, SIMPLEST-IN-THE-LONG-RUN shape —
  "not a patch on an existing problem where we could still run into it later"; lighter
  alternatives win only by being terminally correct, never by being cheaper, and timing is
  execution advice, never a ground to shrink the fix. THE FABLE REVIEW RETURNED 2026-08-30:
  **CORRECT WITH AMENDMENTS** — (a) stands on better legs than the two-defects rhetoric (`stops`
  and `ring_chain` are two LOSSY PROJECTIONS of one per-string sounded-membership relation, with
  membership encoded inconsistently between them; the lighter alternatives converge back onto
  the inert chain when made correct, so the minimal correct fix and the proper design COINCIDE);
  the per-span-bit alternative is affirmatively WRONG (the fold-in site cannot know the span's
  eventual end). Amendments forwarded to the builder verbatim: A1 statementInForce MUST skip
  inert chains (missing from the brief — without it every fold-in-carrying span truncates at the
  fold-in's ring end, ~3,359 corpus figures); A2 the growth split's expired-coverage drop flips
  chains to INERT instead of deleting (else derived postures lose strings corpus-wide;
  superseded strings still delete); A3 above; A4 the stop is written at join by the three
  existing authorities, never derived per query. Expected intent-deltas pinned: a strum during a
  fold-in's glide now MERGES (the per-member law extending to fold-ins); fold-in glides set
  covers_travel; fold-in travels feed nothing in the successor arm. Timing judged acceptable,
  arguably right: sighting without the fix would contaminate the absorption verdicts the
  sighting exists to deliver.
  **W4 SETTLED 2026-08-30 (user): the ink quantity is named SUPPRESSED.** chartSuppressedTails /
  suppressed_seconds — the user's "suppressed" over the orchestrator's "covered," for a reason
  the class key itself supplies: a box-class span COVERS its members' rings yet suppresses
  nothing, so "covered" would lie for every box; "suppressed" names the decided ink fact, and
  it was the user's own word for the rule throughout. SpanCover and covers_travel keep their
  names (the cause family finds the cover; the output states the suppression); "absorbed
  landing" remains edge (e)'s term, now unambiguous.
- Inside a span the furniture owns member sustains: tails absorb [U] EXCEPT technique-bearing
  tails (the tail is the canvas its marks live on — forced, not stylistic); remainder rings past
  the span end draw as ordinary tails [U]. The engraving analogue: a chord carries one stem per
  voice, not one per string. Absorption is INK ownership only — it never trims presented
  sustains (the derivation's readers keep seeing the sound; D3 rider).
- Outside spans, crossing tails are simply TRUE (truth-first let-ring — denser than the published
  dashed span, and MORE truthful; same-lane overlap is structurally impossible because of the
  ring bound). Quieting is deferred to the post-re-import sighting [G3].
- Silent members present no head and no tail anywhere; selection traces the bracket. **This is
  its own rule, not "the E25 family"** (expert's label finding): E25 hides a damped SOUND's tail;
  the silent hold presents nothing because nothing sounds at all. One label over two rules is the
  one-word-two-meanings defect class.
- The tap-harmonic tail extends from the TOUCH position; the satellite carries the stop. (The
  display-emphasis question — published tab is stop-primary where our head is touch-primary — is
  flagged for ui-design-expert, record unchanged.)
- Pending statements are visibly pending (the ghost head through the honesty gate); the Alt
  reveal is the universal actual-truth escape.
- **Surface-parity obligation, elevated**: the claim's face has no signed 3D story (G4/G8), and
  the game-side box-chain walk must realign to the interior classification (old F10 rule
  superseded). Under this law those are OBLIGATIONS the signed rules create, not optional
  cleanups — the C-family is gated on them.

## LAW V — VERBS ACT ON THE SCOPE'S STATEMENT.

One scope authority: the selection, else the armed caret's slot, else inert (`chartVerbSlots`).
One meaning per verb per occupant: N governs the silent-hold channel — author / convert (fret
preserved: the finger never moved) / restore / arm held entry. Bare digits retype the record's
own fret; prefixed digits state the named channel; one pending-entry state, three roads in
(keyboard stop, satellite click, N). Keyboard stops mirror displayed digits in display order.
Plans are atomic over their product; the sweep rides the entry (LAW II at edit time).
Open verbs recorded, not judged: #116 move-gesture, #117 P2, W10 tie verb, #65 rebase.

---

## Expressibility — the musical completeness verdict

The expert swept 41 standard guitar/bass figures (strums, broken chords in every form, two-hand
tapping, all four harmonic families, the full bend/slide/vibrato space, legato, palm-mute, dead,
ghost, slap/pop, scrapes, rakes, drones, position statements, whammy) against store/derive/
display on both surfaces. Verdict: **complete except one genuine gap and four undeclared
scope edges.**

- **OK everywhere it matters**, with several rules judged *more* precise than print: two
  independent mute flags where tab conflates the X; ghost-vs-dead disambiguated; the natural
  harmonic's node snapping being the physics itself; derived legato direction beating MusicXML's
  storable-stale pairs.
- **The one gap, CLOSED at the walkthrough** ([D5], ruled and implemented): the trill —
  initially judged a third noise texture needing a field. The user's question overturned the
  framing: GP's trill IS alternation with hammer-ons/pull-offs, so it spells out at import
  through the existing legato vocabulary. No field, no drop, nothing left uncounted.
- **Deliberate and honest**: semi-harmonic (counted nearest-technique), feedback (counted
  unsupported), whammy (planned, vocabulary firewall held everywhere read).
- **Undeclared edges needing one line each**: staccato (dropped as dynamics but it is also
  DURATION, and the stored ring then overstates the sound — the one import path that knowingly
  writes a ring the source contradicts) [D6]; strum direction [D7]; vibrato depth (GP's
  slight/wide collapses to one bool) [D8]; non-string acts — volume swells, fades, golpe — which
  have no (position, string) home [D9].
- **Recorded so nobody "fixes" them**: barre-under-legato continuity is correctly UNSTATABLE (the
  span splits at the run's first new fret, as print practice also restates; FHP carries position
  continuity; the resolver's Pull clause already knows the released fret) — no new field. The
  rejected simpler notations stay rejected: let-ring as a stored mark (the ring IS the record; a
  mark would be a second authority), the ring-through region (F5), stored legato direction, a
  third mute state.

## The dead list, carried forward (do not resurrect silently)

F1 separate hold-marker array. F2 stored hold durations on silent notes. F3 unjustified
zero-sound spans. F4 the plant-offset convention. F5 the ring-through region. F6 classification
from raw overlap, the pure ring-discriminator, and the pure interior-kill. F7 the #118 arpeggio
display flag. F8 auto-extend on N. F9 the template-as-source substrate. F10 the old
"singles/chugs don't break the chain" display rule (superseded — the game-side walk realignment
is the obligation above). Neither analysis found a reason to resurrect any of them; the expert
independently confirmed F5's and F9's kills from the physical layer.

---

## Decisions the analyses surfaced — the user's list

Ordered by build impact. D1-D4 gate the signed-unbuilt package; D5-D10 are independent.

- **[D1] RULED 2026-08-27 (user: option b) — the mid-chain "still held" claim is unstatable;
  DELETE is the record; N refuses with a voice.** The original finding is RETRACTED: its
  premise (the product claim staying in the standing span as a redundant restatement) has an
  unreachable precondition — the note being converted is itself the bound on its predecessor's
  ring, so no crossing ring can exist at its slot; the shipped guard is correct in every
  reachable case. What the walkthrough then found and ruled, grounded by a music-notation-expert
  review (Fable, 2026-08-27): the repeated-chord member figure has THREE intents with three
  records — (1) *string simply not played*: **Delete**, the smaller strum IS the record —
  published notation's own economy (omission states it; MusicXML `technical`, SMuFL's guitar
  range, and GP all lack any silent-persistence construct — searches named in the review; the
  only published persistence ink, Stropes' duration lines and classical barre/guide-finger
  marks, is fingering-layer CONTINUATION anchored at the original statement, never a fresh
  mid-figure record); (2) *previous note keeps ringing*: **Shift+L tie (W10)** — merged ring,
  interior subset sounding, whole-span arpeggio via B6c; W10 is now load-bearing for the
  sustained-repeat figure; (3) *finger stays down, string unpicked*: **unstatable mid-chain by
  this ruling** — the claim is the default stated aloud (physically true in damped context and
  true of every chug member between strokes, hence informationless; physically marginal
  undamped), the same statement the model already rules unstatable twice (barre-under-legato,
  finger-outlasts-ring). Span-START claims are untouched — the bracket remains the licensed
  posture statement at its statement. Consequences: N's refusal there gets a MESSAGE naming
  Delete (folds into W5's counted-feedback surface — W5 gains weight); the generalized
  persistence-redundancy sweep proposed en route is REFUTED (it over-sweeps genuine statement
  boundaries — an open->palm-muted transition restates the whole frame, silent members
  included); the mechanism realizing the refusal (claim-stated stops folding into the merge
  comparison vs widening the sweep's redundancy to the standing statement at the slot) is
  pinned at the B6c build — one authority, no verb special case — and that build must also pin
  merge-vs-split for claim-carrying subset slots explicitly (the signed text says no-ring
  subsets split). Flagged to ui-design-expert: the span-start bracket digit's polysemy (silent
  vs carried member, disambiguated only by the incoming tail).
  **STAGE 2b: THE MECHANISM PINNED 2026-08-28, both halves.** Merge-vs-split: the member's own
  ring is the whole selector — no ring crossing the claim means the articulation differs, rule
  11 splits, the claim founds the new span's statement, one-slot arpeggio via trigger (b), the
  sandwich tested end to end (BOX / ARPEGGIO / BOX, claim surviving the settle). The REFUSAL:
  realized by ZERO new code — with the ring crossing, the slot merges, the claim lands on a
  stated string, the close publishes no reach, and the existing sweep takes it; the editor
  guard then refuses, probe-proven end to end through planToggleSilentHold. Neither candidate
  mechanism was built: both would have been a second rule stating what the close already
  states. Bonus enforcement the review forced: the growth law now fires at SOUNDING slots too
  (a claim on a new string authored mid-chain splits and dates its face where it was authored,
  never back-dated — zero corpus delta; imported claims exist only in the three roll beats).
- **[D2] RULED 2026-08-27 IN THREE ROUNDS, FINAL — travel splits, and a landed grip re-opens.**
  BUILT — the STAGE 2c stamp at this entry's end. Round 1 (user: "Travel splits."): a member's fret travel splits the span at the
  departure — the last moment its fret channel states the posture's stop before a differing
  statement (when the first differing statement is the first fret-stating keyframe, the
  departure is the onset itself and the span floors at the strike like every crowded close).
  Round 2, KILLED: the traveling-box / grip-continuation model — one span resolving to two
  dictionary entries as it moves is the two-authorities defect (user: "when the template needs
  to change I would really think the span would change too"). Round 3, the FINAL form (all four
  edges ratified): **the span splits at the first departure; a SUCCESSOR span opens where the
  split span's travels have all landed and two or more members ring on at stated stops.** The
  successor's members are the arrived rings (carried), so it classifies arpeggio by the signed
  carried-at-onset trigger, wears bracket digits stating the landed grip, absorbs the arrived
  tails (plain rings post-travel), and takes the new dictionary name; the travel between the
  spans draws as the members' sliding tails — the published chord-slide picture (two fret
  stacks joined by parallel lines) in project furniture. The first restrike of the landed shape
  opens a full non-repeat box by rule 11 (the bracket span never strums), repeat boxes chaining
  after — derived, not ruled. Ratified edges: **(a)** the ONE-finger slide takes the same rule
  by symmetry — the landing grip is a different chord too, so the successor brackets and names
  the new voicing (supersedes round 1's "nothing re-opens"); **(b)** a slide directly into a
  restrike opens no bracket span — the arrived rings end at the restrike and the strike's own
  full box states the new chord (the bracket appears only when the landing breathes); **(c)**
  staggered landings open no successor — truth stays in tails; registered as a WATCH ITEM in
  docs/tracking/watch-items.md with the last-landing widening as its remedy; **(d)** unequal
  travels landing together (voice-leading slides) are included — no parallel-delta condition;
  the bracket shows whatever grip landed. Build shape: the walk gains keyframe reading (the
  split) and the successor-opening arm — statements it already has, no format change; the
  census gains a third counter (spans with all-member travel and a breathing landing).
  **STAGE 2c BUILT 2026-08-29** (committed mid-review on the user's word; the adversarial
  review's remaining findings walk in follow-up commits — the ledger is task #138). The split is
  the continuity law reading a SECOND channel: `memberStatementEnd` = min(ring end, departure),
  one reader at the strike and the landing, so the merge, the growth split, and the lone re-pick
  all inherited travel with zero new branches. The successor is the growth split made at a
  moment inside a ring; classification FELL OUT (the successor strikes zero strings of a shape
  sounding >= 2 — trigger (a) at its purest, the one comparison at construction). One piece of
  walk state the ruling said would not be needed exists and awaits the user's word:
  `pending_landing` (the departure usually closes the span long before its landing, so the grip
  must survive the close; without it, zero successors derive corpus-wide). Census: 779
  successors, all 779 arpeggio (a CrossCheck row pins the equality); edge (b) suppresses 698
  (glide-into-restrike dominates); edge (c) staggered = 7 corpus-wide (the watch item priced);
  10 landings absorbed under a standing statement (unruled — review F9's open disposition).
  THE F1 FIX rode into this commit (user-ruled 2026-08-29, "simpler model is a sign it is
  correct"): `statedStopFrom(note, from)` is THE one reader of "what stop does this note state
  at this offset" — the old reader's stop PARAMETER was itself the restatement and is deleted;
  four authorities collapsed to one at four sites; the fold-in asks at the slot's own offset, so
  a carried string wears its landed fret and a MID-GLIDE string folds into no posture at all (a
  departed statement is out of force). Census: 18 carries re-measured to landed frets, +6 spans
  whose merges are correctly refused across a hand move, successor section byte-identical. A
  latent defect closed by the same unification: a growth split inheriting a departed stop
  could hand the old reader a stale stop and get a bogus arrival; the new reader answers
  landed-or-skip (no corpus instance).
  **[D2] AMENDED 2026-08-29 (user) — the split moves to the LANDING, and successors open
  SEAMLESSLY.** Two rulings from the first sighting of the built arm (a real song: the onset box
  drew, the extent did not, the transit a span-free zone — "having the initial span 0 length
  almost feels more awkward than having it cover that transient state").
  AMENDMENT 1 — THE TRANSIT RIDES THE PREDECESSOR: a span covers its members' travel and splits
  at the LANDING, where the new statement is established, not at the departure where the old one
  was last spelled. Physical warrant: a chord slide keeps the fingers planted — the rings run
  continuously — so the CONTINUITY LAW itself covers the transit; the departure split was an
  early cut into what [D3] already stated. Consequences: the zero-length span ceases to exist
  and "every sounding span is strictly positive" RETURNS as an invariant at full strength; the
  tapped-chord-slide classification hole closes at the start slot AND mid-travel; the span-free
  authoring dead zone closes (a hold authored mid-slide attaches to the covering span); the
  highway extent glows through the slide. Statement and coverage are TWO FACTS, the RingChain
  pattern: for merging and growing, a statement still ends at its departure — a mid-travel strum
  merges into nothing — while coverage runs to the landing, each fact one law. Staggered
  landings (edge c): spanReach over landing-extended member ends — the earliest landing ends the
  span, the successor still refused, the watch item unchanged. Edge (b) unchanged in intent
  (landing directly into a restrike: the restrike's own full box, no successor); its mechanism
  re-derives under tiling, and review F3's margin question folds into that re-derivation.
  AMENDMENT 2 — SEAMLESS SUCCESSOR INK (reverses round 3's "wears bracket digits"; stage 3
  scope): a landing-opened span draws NO opening mark — no bracket bars, no digit stack. The
  continued tails and the chord NAME changing at the landing are the whole statement; the first
  restrike still opens its full box by rule 11. The user's conditional ("if the same fingering
  is held") DISSOLVED under their own argument: a slid-into grip cannot have changed fingering —
  the fingers never lifted — and every successor is slide-opened by construction, so the rule is
  unconditional. The same argument is recorded as a CONSTRAINT for the chord-dictionary world
  (#118): a slide-opened template inherits its predecessor's fingering, making the lie
  unrepresentable. Deliberate divergence from the published two-stacks picture (no second digit
  stack) — a sighting item. Until stage 3 builds, successors keep drawing brackets — interim
  surface divergence, accepted.
  **REBUILT 2026-08-29, the amendment delivered in full.** `pending_landing` is DEAD (a hand-off
  step, `settle_landing`, replaces the parked state; no field outlives a span); coverage_end =
  min(ring, onset + arrival) is the ONE travel bound, so edge (c)'s earliest-landing extent is
  the min itself, no clause; the departure survives only inside `statedStopFrom`, where mid-glide
  silence — not a stored second bound — keeps a travelling member unrestated. Edge (b) reads NO
  display margin (review F3 dissolved by deletion): a member is kept on "rings past the landing,"
  and the landed grip's own moment is the close's question, answered once at emit — a span no
  EVENT states (`last_stated_beat` empty, only ever a landing successor) is not emitted when the
  trim leaves it no length; edge (b) and the outrun landing are one law. The strictly-positive
  invariant is PROVABLE, not checked. Census: zero-length spans 1477 -> 0, outrun landings 789 ->
  0, spans covering their travel 1488 of 1494, successors 854 (F8's at-slot blindness fixed,
  at-slot successors structurally 0), absorbed landings measured 12 (edge (e)'s registered
  population), F5 claims-ride census-silent (roll claims sit on no slide), F7's ride +7 spans,
  `last_strum_beat` GONE (F11-2 — `last_stated_beat`, honestly optional). Per-member mid-travel
  judgment landed as `restatesShape` (the user's open-strings figure: one span, restrikes riding,
  tested with its foreign-stop control). Carried finding for a later walk: the continuation test
  is one law at two widths (`lone_repick_continues` / `restatesShape`, ~40-line unification
  changing two out-of-brief behaviours — the recurring-defect shape, deliberately not taken
  mid-rebuild); and F7's carried-chain detection is a structural inference (a chain written by a
  note beginning before the span), true today, named at its site.
  REVIEW F5 CONFIRMED 2026-08-29 (user: "confirm ride"): a silently-held finger is one of the
  fingers that slid or stayed, so the predecessor's un-superseded claims RIDE into the successor
  exactly as the growth split they mirror carries them. The isolation hunk stays for
  traceability; the reversal option is closed.
  **F7 RESOLVED + AMENDMENT 2 REFINED 2026-08-29 (user).** The lone re-pick RIDES the successor
  as it rides every span — the (ii) law; the 2c blocker was emergent, one identity comparison
  doing two rules' work, and the rules separate: "the bracket span never strums" survives as the
  full-restatement rule alone. THE INK FOLLOWS THE SOUND: at the landing, no mark (name + tails);
  at the first INTERIOR sounding, the arpeggio bracket anchors there — grip digits stated, the
  struck digit among them, carried tails traveling through the bars (the existing carried-digit
  convention; the deferral invents no mark); a FULL restrike wears the full box at its own onset
  wherever it falls (a box means sounded-whole and never moves to a partial sounding); a
  successor that never sounds interiorly draws no bracket at all. The deferral keys on HOW THE
  SPAN OPENED — landing-opened spans defer their bracket to the first sounding; claim-founded
  spans keep their start bracket, because there the start IS the statement, not a continuation.
  Stage 3 scope, with amendment 2.
  **C3 RE-RULED 2026-08-29 (user, on first sight of the built absorption) — ABSORPTION IS KEYED
  ON THE CLASS.** (SUPERSEDED 2026-09-01 by the bracket law in LAW IV: the class key SURVIVES —
  the bracket acts, the box does not — but what the bracket does is CLIP its members' tails at
  the next onset, not absorb their ink. Everything below about carve-outs and hierarchy went with
  the absorption.) The bracket absorbs; the box does not. A member ring covered by an
  ARPEGGIO-classified span suppresses its tail ink (the rails own the ring statement — this is
  where the sea-of-tails flood lives, since let-ring carries are what flip the class); a member
  ring covered by a BOX-classified span draws by the ordinary presented-tier rules, whose
  existing kept-sustain earning already gives exactly the user's "tails on any chord 1/4 note or
  longer" — NO new threshold constant. The key is the COVERING SPAN's class, never the
  sounding's shape: a subset chord restrike inside a bracket suppresses; a full restrike is its
  own box span and draws under it. Carve-outs stand above the key: a TRAVEL-covering span
  absorbs nothing (the slide picture needs its ringing strings — the open-string phantom-tail
  bug, fixed same day), and a landing-opened successor's carried tails draw (amendment 2: the
  continued tails ARE the statement). Hierarchy: travels and landings draw; static brackets
  absorb; boxes defer to presented. RECORDED ALTERNATIVE, needing serious 3D UI design thought
  before it could be attempted (user's words): some form that keeps bracket-covered tails
  visible without flooding the scene — parked, revisit only from a sighting. The user also
  corrected the orchestrator: the Alt reveal shows the ACTUAL stored ring, not the presented
  tail, so it cannot preview absorption-off looks — sighting the post-clip tails needs the real
  render.
  **F9 RULED 2026-08-29 (user) — EDGE (e), the absorbed landing, ratified narrow.** ONE SPAN AT
  A TIME IS DEFINITIVE (user's word). The user's own growth-then-slide figure resolves cleanly
  (strike over a ringing span joins by the growth law; its slide tiles by amendment 1; name
  change at the landing, no box until a strike) — the residue is only the FOREIGN statement
  arriving MID-TRAVEL (cross-voice figures): the travelers' statements are out of force for
  growth (amendment 1's two-facts law), a mid-travel string folds into no posture (F1), so the
  traveling span's coverage truncates at the foreign onset and its landing arrives under the
  standing span — opening nothing, NO name change, the landed grip living in its tails alone.
  DISPOSITION: truth stays in tails (edge (c)'s), registered as a watch item carrying the
  rebuild's measured population, with the remedy recorded (the landing GROWS the standing span
  — the growth-split treatment scoped to this one figure) if it sights wrong. IMMEDIATE
  SIGHTING REQUIRED (user: "a real funky one... one song this happens in and it currently looks
  ODD after importing") — the user sights the known figure as soon as the rebuild lands, before
  the package proceeds.
  REFINED same day (user): the known song's figure is NOT edge (e). Its mid-slide restrike is of
  OPEN members of the sliding shape itself, and an open channel never departs — those stops are
  IN FORCE, so the restrike is an interior subset-sounding (trigger (c)) that chains through: ONE
  span covering slide and restrikes alike, arpeggio by (c), splitting only at the landing.
  Mid-travel soundings are judged PER MEMBER, never per slot: a sounding at an in-force member
  stop rides; only a contradiction (same string, different fret) or a NEW stop truncates — and
  edge (e)'s residue is exactly that foreign new-stop arrival, nothing more. The sighting gate's
  expected picture flips accordingly: the known song should read as one continuous span with the
  open restrikes riding inside and the name changing at the landing.
- **[D3] RULED 2026-08-27 (user) — THE CONTINUITY LAW** (stated in full at LAW III's Extent
  bullet). The user found it by correcting the analyses twice: the clipping that manufactures
  inter-strum gaps is DISPLAY-only (stored chug chains run strike-into-strike, adjacency legal
  at chart_rules.cpp:319), and a genuine stored gap is an AUTHORED statement of detachment, not
  mere acoustic silence — which neutralized the convention objection (persistence ink is
  author-bounded, and the rings ARE authored) and dissolved both the min-extent intersection
  and the class-scoped evidenced-reach restructure. Both prior models are dead: min-extent as
  a separate rule (now the law's box case) and posture-outlasts-sound (the review had already
  refuted its rationale). AMENDMENTS APPROVED with the ruling: (1) the (ii) lone re-pick
  narrows — an ADJACENT re-pick is continuity itself (the re-picked string's own stored
  adjacency is the evidence; the presented-ring witness machinery becomes unnecessary; the
  claim arm and stillHeld untouched), while a GAP re-pick no longer continues — GATED ON THE
  CENSUS like D4: price the gap-re-pick-under-witness population, today and post-let-ring-
  import, before the build commits to the narrowing; (2) D2-final's "absorbs the arrived
  tails" re-ratifies as absorbed-while-continuous — the landing successor ends at its first
  arrived gap, survivors as remainder tails. Riders recorded here because the review found
  them unrecorded: carried members extent-inert (LAW III), and the C3 absorption law builds as
  INK ownership only — presented sustains are never trimmed for it.
  **STAGE 2a BUILT 2026-08-28 — the continuity law shipped and adversarially reviewed.**
  Per-string member chains, spanReach = min, one statementInForce authority replacing
  stillHeld AND the (ii) presented-ring witness (deleted outright — the narrowing fell out of
  the law with no new code). Census proof: interior-gap spans 706 -> 0; spans +1340 (the gap
  fractures, the law's intent); trigger-4 byte-identical; the claim arm held at 38 exactly.
  THE GATE PRICING, recorded: 8 gap re-picks under witnesses pre-law (all sound-witness, zero
  claim), matching the 313 -> 305 span delta exactly; the post-law rig's residual 3 is its
  own slot-reading, not walk continuations. RULINGS LANDED IN THE REVIEW WALK: F2 — the
  statement CHAINS THROUGH a tap on a member string (user: the tap ends the tail underneath
  WITHOUT the hand lifting — the sound was replaced, not silenced; the builder's dead-code
  warrant refuted and replaced; pinned by a discriminating two-hand-run test), with the
  same-fret tap ruled INVALID BY CONSTRUCTION (#139, enforcement design queued); F1 — sounds
  CONTINUE a chain, only MEMBER soundings WRITE one (a tap's ring never decides extent; three
  probe tests, each verified discriminating); the justification rule pinned both ways — it
  decides whether a zero-sound span EXISTS, never how far it runs — with the tap-harmonic's
  ring-as-extent flagged as an OPTIONAL FUTURE RULING (physically the harmonic's ring
  witnesses the held fret staying down; a new rule with a corpus price, deferred to
  sighting). SIGHTING ITEMS: the fractured broken-figure look (detached plucks past a span's
  first gap standing as bare unbracketed notes — user: the logical outcome, eyes needed);
  and the D11 dead-rake sighting item is CLOSED, probe-verified (the rake now derives as one
  whole span — the old witness needed drawn ink, stored adjacency needs none).
- **[D3 ADDENDUM 2026-08-28] THE STANDING ALTERNATIVES for the fractured-figure sighting — three
  poles, all held OPEN by the user's word ("something to think about IF the sighting round goes
  poorly"); nothing is refused, the sighting is the gate.** If the fractured broken-figure look
  sights wrong, the argued field is:
  **(1) The current model stands** — continuity-bounded extent. Its coherence was argued as a
  theorem pair: brackets are evidence-bounded (a bracket never asserts more than sound proves, so
  no lie is drawable and no manual cut-short mechanism is needed), and C3's tail absorption is
  truthful exactly BECAUSE of continuity (hiding a tail is honest only where the extent already
  states it).
  **(2) The point statement** — a "plant this shape" mark at the figure's start (the
  fronted-claims machinery half-contains it): states the instructional fact without asserting an
  extent, so it cannot over-claim, needs no cut, and absorbs nothing it would have to lie about.
  Does not buy the clean-span tail compression — by pole (1)'s own argument, nothing gap-shaped
  honestly can.
  **(3) The fully-authored span model** (the user's four-point statement of it, tabled whole):
  spans manually authored over chords and over notes, templates manually defined, durations
  manually set, membership VALIDATED against the template (authoring fails when a covered note
  does not fit). Argued gains: gap-tolerant handshapes become trivially authorable; the span is
  an instructional statement independent of acoustics. Argued costs, pinned so the sighting
  weighs them with open eyes: the shape is stated twice (template + notes) and kept agreeing by
  a validator — an error workflow policing a border the derived model closes by construction
  (illegal states unrepresentable); manual duration is a span-scoped assertion sound cannot
  check, with upkeep on every edit; and the derivable population (23,353 corpus spans today,
  derived free) becomes hand work — GP carries no spans to read, so imports either guess (a
  Law III violation moved into the importer) or arrive bare. Its companion STORAGE half (stored
  sustain clipped to the drawn form; tails opt-in) was argued SEPARABLE and is the weaker half:
  it reverses the actual-ring ruling, breaks strict-adjacency legato at the root (a clipped
  predecessor ring never reaches an onset), and buys nothing the presented layer does not
  already serve losslessly at read time.
  A fourth, middle pole — statement-carried justification extent (claims cover the gaps, span
  ends at the last fitting arrival) — was argued first and WEAKENED by two user counters: a
  gap-span is an unprovable span-scoped assertion needing a manual cut-short with no clean
  stored home (an authored end crosses derived-descriptors; re-statement precedence is a new law
  covering only re-plants; per-arrival opt-out fights the derivation), and tail absorption
  inside one would draw an acoustic lie. It stays on the page as the known runner-up, its costs
  attached. Nothing here is ruled; the fractured look decides which question is even asked.
- **[D4] RULED 2026-08-28 (user) — FULL ACCEPTANCE: the derivation reads the GP-playback rings
  exactly as imported, no fold-in filter.** The census dissolved both scary numbers: 60.7% of
  fold-ins carry OPEN strings (no reach claim exists in them), the fretted population's median
  distance is 3 with 95.2% within 6 frets, and the physically impossible remainder is 24
  fold-ins corpus-wide — every one a 7-11 fret stretch at the nut. The 727 arpeggio flips are
  therefore overwhelmingly TRUE statements, and the user chose the project's own established
  rhythm (the C4 precedent): playback-truth first, sight, then tune. TWO SIGHTING-GATED
  REFINEMENTS stand designed and corpus-priced, graduating only from the post-re-import
  sighting rounds, never from speculation: (1) the impossibility filter — open carries always
  fold, fretted carries fold within 6 frets of the struck shape (kills exactly the 24; the
  user's 6-fret reach reality, position-checked: all violators sit LOW where frets are widest;
  killing a fold never touches the sound, only the false posture claim); (2) the let-ring
  cap-at-marks rule — the blind cap never crosses a section mark, unlabelled marks included
  (user-confirmed real structure; fixes the 65 blind bleeds; author-approved crossings and
  rest stops untouched). Selective-marking census context: 98.6% of in-passage notes carry the
  mark, but the unmarked residue is 4.5-5x legato-enriched — transcribers lift the mark for
  the release-for-a-hammer-on figure as the exception, not the rule. Original gate record:
  restrict ruling on trigger-4 is deferred until the numbers exist: before the let-ring import
  builds, a scratch rig (the #113 hidden-tag shape, aggregates only) applies the extension
  in memory, re-derives, and reports the trigger-4 flip count plus the carried-fret distance
  distribution (the source-hygiene proxy: GP applies let-ring passage-wide without validating
  hand feasibility, so imported rings can assert holds no hand made — a fretted ring crossing
  a chord onset otherwise physically PROVES the finger stayed, and an open-string carry is a
  voicing member). Original finding, kept for the record: Any tail crossing a chord's onset on an
  un-struck string folds into the posture and classifies the span arpeggio
  (chart_shapes.cpp:407-409, shipped). Today that trigger is starved because import drops
  let-ring entirely; D1 creates 12,098 such rings (4.37% of corpus notes), and every chord struck
  over a sustained let-ring texture — pedal tones and drones under stabs are the mark's natural
  habitat — flips to a bracketed arpeggio span. The 188-flip census measured the interior rule
  only; the let-ring-era flip count is unknown and plausibly much larger, and the C4 instinct was
  that let-ring is texture, NOT span structure. Ask the census rig (#113's shape) for the number
  BEFORE the import builds, then rule: (a) accept (a ring carried into a chord's onset IS the
  shape sounding separately — musically defensible), or (b) restrict trigger-4. A let-ring
  provenance bit steering derivation is the F6-adjacent option; do not take it silently.
- **[D5] RULED 2026-08-27 (user) AND IMPLEMENTED — trills spell out at import as legato
  alternation.** The user's question dissolved the field-vs-drop framing: GP's trill IS
  alternation with HoPo (GP8 guide pp.120/128), so its faithful translation is the tremolo
  precedent one shelf over. `expandTrilledEvents` spells the run out at sixteenths — a knowing
  estimate the FORMAT forces: gpif stores only the auxiliary's absolute pitch (`<Trill>` as a
  direct Note child, no speed; verified against alphaTab's parser and writer). The first note
  keeps the onset's marks; continuations claim legato carrying only the hand-truth mutes; the
  last note absorbs the remainder and the onward tie; hammer/pull derive from the frets alone.
  Unexpandable trills stay single notes and are counted: a ring within one step, or an
  auxiliary the hand cannot reach — below the capo'd open, off the board, or the note's own
  stop; the capo'd open itself is a LEGAL auxiliary (pull off to it, hammer back). No trill
  field ever exists; the two-texture noise taxonomy stands. Corpus incidence: ZERO trills in
  all 115 files — the counters are insurance, and the first trill-bearing file announces
  itself.
- **[D6] RULED 2026-08-27 (user) — staccato imports at HALF the stated duration.** The fraction
  is no longer speculative: alphaTab's MIDI generator, the reference reimplementation of GP
  playback, plays a staccato note for exactly half its beat duration (MidiFileGenerator.ts:
  1210-1214, verified 2026-08-27), so halving is a faithful translation of what the source
  states, not an estimate. The symmetry worth naming: let-ring lengthens the imported ring to
  what sounds; staccato halves it — both marks dissolve into duration truth at import, and no
  staccato field ever exists (the short ring IS the record). Our importer's current silent
  drop (Accent bit 1, gp_score_parser.cpp:221-234, "never counts") is removed by this.
  Accepted consequences: staccato+legato marked together in GP degrades the legato claim to a
  pick through the resolver's existing counted path (the source contradicting itself, resolved
  toward sound truth — census counter for the adjacency population); halved rings often fall
  under the kept-tail bound and draw short or no tail, which is the honest staccato look.
  Implementation queued as its own commit behind the trill import (same files).
- **[D7] RULED 2026-08-27 (user: option b, with a distant roadmap rider) — strum direction is
  deliberately out of the chart record.** Direction changes neither the pitches nor the posture,
  and detection cannot verify it from audio, so it would be display-only advice riding in the
  truth record. At import, GP's marks (beat-level `Brush` with its `Direction` child, and
  `PickStroke`) drop WITH A COUNT per the Feedback precedent — backlog entry, small importer
  change. The rider, at the user's direction: a DISTANT future item
  (docs/plans/todo/strum-direction-support.md) records why this may return — teaching/learning
  charts, and notating precisely how a passage was played in exceptionally difficult charts —
  and that if it does, it enters as a pedagogy-surface datum, not a chart-truth field.
- **[D8] RULED 2026-08-27 (user) — wide vibrato is WANTED and queued; the collapse is interim
  only.** The user plans to support wide vibrato ("probably double the intensity of regular
  vibrato"); assessment: moderate-small, and the #78 re-export window makes NOW the cheap
  moment for the format touch (the change rides the re-import wave every other format change
  already forced). Queued as task #134 immediately behind the staccato commit. NAMING, from
  the standards rather than GP's house terms: the standardized opposition is unmarked VIBRATO
  vs WIDE vibrato (SMuFL: guitarVibratoStroke / guitarWideVibratoStroke; GP's "slight" is its
  own UI label for the default, and "narrow" appears in neither) — so the pair opposes plain
  vibrato to WIDE, never slight/narrow. NAMING RULED by the naming-expert 2026-08-28 (the user
  delegated the call): **"plain" | "wide" | "off"**, in-memory `VibratoState{Off, Plain, Wide}`
  (Off first: value-initialization lands on not-shaking). "plain" beat both candidates on the
  DRIFT TEST — a keyframe stepping wide->standard serializes the middle token, where "on" states
  a start that is not happening and "normal" would be serialized non-zero-slot vocabulary beside
  emphasis's never-serialized zero-slot `normal` (the same word, opposite grammar, eleven lines
  apart in the writer). "wide" was already ruled twice (the Shift+V reservation, 2026-08-12).
  Citation corrected by the same review: SMuFL's vibrato/wideVibrato glyphs live in the
  Multi-segment lines range (U+EAB2/U+EAB3), not the Guitar range. Build rulings the review
  surfaced, adopted: onset `"vibrato": "off"` is a READ ERROR (the no-"pick"-token shape; the
  writer can never produce it), while the keyframe channel accepts all three; the old boolean
  gets a RemovedSpelling row with the re-import remedy AND the keyframe ChannelRule gains its
  first remedy path; `ChartNoteFlag::Vibrato` (zero production callers) deletes with its switch
  case; consumers get one `isShaking()` classifier so nothing open-codes `== Plain` and drops
  wide notes. Verb design proposed for the user's word at build: V toggles the plain tier,
  Shift+V toggles wide (its reserved claimant), each replacing the other tier — no cycling verb.
  **NAMING FINAL 2026-08-28 (user ruling over the expert recommendation, grounded on the
  physical layer): the save values are "narrow" | "wide" | "off", in-memory
  `VibratoState{Off, Narrow, Wide}`.** The deciding argument is the user's: ordinary guitar
  vibrato IS physically narrow — a fraction of a semitone of excursion (the highway's own
  drawn depth is 0.125 semitones) — while wide is the abnormal, deliberate exaggeration; so
  narrow is an accurate intrinsic description, not a diminishing register label, and a
  physically-true description outranks the markedness convention in this project's own
  grounding hierarchy. The full candidate space was exhausted with recorded kills before the
  ruling: plain (expert's pick; declined on reading), ordinary (survives structurally,
  declined on reading), normal and standard (the user's own no-canonical-amount premise plus
  the serialization-grammar trap and the charting-standard homonym), slight (GP house term,
  diminishing at full strength), on (the drift lie and the presence/degree mix). Mechanically
  sound: narrow is a degree word and passes the drift test. Standing obligation: the
  file-formats row states that narrow IS the ordinary vibrato, never a restraint instruction.
  Everything else in the verdict is unchanged: the off-at-onset read error, the
  RemovedSpelling row and keyframe remedy path, the ChartNoteFlag::Vibrato deletion, the
  isShaking() classifier, and the importer mapping — GP's `Slight` becomes `narrow`, `Wide`
  becomes `wide`. **BUILT 2026-08-28** (39 files, all six suites green): both verbs live from
  one vibratoTierLaw template; corpus incidence RE-SIGNED 2026-08-31 to 775 Slight / 15 Wide
  note OCCURRENCES over 423 authored records (the earlier 318 / 10 counted authored elements
  from a one-level-deep scan, and a GP note pool is shared by id, so one record sounds at
  every beat referencing it); unknown widths import as narrow (presence is the shake).
  SIGHTING ITEM from the build: the 2D lane's narrow sine now draws at HALF the technique
  band (wide fills it) — the band was already full, so the doubling had to come from the
  ordinary tier yielding room; a visible change to every shipped narrow note, to be sighted
  with the wide look.
- **[D9] RULED 2026-08-27 (user) — volume swells, fades, and golpe are DELIBERATELY DEFERRED
  (may support later).** One disposition line each in the compatibility doc; golpe noted as
  hard to notate under the (position, string) key but possibly worth it eventually. No plan
  files — the disposition lines with their may-return clauses are the whole record until one
  is wanted. The carve-out (GP's roll mark) became [D11].
- **[D11] RULED 2026-08-28 (user) AND QUEUED FOR IMMEDIATE BUILD — the ROLL mark imports as
  the full fronted-claims figure.** (Vocabulary guard: GP's beat-level "Arpeggio" property is
  engraving's ROLLED CHORD — the vertical wavy line — and is called the roll throughout; it is
  not our derived arpeggio-span sense.) Discovered dropping silently during D7's element
  verification; the user ruled it a legitimate import bug, not a deferral candidate. Shape (i),
  signed: the roll is a HELD grip sounded member by member — the mark asserts the posture — so
  the importer emits the figure the model already derives end to end: the first-sounded member
  struck at the beat position, silent-hold claims at that position for every not-yet-sounded
  member, and the remaining soundings staggered over the STORED spread (gpif carries direction
  Up/Down and the roll's duration — richer than the trill), every member ringing to the beat's
  stated end. Derivation then produces one arpeggio span (silent members at onset, arrivals
  answering their claims and continuing it) with zero new rules. Degenerate rolls (spread
  unfittable, lone-note beats) stay simultaneous and are counted; spread offsets round to the
  grid quantum. Corpus incidence: 3 occurrences — the ruling is correctness of the record;
  sub-grid caret and highway-cascade consequences go to a sighting once built. Sequencing:
  builds NOW (nothing else writes the importer); wide vibrato (#134) follows it.
  **BUILT 2026-08-28**, with three schema findings beyond the ruling: (1) the roll's spread
  rides beat XProperty id 687931393 — NOT the strum's sibling 687935489 that alphaTab reads
  for both marks — verified against four independent implementations and the corpus's clean
  partition; alphaTab's reading would have imported every real roll with zero spread. (2)
  Units are 480 ticks per quarter (alphaTab's 960 is its own half-spread defect), half the
  chart lattice, so every stagger lands on the grid by construction. (3) A third slider
  exists, "Start time" (float 0..1, 0 = the roll ANTICIPATES with its last member on the
  beat) — and two of the corpus's three rolls are notated anticipating. **RULED 2026-08-28
  (user: "implementing anticipation sounds like the right move to be correct") — HONOUR IT**,
  queued as task #136 behind the vibrato build: the figure shifts earlier by
  (1 - start_time) x spread so the last member lands on the beat at full anticipation, the
  claims move with the first-sounded member's slot (the span opens where the hand takes the
  grip), rings still end at the beat's stated end, previous same-string rings yield by the
  existing bound, and starts that cannot fit (measure start, a colliding prior onset) clamp
  on-beat with the count. No reference implementation exists — every open-source reader
  ignores the slider — so the linear reading of GP's two labelled endpoints is the recorded
  semantic. The dead-rake question stays a SIGHTING item (user-confirmed). Build compositions
  recorded: a tremolo-split beat DROPS the roll, counted; the slide-chain follower gained a
  guard so a legato slide never merges away a roll's claim (sustainBoundOf's builder-side
  twin — a stated-twice pressure with no shared walk to call). Corpus-verified: silent holds
  appear in exactly the two roll-bearing files at the predicted counts; the spread-equals-
  beat roll correctly degenerates. **SIGHTING ITEM**: one corpus roll is a dead six-string
  open rake — six zero brackets, and E25's dead-tail hiding fails the witness so the span
  closes at the first arrival; whether a rake's fingers should claim stops at all is a
  question the ruling did not reach.
- **[D10] RULED 2026-08-27 (user: option b) — the converter's default ring for tail-less source
  notes is the SAME-STRING bound with the measure-end horizon**, the identical rule pair the
  let-ring import signed, so the converter and the importer share one duration philosophy. The
  months-old (a) proposal (any-string, half-cap) dies with a recorded cause: it predates the
  continuity law and is incompatible with it — its manufactured stored gap after every strum
  would fracture every converted chart's box chains, repeat boxes, and (ii) continuations into
  per-strum fragments as an artifact of an import default. Where the source genuinely means
  detachment, explicit marks say so (the D6 precedent) and the charter can always shorten.
  **VACATED 2026-08-28 (user), reopened at #78** — the ruling fails on two counts the user
  caught while weighing the horizon-drift flag: (1) the SWEEP counter-example — as a blanket
  rule it would let-ring-ify sweeps and dense cross-string runs (each note ringing to a far
  next-same-string onset), stating false texture and feeding false trigger-4 folds; (2) the
  one-philosophy coupling to the let-ring horizon was a conflation — the source format has no
  let-ring concept, that horizon models MARKED sustain, and the converter default models notes
  the source shows SHORT. The replacement frame, recorded for #78's source-format analysis
  (the reference-implementation discipline, before any rule is written): the user's TWO-LAYER
  principle — STORED sustain chosen for semantic truth (span derivation, chains, adjacency,
  per the source's own handshape/span statements; real source sustains as-is; bare short notes
  short), while the tail-less DISPLAY principle is enforced by the display layer that already
  exists (the kept-bound + C3 absorption), never by shortening storage. The fracture argument
  against the old (a) proposal stands on its own and carries forward.

Carried open items, unchanged by the analyses: G2 (witness watch — absorbed into D3's wording),
G3 quieting sighting, G4 3D lone-claim bracket sighting, G5 W10, G6 fronted-claims ergonomics at
scale (#118 apply-template), G7 the keyframe ruling bundle (#115) **plus** W9-F, W9-G, and the
disconnect's unstruck-tie default (absent from the earlier G-list; the open set is exhaustive
here), G8 surface parity (now an obligation under LAW IV). Flagged to ui-design-expert: the
tap-harmonic head's touch-primary emphasis vs published tab's stop-primary habit.

## What the analyses changed in this change set

Only mechanical truth-alignment, no rule changes: the two stale `file-formats.md` rows the expert
caught (the removed one-axis `mute` key documented as current; the `held` refusal understated as
equal-fret where the shipped rule is the travel hull). Everything else in this document awaits
the user's review; the signed-unbuilt package (interior classification, absorption, min-extent,
repeat-box meaning, truth-first tails) builds only after D1-D4 are ruled. The let-ring import has
left that list: its stage 1 is built (see the LAW I consequence above), and what remains under D4
is only the divergence question the census rig measures, not the import itself.
