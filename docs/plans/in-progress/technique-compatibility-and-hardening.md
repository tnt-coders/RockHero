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
- **Q6** `Natural` + `bend` — bending a harmonic. I believe yes; confirming.
- **Q7** `Slap` / `Pop` + `harmonic` — do bass slap harmonics belong in the model?

**Bass techniques.**

- **Q8** `Slap` / `Pop` + `mute` — slap is often muted; which mute, if any?

**Payloads against each other and against articulation.**

- **Q9** `vibrato` + `bend` — both modulate pitch. Do they compose, or does one subsume the other?
- **Q10** `Hammer` / `Pull` + `slides` — a legato slide is its own articulation; can a hammer-on also carry a slide path, or is that two claims about the same transition?
- **Q11** `Tap` + `slide_out` — tapping into a trail-off?
- **Q12** `tremolo` + `slides` / `bend` — a tremolo-picked bend is playable; a tremolo-picked slide?

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

## A cross-surface gap this exposed

**2D and 3D disagree about where a natural harmonic sits.** `TabNoteView` carries `harmonic` but
**not** the node, so the 2D projection cannot see it: 2D draws the head at the integer fret while 3D
draws it at the true node. For a 3.2 harmonic that is most of a fret's width of disagreement between
two views of the same note. A pinch harmonic's node is read by nothing on either surface, still waiting
on roadmap 25-Q5.

## Next steps

1. User answers Q1–Q12 (and corrects E3/E4 or H1–H4 if any are wrong).
2. Enforce the settled rules as guards in the Phase 5 verbs, so no verb authors an invalid state, and
   as rules where a chart could already carry the violation from import.
3. *Then* re-open the format shape with the matrix in hand, sizing the sum type against how many cells
   actually turned out incompatible.
