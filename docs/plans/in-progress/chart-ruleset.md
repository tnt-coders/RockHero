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
- **Extent — ONE rule, two media** (collapse of min-extent + justified extent): the statement is
  in force while its sounding members all ring, or, for silent shapes, until the justifying
  arrival. Concretely [U]: **span end = max(first sounding ring's end, the last-strum floor)**,
  claims exempt. The floor already exists in the shipped close (chart_shapes.cpp:262 — "floored
  at the last strum so the box always reaches its final restrike"), so the min-extent build is a
  change of accumulator, not of machinery. Survivor rings draw as remainder tails. Corpus stake:
  1.6% of spans uneven, median gap half a beat. **[D3] carries the one unresolved clause.**
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
  voice, not one per string.
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
- **The one GAP**: the unmeasured TRILL — fretting-hand noise texture, the third member of a
  taxonomy that currently has two (pitched noise = tremolo, unpitched = pick-slide, both
  picking-hand). GP's trill mark drops today WITHOUT EVEN A COUNT, which the Feedback precedent
  makes indefensible regardless of the support decision. [D5]
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

- **[D1] The atomicity guard vs the interior rule.** Under B6c, N-converting a REPEATED chord
  member produces a claim that restates the standing stop -> the sweep deletes it -> the strum
  becomes a proper-subset sounding -> the whole span classifies arpeggio: EXACTLY the outcome
  originally asked for on the repeated-chord figure — but the shipped guard refuses any press
  whose statement the settle takes, so N blocks the gesture that now has a correct meaning. The
  guard predates the rule that changed what sweeping means there. Options: (a) N there =
  delete-and-reclassify, one entry (the guard learns that a sweep yielding reclassification
  fulfilled the press); (b) N stays refused, the flow is plain Delete on the member (same
  outcome, honest, less discoverable); (c) both refused (rejects the original figure).
- **[D2] RULED 2026-08-27 (user: "Travel splits."), UNBUILT — a member's fret travel splits the
  span at the departure.** The walk gains keyframe reading; the finding was that it had none.
  What the ruling means, stated once: the split lands where the member's fret channel last
  states the posture's stop before a differing statement (interpolation leaves the stop
  immediately after that point — when the FIRST differing statement is the first fret-stating
  keyframe, the departure is the onset itself, and the span floors at the strike exactly as
  every crowded close already does). Nothing re-opens at the travel's arrival, because no onset
  exists there — the surviving rings run on as tails, and the arrived grip is statable as a
  shape only by the charter (LAW III: never guessed). A whole-chord slide is the same rule at
  every member: the span ends at the common departure and the slide travels in the tails —
  flagged for sighting once built, not re-ruled. This closes the one breach of posture
  constancy, makes sounding holds change by keyframes exactly where silent holds change by
  restatement, and composes with the extent rule as the second way a statement ceases to be in
  force (a ring ending; a stop departing).
- **[D3] The min-extent intersection clause.** Both analyses hit it independently. A lone re-pick
  landing after the first sounding ring's end but under a surviving remainder ring is a
  continuation under today's walk and homeless under min-extent as signed; the expert adds the
  physical sharpening that ring-end is not finger-lift (sound ≠ posture is the model's own
  founding distinction), and a sounding member has NO way to state "the finger outlasts its
  ring" (a same-stop claim mid-span is swept as redundant). Options: (a) membership/continuation
  keep the ring test and min-extent (plus the existing last-strum floor) governs only the
  emitted extent — preserves rule 11's ruling with machinery already in the close; (b) strict
  min governs membership too — simpler, but re-opens the broken-chord derivability the (ii)
  ruling exists for (staggered-onset figures routinely re-pick after the first ring ends).
  Recommendation: (a), and the (ii) witness wording gains "within the span's statement" either
  way.
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
- **[D5] Trills**: (a) support unmeasured fretting-hand alternation (a genuinely new datum — the
  expert searched for a consolidation shape and certifies there is none); (b) keep unsupported
  but COUNT the drop (owed regardless — the only indefensible state is today's silent drop); (c)
  status quo. Minimum action: the count.
- **[D6] Staccato**: (a) shorten the ring at import (sound-truthful, fraction speculative); (b)
  keep dropping, accept the overstated ring with the LAW I caveat; (c) count only.
- **[D7] Strum direction**: support (per-onset datum, pedagogy value, unverifiable from audio) or
  declare deliberately out of scope. Silence is the only wrong state.
- **[D8] Vibrato depth**: widen the channel to slight/wide (format touch: keyframes, importer,
  both renderers) or accept GP's collapse to one bool. Low priority.
- **[D9] Non-string acts** (volume swells, fades, golpe): one "deliberately unsupported" line
  each in the compatibility doc's third-disposition table, unless any is wanted.
- **[D10] Ruling 7** — the converter's default ring for tail-less source notes: (a) next onset on
  ANY string capped at half the kept bound (most conservative estimate); (b) same-string bound
  like let-ring, uncapped (most physically shaped); (c) a constant. Must be settled at or before
  the #78 re-export.

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
