# Tail Event Model — what a note's ring carries, and where it is anchored

Status: **DECIDED 2026-08-26 — kept as the option-space record.** The ruling and the resulting design live in `unified-waypoint-model.md`: substrate W (technique-bearing waypoints), decided by the user's coincident-anchor edit-coupling argument, with bends joining the waypoint as an interpolating channel and `fret` made optional to admit mid-travel statements. This file stays because the dead ends are expensive to re-walk. Originally: **OPTIONS FOR A RULING. Not a build plan.** Opened 2026-08-25 at the user's direction after
the vibrato-at-a-waypoint declaration, on the meta-judgment *"I feel like this format is not QUITE
falling into place correctly and may need some re-analysis."* Written against `master` at
`de6a651d`, with the format inventory and the corpus scan that preceded it as the evidence base.
Every candidate below is stated with the cost it actually carries, as written for the ruling.
**The intermediate 2026-08-26 state, kept for the record:** after critique, a parallel
orchestrator analysis, and the user's stress-testing, the field first narrowed to two substrates
with the choice deferred to the bend study — and then the user's coupling argument decided it the
same day without the study (see the header, and §8's supersession note for the narrowing).

Related records, none of them restated below: `technique-review-walkthrough.md` (W9-D/F/G, W10,
W11, W13), `note-sustain-model.md` (the presentation rules this must not break),
`arpeggio-authoring.md` (the fret-optional hold marker, the other anchored-thing ruling of the same
week), `2d-bend-waypoint-redesign.md` (waypoint selectability requirements, parked), and
`file-formats.md` (the chart document, and the 2026-07-23 no-note-references ruling).

---

## 1. What is actually along a ring today

| datum | stored as | anchor | earns a tail | floors the trim |
|---|---|---|---|---|
| `bend[]` | `{Fraction offset, double semitones}[]` | offset from onset | yes | yes, at the last CHANGING point |
| `slides[]` | `{Fraction offset, int fret}[]` | offset from onset | yes | yes, at the last CHANGING fret |
| `slide_out` | `optional{Fraction offset, int fret}` | offset from onset — **fabricated** (W11) | yes | no (compresses with the tail) |
| `vibrato` | `bool` | **none — the whole ring** | yes | **cannot** |
| `tremolo` | `bool` | **none — the whole ring** | yes | **cannot** |

Four anchoring conventions for five things that are all *"something that is true at or from an
offset inside the ring"*, and a fifth convention arriving with the arpeggio hold marker, which is
keyed `(position, string)` in absolute grid space rather than to any ring.

The localizing fact, from the importer: GP has exactly ONE mechanism for a mid-note technique
change — split the ring into two events joined by a continuation, and put the new technique on the
second event — carrying roughly 470 timeline occurrences across ~50 files. Our importer receives
that one mechanism and applies **three different conventions to the same junction**, twice over
(the tie merge and the legato-slide merge state the identical asymmetry verbatim):

- slide -> a hold waypoint at the junction. **Anchor preserved** (waypoints carry offsets).
- bend -> every point's offset rebased by the junction gap. **Anchor preserved.**
- vibrato / tremolo -> `origin.vibrato = origin.vibrato || source.vibrato`. **Anchor destroyed.**

That is the project's own recurring defect — a rule stated twice — stated twice AND wrong the same
way both times, for the same two fields. `chart_presentation.h` records the belief that made it
look principled: *"Whole-note techniques — vibrato, tremolo, emphasis, muting, harmonics — cannot
change mid-sustain and so never appear here at all."* The user's 2026-08-25 declaration overrules
the premise of that sentence for vibrato: it changes mid-sustain, and it happens often.

---

## 2. The scoping finding a ruling should start from

The four user non-negotiables do not weigh equally on the format. Three of them are verbs and
selection, already ruled, with **zero format delta**:

| ask | what it actually needs | already ruled |
|---|---|---|
| bends authorable anchored at a waypoint | an authoring verb; `bend[]` already carries offsets | plan 40 Phase 7 + `2d-bend-waypoint-redesign.md` |
| waypoints selectable | the editor's selection unit widens past `ChartNoteKey{position, string}` | `2d-bend-waypoint-redesign.md` §2026-08-13 |
| `Shift+L` on a waypoint disconnects it | W10's split-at-a-stated-fret, which is *legal at a waypoint* by the signed ruling | W10, 2026-08-12 |
| **vibrato starting at a waypoint** | **a place to put it** | **nothing** |

So exactly one of the four is a format question. That matters: the format decision can be judged on
its own merits instead of being sized to the whole pressure list. What the other three DO establish
is the lattice the format decision must sit on — see next.

## 3. The lattice the project has already ruled

Two signed rulings, read together, already answer *"where along a ring may something be anchored?"*

- **W10, the head-exists law:** a note head exists exactly where something changes — fret or
  technique. Technique verbs split **only at stated frets**: legal at a waypoint or on the
  post-travel hold segment, **refused strictly between waypoints**, because the fret there is
  interpolated travel and a head must sit on a stated fret. Snapping to the nearest waypoint was
  killed as a clamp; rounding the interpolated fret as invented data.
- **W10, digits state the path:** a digit on a slide note's tail creates or retypes a waypoint,
  anywhere. So the charter can always MAKE a stated point where they want one and then apply the
  technique there — "state a waypoint, then split at it — everything stated, nothing guessed."
- **W9-D:** every pitched waypoint draws its own head, sized to the tail.

Consequence, and it is the load-bearing observation of this document: **the anchor lattice for a
state change is the stated-stop lattice, and it is already complete.** Requiring a state change to
sit on a stated stop costs no expressiveness (the escape hatch is ruled and composable) and matches
the corpus exactly, where every real mid-ring change lands at a junction that the importer already
turns into a hold waypoint. A bend point is a different animal — a *sample of a continuous curve*,
not a state — and may legitimately sit anywhere.

That yields the taxonomy every candidate below is judged against:

1. **Curve samples** — bend points. Continuous, sampled, interpolated between samples.
2. **Stated stops** — where the fretting hand is, and (the open question) what modulation is true
   from there. Discrete, held between points.
3. **End attributes** — the unpitched slide-out. W11 already ruled it owns no offset: it IS the
   note's end.

---

## 4. Candidate A — status quo plus fields

**Format.** `vibrato` gains a start anchor: either `vibratoStart: <fraction>` beside the bool, or
the bool becomes `optional<Fraction>` where present-means-on-from-here. `bend[]` and `slides[]`
unchanged; `slide_out` loses its offset per W11 independently. Arrays stay parallel.

**Authoring.** Select waypoint -> `V` writes `vibratoStart = waypoint.offset`. `B` at a waypoint
writes a bend point at that offset (no format change). `Shift+L` splits per W10; the new note's
onset vibrato is read off the origin's start anchor and the origin's anchor is dropped or kept by a
new rule written for the purpose.

**W9-F / W9-G:** unchanged, both still open. **W11:** deleted separately, no interaction.
**W13:** survives for everything except vibrato — which, per the corpus, is 82% of the pressure
(31 of the 38 chain-occurrences that carry a technique on a waypoint), so A does defuse the item in
practice without answering it.

**Importer.** Figure 1 (slide-then-vibrato, 34 occ): `note.vibrato = true; note.vibrato_start =
gap`. Figure 2 (tie-gains-fall-away, 350 occ): unchanged. Figure 3 (vibrato + fall-away on one
note, 30 occ): unchanged, and correct today.

**Cost inventory.**

- Adds a **third** anchoring convention — a lone offset field that is neither a point list nor a
  state on a point — to a design whose complaint is that it already has too many. Every future
  interval technique (tremolo, a mid-ring palm mute, W9-K's downward whammy) re-asks the same
  question and gets its own field.
- Touches all five hand-maintained "payload set" enumerations, which must now agree about a datum
  none of them was written for: `hasSustainTechnique`, `lastChangingPayloadOffset`,
  `presentsNoDeadTail`, and the two `normalizeChartNote` shed arms (dead modulation, fret-hand
  harmonic). Nothing couples them; a missed one is silent.
- Both importer merges gain a rebase arm for the new field, beside the two they already have —
  the same duplication in a third copy.
- Cannot express vibrato that STOPS mid-ring (2 corpus occurrences: vibrato on a glide origin
  merged with a plain landing, which A would still smear). Adding an end offset makes it a span
  and re-asks the anchor question with a second endpoint.
- `SlideViewState`/`NoteViewState` gain a field each with hand-written `operator==`, where a
  forgotten field drops out of equality silently.

**Honest merit.** Smallest delta by a wide margin, ships the declared figure without touching a
renderer's data model, and *if vibrato is genuinely the only interval technique that will ever want
an anchor*, A is correct and everything below is over-general. The corpus supports that reading
narrowly: slide-then-tremolo is **0** occurrences and tremolo+slide on one note is **1**.

---

## 5. Candidate B — the split model

**Format.** Nothing new. Techniques change only at note boundaries; a mid-ring change is authored
by splitting at the stated point into a second note joined by W10's unstruck tie (stored `Legato`,
derived `Continuation` motion, no stored strike). This is the mechanism the source format itself
uses and the one our importer already half-implements.

**Authoring.** Select waypoint -> `Shift+L` promotes it to a note -> `V` on that note. `B` at a
waypoint: still a bend point on whichever note owns that stretch. `Shift+L` disconnect: native,
this is the model's only operation.

**W9-F:** unchanged. **W9-G:** answered by construction — a mute restates wherever a note begins,
so the junction restates iff it is a note boundary. **W11:** unaffected. **W13:** dissolved —
a waypoint that needs its own techniques *becomes* a note, which is exactly the tell W13 records
(*"what it is really saying is that a picked point with its own properties is a note"*).

**Importer.** Figure 1: stop merging when the landing changes technique; keep a `Continuation`
head — already ruled as W10's tie-merge guard. Figure 2: 350 occurrences that today merge to one
note would each become two. Figure 3: unaffected (an end attribute, not a mid-ring change).

**Cost inventory — and this is where the user's non-acceptance earns its reasons.**

- **It re-creates the double-stored landing fret W13 exists to delete.** A pitched glide's target
  is stored as the origin's waypoint fret; if the arrival is also a note, it is stored again as
  that note's `fret`. W13 states the merge is *the only remaining way to remove the duplication*,
  note references having been rejected 2026-07-23. Under B, a 2-link legato chain (12% of chains,
  417 occurrences) fragments into 3 notes with 2 duplicated frets.
- **It grows the binding-onset set, which shortens OTHER strings' drawn tails.** Presentation rule
  1 trims to a margin before *"the first later note at a different grid position on any string"*.
  Authoring vibrato mid-ring on string 3 therefore changes what string 5 draws. Narrow band, real
  coupling, and invisible at the point of authoring.
- **It puts non-events in the note stream, and every gameplay reader must filter them.** The
  scorer counts onsets. A continuation is not struck at all, so "is this an onset the player must
  produce?" becomes a predicate every scoring, streak, and matching path repeats. The project has
  accepted this once already (W10's equal-fret tie) — accepting it as the ONLY mechanism multiplies
  the filter's call sites by the frequency of mid-ring technique change, which the corpus puts at
  ~470 occurrences.
- Undo weight: a technique change becomes a structural edit (note count changes) rather than a
  field edit.
- Presentation rule 3's per-onset-group tail-drop verdict now runs on groups that contain
  continuation heads, which were never in its model.

**Honest merit.** Zero format surface; it is what the source format does; W10 already ruled its
verb, its attack storage, its settle-by-merge, and its refusals. For an **equal-fret** junction it
is strictly correct and lossless — nothing is duplicated, because there is no glide target to
duplicate. That half should survive any ruling below.

---

## 6. Candidate C — the unified tail-event sequence

**Format.** `bend`, `slides`, and `slide_out` collapse into ONE ordered sequence of anchored events
on the note: `events: [{offset, <one event kind>}]` where the kinds are waypoint (fret), bend point
(semitones), vibrato on/off (or a span), and terminal. The view state mirrors it.

**Authoring.** Every verb writes an event at the caret or at a selected event's offset. Selection
is over events. `Shift+L` at an event splits the sequence.

**W9-F:** improved — `unpitched` stops being a per-waypoint bool (whose own doc has to warn that it
does NOT mean "the glide ends here"), and 2D's question narrows to how the ONE terminal draws.
**W9-G:** gains a data answer (a mark begins where its event says it begins). **W11:** falls out —
the terminal carries no offset because it is the sequence's last element by construction.
**W13:** dissolved — an event at an anchor IS the model.

**Importer.** All three figures map cleanly; both merge sites become one operation (splice the
second event list onto the first at the gap), which kills the two-OR-merge-sites duplication
outright.

**Cost inventory.**

- **It merges two things whose invariants genuinely differ.** A waypoint may not sit on a later
  onset of its own string; a bend point may. A waypoint is clipped by the trim; a scrape's terminal
  is pinned by the leg rule; a bend point is clipped but never rescaled. Heterogeneous ordering
  ("strictly ascending" is per kind, not per sequence) means the single sequence still validates as
  several sequences — the parallel arrays reappear as filters.
- **The pitch curve stops being two clean functions.** Fret and bend are two independent physical
  degrees of freedom in different units that ADD in pitch. Two arrays is not a rule stated twice;
  it is two functions stored once each.
- **Runtime cost on a deadline path.** Both renderers sample the tail per frame per note
  (`highwaySlideStateAt`, `highwayBendSemitonesAt`, the 2D polyline and sine). Today each samples a
  small contiguous array of one kind. An interleaved variant sequence makes every sample scan both
  kinds and branch on the alternative. This is a design-time performance decision, not a later
  tuning knob.
- Largest reshape by far: chart types, reader, writer, `file-formats.md`, the normalizer, the
  validator's structural refusals, `presentedChartNotes` and all four of its rules, the projection,
  both renderers, hit-testing, every editor planner, the importer, and the scoring plan.

**Honest merit.** It is the only shape where *"what happens along the ring"* has literally one
home, and three open items (W9-L's flattening, W11, W13) fall out of it rather than being fixed.
Weigh that against the fact that W11 is **already ruled** and W9-L's remedy is **already stated**
("the simpler shape is to stop flattening: the view state mirrors the domain") — both are available
for far less than C costs.

---

## 7. Candidate D — hybrids

### D1 — C for pitch-modulation events only

Merge `bend` and `vibrato` (both semitone-space modulation of the sounding pitch) into one
modulation curve; leave `slides` and the terminal alone. Attractive because 3D already folds
vibrato INTO the bend semitones (`highwayBentNoteY`) and shares one saturation with it.

Killed on evidence: 2D deliberately draws them as two independent marks in one technique band,
which is the surfaces' own accepted divergence, and the corpus says bend+vibrato co-occurrence is
rare. Merging the storage would force the two surfaces to agree on a coupling only one of them
uses, and it would make "is this note bent?" (a chip, a number, a scoring window) unanswerable
without filtering an oscillation out of the curve. It also inherits none of the anchoring win: a
merged curve still has to say when the oscillation starts.

### D2 — Stated stops carry state (RECOMMENDED)

**Format, in place.**

- `SlideWaypoint` generalizes into the ring's **stated stop**: `{offset, fret, vibrato, tremolo}`,
  where the modulation flags mean *"true from here until the next stop says otherwise."*
- `ChartNote` keeps `fret`, `vibrato`, `tremolo` **as the state at the onset** — stop zero, spelled
  as note fields because the onset is also the note's identity for selection, hit-testing, and
  transposition.
- `slide_out` becomes an end attribute carrying a fret only (W11, unchanged).
- `bend[]` is untouched: a curve stays a curve.
- One reading authority, `ringStateAt(note, offset)`, replaces every direct read of a modulation
  flag by anything that draws or scores a tail.

**Why this is the shape.** The decisive property is not the vibrato anchor — it is that **W10's
split and W10's merge become exact inverses**, because both sides of the boundary carry the same
type. Today a note carries state a waypoint cannot, so crossing that boundary is lossy in one
direction, which is precisely the observed importer bug: the merge smears vibrato because there is
nowhere for it to land. Fix the type and the verb becomes reversible by construction rather than by
a guard that must remember every field.

Note what this makes of B: **B is not a rival, it is the verb.** `Shift+L` promotes a stop to a
note onset (its fret and state become the new note's onset fret and state, the remaining stops move
across, the origin's ring ends at the junction) and demotes back on the toggle. D2 is the storage
that makes the promotion lossless. The user declined B *as the vibrato answer* — under D2 that is
exactly right, because a state change should not have to invent a hand-strike boundary the music
does not have.

**Authoring flows.**

- *Select waypoint -> `V`* -> sets `vibrato = true` on that stop. Mid-tail with no fret change:
  the charter states a HOLD stop at the offset (W10's digit rule, or `Insert`, both already ruled)
  and sets vibrato on it. A hold stop at an unchanged fret is already legal, already emitted by the
  importer, and already drawn.
- *`B` at a waypoint* -> a bend point at the stop's offset. No format change; the verb reads the
  selected stop's offset as its anchor. This is the non-negotiable, satisfied by selection alone.
- *`Shift+L` on a waypoint* -> W10's split at a stated fret, now lossless in both directions.
- *Waypoint selection* -> the selection unit widens to a point key; the bend-waypoint redesign's
  2026-08-13 requirements apply unchanged.

**What the open items become.**

- **W11** — already ruled; D2 implements it as "the terminal is not a stop, it is the end."
- **W13** — dissolved for the interval techniques, and answered *in the user's own terms* for the
  rest: the compatibility matrix question ("which techniques are per-onset, which are per-point,
  which are ranged") gets a structural answer — per-onset techniques (attack, emphasis, harmonic
  node) stay on the note because they describe a strike; ranged techniques live on stops. The
  three-bools-on-a-waypoint shape W13 tells us to distrust is avoided precisely because the stop
  and the onset become the SAME struct rather than a waypoint growing note-like fields.
- **W9-L** — the flattening dies: the terminal is not in the stop list, so `SlideViewState` loses
  `unpitched` (a scrape's whole path is unpitched by the note's attack; the terminal by
  definition), and `linkedWaypoint` loses its remaining job.
- **W9-F** — narrowed to one glyph: the only unpitched marks left are the terminal and a scrape.
  D17's "broken rather than solid" lever now applies to exactly one thing.
- **W9-G** — gains a data answer to rule on: a mark's state is derived per segment, so the question
  becomes "does a mark redraw where its state does not change?" That is the head-exists law applied
  to marks, and it is a display ruling this model makes *statable* rather than one it makes.

**Importer.** Figure 1 (34 occ): the merge folds the landing's stop — `stops.push_back({offset =
gap, fret = landing.fret, vibrato = landing.vibrato, tremolo = landing.tremolo})` — replacing the
two `||` lines. Both merge sites become the same operation, so the twice-stated asymmetry collapses
to one statement. Figure 2 (350 occ): unchanged, and W11 removes the fabricated offset. Figure 3
(30 occ): unchanged — vibrato at the onset with a fall-away at the end is exactly a note-level flag
plus an end attribute, and this is the case where the bool was always right.

**Cost inventory, honestly.**

- **Two spellings of one struct.** The onset's state sits on `ChartNote`, every later stop's in the
  array. That is the rule-stated-twice *shape*, and it must be defused deliberately: one reading
  authority (`ringStateAt`), one "anywhere on the ring" predicate, and a naming pass that makes a
  bare `note.vibrato` read as "at the onset" and nothing else. The alternative — lifting the onset
  into the array as stop zero — removes the second spelling but makes `note.fret` a vector access
  in every consumer, adds an "the array is never empty" invariant enforceable only by validation,
  and puts identity (string/fret for hit-testing and transposition) behind an indirection. Recorded
  as the shape that would have made this yield unnecessary if the ring had been modeled as a stop
  sequence from the start; not recommended as a retrofit at this size.
- Presentation rule 2 changes meaning, deliberately: `lastChangingPayloadOffset` becomes "the last
  stop that differs from its predecessor in ANY field", so a vibrato start now FLOORS the trim.
  That is a correctness gain — a tail cut before the vibrato begins presents a note that never
  vibratos, which today it silently does — and it is a visible change in drawn tail lengths that
  the user must sign.
- The five hand-maintained payload enumerations shrink but do not vanish: onset vibrato still earns
  a tail with no stops present, so `hasSustainTechnique` keeps a short list.
- Both renderers change their phase origin: the 2D sine starts at the vibrato-start x rather than
  `onset_x`, and the 3D wobble phases on the stop's seconds rather than `seconds -
  note.start_seconds`. The 3D fold of vibrato into the bend axis survives untouched.
- The projection carries state per segment; `SlideViewState` gains the same fields and their
  hand-written `operator==` entries (the float-equal workaround makes a forgotten field silent).
- The scrape override set is **free**: `savedChartNote` is already the sole authority for what a
  scrape strips, and the validator asks it as a fixpoint, so new fields inside the stop struct are
  covered with no new rule. This is the one place the codebase already solved this class.
- Format changes in place, no migration, no version bump — and the timing is unusually cheap:
  **no local package loads at HEAD today** (the removed `chords`/`shapes` keys already force a
  re-export, tracked as task #78), so the re-export cost of a further in-place change is already
  sunk.

### D3 — Techniques as absolute marks

Follow the arpeggio hold marker's precedent: a second array keyed `(position, string)` carrying
technique state, anchored in absolute grid space rather than to a ring.

Killed, and the reason is worth stating because it draws the boundary law: the arpeggio marker is
right precisely because **no ring exists** where it is placed — the hand is down and nothing
sounds, so there is no host to be relative to, and a marker that resolves to nothing degrades
inertly. A vibrato mark always has a host. Anchoring it absolutely re-creates positional coupling
across two arrays (moving a note must move its marks; the arpeggio doc's own edit-consistency
section scores exactly this cost), stores a position the ring already defines locally, and makes
"is this note vibrato'd" a cross-array search on the render path.

**The law that falls out, and it settles the "another thing anchored along rings" pressure without
merging anything:** *ring-relative offsets for what rides a ring; absolute `(position, string)` for
what exists without one.* Under that law the arpeggio marker is not a fifth anchoring convention —
it is the other side of a stated boundary.

---

## 8. Recommendation — SUPERSEDED 2026-08-26: the decision is DEFERRED to the bend study

> **The D2 recommendation below no longer stands as a recommendation.** After the adversarial
> critique (whose findings are folded through this doc) and a parallel independent analysis run by
> the orchestrator, the user stress-tested both surviving substrates and ruled the decision
> deferred. What follows is the record of that arc and the settled facts; §8's original D2 text is
> kept below as the fullest statement of substrate W.
>
> **Discovered during the 2026-08-26 reconciliation, and it reframes the fork:** substrate S is
> not a new idea — `note-format-and-tablature-plan.md` DECIDED mid-sustain vibrato spans on
> **2026-07-06** ("lands with the next format touch"), with a designed spelling
> (`"vibrato": [{ "offset", "until" }]` objects, omitted `until` = to-end, `true` kept as the
> whole-sustain shorthand, multiple spans for on-off-on pulses) and the OR-smear named as the
> motivating defect. None of the 2026-08-25/26 analyses found it; the debate independently
> re-derived it. So the study's question is properly **confirm or overturn a standing decision**:
> S carries the prior ruling's weight, W carries the newer bend/stop insights that ruling never
> weighed. If S is confirmed, harmonize the spelling there (that plan's shorthand-friendly
> spelling vs this record's explicit-ends lean is an open sub-question — the user has ruled
> neither).
>
> **The two substrates, both standing at full strength:**
> - **S — interval spans on the note.** `"vibrato": [[start, end], …]` beside `bend[]` and
>   `slides[]`; slides unchanged. The user's three stress cases are all sayable with no mediating
>   rule: slide→vib→slide→vib is two spans; vibrato DURING a slide (user: rare but real — this
>   killed the orchestrator's earlier derived-extent form, whose "travel kills vibrato" premise
>   the user overruled) is a span overlapping the travel; a mid-hold vibrato END is a span ending
>   early. The end is stored because the user's cases prove it underivable.
> - **W — technique-bearing stops** (D2 below): `slides[]` generalizes into stated stops carrying
>   state; the user is explicitly open to the current slide save format dissolving entirely
>   ("our current slide save format would probably dissolve completely"), and values that a stop's
>   fret is information — equal fret = held, different fret = travel.
>
> **Settled either way (binding):** vibrato is interval state, never whole-note — multiple regions
> per tail, legal during travel, delayed start and mid-hold end all legal; the whole-note bool and
> the importer's OR-smear are defects; `chart_presentation.h:44`'s "cannot change mid-sustain"
> premise is overruled (correct the header when the model builds). Split/merge is PARITY once the
> booleans die — spans partition and rebase exactly as bend points already rebase at the two merge
> sites, so losslessness stops discriminating between substrates. The per-stop validator/normalize
> rework is a COST, not a wall (user: "the validator could be updated as needed, no?"). The
> dissolve law generalizes (user's formulation): a pending point dissolves at settle iff it
> changes NEITHER the path function NOR the state. And the editing experience the user specified —
> a visible, selectable point at every state change, showing a fret; clear-then-linger-until-
> settle-then-dissolve; vibrato-into-vibrato leaving no point — is achievable under BOTH
> substrates (stored stops, or handles derived from span edges), so the UX does not pick the
> winner.
>
> **The decider: the bend-editing design study** (restart `2d-bend-waypoint-redesign.md` as its
> vehicle). The user: bends are unsettled, uneditable, displayed poorly, with almost no 2D
> vertical space — "bends really throw the biggest monkey wrench into BOTH of these designs."
> If compound bends want per-stop segment structure (offsets bounded by the next stop, carryover
> handled at boundaries), W takes the board; if bends stay a free note-level curve, S's
> independent arrays win on rule simplicity. The study answers compound-bend authoring, carryover,
> and the display's space budget — and its conclusion picks the substrate. Rule once, after it.

**D2 — stated stops carry state** (kept as substrate W's fullest statement). Generalize `SlideWaypoint` into the ring's stated stop carrying
`{offset, fret, vibrato, tremolo}`; keep `bend[]` as a curve; make the terminal an end attribute
(W11); stop flattening it into the view (W9-L); and let W10's `Shift+L` remain the verb that turns
a stop into a note and back.

**The principle it rests on:** *generalize the one authority that already exists rather than
introducing a second.* The ring already has an anchor lattice — the stated stops — and the project
has already ruled that a technique change may only sit on it. What is missing is not a new
anchoring mechanism; it is that the existing anchor cannot hold the thing the music puts there. And
the sharper form of the same principle: **a boundary is only lossless when both sides of it carry
the same type.** Today the note carries state its stops cannot, so every crossing (import merge,
editor split, editor merge) must remember the field list by hand — and the record shows it did not,
twice, for the same two fields.

**Where this yields to correctness, and what shape would have made the yield unnecessary.** D2 adds
two fields to a struct rather than deleting anything, and it leaves the onset's state spelled in a
second place. That is a yield. The shape that would have removed it: a note modeled from the start
as `{attack, string, sustain, stops[]}` with the onset as stop zero — one spelling, no note-level
modulation flags, and the split/merge inverse property for free. The reason not to retrofit it here
is size and identity, not principle: `fret` is what selection, hit-testing, transposition, and the
legato resolver key on. If a THIRD interval technique ever wants an anchor (a mid-ring palm mute,
W9-K's downward whammy channel), that is the trigger to reopen the full lift rather than to add a
third field.

**What NOT to build with it.** Do not move `palm_mute` / `dead` onto stops in this change: the
corpus shows one occurrence of a tie gaining a mute, E25 already removes a dead note's tail
entirely, and W9-G is a display question that D2 makes answerable without needing the data half.
Admission rule for a future field: it must be an interval property of a ringing string that the
corpus shows changing mid-ring.

---

## 9. Open questions a ruling must answer

1. **State or delta?** A stop's flags say "true from here on" (state) or "flips here" (delta).
   Recommend **state** — a delta makes every read a fold from the onset and makes a dropped point
   silently change meaning. Needs pinning either way.
2. **Does a vibrato start floor the presentation trim?** Recommend yes (a tail cut before the
   vibrato presents a note that never vibratos). This visibly changes drawn tail lengths.
3. **Rename the format key?** `slides[]` names a technique the array will no longer belong to;
   `stops[]` names what it holds. Cheap now (re-export already forced), expensive later.
4. **Is vibrato-OFF at a stop authorable?** The corpus has 2 occurrences of vibrato on a glide
   origin with a plain landing. Allowing it costs nothing structurally; refusing it keeps the
   authoring surface smaller.
5. **Does a mark restate at a junction where its state does not change?** (W9-G's display half,
   now separable from its data half.)
6. **Does `SlideViewState::unpitched` die in the same change**, with the terminal leaving the
   flattened list (W9-L)? Recommend yes — the two are one edit.
7. **Selection unit.** Does the editor's point-selection work land here or with the parked
   `2d-bend-waypoint-redesign.md`? Three of the four non-negotiables are gated on it, not on the
   format.
8. **The 2D/3D vibrato model divergence** — a separate mark on one surface, folded into the bend
   axis on the other — is untouched by every candidate here. Rule it in the same pass or record it
   deliberately.
9. **Sequencing.** W13 warned that the W9-B fold would bake today's `{offset, fret}` waypoint into
   the shared view state and have to be reopened; it shipped 2026-08-21, so that reopening is now
   part of this work's cost. Decide whether the chosen substrate lands before W10's build (one
   projection to touch) and before or after the note-sustain branch merges.
10. **The importer default for GP's anchorless vibrato flag** (proposed, awaiting sign-off — the
   ruling-7 shape): when a merged note carries slides, anchor the vibrato at the LAST waypoint
   (the musically-right reading of the corpus's dominant figure — 31 of the 34 slide-then-vibrato
   occurrences arrive through the legato merge); else from the onset. Under substrate S the
   default writes `[[last-waypoint, sustain-end]]`; under W it sets the last stop's state.
