# The Chord Dictionary — authored names and fingerings for derived shapes

Status: **PROPOSED 2026-08-27, future work.** Decided in conversation the night the arpeggio
corrections and the unified keyframe substrate landed; recorded here so the design survives until
the work is scheduled. Session task #118 points at this file. Nothing below is built, and the two
open points at the bottom are the user's to rule at build time.

## Why this exists — the datum that decided it

The frets a span plays (chord) or holds (arpeggio) determine **neither the fingering nor the
name**: the same fret-set has multiple legitimate fingerings, and the same grip takes different
names by harmonic context. Both are underivable, and the NAME is shared identity across
repetitions — inlining it per span would restate one fact fifty times and make renaming a
fifty-edit change, the two-copies defect. By the project's law that only relational choices are
authored, the name is the textbook authored relation. The chord template exists to settle exactly
this. (This overturned an earlier same-day position that no stored chord entity should exist;
that position had weighed only fingerings, where per-instance inline data is defensible. The
name is what changes the verdict.)

No mechanism exists today: chord names died with the stage-C posture deletion (`shapes`/`chords`
left the format; posture `name`/`fingers` deleted with their then-dead consumers).

## The model

An authored, song-level **dictionary**: entries of `{shape signature, name, fingering}`.

- **Matched against the DERIVED posture by shape, at read time.** Never referenced by notes or
  posts. Editing a member changes the posture, and the lookup re-resolves or misses exactly as
  spans themselves re-derive — no reference shear, no deviation rules, frets keep their one
  authority (the members). The dictionary *decorates* shapes; it never dictates them.
- **Identical definitions collapse**: defining an entry equal to an existing one IS the existing
  dictionary key (user rule).
- **Optional at authoring and import.** The imported corpus does not provide templates (the
  source content lacks chord definitions — do not design around seeding; at most seed
  opportunistically if a source ever carries diagrams). A nameless span is a legitimate, visible
  state: it draws its frets and simply has no name chip.
- **Completeness pressure lives at SAVE** (user rule): on save, warn and encourage — step through
  every span lacking an entry, offering pick-a-matching-template or define-new (collapse rule
  applies), skippable per span, never blocking the save. Same temperament as the grid-off
  warning: always warn, no don't-ask-again, never block. Knob to sight in practice: warn every
  save vs only when the unnamed count changed — default every save.

## Arpeggio integration — the stamp model

The user's framing: "arpeggio brackets being defined at the template level." Two readings were
weighed; the recorded shape is **template-as-stamp**:

- **Posts remain the authored per-slot truth.** Every coherence ruling of 2026-08-27 is built on
  them: the stored fret as the authority arriving notes are checked against, contradiction
  splits, growth splits, re-merge on equalization, the lone-member cascade, the redundant-marker
  collapse. The dictionary does not disturb any of it.
- **"Apply template to span" is a verb**: it authors the missing posts from the template's
  unsounded members in one atomic plan (one undo entry). Brackets then appear across the whole
  shape — defined at the template level as an *experience* — while the derivation keeps reading
  one authority. Deviating afterwards edits honest per-slot data that splits and merges by the
  signed rules.
- The rejected reading, **template-as-source** (silent members derive from template-minus-sounded
  and posts die), is recorded for honesty: subset matching is ambiguous (two sounded notes fit
  inside many templates), forcing a stored per-span pick with nowhere shear-free to live (spans
  are derived), and it would reopen the entire same-day coherence ruling set. Do not take this
  path without re-litigating those rulings explicitly.

## Fingering precedence

Fingering lives in the dictionary entry; a per-instance INLINE finger on a note or post stays
legal (a refingered repetition is real musical data) and **overrides the dictionary's where
present**. The stop-bearing records' names were chosen to survive gaining a `finger` field.

## Known constraint, accepted until a real chart contradicts it

Shape-keyed lookup gives **one name per shape per song**. Two names for one grip in one song
would need a stored per-span pick, which reopens the reference question in miniature. Accepted
as the model's constraint; revisit only on a concrete counterexample.

## Open points for the build-time ruling

1. The save-time step-through's exact flow (ordering, skip semantics, the every-save knob above).
2. Whether the dictionary entry carries anything beyond `{shape, name, fingering}` (a display
   root? a capo-relative form?) — undecided, default no.
3. The name chip's display surface (the timeline ruler's chord/arpeggio band is the live
   candidate) — a sighting question when built.
