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
  - **Knowing-estimate caveat** (expert, record-don't-change): horizon rings (let-ring to
    measure-end [U]) and converter defaults (ruling 7, unsigned [D10]) write estimates into this
    truth field. That is the accepted cost of a chart needing a definite ring; this caveat is the
    record of it, so A-truth never reads as unqualified where import policy filled the value.
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
  (let-ring -> extended rings [U/D4-gated]; tap-harmonic canonization [U/#78]) and translate its
  spellings into these records, counting every drop (the Feedback precedent, rule 23).

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
- **Why the split laws exist** (the collapse's clearest insight): within one span the posture is
  CONSTANT BY CONSTRUCTION — every split rule exists precisely to keep it so. That constancy is
  what makes "the shape" a well-defined denominator for the subset test, the repeat-box identity
  test, and the bracket's claim. The one place the walk currently lets posture drift is [D2].

## LAW IV — INK HAS ONE OWNER. Every displayed fact draws exactly once, owned by the most specific furniture that states it.

- Brackets state MEMBERSHIP at statement boundaries: span starts only (heads for struck members,
  bracket digits for silent/carried — chord frames print once at the change, not per strum), and
  the harmonic-measured lone claim's own slot (2D satellite [S]; 3D first cut [U], sighting
  pending).
- Repeat boxes mean LITERALLY the same shape struck again identically — the simile mark's idea,
  specialized and stricter — and nothing else. Partial restrikes are arpeggio interior [U] and
  draw as plain notes under the span's rails.
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
- **[D2] RULED 2026-08-27 IN THREE ROUNDS, FINAL — travel splits, and a landed grip re-opens.**
  UNBUILT. Round 1 (user: "Travel splits."): a member's fret travel splits the span at the
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
- **[D4] CENSUS GATE SIGNED 2026-08-27 (user: "I like the census gate.").** The accept-vs-
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
  own UI label for the default, and "narrow" appears in neither) — so the pair is on/wide,
  never slight/narrow. Format spelling proposed for sign-off with the build: the vibrato
  onset field and keyframe channel take "on" | "wide" | "off" in place of the booleans, old
  booleans refused with the re-import remedy (the established pattern, absorbed by #78).
- **[D9] RULED 2026-08-27 (user) — volume swells, fades, and golpe are DELIBERATELY DEFERRED
  (may support later).** One disposition line each in the compatibility doc; golpe noted as
  hard to notate under the (position, string) key but possibly worth it eventually. No plan
  files — the disposition lines with their may-return clauses are the whole record until one
  is wanted. CARVE-OUT still open: GP's `Arpeggio` roll mark (beat-level Up/Down), discovered
  dropping silently during D7's element verification — unlike the other three it carries SOUND
  information (a roll is staggered onsets), so it gets its own discussion immediately after
  D10 rather than a scope line.
- **[D10] RULED 2026-08-27 (user: option b) — the converter's default ring for tail-less source
  notes is the SAME-STRING bound with the measure-end horizon**, the identical rule pair the
  let-ring import signed, so the converter and the importer share one duration philosophy. The
  months-old (a) proposal (any-string, half-cap) dies with a recorded cause: it predates the
  continuity law and is incompatible with it — its manufactured stored gap after every strum
  would fracture every converted chart's box chains, repeat boxes, and (ii) continuations into
  per-strum fragments as an artifact of an import default. Where the source genuinely means
  detachment, explicit marks say so (the D6 precedent) and the charter can always shorten.
  Lands with the #78 re-export.

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
the user's review; the signed-unbuilt package (let-ring import, interior classification,
absorption, min-extent, repeat-box meaning, truth-first tails) builds only after D1-D4 are ruled.
