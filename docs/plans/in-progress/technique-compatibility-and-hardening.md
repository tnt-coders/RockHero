# Technique Compatibility Matrix, and Hardening the Format Against Invalid Combinations

Status: **REVIEWED 2026-08-08, awaiting the user's sign-off on the full matrix.** Started
2026-08-07 after the user observed that *"tap and pinch harmonic cannot be executed together"* and
that nothing in the format or the rules prevents it. Their direction: *"establish this full matrix
then re-analyze the save file format to see it can be hardened even more to make invalid
combinations impossible as much as it can."* A deep review pass (2026-08-08) closed the recording
gaps, numbered the two enforced-but-unnumbered rules E20/E21, and surfaced the open question
cluster in [What the review left open](#what-the-2026-08-08-review-left-open) — sign-off covers
the rendered matrix plus those questions.

**Order matters and is deliberate: the matrix settles first, the format second.** Restructuring the
type on a guessed cell means restructuring it again when the cell flips.

This is a **prerequisite for plan 40 Phase 5**, not a follow-up. Phase 5's scope already promises
"illegal-combination guards" but names only one (a node only with a harmonic — which the 2026-08-08
collapse made structural, so that guard no longer needs writing), and every remaining
technique verb needs to know what it must clear or refuse. A verb that authors an impossible state
violates Phase 5's own rule against authoring an invalid state.

## The surface

`ChartNote` carries these technique fields (`chart.h`):

| Field | Values |
|---|---|
| `attack` | `Pick`, **`Pinch`**, `Hammer`, `Pull`, `Tap`, `Pop`, `Slap`, `PickSlide` |
| `mute` | `None`, `Palm`, `Full` |
| ~~`harmonic`~~ | **DELETED 2026-08-08** — collapsed onto `attack` + `harmonic_node` |
| `harmonic_node` | `optional<double>` — the node position, in fret units, **and the assertion that the note is a harmonic** |
| `vibrato`, `tremolo`, `accent` | `bool` |
| `bend` | `vector<BendPoint>` — `{offset, semitones}` |
| `slides` | `vector<SlideWaypoint>` — `{offset, fret}` |
| `slide_out` | `optional<SlideOut>` — `{offset, fret}` |

`fret == 0` means an open string. `sustain` is the drawn tail, **not** the physical ring — a
zero-sustain note still sounds, which matters for several cells below.

## The criterion (settled 2026-08-07)

**Forbid a combination only when it cannot be *executed*. Allow the merely unusual.**

Three reasons, all of them consistent with calls already made: the importer must faithfully carry what
sources contain, so forbidding the odd means silently dropping real data; the user already drew this
line for pinch-on-natural, choosing not to *support* it while leaving the door open rather than
declaring it invalid; and it matches the legato-distance decision, where the author asserting a
technique is the authority rather than us.

This has a consequence worth stating plainly: **most remaining cells resolve to "allowed,"** so the
matrix's real content is the handful of hard impossibilities. That shrinks the hardening payoff too —
fewer forbidden combinations means fewer things worth making structurally impossible — and the
restructure below should be sized against the impossibilities that actually survive, not against the
number of questions asked.

A useful sub-distinction emerged while applying it. "Cannot be executed" covers two things:

- **Physically impossible** — the motion contradicts itself (E3, E7).
- **Incoherent data** — the combination is playable but the stored value would describe something that
  does not exist (E8: a pitch offset from a note with no pitch). This is *not* the same as pointless,
  and it is still forbidden.

"Pointless but coherent" is allowed. Q1 is the worked example: palm-muting a natural harmonic mostly
defeats the technique, but the harmonic still sounds and the data still means something, so it stands.

## Established

Each of these is either enforced in code today or physically unambiguous. Rows written before the
2026-08-08 collapse keep their original vocabulary: read `harmonic` as `harmonic_node`, and
`Natural` as "a fret-hand harmonic — a node with a non-`Pinch` attack."

| # | Rule | Source |
|---|---|---|
| ~~**E1**~~ | **HALF DELETED 2026-08-08.** The `harmonic != None` half is gone: with the field collapsed there is nothing for a node to disagree with, so the state cannot be built. Only the range check survives, on `harmonic_node`. | `chart_rules.cpp` |
| **E2** | `PickSlide` excludes the pitched techniques — `mute`, `harmonic_node`, `vibrato`, `tremolo`, `bend` — and requires the unpitched `slide_out` **terminal** exactly at `sustain`, with `slides` as optional turnaround waypoints and the whole path always traveling. `accent` is the scrape's own technique and allowed. | **Enforced**, the `PickSlide` block in `chart_rules.cpp`. **Reshaped 2026-08-08 by walkthrough D2+D4** (user): the terminal is definitionally unpitched, so it is the `slide_out` — a pitched waypoint terminal would imply a turnaround or a held landing — and an accented scrape is just an aggressively played one. |
| ~~**E3**~~ | **UNVIOLATABLE 2026-08-08** — `Pinch` *is* an attack now, so it cannot be paired with a different one. Kept for the record: | Established 2026-08-07. A pinch harmonic is produced by the pick stroke with the thumb catching the string, so every attack that *replaces* the pick stroke — `Hammer`, `Pull`, `Tap`, `Slap`, `Pop`, and the left-hand tap stored as `Hammer` — excludes it. `PickSlide` already excluded by E2. **Not enforced anywhere today.** |
| **E4** | `Hammer` and `Tap` require a positive **sounding position** — `fret` for an ordinary note, `node` for a natural harmonic | You cannot hammer onto, or tap, an open string or the nut. **Amended 2026-08-07** from the original `fret > 0`, which rejected every tap harmonic (`fret == 0`, `node == 12`); see the accessor note below. Not enforced today. |
| **E5** | `Pull` requires a preceding note on the same string at a **higher** fret | Something must be released to sound it. **Relational** — see the ceiling below. |
| **E6** | Legato direction derives from that relationship | `docs/plans/in-progress/legato-authoring-model.md`. **Relational.** |
| **E7** | `Natural` harmonic excludes `slides` and `slide_out` | User, 2026-08-07: *"A natural harmonic CANNOT be slid by definition. It is physically impossible."* A natural harmonic is a light touch at a node, not a press; sliding moves the touch off the node and the harmonic simply stops. A slide is unambiguously fretting-hand travel with no whammy equivalent, so unlike bend and vibrato below this cell has no ambiguity. **Nothing to remove:** searched for supporting logic and found none — the projections only zero a harmonic for scrapes. Record it so nobody *adds* support later. |
| **E8** | `Full` mute excludes `harmonic` | A full mute sounds no pitch; a harmonic *is* a pitch, so they contradict by definition. The "almost muted harmonic" the user weighed is *partial* damping, which is what `Palm` already means — so full mute never has to stretch to cover it, and that case is Q1 instead. |
| **E9** | `Natural` harmonic excludes `bend` and `vibrato` — **natural only, NOT pinch** | User, 2026-08-07. Same physics as E7: a light touch at a node cannot press the string, so the fretting hand cannot modulate the pitch. A **pinch** harmonic's fretting hand *is* pressing a real fret, so bending it works normally and a bent pinch squeal is a staple — excluding it would make a very common figure unrepresentable. This is the second cell where the two harmonic kinds need opposite answers. |
| **E10** | `Full` mute excludes `bend` (but **allows** `slides` and `slide_out`) | User, 2026-08-07. Incoherent data rather than an impossible motion: a bend stores semitones, an offset from a pitch a dead note does not have. Positions survive the same test — a slide's waypoints and a `slide_out`'s target are places, not pitches, and the pick-slide precedent already treats fret data as right-hand travel. |
| **E11** | `Full` mute excludes `vibrato` | User, 2026-08-07. Completes the row: a full mute excludes every **pitch-modulating** payload and allows every **position-valued** one. Vibrato asserts pitch modulation of a note with no pitch — it stores only presence rather than a magnitude like `bend`, but it describes the same nonexistent thing. |
| **E12** | `Pull` excludes every harmonic | A pull-off sounds the string by *releasing* a finger so a lower stopped pitch rings — and that pitch rings over the full speaking length with nothing damping a node, so the result is an ordinary note by construction. A contrived arrival at a node (a finger resting lightly below the released one) has the release *damping* rather than exciting, and barely sounds. |
| **E13** | `Natural` harmonic **allows** `Hammer` and `Tap`, and that pairing *is* the tap harmonic | A finger strikes the string over a node and the strike both excites and damps. `Hammer` is the fretting-hand form, `Tap` the picking-hand one (hold 5, tap 17 — the most common form of all). Promotes H2 from candidate to established, and needs **no new harmonic kind** — see below.  **Frequency is very lopsided** (user, 2026-08-07): the `Hammer` form — the fretting hand rapping a node — is *"VERY rare. It is possible though"*, while the `Tap` form is common. Both legal; the editor should not make the rare one easy to author by accident, and it does not deserve prominent notation. |
| **E14** | `Slap` / `Pop` **allow** `Natural` harmonic | Q7. A slap harmonic — thumb striking the string while a fretting finger rests on the 12th, 7th or 5th node — is a staple of the slap idiom, and popping over a node is the same vocabulary. `Pinch` stays excluded by E3, which already lists every attack that replaces the pick stroke. |
| **E15** | `Slap` / `Pop` **allow** every `mute` | Q8. `Full` + `Slap` *is* the slapped dead note, core rhythmic material in a slap line rather than an oddity — forbidding it would make slap lines unrepresentable. `Palm` + `Slap` is awkward, since the thumb and the palm heel want different positions, but the hand spans it, so the criterion allows it. |
| **E16** | `vibrato` and `bend` **compose** | Q9. Bend up to pitch, then vibrato on the bent note — the blues-lead vocabulary entire. No data conflict either: `bend` is a path and `vibrato` oscillates around whatever the path is, so neither subsumes the other. |
| **E17** | `attack` **allows** every slide payload — `Hammer`/`Pull` + `slides`, `Tap` + `slide_out` | Q10 and Q11, both **dissolved** rather than decided: `attack` names the onset and `slides`/`slide_out` name the sustain, so "hammered onto, then slid while ringing" is two moments, not two claims about one transition. See the separation below. |
| **E18** | `tremolo` **allows** `slides` and `bend` | Q12. A tremolo-picked bend is ordinary and the rising tremolo-picked slide is a standard metal figure. Consistent with H1, which constrains the *onset* to a pick while tremolo describes the sustain. |
| **E19** | A `Pull`'s predecessor cannot be a **fret-hand harmonic** — and pull-from-a-**pinch** is explicitly ALLOWED | User, 2026-08-07: *"Natural harmonic should be able to be followed by a hammer on, not a pull off. Pull off would be physically impossible."* The converse of E12 and a **separate cell**: E12 forbids pulling *into* a harmonic, this forbids pulling *from* one. The fretting hand is touching a node, not pressing, so nothing can be released. A pinch's fretting hand *is* pressing a real fret, so pulling off from a pinch works — promoted to rule status by the 2026-08-08 review. **Relational** — see the ceiling. A natural harmonic followed by a *hammer-on* is fine: the string is already ringing and a finger coming down stops it at a new pitch. |
| **E20** | `Pinch` **requires** `harmonic_node` | **Enforced** (`chart_rules.cpp`). A pinch is picking while damping a node — the overtone that squeals is determined by where the thumb lands — so one without a node is missing data. This is what lets node presence alone assert the harmonic. Numbered by the 2026-08-08 review; previously narrated but never a table row. |
| **E21** | `harmonic_node` > the **physical stop**, strict — the fret, or the capo when `fret == 0` | **Enforced** (`chart_rules.cpp`). A node lies on the speaking length, so nothing vibrates at or behind the stop; a node *at* the stop is the stop. Generalized by D1 (2026-08-08): under the 0-means-open convention a capo'd open string stores `fret = 0`, so the stop the node must clear is derived, not stored. |
| **E22** | A **fret-hand harmonic** (`fret == 0` + node + neither tapping-hand attack) requires `node <= g_max_fret` | **Enforced** (`chart_rules.cpp`), adopted with D1. The fretting hand touches the node, and a finger on the fretboard cannot be past the last fret; this is also what keeps the derived hand window inside `g_max_fret`. A pinch (thumb over the body) and a tap harmonic (picking-hand finger) escape the bound — only the universal 48 limit applies to them. Same discriminator as `fretFor`'s node branch, deliberately. |
| **E23** | A **tap harmonic** (node + `Tap`) excludes `tremolo` | User, 2026-08-08: not executable fast enough. The model agrees structurally: the tap's damping finger *leaves* the string, so nothing holds the node under re-picking and the harmonic dies. A natural or artificial harmonic keeps a finger on the node, which is why those still allow tremolo (A.H. tremolo is "oddly actually possible" — user). Unenforced; enforcement pass. |
| **E24** | `Full` mute **allows** `Hammer`, `Pull`, and `Tap` — the ghost legato | Walkthrough D5, user 2026-08-09. Muted hammer/pull "clucks" are standard funk and R&B vocabulary (bass especially) and dead-note taps are core percussive-fingerstyle material — executable, so the criterion allows them. E4's positive-sounding-position requirement still binds the hammered and tapped forms. The same ruling kept E10 whole: **pre-bend included in the bend exclusion** — bend points store semitone offsets from a pitch a dead note lacks (incoherent data, not merely pointless), no source notates the muted pre-bend, and the cell reopens only on real chart evidence (D6). |

**The tap harmonic needs no enum value — adding one would manufacture invalid states.** Tap was
slated as a third `NoteHarmonic` value. Compare what the fields hold each way:

| | `Natural` + `Hammer`/`Tap` | a new `Tap` kind |
|---|---|---|
| `fret` | 0 (or the stopped fret) | same |
| `touch` (node) | the node | the node |
| `attack` | `Hammer` or `Tap` — **which hand struck it** | `Pick`? — nothing picks it |

The third row is the giveaway, twice over. A `Tap` kind leaves `attack` describing something false,
since nothing picks a tapped note and its only honest values are the two that already exist. And a
single kind **cannot say which hand tapped** — a distinction the attack axis draws for free and the
notation would otherwise have to invent a mark for. Worse, the kind creates a new invalid combination
(`Tap` kind + `Pick` attack) for the format to forbid, which is the class of state this document
exists to eliminate. Same shape as the whammy resolution: the distinction was already carried by
fields that exist. **Keep `NoteHarmonic` two-valued.**

Two consequences follow.

**E4's precondition was stated on the wrong quantity, and its enforcement needs no accessor.** It
required `fret > 0` because a hammer needs somewhere to land — but a tap harmonic on an open string
has `fret == 0` and `node == 12`, so the rule rejected the very technique E13 allows. The quantity
it wants is the **sounding position**: where the striking finger meets the string. An earlier
revision planned a `soundingPosition(note)` accessor shared with the renderer; the 2026-08-08
review retired that plan, because the two consumers deliberately diverged into *different*
questions — the renderer's `noteFretboardX` wants an x-coordinate (the exact node), while core's
`fretFor` wants the hand's fret (`ceil` of the node, and `note.fret` for a `Tap` attack, whose node
is the *other* hand's). Writing E4 on `fretFor` would reject the open-string tap harmonic all over
again (`fretFor` returns 0 for it). The correct enforcement form is simply:

```
note.fret > 0 || note.harmonic_node.has_value()
```

— node positivity needs no separate test because the enforced range rule already guarantees
`node > 0`. The capo caveat an earlier revision recorded here dissolved with D1: under the
0-means-open convention a capo'd open string stores `fret = 0`, so `fret > 0` already means a
real stop and no capo variant is needed.

**Do not evaluate E5 on the sounding position.** E5 tests that a `Pull`'s predecessor sits at a
*higher fret*. Reading it on the sounding position would make a harmonic at node 12 followed by a
pull to fret 5 **pass**, since 12 > 5, silently allowing exactly what E19 forbids. The two rules
ask different questions of the same number: a hammer needs somewhere to **land**, a pull needs
something **pressed**, and a node is a place but not a press. E19 is the rule that has to carry
it.

**`Natural` is a misnomer for what the field means.** A harmonic's pitch comes from the ratio of
node→bridge to fret→bridge, and fret positions are logarithmic, so the midpoint of a string stopped
at fret 5 lies exactly at fret 17. An absolute node plus `fret` therefore determines the pitch in
*every* case, and an open-string natural harmonic is just the `fret == 0` case — not a separate
reference frame. What actually separates `Natural` from `Pinch` is **which hand damps the node**,
precisely the axis the notation already encodes by darkness. Two things follow: a *fretted* tap
harmonic is already representable, since no rule requires a natural harmonic to be open; and the
enum's names describe techniques where the field means a hand. Renaming is a user call, not folded in
here.

**Where the line falls: does the technique happen at the onset, or across the sustain?** This question
decided two cells in opposite directions, so it is the operative test rather than intuition about
whether something "is a way of playing the note".

- **`Pinch` → attack.** The thumb graze happens *inside the single onset event*. One stroke, one moment.
- **`tremolo` → payload.** The field means *"unmeasured noise picking — as fast as possible, no real
  timing"*, with measured repetition spelled out as discrete notes instead. It is a texture across the
  **duration**; the onset is merely its first stroke, and the model deliberately never represents the
  individual strikes. Asked directly whether tremolo should be an attack, the user's instinct was that
  it *"feels wrong"* — correct, and the field's own definition is why. The name itself was
  re-litigated and kept (walkthrough D3, 2026-08-08): untimed as-fast-as-possible picking IS tremolo
  picking, and the settled taxonomy is **two noise textures distinguished by pitch, one per axis** —
  tremolo is *pitched* noise on the flag (the fret still sets a measurable pitch), the scrape is
  *unpitched* noise on the attack — which is why they never share a field.

The consequence for `tremolo` is that it constrains **nothing** on the attack axis, which is what
rejected H1. Its one exclusion, E2's, exists because the field comment says pick slides *"share the
noise vocabulary intrinsically through their attack, without this flag"* — so setting it on a scrape is
**double encoding, not a contradiction**. That is a second and independent argument for the ranked
candidate that moves `PickSlide` off the attack axis: the comment is an admission that a
sustain-spanning texture currently rides on the onset field.

**`attack` is the onset; every payload is the sustain.** This one line dissolved Q10 and Q11 and
should be checked first against any future "aren't these two claims about the same thing?" worry.
`attack` says how the note *began*; `mute`, `harmonic`, `vibrato`, `tremolo`, `bend`, `slides` and
`slide_out` all describe what happens *while it rings*. So a hammer-on carrying a slide path is not a
contradiction — it is one note hammered onto and then slid, two different moments. The intuition that
they collide comes from tab notation, where a slide is *drawn* as a connector between two notes and
therefore looks like a property of the transition into one; our model instead puts the slide inside
the note as a path across its sustain.

The separation also explains why `PickSlide` needs the elaborate E2: it is the **one attack whose
meaning extends across the sustain**, which is why it must own its slide payloads — the required
`slide_out` terminal plus optional turnarounds — and exclude every pitched payload. Being the sole exception is a reason to keep an eye on it — the hardening candidate that
splits the scrape into its own variant is really a proposal to stop it from sitting on the `attack`
axis while behaving like a payload.

**The full-mute row reduces to one sentence.** A full mute sounds no pitch, so it excludes everything
**pitch-valued** — `harmonic` (E8), `bend` (E10), `vibrato` (E11) — and allows everything
**position-valued** — `slides` and `slide_out` (E10). That is a cleaner rule than the five-way bundle
H4 attempted, and it generalizes: the question to ask of any future payload is whether it names a
pitch or a place.

**E3 is the one that started this, and the collapse retired its verb problem** — attack is a single
field now, so assigning one clears the other structurally. What replaced it is a different verb
obligation, found by the 2026-08-08 review: the pinch verb must **author a node** (E20), and any
verb that moves a pinch's attack *away* from `Pinch` must clear or re-ask the node — the stored
value is an off-neck graze position, and under any other attack `nodeIsOnNeck` reads the same
number as a fret-hand node, silently teleporting the hand (a node of 24.0 becomes "hand at fret
24"). Neither guard exists today; both belong to the enforcement pass.

## High confidence, wants confirmation

| # | Proposed rule | Reasoning |
|---|---|---|
| ~~**H1**~~ | **REJECTED 2026-08-07** — `tremolo` does **not** require `attack == Pick` | User instinct (*"Should tremolo be an attack? That feels wrong."*) exposed the flaw. H1 reasoned that tremolo picking *is* repeated picking, so an attack replacing the pick stroke contradicts it — but an attack describes only the **onset**, not the whole duration, so it never "replaces the picking". Hammer onto a note and then tremolo-pick it: `Hammer` + `tremolo`, executable and uncontradictory. H1 conflated onset with sustain, which is exactly what E17 warns against. Tremolo is **orthogonal to attack**; the only exclusion is E2's, and that one is redundancy rather than contradiction. |
| ~~**H2**~~ | **PROMOTED to E13** 2026-08-07 — `Natural` harmonic does **not** require `Pick` | A tapped harmonic — tapping directly over the node — is a real technique, so `Tap` + `Natural` is playable. This is the asymmetry with E3 and the reason the two harmonic kinds cannot share one rule. |
| ~~**H3**~~ | **CONFIRMED 2026-08-08 in its strongest form: `accent` is compatible with EVERYTHING, no exception** (walkthrough D4). It is dynamics, orthogonal to how the note is produced — the user: an accented scrape "would just be an aggressively played pick slide." The 2026-08-07 amendment's scrape carve-out is gone; E2 no longer excludes accent, and the glow's tight plectrum clearance (0.331 px at a 25 px head) is accepted as-is with `glow_size` as the joint retune knob if it ever needs air. |
| ~~**H4**~~ | **SUPERSEDED.** It bundled five cells under one "no pitch" argument and got two wrong. Settled instead as: `harmonic` excluded (E8), `bend` excluded (E10), `slides` and `slide_out` **allowed** (E10 — positions, not pitches), `vibrato` still open as Q4. The lesson is that "no pitch" separates *pitch-valued* payloads from *position-valued* ones rather than excluding everything. |

## Possible, but deliberately unsupported for now

A third disposition beside "impossible, forbid" and "pointless but coherent, allow": combinations that
**can** be executed but are so rare that supporting them costs more than it returns. The user's rule
for these (2026-08-07): keep them forbidden for now, allow one **only if it is easy**, and otherwise
revisit the whole group together.

| Combination | Why deferred rather than forbidden |
|---|---|
| **Pinch on a natural harmonic** | User: *"TECHNICALLY possible... But so rare I have literally never seen it."* Already impossible by construction: one note carries one `harmonic_node`, so a second, independently-damped node cannot be written — the single-node shape **is** the enforcement, and a future design wanting it would need a second node field. Do not "fix" that into a set without meaning to. |
| ~~**`accent` on a scrape**~~ | **UN-DEFERRED 2026-08-08 (walkthrough D4): allowed and shipped.** The measured glow clearance that motivated the deferral (fade band clears the plectrum's diagonal shoulder by **0.331 px** at note height 25 against the disc's **1.560**, because the shoulder reaches 0.547 of the head against the disc's 0.500) was accepted as-is — visually tight but real. The knob is `glow_size`, which the round head shares; retune jointly if it ever needs air. |

## Resolution log — Q1–Q12, all closed 2026-08-07

Every cell put to the user, and every cell the criterion closed on its own, with the reasoning that
decided it. Kept in question order because the *arguments* are the durable part — the rules they
produced live in Established above, and this is where to look when one of them seems wrong later.

**Mutes against pitch.** Palm muting damps but does not kill the pitch; full muting kills it.

- ~~**Q1** `Palm` + `Natural` harmonic~~ — **ALLOWED** (user, 2026-08-07). The damping shortens the
  harmonic without preventing it, and the palm sits near the bridge while the node can be far up the
  neck. The worked example for "pointless but coherent is allowed".
- ~~**Q2** `Palm` + `bend` / `slides`~~ — **ALLOWED** (user, 2026-08-07). Palm muting damps sustain at
  the bridge while the fretting hand bends or slides normally; nothing contradicts and the data stays
  coherent — a real offset from a real pitch. Also common rather than merely possible.
- ~~**Q3** `Full` + `slides`~~ — **ALLOWED, and `Full` + `bend` is NOT** (user, 2026-08-07). The split
  falls exactly where the *data* does. A slide's waypoints are **positions**, which stay meaningful
  with no pitch — the pick-slide precedent proves it, since a scrape's frets are right-hand travel
  rather than pitch. A bend stores **semitones**, an offset from a pitch that a full mute does not
  have, so it is incoherent in the E8 sense rather than merely pointless. The user's case for allowing
  the slide is concrete: Van Halen dragging a muted hand up and down under heavy phaser, which
  full-mute-plus-slide is the natural way to represent. `slide_out` follows `slides` for the same
  reason — it is a position too.
- ~~**Q4** `Full` + `vibrato`~~ — **DISALLOWED** (user, 2026-08-07: "Full mute vibrato makes no
  sense"). See E11 — it completes the full-mute row into one principle.

**Harmonics against articulation.**

- ~~**Q5** `Natural` + `Hammer`/`Pull`~~ — **ANSWERED 2026-08-07.** `Pull` forbidden on every
  harmonic (E12); `Hammer` and `Tap` **allowed** on `Natural`, where that pairing *is* the tap
  harmonic (E13, promoting H2). `Hammer` on `Pinch` was already E3. Amends E4, whose `fret > 0` test
  rejected every tap harmonic, and retires the third-`Kind` plan. Two consequences below.
- ~~**Q6** `Natural` + `bend`~~ — **ANSWERED 2026-08-07, absorbed into E9** (the user forbade bend
  *and* vibrato on a natural in one ruling). The row was lost when the log was restructured; restored
  by the 2026-08-08 review so the numbering has no silent hole.
- ~~**Q7**–**Q12**~~ — **ALL ANSWERED 2026-08-07 by the criterion itself**, no ruling needed: every one
  is executable, and five of the six are *staples* whose forbidding would make real charts
  unrepresentable. Recorded as E14–E18. Two of them dissolved rather than resolved — see the onset /
  sustain separation below. Overturn any of them if you disagree; they were not put to you because a
  forbid-a-staple error is costly while an allow-something-odd error is free.

## Whammy is beat-scoped, which is why E9 needed no format change

The finger-versus-bar problem looked like it blocked E9: our `bend` is a pitch curve that does not say
whether the fretting hand or the whammy bar produced it, and a bar dive on a natural harmonic is a
staple. It dissolves on **scope**, not on a ruling.

Guitar Pro stores it as **`beat.whammy`** (`gp_score.h:157`, parsed from the `Whammy` /
`WhammyExtend` elements at `gp_score_parser.cpp:545`) — *beat*-scoped, because a bar dive bends every
sounding string at once. Our `bend` is *note*-scoped. So a dive was never going to be a note payload;
modelling it as one would mean duplicating it across every ringing note. Therefore:

- `bend` and `vibrato` are note fields the fretting hand produces → **incompatible with a natural
  harmonic** (E9), with no ambiguity left to resolve.
- a bar dive lives at beat scope → **there is no note cell for it to occupy**.

**The whammy feature itself is deliberately NOT specced here** (user asked; answer was no, not yet).
It is a whole feature — notation on both surfaces, detection of a continuously sliding pitch,
scoring, and how a dive interacts with sustain — and belongs in its own roadmap plan rather than
widening this one. What is recorded now is only the piece the *format* needs, because the restructure
below is imminent and discovering later that whammy wants to be a note field would mean restructuring
twice.

**Already true today, worth knowing:** the importer sees whammy and drops it knowingly, reporting
*"N whammy-bar beats were imported without their bar dives"* (the whammy diagnostic at the end of
`gp_chart_builder.cpp`). So source charts in the corpus already carry data we discard — this is a
recorded gap with a count attached, not future-proofing.

**Tripwire:** if whammy is ever modelled as a **note** payload rather than beat-scoped, **E9 reopens**,
because the finger-versus-bar distinction would then matter inside a note again.

## What the 2026-08-08 review left open

The deep-review pass swept the full cross-product and closed every recording gap it could close
from the settled criterion alone (see the rendered matrix below). What it could NOT close is one
question cluster, plus a handful of newly recorded default-allow cells the sign-off should glance
at:

~~**The fretted-harmonic cluster**~~ — **SETTLED 2026-08-08 as walkthrough D1** ("Adopt all of
it"): a fret-hand harmonic is exactly `fret == 0` + node + neither tapping-hand attack; `fret > 0`
+ node + non-`Pinch` is the picking-hand-damped family (fretted tap, harp, artificial), whose hand
placement `fretFor` now reads correctly; E7/E9/E19 re-keyed on "no real stop"; E21 generalized to
the physical stop (capo when `fret == 0`); E22 enforced. Capo'd naturals store `fret = 0` under
the user's 0-means-open convention — the capo never appears as a fret number. See the E-table and
overlay rows, all updated.

**The capo frame question — the storage half is settled (D1), the import half is D9.** RockHero's
`fret` is absolute with 0 meaning the open string, capo'd or not, and frets 1..capo are invalid
(validation for that is gated on D9 — enforcing it before GP's frame is measured could reject
valid imports). What remains empirical: whether GP's *ordinary* note frets are nut-absolute or
capo-relative, which decides whether import must shift them by the capo. The harmonic labels lean
capo-relative (the capo-1 score's 7.0/8.2 are standard open-string-family labels — correct
capo-relative, junk absolute); the decisive test is whether capo'd scores use frets 1..capo.

**Newly recorded cells, closed by the criterion, flagged for the glance:** Palm + Pinch (the
palm-muted squeal — the single most common pinch context, previously resting on silent
default-allow); Pinch + slides / slide_out / vibrato (a pinch's fretting hand presses a real
fret); Natural + tremolo (re-exciting a ringing harmonic); Full mute + tremolo (the tremolo-picked
dead note — texture, not pitch, so the full-mute principle allows it); bend + slides on one note
(allowed — a coherent sequential reading exists; simultaneity is not representable as distinct
data); slides + slide_out (already structurally governed: the slide-out must end strictly after
every waypoint); vibrato and tremolo on zero-sustain notes (coherent — the note still sounds).

~~**Newly recorded open cells for the ruling**~~ — **RULED 2026-08-09 (walkthrough D5/D6):** Full
mute + `Hammer` / `Tap` / `Pull` all **allowed** as E24 (the ghost legato of funk and percussive
fingerstyle), and E10 stays whole — the muted pre-bend remains excluded with the bend it belongs
to, reopening only on real chart evidence.

**Relational refinements recorded (enforcement-pass material, no format impact):** E5's
"predecessor's fret" must mean the *released* fret (last slide waypoint, else the fret) or a
predecessor that slid away breaks the comparison; a `PickSlide` predecessor cannot justify a pull
(its fret is picking-hand travel — same physics as E19); a fully-muted predecessor CAN (its finger
is a real press, and releasing it is the ghost pull); "which note is the predecessor" is
unambiguous because duplicate onsets per (position, string) are already invalid; `Hammer`
deliberately has no predecessor constraint (hammer-from-nowhere is the left-hand tap); `Pinch` has
no relational constraints at all. The E5/E19 interplay and the exhaustive invalidating-edit
inventory live in `docs/plans/in-progress/legato-authoring-model.md`.

**Transitive impossibilities, made visible so verb guards know them:** Full + Pinch (E8 + E20 —
representable in a saved file until E8 is enforced); Full + any node-bearing note regardless of
attack (E8); PickSlide + anything, node included (E2, enforced). The full-mute verb on a pinch,
and the pinch verb on a full-muted note, must refuse or clear — per-pair verb behavior
(refuse vs. clear vs. convert) is otherwise undecided for every forbidden pair and belongs to the
enforcement pass.

## The full matrix, rendered for sign-off

Legend: **OK** = allowed (default or by rule n); **FORBID En** = forbidden by rule n; **REQ** =
required; **[enf]** = enforced in `chart_rules.cpp` today; **OPEN** = awaiting the user's ruling;
**DEFER** = deliberately unsupported for now. When a node is present, the harmonic overlay (second
table) overrides the attack row.

### Attack × payload (no node present, except the first column)

| attack | node? | Palm | Full | vibrato | tremolo | accent | bend | slides | slide_out |
|---|---|---|---|---|---|---|---|---|---|
| **Pick** | OK (picked natural) | OK | OK (dead note) | OK | OK | OK H3 | OK | OK | OK |
| **Pinch** | **REQ E20 [enf]** | OK (muted squeal) | FORBID E8+E20 | OK E9 | OK | OK H3 | OK E9 (bent squeal) | OK | OK |
| **Hammer** | OK E13 (rare) | OK | OK E24 (ghost hammer) | OK | OK | OK H3 | OK | OK E17 | OK E17 |
| **Pull** | **FORBID E12** | OK | OK E24 (ghost pull) | OK | OK | OK H3 | OK | OK E17 | OK E17 |
| **Tap** | OK E13 (tap harmonic) | OK | OK E24 (ghost tap) | OK | OK | OK H3 | OK | OK E17 | OK E17 |
| **Pop** | OK E14 | OK E15 | OK E15 | OK | OK | OK H3 | OK | OK | OK |
| **Slap** | OK E14 | OK E15 | OK E15 (slapped dead note) | OK | OK | OK H3 | OK | OK | OK |
| **PickSlide** | FORBID E2 [enf] | FORBID E2 [enf] | FORBID E2 [enf] | FORBID E2 [enf] | FORBID E2 [enf] | **OK** (D4: aggressively played scrape) | FORBID E2 [enf] | OK (optional turnarounds) [enf] | **REQ E2 [enf]** (the unpitched terminal, at sustain) |

Plus E4 (unenforced): `Hammer` and `Tap` require `fret > 0 || harmonic_node.has_value()` — the
form is final; D1's 0-means-open convention removed the capo caveat.

### Harmonic overlay (node present — overrides the row above)

| configuration | bend | vibrato | slides | slide_out | Palm | Full | tremolo | as Pull's predecessor |
|---|---|---|---|---|---|---|---|---|
| **Fret-hand harmonic** (node + `fret == 0`, attack Pick/Hammer/Slap/Pop) | FORBID E9 | FORBID E9 | FORBID E7 | FORBID E7 | OK Q1 | FORBID E8 | OK | FORBID E19 |
| **Open-string tap harmonic** (node + Tap, `fret == 0`) | FORBID E9 | FORBID E9 | FORBID E7 | FORBID E7 | OK | FORBID E8 | FORBID E23 | FORBID E19 |
| **Harmonic over a real stop** (node + `fret > 0`, non-Pinch — the fretted tap, harp, and artificial family) | OK | OK | OK | OK | OK | FORBID E8 | FORBID E23 iff attack is `Tap`, else OK | OK (the stop is pressed) |
| **Pinch** (node + Pinch) | OK E9 | OK E9 | OK | OK | OK | FORBID E8+E20 | OK | **OK** (E19 note) |

**D1 re-keyed the natural-only rules (2026-08-08):** E7, E9 and E19's physics is "no real stop is
pressed," so they apply to `fret == 0` + node + any non-`Pinch` attack — the open-string tap
harmonic included — and stop applying the moment a real stop exists (`fret > 0`), where bending,
sliding, and pulling off are ordinary fretting-hand work. Note the two discriminators differ
deliberately: E7/E9/E19 include the `Tap` attack (nothing pressed is nothing pressed), while
`fretFor` and E22 exclude it (an open-string tap harmonic's node belongs to the picking hand, so
the fret hand is not there). "Stop" is not a field — it is what `fret` *means*; the physical stop
is derived (`fret == 0` → nut or capo).

Universal, all enforced: node in (0, 48]; node > the physical stop (E21 — the fret, or the capo
when `fret == 0`); and a fret-hand harmonic's node on the neck (E22).

### Mute × payload

| mute | node | vibrato | tremolo | accent | bend | slides | slide_out |
|---|---|---|---|---|---|---|---|
| **None** | OK | OK | OK | OK H3 | OK | OK | OK |
| **Palm** | OK Q1 | OK | OK | OK H3 | OK Q2 | OK Q2 | OK Q2 |
| **Full** | FORBID E8 | FORBID E11 | OK (texture) | OK H3 | FORBID E10 | OK E10 | OK E10 |

The full-mute principle: forbid everything **pitch-valued**, allow everything **position-valued**
(and textures).

### Relational rules (validation only, never structural)

| rule | statement | status |
|---|---|---|
| E5 | `Pull` requires a same-string predecessor whose **released** fret is higher, and which is releasable (not a scrape, not a fret-hand harmonic) | recorded; unenforced |
| E6 | Legato direction derives from that relationship | recorded; the editing workflow is specced in the legato doc |
| E19 | No pull FROM a fret-hand harmonic; pull from a pinch is allowed | recorded; unenforced |
| — | `Hammer` has no predecessor constraint (deliberate: the left-hand tap) | recorded |

### Open items gating sign-off

The open decisions now live as a worked queue with per-item guidance in
**`docs/plans/in-progress/technique-review-walkthrough.md`** (opened 2026-08-08 at the user's
direction, so the list survives any session): ~~the fret-floor/capo cluster (D1)~~ **adopted and
shipped**, the scrape's payload shape (D2–D4, which absorbs H3 — the user now leans
accent-on-everything, un-deferring the scrape cell), ghost legato and the pre-bend question
(D5–D6), the E5 derivation-vs-validity split (D7), the emphasis axis (D8), and the GP capo-frame
measurement (D9).

Enforcement reality after the review: **E1-remnant, E2, E20, E21 are enforced; everything else is
recorded only** and becomes code in the enforcement pass (D12), gated on the walkthrough closing.

**Recorded future technique (no decision needed):** the side-of-pick tap — a tap performed with
the pick's edge, itself slidable — is distinct from the pick slide in many aspects (user,
2026-08-08) and may need its own representation later. Do not force-fit it into `PickSlide` or
`Tap` when it surfaces.

## SHIPPED 2026-08-08 — the collapse, and what it actually cost

The harmonic field is gone. `NoteHarmonic` is deleted, `Pinch` joined `NoteAttack`, and `touch`
became `harmonic_node` (the user's name: it keeps the word "harmonic" that dropping the type would
have lost, while staying honest about the value — `.has_value()` reads "has a harmonic node" and
`*harmonic_node` reads as a position, where a bare `harmonic` holding `12.0` would not).

**Wire format**, changed in place per the standing no-migration rule:

| Before | After |
|---|---|
| `"harmonic": "natural"`, `"touch": 12.0` | `"harmonicNode": 12.0` |
| `"harmonic": "pinch"`, `"touch": 17.0` | `"attack": "pinch"`, `"harmonicNode": 17.0` |
| `"harmonic": "pinch"` | `"attack": "pinch"` |

**The reader refuses either old key** rather than ignoring it. Ignoring would have loaded an
un-reimported package while silently dropping every harmonic in the chart; the refusal names the fix
instead. It is a tripwire, not compatibility — delete it once the corpus is re-imported
(`docs/tracking/backlog.md`).

### What the predictions got right, and the one they got wrong

Right: E1's disagreement half became unrepresentable, E3 became unviolatable, and the render
predicate simplified. After a naming iteration it settled as the attack-only `nodeIsOnNeck` beside
an inline `harmonic_node.has_value()` at each call site — the inline spelling is what keeps the
dereference visible to `bugprone-unchecked-optional-access`, which cannot see through a wrapper.
(An interim `isHarmonic` wrapper for the presence half was deleted by the 2026-08-08 review: two
spellings for one predicate, and the wrapped one was the checker-hostile spelling.)

Wrong, then corrected: I claimed **"a pinch may legitimately carry no node"**, reasoning that Guitar
Pro's `HarmonicFret` is a separate optional property and that players do not aim for a particular node.
The user rejected it on the concept — a pinch *is* picking while damping a node, so the overtone that
squeals is **determined** by where the thumb lands — and asked for the import side to be diagnosed
rather than the model bent around it. They were right, and measurement settled it: across a **118-file
Guitar Pro corpus, all 207 harmonics carry an `HarmonicFret`, every one of the 56 pinches included**.
The `std::optional` in the parser is defensive coding, and I mistook it for evidence about GP's data.

So a pinch requires its node (E20), `chart_rules` enforces that, and node presence alone asserts
the harmonic rather than the two-part test the wrong claim forced.

### The node's range, and how high a harmonic to support

Settled 2026-08-08. The first cut range-checked the node against `g_max_fret` (30), which conflated two
quantities: a **fret** must be a real neck position, a **node** can sit anywhere along the vibrating
string, and a pinch's thumb grazes *past* the neck (real scores use 24.0 — the 4th partial's
bridge-side node, since `12*log2(4) = 24`). That bound would have rejected every bridge-side node from
the 6th partial up. Replaced with `g_max_harmonic_node = 48.0`, which is `12*log2(16)` exactly.

**How high is worth supporting?** Three independent lines put the practical ceiling near the **8th
partial**:

| Line of evidence | Ceiling |
|---|---|
| **Ergonomics** — the nut-side node of partial *n* is at exactly `L/n`, so adjacent nodes are `L/(n(n+1))` apart: 9.0 mm at the 8th, 7.2 mm at the 9th, **2.1 mm at the 17th**, against a 10-15 mm fingertip | ~8-9, and it is a *hard* limit no amount of gain defeats |
| **Literature** — partials 2-5 "easiest to produce and most audible", above them "nearly inaudible without the overdrive of an amp", the "stratospheric" band between frets 2 and 3 being partials 6-9 | 5 acoustic, ~9 amplified |
| **Real charts** — the 118-file GP corpus's node values map to about the 8th | ~8 |

Precision is not the binding constraint: one decimal separates every distinct node through the **17th**
partial with zero collisions, first failing at the 18th (0.990 and 1.050 both round to 1.0). So the
16-partial bound costs nothing and leaves roughly double the headroom anything playable needs.

**The bound is deliberately permissive, and that is not the same question as the picker.** A bound can
only ever *reject a legitimate chart* — including a GP import we do not author — so it is set to refuse
junk and nothing more. Keeping the option list short is a **UI** concern: when harmonic authoring is
built, the picker should offer partials 2-8 (the nut-side nodes plus the named bridge-side ones: 7/19,
5/24, 4/9/16), not the 79 nodes this bound admits.

A related discovery: **GP's `HFret` values are conventional labels, not exact physics.** The true 8th
partial node is 2.313 but GP writes `2.4`; the 5th is 3.863 but GP writes `4.0`. The format therefore
stores what a chart *says* and must never snap a node to a computed ideal.

### 2D now labels a fret-hand harmonic with its node

Shipped 2026-08-08 (user: *"We should also add 2D fractional note label for harmonics now"*).
`tabNoteHeadText` in the paint core returns the node for a harmonic whose node is on the fretboard and
the fret otherwise, with a trailing `.0` dropped so 12 / 7 / 5 stay as narrow as an ordinary fret
number and only genuinely fractional nodes (3.2, 14.7) pay for the glyphs. **A pinch keeps its fret**,
per the same user message — *"pinch harmonics can come later because the fret is accurate and I'm not
sure how we should represent the node in 2D for pinches"* — which is exactly the split
`nodeIsOnNeck` already draws, so no new predicate was needed. Worth an eye on screen: labels
can now reach four characters (`14.7`) where a fret reached two.

### A live data-loss bug the collapse exposed

Import stored the node **only when it differed from the fret** — harmless when a separate field
carried the harmonic, but fatal once the node *is* the harmonic: a GP natural harmonic at fret 12 with
`HarmonicFret` 12 would have imported as **not a harmonic at all**. Verified by reverting the fix and
watching `isHarmonic` return false. Import now always sets the node for a fret-hand harmonic, falling
back to the fret, which also removes the old double encoding where an absent node silently meant "the
fret itself" and drew half a fret off.

### One guarantee traded, not gained

Honest accounting: the superseded `Harmonic { Kind; node }` bundle would have made a node required
for a pinch **structurally**. The collapse cannot, because the attack is set independently of the
node — so the guarantee exists **by rule instead** (E20, enforced), which is weaker than
unrepresentable but is a guarantee all the same. (An earlier revision of this paragraph argued the
requirement should not exist at all "since GP often has no node to give" — that was the corrected
wrong claim above; GP always supplies one.) The collapse trades structural-for-rule on that one
requirement in exchange for deleting the enum, the disagreement state, and E3. Worth it, but not
free.

### A consequence of pinch living on the attack

Switching a note's attack to `PickSlide` now destroys its pinch-ness, where the old shape kept
`harmonic == Pinch` latent in memory for the "switch back and restore" behavior. That is inherent —
one field cannot hold two attacks — and currently unreachable, since the scrape toggle has no UI
route. Recorded so it is not mistaken for a regression later.

## SETTLED 2026-08-08: `fret` is the stop — and 0 is the open string, capo'd or not (D1)

**Revised the same day by D1** (user: *"Our fretting is absolute... the fret where the capo IS is
treated as 0... 0 means 'open string' even with a capo and each fret is absolute"*): a capo'd
natural harmonic stores `fret = 0`, not the capo number — the capo never appears as a fret. The
*physical* stop is derived (`fret == 0` → the capo), which is what E21 now compares against, and
`fret == 0` + node + neither tapping-hand attack becomes the complete fret-hand-harmonic
discriminator: it needs no capo context, it fixes `fretFor` for the harp/artificial-harmonic
family (`fret > 0` + node + non-`Pinch` = fretting hand pressing the stop, picking hand damping
the node), and it re-keys E7/E9/E19 onto "no real stop" so bending or sliding a harmonic held
over one stays legal. Sub-capo fret validation (frets 1..capo invalid) is part of the convention
but **gated on D9** — enforcing it before the GP frame is measured could reject valid imports.
The section below keeps its original wording as the record of the step that got here.

The user asked whether `node >= fret` should be a rule, then settled the representation it depends on:
*"natural harmonics do in fact need to have fret assigned to 0 OR in songs with a capo, it must be
defined at the capo. That seems like the only truly correct way to represent them."*

**`>=` was not right either, and that was the tell.** Guitar Pro has natural harmonics at fret 6 / node
5.8, fret 3 / node 2.7, fret 15 / node 14.7 — 9 of 109 measured — whose node sits *below* the fret,
because GP rounds the **fret** to an integer while the node is the true position. No comparison rule
survives that, because `fret` was carrying two meanings: the **stop** for an ordinary note, a pinch, or
a tap harmonic, but a **rounded copy of the node** for a natural harmonic, which has no stop at all.
Same shape of defect the harmonic-field collapse removed — one slot, two meanings.

`fret` is now always the stop: 0, or the **capo**, which is what stops a capo'd string. So
`node > fret` is universal and enforced — strictly `>`, since a node *at* the stop **is** the stop and
sounds nothing.

**The capo point fixes real data rather than only tidying the model.** A capo-1 score in the corpus
writes its natural harmonics at 7.0 and 8.2: nut-referenced positions that are *not* nodes of the
capo'd string, whose 3rd partial sits at 8.02. As written they would not ring. GP is capo-blind here, so
import now resolves the notated label to a partial **offset** against an open string and then places it
against the real stop. The importer's own regression test carries a capo of 2, where "3.2" correctly
becomes 5.156 rather than 3.156.

**Two helpers, and the naming iteration that got there.** The user pushed back twice — *"handFret
still reads really odd"*, *"something just smells about this"* — and the smell was real: I had been
pushing **policy** into general-purpose accessors, which is why they kept needing contorted names.
Fixed by separating the pure question from the policy, then collapsing what was left:

| helper | question | answer |
|---|---|---|
| `nodeIsOnNeck(attack)` | is the node somewhere a display can point at? | everything but a **pinch**, whose thumb grazes out over the body. A predicate about the *attack* alone, so callers keep `harmonic_node.has_value()` inline — plainer, and visible to the optional-access checker, which cannot see through a wrapper. |
| `fretFor(note)` | which fret is the **fretting hand** on? | the node's fret for a fret-hand harmonic; the stop for a pinch or a two-hand tap, whose node is the *other* hand's; the fret otherwise. |

`fretFor` uses **`ceil`**, and that was a third correction. Fret `N` spans wire `N-1` to wire `N`
(`highwayNoteCenterX` is their midpoint), so a node at 2.669 lies in fret **3** and 3.156 in fret **4**.
A fret-hand window over `[f, f+w-1]` covers fret units `[f-1, f+w-1]`, so an edge harmonic is only
reliably covered when `H-1 <= node <= H`. Measured over every node below fret 25: **`floor` fails 18
times, `round` 7, `ceil` never.** The user raised the concern (`round` was unsafe — correct) and
proposed `floor`, which inverts because fret `N` spans `[N-1, N]` rather than `[N, N+1]`; they confirmed
the span against a real guitar.

**Not every rounding is that question.** `noteFretboardX` in the renderer once floored the node and
lerped to the next wire. That is *interpolation*, not containment — and since `highwayFretLineX` takes a
fractional coordinate and is linear in it, the whole dance collapsed to one call with no rounding at
all. Removed, because a stray `floor` sitting next to a `ceil` rule costs a reader time.

**The naming iterated to its final shape** on the user's repeated objections (`fretHandFret` and
`fretboardHarmonicNode` verbose; `handFret` "still reads really odd"; `anchorNode` redundant with
the field): the interim names were dissolved entirely into the two helpers in the table above —
`fretFor` absorbed the hand-fret question, and the anchor predicate became attack-only
`nodeIsOnNeck` beside an inline `has_value()`.

**Still inert:** `capo` is stored, imported, and surfaced in the package description, but consumed by
nothing else — no projection or renderer offsets by it. Whether note frets are nut-absolute or
capo-relative could not be settled from 2 capo'd scores; it does not block this, because `node > fret`
holds under either convention and only the literal numbers differ.

## Recovering a node from a source that records only a fret

Settled 2026-08-08. Both importers face it, and neither needs a bespoke algorithm: a source's fret for a
natural harmonic **is** a rounded label for the node, so snapping recovers it. Worked examples:

| source says | resolves to | partial | error |
|---|---|---|---|
| fret 5 | 4.980 | 4th | 0.020 |
| fret 3 | **3.156** | **6th** | 0.156 |
| fret 9 | 8.844 | 5th | 0.156 |
| fret 12 | 12.000 | 2nd | 0.000 |

Fret 3 resolves to the 6th partial, *not* the 7th at 2.669 — distance decides, and the 6th is less than
half as far. **And eight integer frets have no harmonic near them at all**: 1, 11, 13, 14, 18, 20, 21,
23, missing by 0.669 to 1.312. A source naming one of those is bad data, and snapping anyway would move
it a whole fret and sound a *different partial*, so those drop the harmonic with a diagnostic. Half a
fret is the threshold: real labels land within 0.331 of a true node, implausible ones miss by 0.669 or
more.

A **pinch** cannot use this, because its fret is a stop rather than a node label and the source records
no partial — so it defaults to the octave (the lowest-order harmonic available at any fret, hence the
easiest to ring), also with a diagnostic.

**Implemented in the GP importer 2026-08-08** — the review found this section was recorded as
settled but never built: snapping was unconditional and both diagnostics were missing, so a junk
label silently moved a whole fret. The importer now applies the half-fret threshold (implausible
natural labels drop the harmonic with a conversion note; unusable stopped-harmonic labels fall to
the octave with one), and the same pass fixed Guitar Pro's fuller harmonic-type vocabulary: `Tap`
harmonics keep their stop and become the `Tap` attack (the natural path had been erasing the stop
with the capo), and an open-string pinch on a capo'd track now speaks from the capo. `Semi`
imports as a **pinch** (user ruling, same day: a semi-harmonic is a pinch whose fundamental keeps
ringing — a pinch not fully executed — so the pinch is the honest nearest technique until the
format distinguishes them, kept loud with its own conversion note). `Feedback` stays deliberately
unsupported — feedback needs a real amp in the room, which headphone play cannot produce — and
drops the harmonic loudly along with unknown types. `Artificial` still imports as
`Pick` + stop + node — faithful data whose hand placement awaits the fretted-harmonic cluster
ruling above. The standalone converter's psarc source has no semi/feedback/tap types, so only the
threshold parity matters there (already present).

## Hardening the format: what can become impossible, and the ceiling

The project already prefers this shape — "sum types over inheritance… so illegal states can't exist"
is recorded editor-core practice — and format changes are cheap here: the standing rule is that
formats **change in place**, with no migration and no version bump, because the only users are us.
So the constraint is blast radius, not compatibility.

### Can be made structurally unrepresentable (intra-note)

1. ~~**Make the node required and drop the kind**~~ — **SHIPPED 2026-08-08** as the collapse above:
   `std::optional<double> harmonic_node` where presence *is* the harmonic. Its three wins landed: a
   node-without-a-harmonic and a harmonic-without-a-node are both unrepresentable (E1's
   disagreement half deleted real enforcement code); one name in every case with the attack saying
   which hand; and the wrong-anchor defect died with the double encoding — the importer now always
   produces a node, and the renderer's absent-node path is gone. The one caveat is recorded under
   "One guarantee traded" — the *pinch* half of "required" is rule-enforced (E20), not structural.
2. **Make the scrape its own variant — DEMOTED 2026-08-08, likely not worth it.** The user
   challenged the cost (D2 discussion): scrapes must respect minimum note distance, sustain
   overlap, and every other note-interaction rule, so wouldn't a variant duplicate all of that?
   Two clarifications and a re-assessment, recorded so the reasoning survives:
   - The proposal was never a separate entity — the variant lives **inside** `ChartNote` (this
     section's own words: "position/string/fret/sustain plus a sum type for the articulation"),
     so a scrape stays one event in the one sorted stream and every relational mechanism
     (min-distance, 40-Q2-B overlap normalization, duplicate onsets, sorting, selection, FHP)
     stays single-implementation. The real cost is **dispatch breadth**: every consumer reading
     `note.mute` / `note.bend` / `note.vibrato` switches on the variant — the blast-radius bullet
     below, mechanical but wide.
   - The walkthrough shrank the payoff: D4 pulls `accent` out of the exclusion set, and D2 has
     the scrape *using* `slides` and `slide_out`, so the variant's remaining structural win is
     five excluded fields plus the required terminal — which one enforced rule already covers —
     plus deleting the in-memory override mechanism. Weak against the breadth cost.
   Current lean: enforced rules plus verb guards suffice; revisit only at the format-shape step
   if validation churn proves otherwise. D2's semantics (required `slide_out` terminal, optional
   turnaround waypoints) are decided independently and implemented in the flat struct.
3. **Derive a scrape's `sustain` from its path.** If the variant stores the path and exposes `sustain`
   as the last waypoint's offset, then "the path ends exactly at `sustain`" is true by construction —
   an invariant that currently needs a rule, a normalization step, *and* care in three planners.
4. ~~**Put `Pinch` only on the picked form** (E3), and `tremolo` only on the picked form (H1)~~ —
   **dead both ways**: `Pinch` became an attack (shipped) and H1 was rejected (tremolo is orthogonal
   to attack).
5. ~~**A dead-note form**~~ — the conditional died with H4; the dead note's payload set is settled
   by the full-mute row instead (no node/bend/vibrato; yes slides/slide_out; tremolo and accent per
   the principle and H3), which a future sum type may or may not bother making structural.

That points at `ChartNote` becoming position/string/fret/sustain plus a **sum type for the
articulation that owns the fields legal for it**, rather than a flat struct of independent optionals.

### Cannot be made structural — the ceiling

**A per-note type cannot express a relational invariant.** These stay validation, permanently:

- E5, `Pull` needing a higher predecessor, and E6, the derived direction — both depend on *other notes*
- minimum sustain distance and the 40-Q2-B overlap normalization
- a scrape's "must keep traveling" (needs consecutive waypoints, so it is intra-*payload* and could be
  structural with a non-empty, strictly-changing sequence type — worth considering, unlike the rest)

So hardening has a real boundary: **intra-note combinations can become impossible; anything that reads
a neighbour cannot.** Worth stating plainly so the effort is not oversold.

### Costs to weigh before committing

- **Blast radius.** Every consumer that reads `note.attack` / `note.mute` / `note.bend` changes: both
  projections, both renderers, the GP importer, all six `chart_edits` planners, the rules, and the
  document serializer. The importer is the worst of these — it builds notes field-by-field.
- **Generic verbs get harder.** "Set accent on the selection" is trivial across a flat struct and needs
  visitation across a sum type. If accent is compatible with everything (H3, still awaiting
  confirmation), that argues for keeping the universally-compatible fields *outside* the variant.
- **Over-modelling risk.** If most cells turn out compatible, a variant per attack duplicates shared
  fields for little gain. The matrix decides how much structure is justified — which is exactly why it
  comes first.

## Are all harmonics one set of data? (user proposal, 2026-08-07)

*"Does this mean pinch harmonics could be represented by a fret and node the same way as a natural
harmonic so technically all harmonics are one set of data? fret and node? But the ATTACK can differ?
Natural, Pinch, Tap?"*

**Half of this is right, and the right half is already true.**

**The data unifies.** One formula covers every harmonic: the sounding pitch is set by the ratio of the
node→bridge length to the fret→bridge length. Fret spacing is logarithmic, so a node 12 frets above a
stop at fret 5 is exactly the midpoint of the speaking length and yields the octave. `(fret, node)`
therefore determines the pitch for natural, pinch, and tap alike, with no per-kind special case. Two
things follow: `fret == 0` stops being a placeholder on a natural harmonic and starts **meaning** "open
string"; and the unified shape *gains* expressiveness, because it represents picking a harmonic over a
fretted note — one finger pressing 5, another resting on 17 — which is rare but real and awkward today.

**The attack CAN carry it — pinch belongs on the attack axis.** The first draft of this section argued
that pinch and natural "collide on `Pick`", treating `attack` as *what excites the string*. That was a
definition imposed on the field rather than read off it: the enum's own doc says **"How the onset is
produced"**, and under that a pinch qualifies plainly. The thumb graze is not a separate action added
to a pick stroke — it is one compound stroke with its own hand angle and follow-through that players
learn as a single thing. The enum already discriminates at exactly that grain, since `Slap` and `Pop`
are both picking-hand excitation differing only in manner.

**So `NoteHarmonic` collapses entirely** (user, 2026-08-07: *"Pinch is kind of a different 'attack'
though... should that be considered as its own attack?"*). Two questions replace the field:

- **Is this a harmonic?** — does it carry a `node`.
- **Which hand damps it?** — is the attack `Pinch`. Fretting-hand damping is the **default**, what
  happens when nothing says otherwise, so it needs no value of its own.

Every case survives: a slapped natural harmonic is `Slap` + node (E14 holds, and the earlier "two
values, one field" objection evaporates — `Pinch` and `Slap` are *mutually exclusive* attacks, which
is what a single-valued enum wants); tapped is `Tap` + node; hammered is `Hammer` + node; the
two-finger picked harmonic over a fretted note is `Pick` + fret 5 + node 17.

**This is the hardening the document was opened to find.** Three things stop being rules and become
impossible to express:

| | before | after the collapse |
|---|---|---|
| **E1** | enforced rule policing `harmonic` disagreeing with `touch` — one of only *two* rules that exist in code | nothing left to disagree; the rule is vacuous |
| **E3** | `Pinch` requires `attack == Pick`, **unenforced** | cannot be violated when pinch *is* the attack |
| renderer predicate | `harmonic == Natural`, a technique test | **anchor at the node unless the attack is `Pinch`** — pinch is the only case whose damping finger is off the fretboard and over the body |

That last row also fixes a misclassification the field-based reading would have produced: a
right-hand tap harmonic has its damping finger *on* the fretboard at the node, so the head should
anchor there — and the new predicate gives that for the correct reason rather than by accident.

Rules that merely *allow* something (E13, E14) need no restatement, since allowances are the default
and only forbids need rules. Rules that split on the two kinds (E7, E9) restate as "a node with a
non-`Pinch` attack", which is longer to say but no less precise.

**Two consequences to settle before building it.**

- ~~**It interacts with H1.**~~ **Gate removed 2026-08-07.** This was logged as needing a ruling on the
  tremolo-picked pinch. H1 is now rejected outright — tremolo is orthogonal to attack — so
  `Pinch` + `tremolo` needs no special case. It is very hard to execute and probably never charted, but
  hard is not impossible, and forbidding it would take a cross-axis rule that can never be structural.
- **It costs a re-import.** The wire format changes from `"harmonic": "pinch"` to `"attack": "pinch"`,
  and a natural harmonic becomes a bare `"node"` with no harmonic key. The reader rejects unknown
  values outright, so all 39 `.rock` packages need re-importing — the cost already accepted for the
  FramePadding correction.

**A case the collapse forced into view:** you *can* pull off from a **pinch** harmonic, because the
fretting hand is pressing a real fret and therefore has something to release. E19 is correctly
natural-only; that case went unchecked until the model made it unavoidable.

## Tap harmonics: already representable (revised 2026-08-07; vocabulary pre-collapse)

*"We will add tap harmonics as well later which will similarly need a node field."* — the user, when
this was still expected to be a third `Kind`. **Q5 superseded that.** A tap harmonic is a natural
harmonic whose node is struck rather than picked — in post-collapse vocabulary, a **node** plus
`attack == Hammer` (fretting hand) or `Tap` (picking hand) — and it ships the moment E4 stops
testing `fret`. See E13 and the finding above. (The section below keeps its original pre-collapse
wording; `harmonic == Natural` reads as "a node with a fret-hand attack" now.)

What that resolves, against the two consequences logged under the old premise:

- **The notation is not stressed after all.** The harmonic family stays 2-way, so the pinch bar keeps
  its single job and the third notation rule never fires — letters appear only when a family outgrows
  shape and darkness, and this family does not grow. Whether a tap harmonic wants its own cue is a
  question about the **attack**, where `Tap` and `Hammer` already carry marks, not about the harmonic
  family. The notation-consistency thread inherits nothing from here.
- **The new matrix cell resolved itself.** The guess was that a tap harmonic "probably implies
  `attack == Tap`". Inverted: the attack is what *makes* it a tap harmonic, and both tapping attacks
  qualify. So there is no third harmonic-versus-attack rule — E3 (`Pinch` requires `Pick`) and E13
  (`Natural` allows the tapping attacks) are the complete pair.

One thing genuinely deferred: a **pinch** harmonic cannot be tapped (E3), which the user independently
confirmed — *"tap and pinch harmonic cannot be executed together."*

## The 2D node question (and a retracted claim)

**Retracted:** an earlier revision of this document called it a cross-surface *gap* that 2D does not
carry the node. That was wrong. 2D's axes are **time and string** — there is no fretboard axis to
place a node on, so the concept does not apply there and `TabNoteView` omitting it is correct.
3D has a fret axis, which is why the node positions the head there.

**What 2D needs instead is to report the node as a NUMBER** (user, 2026-08-07): the drawn fret number
should show where the hand actually goes, not the integer anchor. Two findings, both computed rather
than estimated (`-12*log2(1-j/k)` for the node at string fraction `j/k`):

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
nodes need somewhere to go**, and a four-character string will not fit the fret font on a 26 px head.
Options to weigh in the notation pass: a smaller font for the fractional part, the fraction as a
subscript, the node beside the head instead of on it, or the integer on the head with the fraction
carried elsewhere.

A pinch harmonic's node is still read by nothing on either surface, waiting on roadmap 25-Q5 — which
the user confirmed on 2026-08-08 is a real future requirement, not a maybe, because the node
determines the squeal's pitch. A pinch stays a normal picked note until then. No format work remains
for it: `harmonic_node` already holds the strike position and already round-trips.

## Next steps

1. ~~User answers Q1–Q12.~~ **Done 2026-08-07** — rules E1–E19, plus the review's E20/E21. Wanting
   a ruling now: **H3** (accent compatible with everything but a scrape — H1 was *rejected*, not
   deferred), the **sign-off on the rendered matrix above**, and the review's open cluster (the
   fretted harmonic / E22, the capo frame, ghost legato).
2. Enforce the settled rules as guards in the Phase 5 verbs, so no verb authors an invalid state,
   and as rules where a chart could already carry the violation from import. **Most of the matrix
   is unenforced today** — the E1 range remnant, E2, E20 and E21 exist in `chart_rules.cpp`;
   everything else is recorded but unchecked, so this step is where the matrix stops being a
   document and starts being a guarantee. E4 needs no accessor (its form is fixed above). Two test
   fixtures currently violate recorded rules and must be fixed with the enforcement: the tab-paint
   fixture pairs a full mute with a pinch (E8+E20), and the GP importer fixture pairs a natural
   harmonic with a bend (E9).
3. *Then* re-open the format shape with the matrix in hand, sizing the sum type against how many
   cells actually turned out incompatible. The count is known and **lopsided — the great majority
   of cells are compatible**, which argues against an elaborate sum type and for the narrow
   hardenings still standing: the scrape as its own variant, and a path-derived scrape sustain
   (item 1 shipped as the collapse). Size the work against that, not against the length of this
   document.
