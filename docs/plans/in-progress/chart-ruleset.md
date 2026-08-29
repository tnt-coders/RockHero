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
  REVIEW F5, leaning resolved by amendment 2's warrant: a silently-held finger is one of the
  fingers that slid or stayed, so the predecessor's un-superseded claims RIDE into the successor
  exactly as the growth split they mirror carries them — built isolated for a one-edit reversal,
  pending the user's word in the walk.
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
  one vibratoTierLaw template; corpus incidence 318 Slight / 10 Wide; unknown widths import
  as narrow (presence is the shake). SIGHTING ITEM from the build: the 2D lane's narrow sine
  now draws at HALF the technique band (wide fills it) — the band was already full, so the
  doubling had to come from the ordinary tier yielding room; a visible change to every
  shipped narrow note, to be sighted with the wide look.
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
