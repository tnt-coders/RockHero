# Technique Compatibility Matrix, and Hardening the Format Against Invalid Combinations

Status: **SIGNED AND ENFORCED.** The full matrix is signed and every rule in it lives in code: the
intra-note rules in `validateChartNotes` (`chart_rules.cpp`), the relational ones as clauses of
`resolveLegato` (`chart_legato.h`), and E25 as a rule of `presentedChartNotes`
(`chart_presentation.h`). It exists because nothing in the format or the rules prevented a note from
claiming *"tap and pinch harmonic"*, which cannot be executed together, and the direction taken was
to *"establish this full matrix then re-analyze the save file format to see it can be hardened even
more to make invalid combinations impossible as much as it can."*

> **Legato stores no direction.** `attack` reads `Pick`, `Pinch`, `Legato`, `LeftTap`, `Tap`, `Pop`,
> `Slap`, `PickSlide`, `None`, where `Legato` is the authored claim "this onset connects to its
> same-string predecessor" and `LeftTap` is the local statement. **Validation is intra-note only**,
> so the relational rows (E5, E6, E12's release half, E19, E27) are not validation rules that can
> refuse a document: they are clauses of the one resolver, `resolveLegato`, and a claim they refuse
> plays and draws as the plain pick it sounds like until a settle sweep flattens it. Where a row
> below says `Hammer` or `Pull` it names the MOTION the resolver derives (`LegatoMotion`), never a
> stored value.

**Order matters and is deliberate: the matrix settles first, the format second.** Restructuring the
type on a guessed cell means restructuring it again when the cell flips.

This is a **prerequisite for plan 40 Phase 5**, not a follow-up. Phase 5's scope already promises
"illegal-combination guards" but names only one — a node only with a harmonic, which the collapse
below made structural, so that guard no longer needs writing — and every remaining technique verb
needs to know what it must clear or refuse. A verb that authors an impossible state violates Phase
5's own rule against authoring an invalid state.

## The surface

`ChartNote` carries these technique fields (`chart.h`):

| Field | Values |
|---|---|
| `attack` | `Pick`, **`Pinch`**, `Legato`, `LeftTap`, `Tap`, `Pop`, `Slap`, `PickSlide`, `None` |
| `held` | `optional<int>` — the fretting hand's stop under a picking-hand onset |
| `palm_mute` / `dead` | two independent bools, queried together through `isMuted(palm_mute, dead)` |
| `harmonic_node` | `optional<double>` — the node position, in fret units, **and the assertion that the note is a harmonic** |
| `vibrato` | `VibratoState` (`Off` \| `Narrow` \| `Wide`) — a WIDTH axis. `Narrow` is the ordinary vibrato, `Wide` the deliberate exaggeration |
| `tremolo` | `bool` |
| `emphasis` | `NoteEmphasis` (`Normal` \| `Ghost` \| `Accent`) |
| `bend` | `double` — semitones at the onset, never negative |
| `keyframes` | `vector<Keyframe>` — `{offset, optional fret, optional bend, optional vibrato}` |
| `slide_out` | `optional<int>` — the unpitched exit's fret |

Three shapes of the model the rows below lean on. `fret == 0` means an open string. `sustain` is the
note's ACTUAL ring, not the drawn tail — what is drawn is the presented form
(`presentedChartNotes`), which trims and rests it — and a zero-sustain note still sounds. A
`slide_out` carries no offset of its own because it needs none: pressure releases off the note's
END, so its moment is the ring's end by definition and a stored copy could only ever drift from it.
The `None` attack is the silent hold, which names no onset and so sits outside this matrix; its
`sustain` is exactly zero.

## The criterion

**Forbid a combination only when it cannot be *executed*. Allow the merely unusual.**

Three reasons, all of them consistent with calls already made: the importer must faithfully carry
what sources contain, so forbidding the odd means silently dropping real data; the line was already
drawn this way for pinch-on-natural, choosing not to *support* it while leaving the door open rather
than declaring it invalid; and it matches the legato-distance decision, where the author asserting a
technique is the authority rather than us.

This has a consequence worth stating plainly: **most remaining cells resolve to "allowed,"** so the
matrix's real content is the handful of hard impossibilities. That shrinks the hardening payoff too
— fewer forbidden combinations means fewer things worth making structurally impossible — and the
restructure below should be sized against the impossibilities that actually survive, not against the
number of questions asked.

A useful sub-distinction emerged while applying it. "Cannot be executed" covers two things:

- **Physically impossible** — the motion contradicts itself (E3, E7).
- **Incoherent data** — the combination is playable but the stored value would describe something
  that does not exist (E8: a pitch offset from a note with no pitch). This is *not* the same as
  pointless, and it is still forbidden.

"Pointless but coherent" is allowed. Q1 is the worked example: palm-muting a natural harmonic mostly
defeats the technique, but the harmonic still sounds and the data still means something, so it
stands.

## Established

Each of these is either enforced in code today or physically unambiguous. Read `Natural` as "a
fret-hand harmonic — a node with a non-`Pinch` attack."

| # | Rule | Source |
|---|---|---|
| **E1** | `harmonic_node` lies inside the valid range | **Enforced**, `chart_rules.cpp`. The range check is all E1 is: with the harmonic collapsed onto the node's presence there is nothing left for a kind to disagree with, so that half of the rule became unrepresentable. |
| **E2** | `PickSlide` excludes the pitched techniques — muting, `harmonic_node`, `vibrato`, `tremolo`, `bend` — and requires the unpitched `slide_out` **terminal**, with `keyframes` as optional turnaround stops and the whole path always traveling. `emphasis` is the scrape's own technique and allowed. | **Enforced**, the `PickSlide` block in `chart_rules.cpp`. Shaped by walkthrough D2+D4: the terminal is definitionally unpitched, so it is the `slide_out` — a pitched keyframe terminal would imply a turnaround or a held landing — and an accented scrape is just an aggressively played one. The terminal needs no offset; it is the ring's end by construction. |
| ~~**E3**~~ | **Unviolatable** — `Pinch` *is* an attack, so it cannot be paired with a different one. Kept because the physics still explains the family: | A pinch harmonic is produced by the pick stroke with the thumb catching the string, so every attack that *replaces* the pick stroke — the legato motions, `Tap`, `Slap`, `Pop`, and the left-hand tap — excludes it. `PickSlide` is already excluded by E2. |
| **E4** | The two attacks that STRIKE from nowhere — `LeftTap` and `Tap` — require a positive **sounding position**: `fret` for an ordinary note, `node` for a natural harmonic | You cannot strike an open string or the nut. Amended from the original `fret > 0`, which rejected every tap harmonic (`fret == 0`, `node == 12`); see the accessor note below. **Enforced**, spelled once as `nothingToStrike` (`chart.h`) and consumed through the shared repair `flattenStrandedStrike`, which the normalizer, the importer, and the editor's plan finalize all run: validation refuses, the other two flatten the stranded attack to a pick. A `Legato` claim is deliberately NOT bound by it — whether it strikes at all is the resolver's answer. |
| **E5** | A pull-off needs a preceding note on the same string at a **higher** released fret, **still holdable at the onset** | Something must be released to sound it — and still held to release. The hold half (D13): past the kept-sustain bound (`g_minimum_kept_sustain_seconds`) a held predecessor necessarily carries a ring reaching the minimum-sustain-distance margin, so a shorter one is a proven release (`predecessorHoldReaches`). **Not a validation rule:** it is the resolver's Pull clause, so a claim it refuses reads as a plain pick rather than refusing the document. |
| **E6** | Legato direction derives from that relationship | `docs/plans/in-progress/legato-final-spec.md`. **The resolver is the sole authority**, asked per chart revision by both surfaces, the gameplay build, the reader, and the `L` planner. |
| **E7** | A fret-hand harmonic excludes `keyframes` and `slide_out` | *"A natural harmonic CANNOT be slid by definition. It is physically impossible."* A natural harmonic is a light touch at a node, not a press; sliding moves the touch off the node and the harmonic simply stops. A slide is unambiguously fretting-hand travel with no whammy equivalent, so unlike bend and vibrato below this cell has no ambiguity. **Nothing to remove:** no supporting logic was ever written. Record it so nobody *adds* support later. |
| **E8** | `dead` excludes only the **pinch's** harmonic, not every harmonic | *"Sometimes you do hold a position of a harmonic while deadening the strings and the harmonic in this case would be more positional information than it would be pitch."* A node with a hand standing on it goes on naming where that hand is once the pitch is gone, exactly as a dead note's own `fret` does, so it survives and the note stays **dead**: detection and scoring still read percussive. The one node that does not survive is the **pinch's**, because it is the one that lies off the neck (`nodeIsOnNeck`) — it records the picking thumb's graze, so it names no position the fret does not already give and asks for a squeal a damped string cannot make. The "almost muted harmonic" case is *partial* damping, which is what `palm_mute` already means, so the dead flag never has to stretch to cover it; that case is Q1 instead. |
| **E9** | A fret-hand harmonic excludes `bend` and `vibrato` — **fret-hand only, NOT pinch** | Same physics as E7: a light touch at a node cannot press the string, so the fretting hand cannot modulate the pitch. A **pinch** harmonic's fretting hand *is* pressing a real fret, so bending it works normally and a bent pinch squeal is a staple — excluding it would make a very common figure unrepresentable. This is the second cell where the two harmonic kinds need opposite answers. The exclusion is of the whole WIDTH axis, not of one width: the strip returns `vibrato` to `Off`, so a wide shake is refused exactly as the ordinary one is. |
| **E10** | `dead` excludes `bend` (but **allows** `keyframes` and `slide_out`) | Incoherent data rather than an impossible motion: a bend stores semitones, an offset from a pitch a dead note does not have. Positions survive the same test — a keyframe's fret and a `slide_out`'s target are places, not pitches, and the pick-slide precedent already treats fret data as right-hand travel. |
| **E11** | `dead` excludes `vibrato` | Completes the row: a dead note excludes every **pitch-modulating** payload and allows every **position-valued** one. Vibrato asserts pitch modulation of a note with no pitch — it stores a WIDTH rather than a magnitude like `bend`, but it describes the same nonexistent thing, and the hardening strip therefore takes the whole axis back to `Off` rather than clearing a flag. |
| **E12** | `Pull` excludes every harmonic | A pull-off sounds the string by *releasing* a finger so a lower stopped pitch rings — and that pitch rings over the full speaking length with nothing damping a node, so the result is an ordinary note by construction. A contrived arrival at a node (a finger resting lightly below the released one) has the release *damping* rather than exciting, and barely sounds. **Not a validation rule:** it is the resolver's Pull-clause node veto, so a noded note under a higher predecessor simply resolves to nothing (and the `L` verb therefore skips it) instead of the document being refused. The intra-note half — a node's own range and stop rules — stays validation. |
| **E13** | A fret-hand harmonic **allows** the hammer motion and `Tap`, and that pairing *is* the tap harmonic | A finger strikes the string over a node and the strike both excites and damps. The hammer motion is the fretting-hand form, `Tap` the picking-hand one (hold 5, tap 17 — the most common form of all). Promotes H2 to established, and needs **no new harmonic kind** — see below. **Frequency is very lopsided**: the fretting hand rapping a node is *"VERY rare. It is possible though"*, while the `Tap` form is common. Both legal; the editor should not make the rare one easy to author by accident, and it does not deserve prominent notation. |
| **E14** | `Slap` / `Pop` **allow** a fret-hand harmonic | Q7. A slap harmonic — thumb striking the string while a fretting finger rests on the 12th, 7th or 5th node — is a staple of the slap idiom, and popping over a node is the same vocabulary. `Pinch` stays excluded by E3, which already lists every attack that replaces the pick stroke. |
| **E15** | `Slap` / `Pop` **allow** every mute | Q8. `dead` + `Slap` *is* the slapped dead note, core rhythmic material in a slap line rather than an oddity — forbidding it would make slap lines unrepresentable. `palm_mute` + `Slap` is awkward, since the thumb and the palm heel want different positions, but the hand spans it, so the criterion allows it. |
| **E16** | `vibrato` and `bend` **compose** | Q9. Bend up to pitch, then vibrato on the bent note — the blues-lead vocabulary entire. No data conflict either: a bend is a pitch offset and vibrato oscillates around whatever that offset is, so neither subsumes the other. |
| **E17** | `attack` **allows** every slide payload — a legato motion + `keyframes`, `Tap` + `slide_out` | Q10 and Q11, both **dissolved** rather than decided: `attack` names the onset and the path names the sustain, so "hammered onto, then slid while ringing" is two moments, not two claims about one transition. See the separation below. |
| **E18** | `tremolo` **allows** `keyframes` and `bend` | Q12. A tremolo-picked bend is ordinary and the rising tremolo-picked slide is a standard metal figure. Consistent with H1, which constrains the *onset* to a pick while tremolo describes the sustain. |
| **E19** | A `Pull`'s predecessor cannot be a **fret-hand harmonic** — and pull-from-a-**pinch** is explicitly ALLOWED | *"Natural harmonic should be able to be followed by a hammer on, not a pull off. Pull off would be physically impossible."* The converse of E12 and a **separate cell**: E12 forbids pulling *into* a harmonic, this forbids pulling *from* one. The fretting hand is touching a node, not pressing, so nothing can be released. A pinch's fretting hand *is* pressing a real fret, so pulling off from a pinch works. **Relational** — see the ceiling. **Not a validation rule:** it is the resolver's predecessor-disqualification clause, and it disqualifies a fret-hand-harmonic predecessor for BOTH motions rather than for the pull alone — a touch holds nothing to hand over, so no lower fret can rescue a hammer from one either. Read `fretHandHarmonic` (`chart.h`) for the exact predicate. |
| **E20** | `Pinch` **requires** `harmonic_node` | **Enforced** (`chart_rules.cpp`). A pinch is picking while damping a node — the overtone that squeals is determined by where the thumb lands — so one without a node is missing data. This is what lets node presence alone assert the harmonic. |
| **E21** | `harmonic_node` > the **physical stop**, strict — the FRETTING hand's stop (`held` under a right-hand onset, the note's own fret otherwise), or the capo when that stop is 0 | **Enforced** (`chart_rules.cpp`). A node lies on the speaking length, so nothing vibrates at or behind the stop; a node *at* the stop is the stop. Generalized by D1: under the 0-means-open convention a capo'd open string stores `fret = 0`, so the stop the node must clear is derived, not stored. Generalized again with the tap-harmonic arm: under a right-hand onset the note's own fret is where the PICKING hand landed, so the stop is the claimed one — asking the note's fret refused every tapped harmonic outright, its node being twelve frets above the held stop and level with the tapped point. |
| **E22** | A **fret-hand harmonic** (`fret == 0` + node + neither tapping-hand attack) requires `node <= g_max_fret` | **Enforced** (`chart_rules.cpp`), adopted with D1. The fretting hand touches the node, and a finger on the fretboard cannot be past the last fret; this is also what keeps the derived hand window inside `g_max_fret`. A pinch (thumb over the body) and a tap harmonic (picking-hand finger) escape the bound — only the universal `g_max_harmonic_node` limit applies to them. Same discriminator as `fretFor`'s node branch, deliberately. |
| **E23** | A **tap harmonic** (node + `Tap`) excludes `tremolo` | Not executable fast enough. The model agrees structurally: the tap's damping finger *leaves* the string, so nothing holds the node under re-picking and the harmonic dies. A natural or artificial harmonic keeps a finger on the node, which is why those still allow tremolo (A.H. tremolo is "oddly actually possible"). **Enforced.** |
| **E25** | A `dead` note **draws** a sustain tail only when something keeps making noise or travelling — `tremolo`, or a slide payload | **Enforced as rule 4 of `presentedChartNotes`**: the drawn tail goes and the STORED ring stays. A dead note's damped stroke has a duration like any other, and that duration is the timing a legato claim after the cluck reads — pinning it at zero re-broke every such claim once already, and it contradicted the model's positive-sustain invariant. A dead note does not ring, so a plain muted tail drawn on one is silence pretending to be sound; the two things that legitimately fill it are repeated raking (you CAN tremolo pick a mute) and a dragged muted slide (E10 already allows the positions). Two consequences fall out for free: a muted tail therefore ALWAYS draws in the noise/travel idiom with no conditional, and nothing about the mute bounds legato — the hold test reads the ring every note carries, so a chug chained to its restrike connects and a cluck the hand left long before does not (strict adjacency). `palm_mute` is untouched — palm-muted notes ring. The editor's X and tremolo-off verbs trim nothing, because no plan ever writes the drawn form. |
| **E24** | `dead` **allows** the hammer motion, the pull motion, and `Tap` — the **muted legato** ("ghost" is reserved for the emphasis axis, D8) | Walkthrough D5. Muted hammer/pull "clucks" are standard funk and R&B vocabulary (bass especially) and dead-note taps are core percussive-fingerstyle material — executable, so the criterion allows them. E4's positive-sounding-position requirement still binds the hammered and tapped forms. The same ruling kept E10 whole: **pre-bend included in the bend exclusion** — a bend stores a semitone offset from a pitch a dead note lacks (incoherent data, not merely pointless), no source notates the muted pre-bend, and the cell reopens only on real chart evidence (D6). |
| ~~**E26**~~ | ~~A **dead predecessor justifies no connection**~~ — **WITHDRAWN.** A dead predecessor is an ordinary one. | The premise — "a deadened string has no energy to carry" — does not hold: the hammering finger supplies the energy in EVERY hammer-on, and a ringing predecessor adds continuity, not the strike; what a dead note lacks is a sounding pitch to connect *from*, a notation distinction, not a playability one. The cost that decided it: the GP importer maps every hopo destination to a `Legato` claim, so under E26 a "dead note → h" figure (the standard muted-scratch-then-hammer of funk rhythm guitar) flattened to a **picked** note — the one instruction the game actually needs from this axis, lost — and the same happened to the successor of any note deadened with X. The 3D highway draws a `LeftTap` and a hammer identically, so the distinction bought nothing the player could see. What bounds the muted cluck instead is the hold test, unchanged (E25's consequence above). |
| **E27** | A **scrape predecessor justifies no connection** — neither motion; the note after a scrape is picked | Walkthrough W14. A scrape's travel is the **pick's** position on the string, not a fretting finger's, so nothing waits at its end to release or to continue from — pull-from-a-scrape was only ever authorable by treating the slide-out's end as a released finger, and that was the fiction. **A resolver clause** (`resolveLegato` disqualifies `PickSlide` beside the fret-hand harmonic), and a net deletion: `releasedFret` lost its scrape branch (the resolver was its only reader), and the `L` assist's gesture-carrier guard lost its `PickSlide` clause (a scrape never reaches the guard — its hold is never the only blocker — so the guard is about trail-offs alone). |

**The tap harmonic needs no enum value — adding one would manufacture invalid states.** Compare what
the fields hold each way:

| | a node + the hammer motion or `Tap` | a new `Tap` harmonic kind |
|---|---|---|
| `fret` | 0 (or the stopped fret) | same |
| node | the node | the node |
| `attack` | the motion or `Tap` — **which hand struck it** | `Pick`? — nothing picks it |

The third row is the giveaway, twice over. A `Tap` kind leaves `attack` describing something false,
since nothing picks a tapped note and its only honest values are the two that already exist. And a
single kind **cannot say which hand tapped** — a distinction the attack axis draws for free and the
notation would otherwise have to invent a mark for. Worse, the kind creates a new invalid
combination (`Tap` kind + `Pick` attack) for the format to forbid, which is the class of state this
document exists to eliminate. Same shape as the whammy resolution: the distinction was already
carried by fields that exist. **The durable rule: never add a harmonic *kind* for something the
attack axis already says.**

Two consequences follow.

**E4's precondition was stated on the wrong quantity, and its enforcement needs no accessor.** It
required `fret > 0` because a hammer needs somewhere to land — but a tap harmonic on an open string
has `fret == 0` and `node == 12`, so the rule rejected the very technique E13 allows. The quantity
it wants is the **sounding position**: where the striking finger meets the string. A shared
`soundingPosition(note)` accessor was planned and then dropped, because the two consumers
deliberately ask *different* questions — the renderer's `highwayNoteFretboardX` wants an
x-coordinate (the exact node), while core's `fretFor` wants the hand's fret (`ceil` of the node, and
`note.fret` for a `Tap` attack, whose node is the *other* hand's). Writing E4 on `fretFor` would
reject the open-string tap harmonic all over again (`fretFor` returns 0 for it). The correct
enforcement form is simply:

```
note.fret > 0 || note.harmonic_node.has_value()
```

— node positivity needs no separate test because the enforced range rule already guarantees `node >
0`, and no capo variant is needed: under the 0-means-open convention a capo'd open string stores
`fret = 0`, so `fret > 0` already means a real stop.

**Do not evaluate E5 on the sounding position** — a trap that binds `resolveLegato`'s clauses
exactly as it bound validation. E5 tests that the predecessor sits at a *higher fret*. Reading it on
the sounding position would make a harmonic at node 12 followed by a pull to fret 5 **pass**, since
12 > 5, silently allowing exactly what E19 forbids. The two rules ask different questions of the
same number: a hammer needs somewhere to **land**, a pull needs something **pressed**, and a node is
a place but not a press. E19 is the rule that has to carry it — which is why the resolver
disqualifies a fret-hand-harmonic predecessor before it ever compares frets.

**"Natural" would be a misnomer for what the node means**, which is why no such value exists. A
harmonic's pitch comes from the ratio of node→bridge to fret→bridge, and fret positions are
logarithmic, so the midpoint of a string stopped at fret 5 lies exactly at fret 17. An absolute node
plus `fret` therefore determines the pitch in *every* case, and an open-string natural harmonic is
just the `fret == 0` case — not a separate reference frame. What separates a fret-hand harmonic from
a pinch is **which hand damps the node**, precisely the axis the attack already encodes. One thing
follows worth stating: a *fretted* tap harmonic is representable, since no rule requires a fret-hand
harmonic to be open.

**Where the line falls: does the technique happen at the onset, or across the sustain?** This
question decided two cells in opposite directions, so it is the operative test rather than intuition
about whether something "is a way of playing the note".

- **`Pinch` → attack.** The thumb graze happens *inside the single onset event*. One stroke, one
  moment.
- **`tremolo` → payload.** The field means *"unmeasured noise picking — as fast as possible, no real
  timing"*, with measured repetition spelled out as discrete notes instead. It is a texture across
  the **duration**; the onset is merely its first stroke, and the model deliberately never
  represents the individual strikes. Asked directly whether tremolo should be an attack, the
  instinct was that it *"feels wrong"* — correct, and the field's own definition is why. The name
  itself was re-litigated and kept (walkthrough D3): untimed as-fast-as-possible picking IS tremolo
  picking, and the settled taxonomy is **two noise textures distinguished by pitch, one per axis** —
  tremolo is *pitched* noise on the flag (the fret still sets a measurable pitch), the scrape is
  *unpitched* noise on the attack — which is why they never share a field.

The consequence for `tremolo` is that it constrains **nothing** on the attack axis, which is what
rejected H1. Its one exclusion, E2's, exists because the field comment says pick slides *"share the
noise vocabulary intrinsically through their attack, without this flag"* — so setting it on a scrape
is **double encoding, not a contradiction**. That is a second and independent argument for the
ranked candidate that moves `PickSlide` off the attack axis: the comment is an admission that a
sustain-spanning texture currently rides on the onset field.

**`attack` is the onset; every payload is the sustain.** This one line dissolved Q10 and Q11 and
should be checked first against any future "aren't these two claims about the same thing?" worry.
`attack` says how the note *began*; muting, the node, `vibrato`, `tremolo`, `bend`, `keyframes` and
`slide_out` all describe what happens *while it rings*. So a hammer-on carrying a slide path is not
a contradiction — it is one note hammered onto and then slid, two different moments. The intuition
that they collide comes from tab notation, where a slide is *drawn* as a connector between two notes
and therefore looks like a property of the transition into one; our model instead puts the slide
inside the note as a path across its sustain.

The separation also explains why `PickSlide` needs the elaborate E2: it is the **one attack whose
meaning extends across the sustain**, which is why it must own its slide payloads — the required
`slide_out` terminal plus optional turnarounds — and exclude every pitched payload. Being the sole
exception is a reason to keep an eye on it: the hardening candidate that splits the scrape into its
own variant is really a proposal to stop it from sitting on the `attack` axis while behaving like a
payload.

**The dead-note row reduces to one sentence**, though E8's amendment made the sentence finer than it
first looked. A dead note sounds no pitch, so it excludes everything that NEEDS one — `bend` (E10),
`vibrato` (E11), and the pinch's node (E8) — and allows everything that still names a **place**:
`keyframes` and `slide_out` (E10), and the on-neck `harmonic_node` (E8). That is a cleaner rule than
the five-way bundle H4 attempted, and it generalizes — but not in the form of "does this payload
name a pitch or a place", which put every harmonic on the pitch side. A node names BOTH. So the
question to ask of any future payload is which of its readings SURVIVES the deadening: a bend has
only the pitch reading and goes, a keyframe fret has only the place reading and stays, and a node
stays exactly where a hand is standing on it to be read.

**E3 is the one that started this, and the collapse retired its verb problem** — attack is a single
field now, so assigning one clears the other structurally. What replaced it is a different verb
obligation: the pinch verb must **author a node** (E20), and any verb that moves a pinch's attack
*away* from `Pinch` must clear or re-ask the node — the stored value is an off-neck graze position,
and under any other attack `nodeIsOnNeck` reads the same number as a fret-hand node, silently
teleporting the hand (a node of 24.0 becomes "hand at fret 24"). Both guards ship: the pinch verb
authors the octave at the stop when no node exists, and any attack change leaving `Pinch` (or `Tap`)
for a fretting-hand attack clears the node with it.

## High confidence, wants confirmation

| # | Proposed rule | Reasoning |
|---|---|---|
| ~~**H1**~~ | **REJECTED** — `tremolo` does **not** require `attack == Pick` | The instinct (*"Should tremolo be an attack? That feels wrong."*) exposed the flaw. H1 reasoned that tremolo picking *is* repeated picking, so an attack replacing the pick stroke contradicts it — but an attack describes only the **onset**, not the whole duration, so it never "replaces the picking". Hammer onto a note and then tremolo-pick it: the hammer motion + `tremolo`, executable and uncontradictory. H1 conflated onset with sustain, which is exactly what E17 warns against. Tremolo is **orthogonal to attack**; the only exclusion is E2's, and that one is redundancy rather than contradiction. |
| ~~**H2**~~ | **PROMOTED to E13** — a fret-hand harmonic does **not** require `Pick` | A tapped harmonic — tapping directly over the node — is a real technique, so `Tap` + a node is playable. This is the asymmetry with E3 and the reason the two harmonic kinds cannot share one rule. |
| ~~**H3**~~ | **CONFIRMED in its strongest form: `emphasis` is compatible with EVERYTHING, no exception** (walkthrough D4). | It is dynamics, orthogonal to how the note is produced — an accented scrape "would just be an aggressively played pick slide." E2 therefore does not exclude emphasis. The measured cost, accepted as-is: an accent's fade band clears the plectrum's diagonal shoulder by **0.331 px** at note height 25 against the disc's **1.560**, because the shoulder reaches 0.547 of the head against the disc's 0.500. Visually tight but real; the joint retune knob is `glow_size`, which the round head shares. |
| ~~**H4**~~ | **SUPERSEDED.** It bundled five cells under one "no pitch" argument and got two wrong. | Settled instead as: the pinch's node excluded (E8), `bend` excluded (E10), `keyframes` and `slide_out` **allowed** (E10 — positions, not pitches), `vibrato` excluded (E11, Q4). The lesson is that "no pitch" separates *pitch-valued* payloads from *position-valued* ones rather than excluding everything. |

## Possible, but deliberately unsupported for now

A third disposition beside "impossible, forbid" and "pointless but coherent, allow": combinations
that **can** be executed but are so rare that supporting them costs more than it returns. The rule
for these: keep them forbidden for now, allow one **only if it is easy**, and otherwise revisit the
whole group together.

| Combination | Why deferred rather than forbidden |
|---|---|
| **Pinch on a natural harmonic** | *"TECHNICALLY possible... But so rare I have literally never seen it."* Already impossible by construction: one note carries one `harmonic_node`, so a second, independently-damped node cannot be written — the single-node shape **is** the enforcement, and a future design wanting it would need a second node field. Do not "fix" that into a set without meaning to. |

## Resolution log — Q1–Q12

Every cell put to the user, and every cell the criterion closed on its own, with the reasoning that
decided it. Kept in question order because the *arguments* are the durable part — the rules they
produced live in Established above, and this is where to look when one of them seems wrong later.

**Mutes against pitch.** Palm muting damps but does not kill the pitch; deadening kills it.

- ~~**Q1** `palm_mute` + a fret-hand harmonic~~ — **ALLOWED.** The damping shortens the harmonic
  without preventing it, and the palm sits near the bridge while the node can be far up the neck.
  The worked example for "pointless but coherent is allowed".
- ~~**Q2** `palm_mute` + `bend` / `keyframes`~~ — **ALLOWED.** Palm muting damps sustain at the
  bridge while the fretting hand bends or slides normally; nothing contradicts and the data stays
  coherent — a real offset from a real pitch. Also common rather than merely possible.
- ~~**Q3** `dead` + `keyframes`~~ — **ALLOWED, and `dead` + `bend` is NOT.** The split falls exactly
  where the *data* does. A keyframe's fret is a **position**, which stays meaningful with no pitch —
  the pick-slide precedent proves it, since a scrape's frets are right-hand travel rather than
  pitch. A bend stores **semitones**, an offset from a pitch that a dead note does not have, so it
  is incoherent in the E8 sense rather than merely pointless. The case for allowing the slide is
  concrete: a muted hand dragged up and down under heavy phaser, which dead-plus-slide is the
  natural way to represent. `slide_out` follows for the same reason — it is a position too.
- ~~**Q4** `dead` + `vibrato`~~ — **DISALLOWED** ("Full mute vibrato makes no sense"). See E11 — it
  completes the dead-note row into one principle.

**Harmonics against articulation.**

- ~~**Q5** a fret-hand harmonic + the legato motions~~ — **ANSWERED.** `Pull` forbidden on every
  harmonic (E12); the hammer motion and `Tap` **allowed**, where that pairing *is* the tap harmonic
  (E13, promoting H2). The hammer motion on a `Pinch` was already E3. Amends E4, whose `fret > 0`
  test rejected every tap harmonic, and retires the third-kind plan. Two consequences above.
- ~~**Q6** a fret-hand harmonic + `bend`~~ — **ANSWERED, absorbed into E9** (bend *and* vibrato
  forbidden on a fret-hand harmonic in one ruling).
- ~~**Q7**–**Q12**~~ — **ALL ANSWERED by the criterion itself**, no ruling needed: every one is
  executable, and five of the six are *staples* whose forbidding would make real charts
  unrepresentable. Recorded as E14–E18. Two of them dissolved rather than resolved — see the onset /
  sustain separation above. Overturn any of them if you disagree; they were not put to the user
  because a forbid-a-staple error is costly while an allow-something-odd error is free.

## Whammy is beat-scoped, which is why E9 needed no format change

The finger-versus-bar problem looked like it blocked E9: our `bend` is a pitch offset that does not
say whether the fretting hand or the whammy bar produced it, and a bar dive on a natural harmonic is
a staple. It dissolves on **scope**, not on a ruling.

Guitar Pro stores it as **`beat.whammy`** (`gp_score.h`, parsed from the `Whammy` / `WhammyExtend`
elements in `gp_score_parser.cpp`) — *beat*-scoped, because a bar dive bends every sounding string
at once. Our `bend` is *note*-scoped. So a dive was never going to be a note payload; modelling it
as one would mean duplicating it across every ringing note. Therefore:

- `bend` and `vibrato` are note fields the fretting hand produces → **incompatible with a fret-hand
  harmonic** (E9), with no ambiguity left to resolve.
- a bar dive lives at beat scope → **there is no note cell for it to occupy**.

**The whammy feature itself is deliberately NOT specced here.** It is a whole feature — notation on
both surfaces, detection of a continuously sliding pitch, scoring, and how a dive interacts with
sustain — and belongs in its own roadmap plan (`docs/plans/todo/whammy-bar-support.md`) rather than
widening this one. What is recorded here is only the piece the *format* needs, because discovering
later that whammy wants to be a note field would mean restructuring twice.

**Already true today, worth knowing:** the importer sees whammy and drops it knowingly, reporting
*"N whammy-bar beats were imported without their bar dives"* (the whammy diagnostic at the end of
`gp_chart_builder.cpp`). So source charts in the corpus already carry data we discard — a recorded
gap with a count attached, not future-proofing.

**Tripwire:** if whammy is ever modelled as a **note** payload rather than beat-scoped, **E9
reopens**, because the finger-versus-bar distinction would then matter inside a note again.

## What the review left open

The deep-review pass swept the full cross-product and closed every recording gap it could close from
the settled criterion alone (see the rendered matrix below). What it could not close from the
criterion is recorded here, all of it since ruled.

~~**The fretted-harmonic cluster**~~ — **SETTLED as walkthrough D1** ("Adopt all of it"): a
fret-hand harmonic is exactly `fret == 0` + node + neither tapping-hand attack; `fret > 0` + node +
non-`Pinch` is the picking-hand-damped family (fretted tap, harp, artificial), whose hand placement
`fretFor` reads correctly; E7/E9/E19 re-keyed on "no real stop"; E21 generalized to the physical
stop (capo when `fret == 0`); E22 enforced. Capo'd naturals store `fret = 0` under the 0-means-open
convention — the capo never appears as a fret number.

**The capo frame — Guitar Pro is capo-relative (D9).** Confirmed by an authored experiment — with a
capo at 3, an entered "1" sounds the pitch at absolute fret 4 — corroborated by a real capo'd tab
and by the harmonic labels (the capo-1 score's 7.0/8.2 are standard open-string-family labels,
correct capo-relative). **Import therefore shifts fretted notes by the capo** (relative F > 0 →
absolute F + capo; 0 stays the open string per D1), the harmonic stop reads the shifted note fret,
and the natural-label formula (`capo + snapped offset`) was already exactly right. GP cannot even
express an absolute sub-capo fret, so imports never produce one; the sub-capo *validation* (frets
1..capo invalid) ships together with the editor verb gate — plus the template, fret-hand-position,
and pitched-glide-keyframe analogs, and the import FHP generators floored at
`firstPlayableFret(capo)` — so no verb can author what validation rejects. That floor covers a
scrape's turnarounds and every slide-out too (W9-J): every fret a slide gesture names sits at or
above capo + 1, the open string included, because the pick travels the sounding string.

**Cells closed by the criterion and recorded for the glance:** `palm_mute` + `Pinch` (the palm-muted
squeal — the single most common pinch context, previously resting on silent default-allow); `Pinch`
+ keyframes / slide_out / vibrato (a pinch's fretting hand presses a real fret); a fret-hand
harmonic + tremolo (re-exciting a ringing harmonic); `dead` + tremolo (the tremolo-picked dead note
— texture, not pitch, so the dead-note principle allows it); bend + keyframes on one note (allowed —
a coherent sequential reading exists; simultaneity is not representable as distinct data); keyframes
+ slide_out (already structurally governed: the slide-out must end strictly after every keyframe);
vibrato and tremolo on zero-sustain notes (coherent — the note still sounds).

~~**Newly recorded open cells for the ruling**~~ — **RULED (walkthrough D5/D6):** `dead` + the
hammer motion / `Tap` / the pull motion all **allowed** as E24 (the muted legato of funk and
percussive fingerstyle — not "ghost", which names the emphasis tier), and E10 stays whole — the
muted pre-bend remains excluded with the bend it belongs to, reopening only on real chart evidence.

**Relational refinements recorded (enforcement-pass material, no format impact):** E5's
"predecessor's fret" must mean the *released* fret (`releasedFret`, the ring's state at its end) or
a predecessor that slid away breaks the comparison; a `PickSlide` predecessor cannot justify a pull
(its fret is picking-hand travel — same physics as E19, and E27 is the rule); a dead predecessor CAN
(its finger is a real press, and releasing it is the muted pull — with the hold test alone bounding
it); "which note is the predecessor" is unambiguous because duplicate onsets per (position, string)
are already invalid; the hammer motion deliberately has no predecessor constraint
(hammer-from-nowhere is the left-hand tap); `Pinch` has no relational constraints at all. The E5/E19
interplay and the exhaustive invalidating-edit inventory live in
`docs/plans/in-progress/legato-authoring-model.md`.

**Transitive impossibilities, made visible so verb guards know them:** `dead` + `Pinch` (E8 + E20);
`dead` + any node-bearing note regardless of attack (E8); `PickSlide` + anything, node included
(E2). All enforced. Per-pair verb behavior is resolved by the single-gate shape: verbs skip
ineligible notes where an eligible subset makes sense (E4, E23 in the attack verb), and the shared
finalize gate refuses any plan whose saved form violates the matrix — no per-pair case-work exists
or is needed.

## The full matrix, rendered for sign-off

Legend: **OK** = allowed (default or by rule n); **FORBID En** = forbidden by rule n; **REQ** =
required; **[enf]** = enforced in `chart_rules.cpp`; **DEFER** = deliberately unsupported for now.
When a node is present, the harmonic overlay (second table) overrides the attack row. `Hammer` and
`Pull` name the motions the resolver derives from a `Legato` claim; `LeftTap` follows the `Hammer`
row, being the hammer motion with no predecessor read at all.

### Attack × payload (no node present, except the first column)

| attack | node? | palm_mute | dead | vibrato | tremolo | emphasis | bend | keyframes | slide_out |
|---|---|---|---|---|---|---|---|---|---|
| **Pick** | OK (picked natural) | OK | OK (dead note) | OK | OK | OK H3 | OK | OK | OK |
| **Pinch** | **REQ E20 [enf]** | OK (muted squeal) | FORBID E8+E20 | OK E9 | OK | OK H3 | OK E9 (bent squeal) | OK | OK |
| **Hammer** | OK E13 (rare) | OK | OK E24 (muted hammer) | OK | OK | OK H3 | OK | OK E17 | OK E17 |
| **Pull** | **FORBID E12** | OK | OK E24 (muted pull) | OK | OK | OK H3 | OK | OK E17 | OK E17 |
| **Tap** | OK E13 (tap harmonic) | OK | OK E24 (muted tap) | OK | OK | OK H3 | OK | OK E17 | OK E17 |
| **Pop** | OK E14 | OK E15 | OK E15 | OK | OK | OK H3 | OK | OK | OK |
| **Slap** | OK E14 | OK E15 | OK E15 (slapped dead note) | OK | OK | OK H3 | OK | OK | OK |
| **PickSlide** | FORBID E2 [enf] | FORBID E2 [enf] | FORBID E2 [enf] | FORBID E2 [enf] | FORBID E2 [enf] | **OK** (D4: aggressively played scrape) | FORBID E2 [enf] | OK (optional turnarounds) [enf] | **REQ E2 [enf]** (the unpitched terminal) |

Plus E4 (enforced): `LeftTap` and `Tap` require `fret > 0 || harmonic_node.has_value()` — the form
is final; D1's 0-means-open convention removed the capo caveat.

### Harmonic overlay (node present — overrides the row above)

| configuration | bend | vibrato | keyframes | slide_out | palm_mute | dead | tremolo | as Pull's predecessor |
|---|---|---|---|---|---|---|---|---|
| **Fret-hand harmonic** (node + `fret == 0`, attack Pick/Hammer/Slap/Pop) | FORBID E9 | FORBID E9 | FORBID E7 | FORBID E7 | OK Q1 | FORBID E8 | OK | FORBID E19 |
| **Open-string tap harmonic** (node + Tap, `fret == 0`) | FORBID E9 | FORBID E9 | FORBID E7 | FORBID E7 | OK | FORBID E8 | FORBID E23 | FORBID E19 |
| **Harmonic over a real stop** (node + `fret > 0`, non-Pinch — the fretted tap, harp, and artificial family) | OK | OK | OK | OK | OK | FORBID E8 | FORBID E23 iff attack is `Tap`, else OK | OK (the stop is pressed) |
| **Pinch** (node + Pinch) | OK E9 | OK E9 | OK | OK | OK | FORBID E8+E20 | OK | **OK** (E19 note) |

**D1 re-keyed the natural-only rules:** E7, E9 and E19's physics is "no real stop is pressed," so
they apply to `fret == 0` + node + any non-`Pinch` attack — the open-string tap harmonic included —
and stop applying the moment a real stop exists (`fret > 0`), where bending, sliding, and pulling
off are ordinary fretting-hand work. Note the two discriminators differ deliberately: E7/E9/E19
include the `Tap` attack (nothing pressed is nothing pressed), while `fretFor` and E22 exclude it
(an open-string tap harmonic's node belongs to the picking hand, so the fret hand is not there).
"Stop" is not a field — it is what `fret` *means*; the physical stop is derived (`fret == 0` → nut
or capo).

Universal, all enforced: node in `(0, g_max_harmonic_node]`; node > the physical stop (E21 — the
fret, or the capo when `fret == 0`); and a fret-hand harmonic's node on the neck (E22).

### Mute × payload

| mute | node | vibrato | tremolo | emphasis | bend | keyframes | slide_out |
|---|---|---|---|---|---|---|---|
| **neither** | OK | OK | OK | OK H3 | OK | OK | OK |
| **`palm_mute`** | OK Q1 | OK | OK | OK H3 | OK Q2 | OK Q2 | OK Q2 |
| **`dead`** | FORBID E8 | FORBID E11 | OK (texture) | OK H3 | FORBID E10 | OK E10 | OK E10 |

The dead-note principle: forbid everything **pitch-valued**, allow everything **position-valued**
(and textures).

Plus **E25** on the dead note's own drawn duration: a dead note draws a tail only with `tremolo` or
a slide payload — the two things that keep a dead string making noise or travelling — so a muted
tail is never merely held. The display consequence pairs with **D17** (the walkthrough): the teeth
mean REPEATED ATTACKS rather than noise, so a muted tremolo slide is "noisy travel" (teeth plus
diagonal) while a plain muted slide is one smooth drag — and a pick slide, which can never be
tremolo picked, loses its teeth and draws as the single continuous drag it is. Pitched-versus-noise
is stated at the head (the plectrum silhouette, the mute X), never by the tail.

### Relational rules — resolver clauses, not validation

None of them can refuse a document: they are the clauses of `resolveLegato`
(`common/core/chart/chart_legato.h`), and a claim they refuse resolves to `Unjustified`, which
draws, plays and scores as the plain pick it sounds like until a settle sweep flattens it. The
physics is unchanged; only what happens when it is not satisfied.

| rule | statement | status |
|---|---|---|
| E5 | `Pull` requires a same-string predecessor whose **released** fret is higher — `releasedFret`, the ring's state at its own end. A fret-hand-harmonic predecessor is disqualified outright (E19), and so is a scrape (E27 — its slide-out is the pick's position, which is why `releasedFret` has no scrape branch). Muted predecessors, palm or dead, are ordinary (E24); a dead one is bounded by the same hold test as every other note, reading the ring it carries (E25 takes only the drawn tail). The predecessor must also be **still ringing at the pull's onset** (D13): strict adjacency against its stored ring. | **resolver clause** (`resolveLegato`, judging `releasedFret` + `predecessorHoldReaches` against the predecessor's stored ring; one authority for the surfaces, the gameplay build, the reader, and the `L` planner) |
| E6 | Legato direction derives from that relationship | **the whole read model**: direction is never stored, so this stopped being a rule about data and became the resolver itself |
| E19 | No connection FROM a fret-hand harmonic (either motion — a touch holds nothing to hand over); from a pinch is allowed | **resolver clause** (the predecessor disqualification, via `fretHandHarmonic`) |
| E27 | No connection FROM a scrape (either motion — its travel is the pick's position, so no finger waits at its end) | **resolver clause** (the predecessor disqualification, on the `PickSlide` attack) |
| — | The hammer motion has no predecessor constraint (deliberate: the left-hand tap, its own stored value `LeftTap`) | **structural**: `LeftTap` resolves to the hammer motion unconditionally and reads no predecessor at all |

### Enforcement reality

The rules live once in `validateChartNotes` (`chart_rules.cpp`), which delegates every intra-note
rule to `validateChartNoteAlone` — structural refusals no repair can express, then the fixpoint that
a note equals its own `normalizeChartNote` and `savedChartNote` forms. Every editor planner funnels
its candidate through the shared `finalizePlan` gate, which validates the SAVED form via
`savedChartNote`, the one memory-vs-document seam the writer also uses; the sole deliberate
exception is `planSettleChart`, whose sweep only ever turns a `Legato` into a `Pick`. Import sheds
harmonic-impossible techniques loudly, then runs `normalizeChart` — the one normalizer every load
path calls, with `sweepUnjustifiedLegato` as a late stage — so a chart is never born invalid.

The queue that gated sign-off is `docs/plans/in-progress/technique-review-walkthrough.md`, and every
gating item there is closed: the fret-floor/capo cluster (D1), the scrape's payload shape (D2–D4,
which absorbed H3), muted legato and the pre-bend question (D5–D6), the E5 derivation-vs-validity
split (D7), the emphasis axis (D8), and the GP capo-frame measurement (D9). The walkthrough's
remaining `W` items are implementation work and display rulings rather than matrix rulings.

**Recorded future technique (no decision needed):** the side-of-pick tap — a tap performed with the
pick's edge, itself slidable — is distinct from the pick slide in many aspects and may need its own
representation later. Do not force-fit it into `PickSlide` or `Tap` when it surfaces.

## The harmonic collapse: node presence IS the harmonic

There is no harmonic *kind*. `harmonic_node` is `std::optional<double>` and its presence is the
assertion; `attack` says which hand damps it. The name keeps the word "harmonic" that a bare `node`
would have lost while staying honest about the value — `.has_value()` reads "has a harmonic node"
and `*harmonic_node` reads as a position, where a bare `harmonic` holding `12.0` would not.

Two questions replace the deleted field:

- **Is this a harmonic?** — does it carry a node.
- **Which hand damps it?** — is the attack `Pinch`. Fretting-hand damping is the **default**, what
  happens when nothing says otherwise, so it needs no value of its own.

Every case survives: a slapped natural harmonic is `Slap` + node (E14, and the "two values, one
field" objection evaporates — `Pinch` and `Slap` are *mutually exclusive* attacks, which is what a
single-valued enum wants); tapped is `Tap` + node; hammered is a legato claim + node; the two-finger
picked harmonic over a fretted note is `Pick` + fret 5 + node 17.

The wire keys are `"harmonicNode"` and, for a pinch, `"attack": "pinch"`. **The reader refuses
either retired key** — `"harmonic"` and `"touch"` — rather than ignoring it, naming re-import as the
remedy; ignoring would have loaded an un-reimported package while silently dropping every harmonic
in the chart. It is a tripwire, not compatibility — delete it once the corpus is re-imported
(`docs/tracking/backlog.md`).

The render predicate is the attack-only `nodeIsOnNeck` beside an inline `harmonic_node.has_value()`
at each call site. The inline spelling is deliberate: it keeps the dereference visible to
`bugprone-unchecked-optional-access`, which cannot see through a wrapper — which is why no
`isHarmonic` presence wrapper exists.

**A pinch always has a node.** The reasoning that a pinch "may legitimately carry no node" — Guitar
Pro's `HarmonicFret` being a separate optional property, and players not aiming for a particular
node — is wrong on the concept and wrong on the data. A pinch *is* picking while damping a node, so
the overtone that squeals is **determined** by where the thumb lands; and across a **118-file Guitar
Pro corpus, all 207 harmonics carry a `HarmonicFret`, every one of the 56 pinches included**. The
`std::optional` in the parser is defensive coding, not evidence about GP's data. So a pinch requires
its node (E20), `chart_rules` enforces it, and node presence alone asserts the harmonic.

**One guarantee is traded, not gained.** A `Harmonic { Kind; node }` bundle would have made the node
required for a pinch **structurally**. The collapse cannot, because the attack is set independently
of the node — so the guarantee exists **by rule instead** (E20, enforced), which is weaker than
unrepresentable but is a guarantee all the same. The collapse trades structural-for-rule on that one
requirement in exchange for deleting the enum, the disagreement state, and E3.

**A consequence of pinch living on the attack:** switching a note's attack to `PickSlide` destroys
its pinch-ness, where a separate harmonic field would have kept it latent in memory for a "switch
back and restore" behavior. That is inherent — one field cannot hold two attacks. Recorded so it is
not mistaken for a regression later.

### The node's range, and how high a harmonic to support

Range-checking the node against `g_max_fret` conflates two quantities: a **fret** must be a real
neck position, a **node** can sit anywhere along the vibrating string, and a pinch's thumb grazes
*past* the neck (real scores use 24.0 — the 4th partial's bridge-side node, since `12*log2(4) =
24`). That bound would reject every bridge-side node from the 6th partial up. The bound is
`g_max_harmonic_node = 48.0`, which is `12*log2(16)` exactly.

**How high is worth supporting?** Three independent lines put the practical ceiling near the **8th
partial**:

| Line of evidence | Ceiling |
|---|---|
| **Ergonomics** — the nut-side node of partial *n* is at exactly `L/n`, so adjacent nodes are `L/(n(n+1))` apart: 9.0 mm at the 8th, 7.2 mm at the 9th, **2.1 mm at the 17th**, against a 10-15 mm fingertip | ~8-9, and it is a *hard* limit no amount of gain defeats |
| **Literature** — partials 2-5 "easiest to produce and most audible", above them "nearly inaudible without the overdrive of an amp", the "stratospheric" band between frets 2 and 3 being partials 6-9 | 5 acoustic, ~9 amplified |
| **Real charts** — the 118-file GP corpus's node values map to about the 8th | ~8 |

Precision is not the binding constraint: one decimal separates every distinct node through the
**17th** partial with zero collisions, first failing at the 18th (0.990 and 1.050 both round to
1.0). So the 16-partial bound costs nothing and leaves roughly double the headroom anything playable
needs.

**The bound is deliberately permissive, and that was taken to be a different question from the
picker.** A bound can only ever *reject a legitimate chart* — including a GP import we do not
author — so it is set to refuse junk and nothing more. **RULED 2026-09-15: the picker offers the
WHOLE bound**, partials 2-16 through `g_max_harmonic_partial`, sorted by partial lowest first — not
the short 2-8 list this paragraph once proposed. The shortening the UI wanted falls out of the LABEL
window instead of a second cap: a typed fret names only the nodes within half a fret of it, so any
one press offers a handful of rows (a typed 5 offers three) rather than the 79 nodes this bound
admits, and ordering by partial puts the loudest — the one a charter means by the label — first. The
SNAP cap stays 8 for the corpus reason above: it is import's rule, and raising it would need the
corpus re-measured.

**RULED 2026-09-16: the verb offers every CHANGE the selection allows, and asks only where there is
more than one.** `H` stopped being a technique toggle — `ChartTechnique::Harmonic` is deleted, and
the verb raises `EditorAction::ChooseChartHarmonic` through
`IEditorController::onChartHarmonicRequested()` — because with a multi-valued "on", "restore what the
last press removed" and "set" diverge: an invisible window picking the restore made `H` after a clear
behave differently from `H` on any other plain note. **Which rows CHANGE anything is the PLANNER's
answer**, never a count kept beside it: each node row (`planSetHarmonic`) and the clear
(`planClearHarmonic`) are planned over the live chart, and a `NoChange` plan is not a change — zero
changes is an inert press, one applies in the keystroke, two or more ask. The node rows are those of the
member whose label names the MOST nodes (a carrier's label is the fret its node lies at —
`harmonicLabelFret`, which is also what the clear presses back down — so a note touching 4.98 is
offered the 13th and 15th partials of a 5), and EVERY one of them is shown, a ticked row that changes
nothing included, while the **"No harmonic"** row comes last, after a separator, only where the clear
itself changes something. Several changes open the picker, answered by
`onChartHarmonicNodeRequested(std::optional<int>)` → `SetChartHarmonicNode{partial}`, an absent
partial being the clear. The TICKED row is the node the ANCHOR member — the note the rows were read
from, whose head the menu sits on — is touching, not a statement about the selection as a whole; the
PRESELECTED row is what the toggle
would have done — "No harmonic" when every member carries a fret-hand harmonic (`carriesNeckHarmonic`:
a node whose attack keeps it on the neck, a pinch excluded), else the lowest partial that CHANGES
something — so `H`
`Return` still clears a harmonic and still sets the lowest partial on a plain note. The clear is the
FRET HAND's alone: `planClearHarmonic` writes only notes that `carriesNeckHarmonic`, the thumb's node
being `planClearPinchHarmonic`'s, so a pinch selected beside a fret-hand carrier is left untouched by
`H`. Consecutive
choices on one selection FOLD into one undo entry through the shared gesture authority
(`commitChartGestureStep`, an empty `ChartHarmonicGesture` alternative), and a choice back to the
pre-run state retires the entry; restoring a node the label cannot name — an imported artificial 17.0
on a fret 5 — is `Ctrl+Z` only. The pinch is expected to carry a node of its own eventually and to
adopt the same law then.

A related fact: **GP's `HFret` values are conventional labels, not exact physics.** The true 8th
partial node is 2.313 but GP writes `2.4`; the 5th is 3.863 but GP writes `4.0`. The format
therefore stores what a chart *says* and must never snap a node to a computed ideal.

### 2D labels a fret-hand harmonic with its node

`tabNoteHeadText` in the paint core returns the node for a harmonic whose node is on the fretboard
and the fret otherwise, with a trailing `.0` dropped so 12 / 7 / 5 stay as narrow as an ordinary
fret number and only genuinely fractional nodes (3.2, 14.7) pay for the glyphs. **A pinch keeps its
fret** — *"pinch harmonics can come later because the fret is accurate and I'm not sure how we
should represent the node in 2D for pinches"* — which is exactly the split `nodeIsOnNeck` already
draws, so no new predicate was needed. Worth an eye on screen: labels can reach four characters
(`14.7`) where a fret reached two.

**The rule takes the stop being labeled.** `tabNoteHeadText(note, fret_at_head)` receives the stop
rather than reading `note.fret` — the onset passes its own fret, a linked junction passes the fret
the glide has reached, and the node rides the stop (fret spacing is logarithmic, so the node's
offset above the stop is constant in fret units). One authority, two call sites: every head of a
gesture states the same *quantity* rather than a node at the onset and a fret at the junctions. The
3D fretboard axis takes the same parameter for the same reason (`highwayNoteFretboardX`), so the two
surfaces cannot disagree about what a glide arrives at.

### Import always sets the node

Storing the node **only when it differed from the fret** is fatal once the node *is* the harmonic: a
GP natural harmonic at fret 12 with `HarmonicFret` 12 would import as **not a harmonic at all**.
Import therefore always sets the node for a fret-hand harmonic, falling back to the fret, which also
removes the double encoding where an absent node silently meant "the fret itself" and drew half a
fret off.

## `fret` is the stop — and 0 is the open string, capo'd or not (D1)

*"Our fretting is absolute... the fret where the capo IS is treated as 0... 0 means 'open string'
even with a capo and each fret is absolute"*: a capo'd natural harmonic stores `fret = 0`, not the
capo number — the capo never appears as a fret. The *physical* stop is derived (`fret == 0` → the
capo), which is what E21 compares against, and `fret == 0` + node + neither tapping-hand attack is
the complete fret-hand-harmonic discriminator: it needs no capo context, it gives `fretFor` the
right answer for the harp/artificial-harmonic family (`fret > 0` + node + non-`Pinch` = fretting
hand pressing the stop, picking hand damping the node), and it re-keys E7/E9/E19 onto "no real stop"
so bending or sliding a harmonic held over one stays legal.

**Why a `node >= fret` rule was never right, and why that was the tell.** Guitar Pro has natural
harmonics at fret 6 / node 5.8, fret 3 / node 2.7, fret 15 / node 14.7 — 9 of 109 measured — whose
node sits *below* the fret, because GP rounds the **fret** to an integer while the node is the true
position. No comparison rule survives that while `fret` carries two meanings: the **stop** for an
ordinary note, a pinch, or a tap harmonic, but a **rounded copy of the node** for a natural
harmonic, which has no stop at all. Same shape of defect the harmonic collapse removed — one slot,
two meanings. With `fret` always the stop, `node > fret` is universal and enforced — strictly `>`,
since a node *at* the stop **is** the stop and sounds nothing.

**The capo point fixes real data rather than only tidying the model.** A capo-1 score in the corpus
writes its natural harmonics at 7.0 and 8.2: nut-referenced positions that are *not* nodes of the
capo'd string, whose 3rd partial sits at 8.02. As written they would not ring. GP is capo-blind
here, so import resolves the notated label to a partial **offset** against an open string and then
places it against the real stop. The importer's own regression test carries a capo of 2, where "3.2"
correctly becomes 5.156 rather than 3.156.

**Two helpers, and why they are two.** Pushing *policy* into general-purpose accessors is what made
earlier names read oddly; the fix was separating the pure question from the policy, then collapsing
what was left:

| helper | question | answer |
|---|---|---|
| `nodeIsOnNeck(attack)` | is the node somewhere a display can point at? | everything but a **pinch**, whose thumb grazes out over the body. A predicate about the *attack* alone, so callers keep `harmonic_node.has_value()` inline — plainer, and visible to the optional-access checker, which cannot see through a wrapper. |
| `fretFor(note)` | which fret is the **fretting hand** on? | the node's fret for a fret-hand harmonic; the stop for a pinch or a two-hand tap, whose node is the *other* hand's; the fret otherwise. |

`fretFor` uses **`ceil`**. Fret `N` spans wire `N-1` to wire `N`, so a node at 2.669 lies in fret
**3** and 3.156 in fret **4**. A fret-hand window over `[f, f+w-1]` covers fret units `[f-1,
f+w-1]`, so an edge harmonic is only reliably covered when `H-1 <= node <= H`. Measured over every
node below fret 25: **`floor` fails 18 times, `round` 7, `ceil` never.**

**Not every rounding is that question.** `highwayNoteFretboardX` does no rounding at all:
`highwayFretLineX` takes a fractional coordinate and is linear in it, so a floor-then-lerp dance
collapses to one call. That is *interpolation*, not containment, and a stray `floor` sitting next to
a `ceil` rule costs a reader time.

**Capo consumption.** `capo` drives the import shift (GP's frets are capo-relative, so fretted notes
store `F + capo`), the physical-stop derivation in E21's validation, and the harmonic-node
placement. It is **displayed on both surfaces** too: the projection carries it, both view states
publish it, and both renderers draw it — 3D dims the face from the nut to the capo's fret line and
clamps a rimmed steel bar on it, 2D pins a "Capo N" chip in the lane's corner (both crude first
treatments, roadmap 25-Q6). The one narrow claim that survives: **no displayed fret NUMBER is offset
by it.** Head text, the 3D floor numbers, and the fret-hand chips all print absolute frets with 0
meaning the capo'd open string, which is the sight-readable convention chosen.

## Recovering a node from a source that records only a fret

Both importers face it, and neither needs a bespoke algorithm: a source's fret for a natural
harmonic **is** a rounded label for the node, so snapping recovers it. Worked examples:

| source says | resolves to | partial | error |
|---|---|---|---|
| fret 5 | 4.980 | 4th | 0.020 |
| fret 3 | **3.156** | **6th** | 0.156 |
| fret 9 | 8.844 | 5th | 0.156 |
| fret 12 | 12.000 | 2nd | 0.000 |

Fret 3 resolves to the 6th partial, *not* the 7th at 2.669 — distance decides, and the 6th is less
than half as far. **And eight integer frets have no harmonic near them at all**: 1, 11, 13, 14, 18,
20, 21, 23, missing by 0.669 to 1.312. A source naming one of those is bad data, and snapping anyway
would move it a whole fret and sound a *different partial*, so those drop the harmonic with a
diagnostic. Half a fret is the threshold: real labels land within 0.331 of a true node, implausible
ones miss by 0.669 or more.

A **pinch** cannot use this, because its fret is a stop rather than a node label and the source
records no partial — so it defaults to the octave (the lowest-order harmonic available at any fret,
hence the easiest to ring), also with a diagnostic.

**In the GP importer**, that threshold is applied (implausible natural labels drop the harmonic with
a conversion note; unusable stopped-harmonic labels fall to the octave with one), and Guitar Pro's
fuller harmonic-type vocabulary maps as follows: `Tap` harmonics keep their stop and become the
`Tap` attack; an open-string pinch on a capo'd track speaks from the capo; `Semi` imports as a
**pinch** (a semi-harmonic is a pinch whose fundamental keeps ringing — a pinch not fully executed —
so the pinch is the honest nearest technique until the format distinguishes them, kept loud with its
own conversion note); `Feedback` stays deliberately unsupported — feedback needs a real amp in the
room, which headphone play cannot produce — and drops the harmonic loudly along with unknown types;
`Artificial` imports as `Pick` + stop + node, faithful data whose hand placement D1 settled. The
standalone converter's input format has no semi/feedback/tap types, so only the threshold parity
matters there.

The trill (chart-ruleset.md [D5]) is the one GP mark that imports by SPELLING OUT rather than by
field or drop: `expandTrilledEvents` expands it into the alternation it names — principal,
auxiliary, principal at sixteenths (gpif stores no speed), continuations legato with the resolver
deriving hammer and pull from the frets alone — counted when spelled and counted when it cannot
expand (a ring within one step; an auxiliary the hand cannot reach, where the capo'd open IS
reachable). No trill field exists; the run is ordinary notes, exactly as measured tremolo already
spells out whole beats.

Three non-string acts are DELIBERATELY DEFERRED (chart-ruleset.md [D9] — may support later, none
silently): **volume swells** and **fade-ins** are amplitude gestures with no (position, string) home
in the chart, and **golpe** (body percussion) strikes no string at all — each drops at import, and
any future support enters through its own design pass rather than a chart-truth field. Strum
direction is the fourth deliberate deferral, ruled at [D7] with its own plan file and drop count
(backlog).

## Hardening the format: what can become impossible, and the ceiling

The project already prefers this shape — "sum types over inheritance… so illegal states can't exist"
is recorded editor-core practice — and format changes are cheap here: the standing rule is that
formats **change in place**, with no migration and no version bump, because the only users are us.
So the constraint is blast radius, not compatibility.

### Can be made structurally unrepresentable (intra-note)

1. ~~**Make the node required and drop the kind**~~ — **DONE** as the collapse above:
   `std::optional<double> harmonic_node` where presence *is* the harmonic. Its three wins landed: a
   node-without-a-harmonic and a harmonic-without-a-node are both unrepresentable (E1's disagreement
   half deleted real enforcement code); one name in every case with the attack saying which hand;
   and the wrong-anchor defect died with the double encoding — the importer always produces a node,
   and the renderer's absent-node path is gone. The one caveat is "One guarantee traded" above — the
   *pinch* half of "required" is rule-enforced (E20), not structural.
2. **Make the scrape its own variant — DEMOTED, likely not worth it.** The cost challenge: scrapes
   must respect minimum note distance, sustain overlap, and every other note-interaction rule, so
   wouldn't a variant duplicate all of that? Two clarifications and a re-assessment, recorded so the
   reasoning survives:
   - The proposal was never a separate entity — the variant lives **inside** `ChartNote` (this
     section's own words: "position/string/fret/sustain plus a sum type for the articulation"), so a
     scrape stays one event in the one sorted stream and every relational mechanism (min-distance,
     40-Q2-B overlap normalization, duplicate onsets, sorting, selection, FHP) stays
     single-implementation. The real cost is **dispatch breadth**: every consumer reading the muting
     bools / `note.bend` / `note.vibrato` switches on the variant — the blast-radius bullet below,
     mechanical but wide.
   - The payoff shrank: D4 pulls emphasis out of the exclusion set, and D2 has the scrape *using*
     `keyframes` and `slide_out`, so the variant's remaining structural win is five excluded fields
     plus the required terminal — which one enforced rule already covers — plus deleting the
     in-memory override mechanism. Weak against the breadth cost. Current lean: enforced rules plus
     verb guards suffice; revisit only at the format-shape step if validation churn proves
     otherwise. D2's semantics (required `slide_out` terminal, optional turnaround keyframes) are
     decided independently and implemented in the flat struct.
3. **Derive a scrape's `sustain` from its path.** If the variant stores the path and exposes
   `sustain` as the last stop's offset, then "the path ends exactly at `sustain`" is true by
   construction — an invariant that currently needs a rule, a normalization step, *and* care in
   three planners.
4. ~~**Put `Pinch` only on the picked form** (E3), and `tremolo` only on the picked form (H1)~~ —
   **dead both ways**: `Pinch` became an attack and H1 was rejected (tremolo is orthogonal to
   attack).
5. ~~**A dead-note form**~~ — the conditional died with H4; the dead note's payload set is settled
   by the mute row instead (no node/bend/vibrato; yes keyframes/slide_out; tremolo and emphasis per
   the principle and H3), which a future sum type may or may not bother making structural.

That points at `ChartNote` becoming position/string/fret/sustain plus a **sum type for the
articulation that owns the fields legal for it**, rather than a flat struct of independent
optionals.

### Cannot be made structural — the ceiling

**A per-note type cannot express a relational invariant.** These stay validation, permanently:

- ~~E5, `Pull` needing a higher predecessor, and E6, the derived direction~~ — **these left
  validation by a route this section did not consider.** The premise stands — a per-note type still
  cannot express a relational invariant — but the legato rows stopped being invariants at all: with
  direction derived on read, the relationship is a *question* answered per read rather than a *fact*
  stored and policed. So the honest form of the ceiling is narrower and more useful: **what cannot
  be made structural can sometimes be made underivable instead**, and that is strictly better,
  because an unstored fact cannot go stale. The remaining entries are genuinely stuck: they
  constrain how notes sit relative to each other, which no amount of deriving removes.
- minimum sustain distance and the 40-Q2-B overlap normalization
- a scrape's "must keep traveling" (needs consecutive stops, so it is intra-*payload* and could be
  structural with a non-empty, strictly-changing sequence type — worth considering, unlike the rest)

So hardening has a real boundary: **intra-note combinations can become impossible; anything that
reads a neighbour cannot.** Worth stating plainly so the effort is not oversold.

### Costs to weigh before committing

- **Blast radius.** Every consumer that reads `note.attack` / the muting bools / `note.bend`
  changes: the shared projection, both renderers, the GP importer, every `chart_edits` planner, the
  rules, and the document serializer. The importer is the worst of these — it builds notes
  field-by-field.
- **Generic verbs get harder.** "Set emphasis on the selection" is trivial across a flat struct and
  needs visitation across a sum type. Emphasis being compatible with everything (H3) argues for
  keeping the universally-compatible fields *outside* the variant.
- **Over-modelling risk.** If most cells turn out compatible, a variant per attack duplicates shared
  fields for little gain. The matrix decides how much structure is justified — which is exactly why
  it comes first.

## Are all harmonics one set of data?

*"Does this mean pinch harmonics could be represented by a fret and node the same way as a natural
harmonic so technically all harmonics are one set of data? fret and node? But the ATTACK can
differ?"*

**The data unifies.** One formula covers every harmonic: the sounding pitch is set by the ratio of
the node→bridge length to the fret→bridge length. Fret spacing is logarithmic, so a node 12 frets
above a stop at fret 5 is exactly the midpoint of the speaking length and yields the octave. `(fret,
node)` therefore determines the pitch for natural, pinch, and tap alike, with no per-kind special
case. Two things follow: `fret == 0` stops being a placeholder on a natural harmonic and starts
**meaning** "open string"; and the unified shape *gains* expressiveness, because it represents
picking a harmonic over a fretted note — one finger pressing 5, another resting on 17 — which is
rare but real.

**The attack carries the rest, because pinch belongs on the attack axis.** Reading `attack` as *what
excites the string* would make pinch and natural collide on `Pick`, but that is a definition imposed
on the field rather than read off it: the enum's own doc says **"How the onset is produced"**, and
under that a pinch qualifies plainly. The thumb graze is not a separate action added to a pick
stroke — it is one compound stroke with its own hand angle and follow-through that players learn as
a single thing. The enum already discriminates at exactly that grain, since `Slap` and `Pop` are
both picking-hand excitation differing only in manner.

**This is the hardening the document was opened to find.** Three things stop being rules and become
impossible to express:

| | as a separate harmonic field | with the node alone |
|---|---|---|
| **E1** | an enforced rule policing `harmonic` disagreeing with the node | nothing left to disagree |
| **E3** | `Pinch` requires `attack == Pick`, **unenforceable** | cannot be violated when pinch *is* the attack |
| renderer predicate | a technique test on the harmonic kind | **anchor at the node unless the attack is `Pinch`** — pinch is the only case whose damping finger is off the fretboard and over the body |

That last row also fixes a misclassification the field-based reading would have produced: a
right-hand tap harmonic has its damping finger *on* the fretboard at the node, so the head anchors
there — and the predicate gives that for the correct reason rather than by accident.

Rules that merely *allow* something (E13, E14) need no restatement, since allowances are the default
and only forbids need rules. Rules that split on the two kinds (E7, E9) restate as "a node with a
non-`Pinch` attack", which is longer to say but no less precise.

**A case this forced into view:** you *can* pull off from a **pinch** harmonic, because the fretting
hand is pressing a real fret and therefore has something to release. E19 is correctly
fret-hand-only; that case went unchecked until the model made it unavoidable.

## Tap harmonics: already representable

*"We will add tap harmonics as well later which will similarly need a node field."* — a tap harmonic
is a harmonic whose node is struck rather than picked: a **node** plus a fretting-hand legato claim
or a `Tap` attack, and it works the moment E4 stops testing `fret`. See E13 and the finding above.

What that resolves:

- **The notation is not stressed after all.** The harmonic family stays 2-way, so the pinch mark
  keeps its single job and no third notation rule fires — letters appear only when a family outgrows
  shape and darkness, and this family does not grow. Whether a tap harmonic wants its own cue is a
  question about the **attack**, where `Tap` and the legato marks already carry marks, not about the
  harmonic family.
- **The matrix cell resolved itself.** The guess was that a tap harmonic "probably implies `attack
  == Tap`". Inverted: the attack is what *makes* it a tap harmonic, and both tapping attacks
  qualify. So there is no third harmonic-versus-attack rule — E3 and E13 are the complete pair.

One thing genuinely deferred: a **pinch** harmonic cannot be tapped (E3) — *"tap and pinch harmonic
cannot be executed together."*

## The 2D node question (and a retracted claim)

**Retracted:** calling it a cross-surface *gap* that 2D does not carry the node was wrong. 2D's axes
are **time and string** — there is no fretboard axis to place a node on, so the concept does not
apply there; the shared `NoteViewState` carries the node for both surfaces and the lane simply does
not anchor to it. 3D has a fret axis, which is why the node positions the head there.

**What 2D needs instead is to report the node as a NUMBER**: the drawn fret number should show where
the hand actually goes, not the integer anchor. Two findings, both computed rather than estimated
(`-12*log2(1-j/k)` for the node at string fraction `j/k`):

- **0.1 precision is sufficient for the entire harmonic series through the 12th.** The tightest gap
  between any two distinct node positions inside 24 frets is **0.144 frets** — the 12th and 11th
  harmonics at 1.506 and 1.650 — and 0.1 separates them. So going as high as the 12th costs nothing
  in precision, and there is no case needing two decimals.
- **The common harmonics land on near-integers, which answers the width worry.** 12.000, 7.020,
  4.980, 19.020 and 24.000 round to 12.0, 7.0, 5.0, 19.0, 24.0 — so **suppressing a trailing `.0`
  keeps every commonly used harmonic at one or two characters**, exactly what is drawn today, and it
  also matches how guitarists name them ("the 5th fret harmonic" for a node at 4.980). Only the
  exotic nodes widen: 3.2, 2.7, 8.8, and at worst four characters like 15.9.

The open design question is therefore narrower than it first looked: **only the exotic fractional
nodes need somewhere to go**, and a four-character string will not fit the fret font on a 26 px
head. Options to weigh in the notation pass: a smaller font for the fractional part, the fraction as
a subscript, the node beside the head instead of on it, or the integer on the head with the fraction
carried elsewhere.

A pinch harmonic's node is still read by nothing on either surface, waiting on roadmap 25-Q5 — a
real future requirement rather than a maybe, because the node determines the squeal's pitch. A pinch
stays a normal picked note until then. No format work remains for it: `harmonic_node` already holds
the strike position and already round-trips.

## What remains

The matrix is signed and enforced, and nothing in this document awaits a ruling. What is left is the
format-shape step: re-open the sum type with the matrix in hand, sizing it against how many cells
actually turned out incompatible. The count is known and **lopsided — the great majority of cells
are compatible**, which argues against an elaborate sum type and for the narrow hardenings still
standing: the scrape as its own variant, and a path-derived scrape sustain. Size the work against
that, not against the length of this document.
