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

## Established

Each of these is either enforced in code today or physically unambiguous.

| # | Rule | Source |
|---|---|---|
| **E1** | `touch` requires `harmonic != None`, and must be positive and in range | `chart_rules.cpp:186` |
| **E2** | `PickSlide` excludes `mute`, `harmonic`, `touch`, `vibrato`, `tremolo`, `accent`, `bend`, `slide_out`; and requires a `slides` path that keeps traveling and ends **exactly** at `sustain` | `chart_rules.cpp:289-320` |
| **E3** | `Pinch` requires `attack == Pick` | Established 2026-08-07. A pinch harmonic is produced by the pick stroke with the thumb catching the string, so every attack that *replaces* the pick stroke — `Hammer`, `Pull`, `Tap`, `Slap`, `Pop`, and the left-hand tap stored as `Hammer` — excludes it. `PickSlide` already excluded by E2. **Not enforced anywhere today.** |
| **E4** | `Hammer` requires `fret > 0` | You cannot hammer onto, or left-hand tap, an open string. Not enforced today. |
| **E5** | `Pull` requires a preceding note on the same string at a **higher** fret | Something must be released to sound it. **Relational** — see the ceiling below. |
| **E6** | Legato direction derives from that relationship | `docs/plans/in-progress/legato-authoring-model.md`. **Relational.** |
| **E7** | `Natural` harmonic excludes `slides` and `slide_out` | User, 2026-08-07: *"A natural harmonic CANNOT be slid by definition. It is physically impossible."* A natural harmonic is a light touch at a node, not a press; sliding moves the touch off the node and the harmonic simply stops. A slide is unambiguously fretting-hand travel with no whammy equivalent, so unlike bend and vibrato below this cell has no ambiguity. **Nothing to remove:** searched for supporting logic and found none — the projections only zero a harmonic for scrapes. Record it so nobody *adds* support later. || **E8** | `Full` mute excludes `harmonic` | A full mute sounds no pitch; a harmonic *is* a pitch, so they contradict by definition. The "almost muted harmonic" the user weighed is *partial* damping, which is what `Palm` already means — so full mute never has to stretch to cover it, and that case is Q1 instead. |
| **E9** | `Natural` harmonic excludes `bend` and `vibrato` — **natural only, NOT pinch** | User, 2026-08-07. Same physics as E7: a light touch at a node cannot press the string, so the fretting hand cannot modulate the pitch. A **pinch** harmonic's fretting hand *is* pressing a real fret, so bending it works normally and a bent pinch squeal is a staple — excluding it would make a very common figure unrepresentable. This is the second cell where the two harmonic kinds need opposite answers. |

**E3 is the one that started this**, and it is worth stating what it costs: pressing the pinch verb
on a tapped note must refuse or clear the tap, and pressing `T` on a pinch harmonic must clear the
pinch. Neither happens today, and neither can be left to validation if Phase 5 is to keep its promise
never to author an invalid state.

## High confidence, wants confirmation

| # | Proposed rule | Reasoning |
|---|---|---|
| **H1** | `tremolo` requires `attack == Pick` | Tremolo picking *is* repeated picking, so an attack that replaces the pick stroke contradicts it. Would exclude `Hammer`, `Pull`, `Tap`, `Slap`, `Pop`. |
| **H2** | `Natural` harmonic does **not** require `Pick` | A tapped harmonic — tapping directly over the node — is a real technique, so `Tap` + `Natural` is playable. This is the asymmetry with E3 and the reason the two harmonic kinds cannot share one rule. |
| **H3** | `accent` is compatible with everything | It is dynamics, orthogonal to how the note is produced. |
| **H4** | `Full` mute excludes `harmonic`, `bend`, `slides`, `slide_out`, `vibrato` | A dead note has no pitch to bend, slide, vary, or sound a harmonic on. See Q3 — a raked "dead slide" may be a real counter-example. |

## Open — needs the user's call

Grouped so they can be answered in passes rather than one at a time.

**Mutes against pitch.** Palm muting damps but does not kill the pitch; full muting kills it.

- **Q1** `Palm` + `Natural` harmonic — does damping kill the harmonic, or is it playable?
- **Q2** `Palm` + `bend` / `slides` — palm-muted bends and slides?
- **Q3** `Full` + `slides` — is a raked or dead slide something we want representable? This is the one case that could refute H4.
- **Q4** `Full` + `vibrato` — anything to vary?

**Harmonics against articulation.**

- **Q5** `Natural` + `Hammer` / `Pull` — hammering onto or pulling off to a node?
- ~~**Q6** `Natural` + `bend` / `vibrato`~~ — **RESOLVED as E9**, and the whammy ambiguity that made it
  hard dissolved rather than needing a ruling. See "Whammy is beat-scoped" below.
- **Q7** `Slap` / `Pop` + `harmonic` — do bass slap harmonics belong in the model?

**Bass techniques.**

- **Q8** `Slap` / `Pop` + `mute` — slap is often muted; which mute, if any?

**Payloads against each other and against articulation.**

- **Q9** `vibrato` + `bend` — both modulate pitch. Do they compose, or does one subsume the other?
- **Q10** `Hammer` / `Pull` + `slides` — a legato slide is its own articulation; can a hammer-on also carry a slide path, or is that two claims about the same transition?
- **Q11** `Tap` + `slide_out` — tapping into a trail-off?
- **Q12** `tremolo` + `slides` / `bend` — a tremolo-picked bend is playable; a tremolo-picked slide?

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

## Tap harmonics are coming (user, 2026-08-07)

*"We will add tap harmonics as well later which will similarly need a node field."* A tap harmonic is
sounded by tapping directly over a node, so it is a third `Kind` carrying a **right-hand** node — the
same field, no special case, which is confirmation that the shape above is right.

Two consequences worth logging before it lands:

- **The notation gets stressed.** The harmonic family becomes 3-way (natural / pinch / tap), and the
  pinch **bar** cannot distinguish pinch from tap. Under the notation system's third rule — letters
  appear only when a family has more members than shape and darkness can separate — a 3-way harmonic
  family is exactly where letters become justified. This belongs to the notation-consistency thread,
  but it originates here.
- **A new matrix cell.** A tap harmonic probably implies `attack == Tap`, since the tap *is* how it is
  sounded. That would make it the third harmonic-versus-attack rule beside E3 ("pinch requires Pick")
  and H2 ("natural requires nothing"), and it needs the user's confirmation like the rest.

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

1. User answers Q1–Q12 (and corrects E3/E4 or H1–H4 if any are wrong).
2. Enforce the settled rules as guards in the Phase 5 verbs, so no verb authors an invalid state, and
   as rules where a chart could already carry the violation from import.
3. *Then* re-open the format shape with the matrix in hand, sizing the sum type against how many cells
   actually turned out incompatible.
