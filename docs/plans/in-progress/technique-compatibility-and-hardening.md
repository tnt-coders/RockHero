# Technique Compatibility Matrix, and Hardening the Format Against Invalid Combinations

Status: **OPEN — matrix incomplete, awaiting the user's domain calls.** Started 2026-08-07 after the
user observed that *"tap and pinch harmonic cannot be executed together"* and that nothing in the
format or the rules prevents it. Their direction: *"establish this full matrix then re-analyze the
save file format to see if it can be hardened even more to make invalid combinations impossible as
much as it can."*

**Order matters and is deliberate: the matrix settles first, the format second.** Restructuring the
type on a guessed cell means restructuring it again when the cell flips.

This is a **prerequisite for plan 40 Phase 5**, not a follow-up. Phase 5's scope already promises
"illegal-combination guards" but names only one (`touch` only with a harmonic), and every remaining
technique verb needs to know what it must clear or refuse. A verb that authors an impossible state
violates Phase 5's own rule against authoring an invalid state.

## The surface

`ChartNote` carries these technique fields (`chart.h`):

| Field | Values |
|---|---|
| `attack` | `Pick`, `Hammer`, `Pull`, `Tap`, `Pop`, `Slap`, `PickSlide` |
| `mute` | `None`, `Palm`, `Full` |
| `harmonic` | `None`, `Natural`, `Pinch` |
| `touch` | `optional<double>` — the between-fret node position |
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

Each of these is either enforced in code today or physically unambiguous.

| # | Rule | Source |
|---|---|---|
| **E1** | `touch` requires `harmonic != None`, and must be positive and in range | `chart_rules.cpp:186` |
| **E2** | `PickSlide` excludes `mute`, `harmonic`, `touch`, `vibrato`, `tremolo`, `accent`, `bend`, `slide_out`; and requires a `slides` path that keeps traveling and ends **exactly** at `sustain` | `chart_rules.cpp:289-320` |
| **E3** | `Pinch` requires `attack == Pick` | Established 2026-08-07. A pinch harmonic is produced by the pick stroke with the thumb catching the string, so every attack that *replaces* the pick stroke — `Hammer`, `Pull`, `Tap`, `Slap`, `Pop`, and the left-hand tap stored as `Hammer` — excludes it. `PickSlide` already excluded by E2. **Not enforced anywhere today.** |
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
| **E19** | A `Pull`'s predecessor cannot be a **natural harmonic** | User, 2026-08-07: *"Natural harmonic should be able to be followed by a hammer on, not a pull off. Pull off would be physically impossible."* The converse of E12 and a **separate cell**: E12 forbids pulling *into* a harmonic, this forbids pulling *from* one. The fretting hand is touching a node, not pressing, so nothing can be released. **Relational** — see the ceiling. A natural harmonic followed by a *hammer-on* is fine: the string is already ringing and a finger coming down stops it at a new pitch. |

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

**E4's precondition was stated on the wrong quantity.** It required `fret > 0` because a hammer needs
somewhere to land — but a tap harmonic on an open string has `fret == 0` and `node == 12`, so the rule
rejected the very technique E13 allows. The quantity it wants is the **sounding position**: where the
striking finger meets the string, which is `fret` for an ordinary note and `node` for a natural
harmonic. That is the branch already shipped in the renderer as `noteFretboardX`
(`highway_renderer.cpp`, commit `d2fea3fb`), which anchors a natural harmonic's head and tail on its
node. The rule and the render ask one question, so the branch belongs in core as one named accessor —
roughly `soundingPosition(note)` returning `*note.touch` for a natural harmonic with a node and
`note.fret` otherwise — with E4 reading `soundingPosition(note) > 0` and the renderer calling it.
Fold this into the hardening pass; do not write the branch a third time.

**Do not apply that accessor to E5.** E5 tests that a `Pull`'s predecessor sits at a *higher fret*, and
a natural harmonic has `fret == 0`, so E5 rejects one today — by accident. Reading E5 on
`soundingPosition()` instead would make a harmonic at node 12 followed by a pull to fret 5 **pass**,
since 12 > 5, silently allowing exactly what E19 forbids. The two rules ask different questions of the
same number: a hammer needs somewhere to **land**, a pull needs something **pressed**, and a node is a
place but not a press. E19 is the rule that has to carry it, not the accessor.

**`Natural` is a misnomer for what the field means.** A harmonic's pitch comes from the ratio of
node→bridge to fret→bridge, and fret positions are logarithmic, so the midpoint of a string stopped
at fret 5 lies exactly at fret 17. An absolute node plus `fret` therefore determines the pitch in
*every* case, and an open-string natural harmonic is just the `fret == 0` case — not a separate
reference frame. What actually separates `Natural` from `Pinch` is **which hand damps the node**,
precisely the axis the notation already encodes by darkness. Two things follow: a *fretted* tap
harmonic is already representable, since no rule requires a natural harmonic to be open; and the
enum's names describe techniques where the field means a hand. Renaming is a user call, not folded in
here.

**`attack` is the onset; every payload is the sustain.** This one line dissolved Q10 and Q11 and
should be checked first against any future "aren't these two claims about the same thing?" worry.
`attack` says how the note *began*; `mute`, `harmonic`, `vibrato`, `tremolo`, `bend`, `slides` and
`slide_out` all describe what happens *while it rings*. So a hammer-on carrying a slide path is not a
contradiction — it is one note hammered onto and then slid, two different moments. The intuition that
they collide comes from tab notation, where a slide is *drawn* as a connector between two notes and
therefore looks like a property of the transition into one; our model instead puts the slide inside
the note as a path across its sustain.

The separation also explains why `PickSlide` needs the elaborate E2: it is the **one attack whose
meaning extends across the sustain**, which is why it must own the `slides` path and exclude every
other payload. Being the sole exception is a reason to keep an eye on it — the hardening candidate that
splits the scrape into its own variant is really a proposal to stop it from sitting on the `attack`
axis while behaving like a payload.

**The full-mute row reduces to one sentence.** A full mute sounds no pitch, so it excludes everything
**pitch-valued** — `harmonic` (E8), `bend` (E10), `vibrato` (E11) — and allows everything
**position-valued** — `slides` and `slide_out` (E10). That is a cleaner rule than the five-way bundle
H4 attempted, and it generalizes: the question to ask of any future payload is whether it names a
pitch or a place.

**E3 is the one that started this**, and it is worth stating what it costs: pressing the pinch verb
on a tapped note must refuse or clear the tap, and pressing `T` on a pinch harmonic must clear the
pinch. Neither happens today, and neither can be left to validation if Phase 5 is to keep its promise
never to author an invalid state.

## High confidence, wants confirmation

| # | Proposed rule | Reasoning |
|---|---|---|
| **H1** | `tremolo` requires `attack == Pick` | Tremolo picking *is* repeated picking, so an attack that replaces the pick stroke contradicts it. Would exclude `Hammer`, `Pull`, `Tap`, `Slap`, `Pop`. |
| ~~**H2**~~ | **PROMOTED to E13** 2026-08-07 — `Natural` harmonic does **not** require `Pick` | A tapped harmonic — tapping directly over the node — is a real technique, so `Tap` + `Natural` is playable. This is the asymmetry with E3 and the reason the two harmonic kinds cannot share one rule. |
| **H3** | `accent` is compatible with everything **except a scrape** | It is dynamics, orthogonal to how the note is produced. **Amended 2026-08-07:** as originally written it contradicted E2, which already excludes `accent` from `PickSlide`; that cell is deliberately deferred rather than principled — see the deferred group. |
| ~~**H4**~~ | **SUPERSEDED.** It bundled five cells under one "no pitch" argument and got two wrong. Settled instead as: `harmonic` excluded (E8), `bend` excluded (E10), `slides` and `slide_out` **allowed** (E10 — positions, not pitches), `vibrato` still open as Q4. The lesson is that "no pitch" separates *pitch-valued* payloads from *position-valued* ones rather than excluding everything. |

## Possible, but deliberately unsupported for now

A third disposition beside "impossible, forbid" and "pointless but coherent, allow": combinations that
**can** be executed but are so rare that supporting them costs more than it returns. The user's rule
for these (2026-08-07): keep them forbidden for now, allow one **only if it is easy**, and otherwise
revisit the whole group together.

| Combination | Why deferred rather than forbidden |
|---|---|
| **Pinch on a natural harmonic** | User: *"TECHNICALLY possible... But so rare I have literally never seen it."* Already impossible by construction, since `harmonic` is a single enum — so the single-kind shape **is** the enforcement, and a future design wanting it would need a second harmonic entry with its own node. Do not "fix" that into a set without meaning to. |
| **`accent` on a scrape** | Same category (user, 2026-08-07). Currently forbidden by E2. **Checked whether it is easy: it is not.** Removing `accent` from the rule is a one-token change, but it activates the accent-glow path on a plectrum head, and that path was measured during the ALT H work: the glow's fade band clears the plectrum by **0.331 px** at note height 25 where it clears the disc by **1.560**, because the plectrum's widest point is a diagonal shoulder reaching 0.547 of the head against the disc's 0.500. The fix is `glow_size`, which the **round head shares** — so allowing accent means touching every accented note's glow. Deferred with pinch-on-natural. |

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
*"N whammy-bar beats were imported without their bar dives"* (`gp_chart_builder.cpp:2416`). So source
charts in the corpus already carry data we discard — this is a recorded gap with a count attached,
not future-proofing.

**Tripwire:** if whammy is ever modelled as a **note** payload rather than beat-scoped, **E9 reopens**,
because the finger-versus-bar distinction would then matter inside a note again.

## Hardening the format: what can become impossible, and the ceiling

The project already prefers this shape — "sum types over inheritance… so illegal states can't exist"
is recorded editor-core practice — and format changes are cheap here: the standing rule is that
formats **change in place**, with no migration and no version bump, because the only users are us.
So the constraint is blast radius, not compatibility.

### Can be made structurally unrepresentable (intra-note)

1. **Bundle the node into the harmonic, and make it REQUIRED** (user, 2026-08-07):

   ```
   Harmonic { Kind kind; double node; }   // node required, in fret units
   std::optional<Harmonic> harmonic;      // absent = not a harmonic
   ```

   Three separate wins in one small change:

   - **`touch` without a harmonic becomes unrepresentable**, so E1 disappears as a rule.
   - **It is called `node` for every kind**, and the *kind* says which hand produced it — the fretting
     hand for a natural, the picking hand for a pinch. One concept, one name; the same factoring the
     marks use (shape = kind, darkness = hand). An earlier draft proposed a second name for the pinch
     case, which was worse: it invented vocabulary for one concept.
   - **Required, not optional, because the node determines the pitch.** A harmonic without a node is
     underdetermined. And the optionality currently causes a real defect, verified in
     `highway_renderer.cpp:3796`: an absent node anchors the head at
     `highwayNoteCenterX(note.fret)` — the fret-slot middle, which is the *fretted-note* anchor —
     while a present node interpolates from `highwayFretLineX`, the fret **wire**. So `node = 12.0`
     and an absent node on the same twelfth-fret harmonic draw **half a fret apart**, and the absent
     path uses the wrong anchor for a harmonic besides: a harmonic is touched *at* the wire, where a
     fretted note is pressed behind it. Requiring the node removes both the double encoding and the
     wrong-anchor path.

   **Import consequence:** the importer must always produce a node. Guitar Pro supplies
   `harmonic_fret`; where it omits one for an on-fret harmonic, default `node = fret` **at the import
   boundary**, which is where a default belongs rather than as a permanent ambiguity in the format.
2. **Make the scrape its own variant.** A form carrying only `{fret, sustain, path}` makes **all eight**
   of E2's exclusions structural in one move. Today they are eight separate checks in one `if`.
3. **Derive a scrape's `sustain` from its path.** If the variant stores the path and exposes `sustain`
   as the last waypoint's offset, then "the path ends exactly at `sustain`" is true by construction —
   an invariant that currently needs a rule, a normalization step, *and* care in three planners.
4. **Put `Pinch` only on the picked form** (E3), and `tremolo` only on the picked form (H1), if H1 is
   confirmed. Both become unrepresentable elsewhere rather than validated.
5. **A dead-note form** carrying no pitch payloads at all, if H4 survives Q3.

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
  visitation across a sum type. Since accent is compatible with everything (H3), it argues for keeping
  the universally-compatible fields *outside* the variant.
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

- **It interacts with H1.** A tremolo-picked pinch harmonic would need `attack == Pinch` plus
  `tremolo`, which H1 (`tremolo` requires `Pick`) forbids. Tremolo-picking pinches is almost certainly
  not a real technique, since every stroke would need its own thumb graze — but if it is, H1 becomes
  "requires `Pick` **or** `Pinch`". Another reason H1 wants a ruling.
- **It costs a re-import.** The wire format changes from `"harmonic": "pinch"` to `"attack": "pinch"`,
  and a natural harmonic becomes a bare `"node"` with no harmonic key. The reader rejects unknown
  values outright, so all 39 `.rock` packages need re-importing — the cost already accepted for the
  FramePadding correction.

**A case the collapse forced into view:** you *can* pull off from a **pinch** harmonic, because the
fretting hand is pressing a real fret and therefore has something to release. E19 is correctly
natural-only; that case went unchecked until the model made it unavoidable.

## Tap harmonics: already representable (revised 2026-08-07)

*"We will add tap harmonics as well later which will similarly need a node field."* — the user, when
this was still expected to be a third `Kind`. **Q5 superseded that.** A tap harmonic is a natural
harmonic whose node is struck rather than picked, so it is `harmonic == Natural` plus `attack ==
Hammer` (fretting hand) or `Tap` (picking hand), and it ships the moment E4 stops testing `fret`. See
E13 and the finding above.

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

A pinch harmonic's node is still read by nothing on either surface, waiting on roadmap 25-Q5.

## Next steps

1. ~~User answers Q1–Q12.~~ **Done 2026-08-07** — eighteen established rules, E1–E18. What is left
   wanting confirmation is **H1** (`tremolo` requires `Pick`) and **H3** (`accent` compatible with
   everything but a scrape), plus the two deferred-by-choice cells.
2. Enforce the settled rules as guards in the Phase 5 verbs, so no verb authors an invalid state, and
   as rules where a chart could already carry the violation from import. **Most of the matrix is
   unenforced today** — only E1 and E2 exist in `chart_rules.cpp`; E3–E18 are recorded but nothing
   checks them, so this step is where the matrix stops being a document and starts being a guarantee.
   E4 needs the `soundingPosition()` accessor first, shared with the renderer.
3. *Then* re-open the format shape with the matrix in hand, sizing the sum type against how many cells
   actually turned out incompatible. The count is now known and it is **lopsided — the great majority
   of cells are compatible**, which argues against an elaborate sum type and for the narrow hardenings
   already ranked: the required `Harmonic{kind, node}`, the scrape as its own variant, and a
   path-derived scrape sustain. Size the work against that, not against the length of this document.
