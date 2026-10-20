# Plan 71 — Let-ring import

## Status

**On the roadmap 2026-10-19 (promoted from `docs/plans/todo/`); not started. Gated on G71-TRUTH:
Phase 0's ground truth is the user's hand authoring, which waits for the user's time.**

**Priority (user, 2026-10-19):** let-ring import is the app's BIGGEST correctness pain point. Span
derivation and hold authoring now read fairly correct, and what they derive from is the ring the
import wrote. This plan is the first correctness work to pick up once the active plans allow it.

The figures, counts and function names below describe the import as it stood when the plan was
written. Before Phase 1, re-read `rock-hero-editor/core/src/project/gp_chart_builder.cpp`,
`docs/design/rulesets/gp-import-rules.md` and the corpus census, and re-measure every number.

## Purpose

The GP import figure law (`letRingFigureEnds`) decides how long every let-ring-marked note rings.
Guitar Pro's let-ring mark is inherently inaccurate: it says that notes ring, not how long the hand
holds them. So the import's job is the BEST ESTIMATE of the real ring, not a faithful reading of
the marking, and the figure law derives that estimate from the structure of the music. This plan
collects the places where the estimate reads wrong on sighting, and asks, for each, what the law
should say instead.

Every proposed refinement is judged by one test: does it improve the estimate across the corpus?
The evidence is a hand-authored ground truth (next section), backed by sightings, counts and
cross-voice context, never conformance to a player. Guitar
Pro's manual (a let-ring note rings until a note on its own string or a rest) and alphaTab's player
(`MidiFileGenerator`, `_getNoteDuration`: the same rule per voice, capped one bar from the note's
own onset) serve only as a comparison that locates the disagreements. They are called "the
reference" below.

## Ground truth: the user authors the rings, the rule is fitted to them

Sightings and counts locate where the law and the reference disagree. Neither one says which is
right, and that is why the arms below are still open questions. The plan therefore starts with a
ground truth. It runs before any arm is ruled, and every arm is measured against it. Steps 1 to 3
are Phase 0 and the user's; steps 4 to 6 are Phase 1 (see Phases at the end).

1. **Select.** The user picks a few songs whose let-ring usage covers the arms below: arpeggiated
   chords split across voices (figure L), open-string drones (c), fretted rings that cross a
   grip change (b), tied let-ring chains (d), and rests inside and after marked runs (a). A
   picked run under let ring belongs in the set as well.
2. **Import and author.** Import each song with the current law. In the editor, the user sets
   every marked note's ring to what the player's hand actually does, using the recording and the
   instrument as reference rather than the GP mark. Each song records the bar range the user
   reviewed. Inside that range, an untouched ring counts as confirmed correct. Outside it, a
   ring counts as nothing. This keeps the import from biasing the truth by default.
3. **Store.** The authored packages and their reviewed ranges are kept under the corpus firewall:
   in the local sightings folder beside `letring-import-sightings.md`, never in git.
4. **Diff.** For each marked ring, compare the imported end with the authored one. Classify every
   disagreement by direction (long or short) and by the event that ended the imported ring: the
   figure anchor, the open-string phrase lift, the tie extension, the horizon cap, a same-string
   restrike, or a rest the law sailed through. Measure the derived spans the same way, since
   spans are where a wrong ring shows.
5. **Fit a rule.** Score each candidate against the truth: the current law, the reference, each
   arm's variant, and their combinations. The scores are exact-ring agreement (on the import's
   1/3840 lattice), mean end error in beats, and span agreement. A candidate may read only what
   the GP file states, because that is all the importer has. Fit on some songs and check on the
   rest, so that the rule describes let ring in general rather than the chosen bars.
6. **Corpus check.** Re-run the corpus census with the winning rule. Every change the rule makes
   outside the authored songs is a new sighting to check by eye, not a confirmed improvement.

The outcome may be that no rule built from GP data alone reaches the authored truth on some
figure. That result is still a finding: it marks where let ring needs authoring, or the explicit
let-ring notation the watch item "Imported let-ring after the figure law" holds in reserve,
rather than a better import.

## What is already known to be right

- Unmarked durations import exactly. Across the corpus, every unmarked note rings its notated
  length except for grace-lead steals (GP import rule 5) and six nonuplets rounded onto the
  1/3840 lattice. No parsing, tie, rounding or field error contributes to the rings below.
- On 8,590 of 10,946 marked rings, the law and the reference give the same ring. The remaining
  2,356 split into the four arms below: 772 longer than the reference and 1,584 shorter.

## The sightings

The corpus firewall keeps song names and positions out of the repository. Each figure below has a
letter; the letter's song and bars live in the local sightings file beside the existing ledger
(`letring-import-sightings.md` in the scratch reference folder), together with the full
investigation write-up and the per-rest voice listing (`letring-review/`).

## (a) Rests

**What the law does.** A rest closes no figure and stops no ring. Only a silence longer than one
audibility horizon seams a figure.

**Evidence.** Measured over every rest that "a rest in the note's own voice stops its ring" would
honour: 41 rests and 92 rings in 8 songs, changing 23 spans and adding 21.

- 8 rests lie between marks of one figure (interior) and stop 26 rings. 6 of them, carrying 24
  rings, are figure **L**: on a downbeat the arpeggio voice rests for an eighth while the other
  voice strikes the new chord's bass. The rest is a voice-split filler, not a lift. What actually
  ends the old chord's rings there is the chord change, and it happens in a voice the
  voice-scoped law cannot see.
- 33 rests lie after a figure's last mark (trailing) and stop 66 rings.
- 22 rests (2 interior, 20 trailing; 52 rings) are true silences: no voice of the track sounds at
  the rest. Figure **P** includes both kinds.
- 19 rests (6 interior, 13 trailing; 40 rings) coincide with notes in another voice.

The event-anchor form recorded in the watch item, where a figure ends at the first own-voice
onset or rest after its last mark, changes 66 rings and moves 17 spans out and 15 in. It leaves
figure L unchanged, because L's rest is interior.

**Questions to rule.**
1. A rest stops a ring only where the whole track is silent. This looks like a correct refinement:
   it honours the 22 true silences and none of the voice-split fillers. Its span impact has not
   been measured on its own.
2. For figure L, the real evidence is a chord change across voices. Should the law see a
   contradiction stated in another voice of the same track, for a ring that the hand holds? Today
   only the same-string clamp is cross-voice.

**Code.** `collectEvents` discards rest beats (the `beat.notes.empty()` branch). The track's
silences would have to reach `letRingFigureEnds`, either through `scope_end` or through the
per-note assignment at the end of the walk.

## (b) Fretted rings cut short, and inconsistently

**What the law does.** A fretted ring ends at the figure's anchor: the first onset its own voice
states after the figure's last mark. The fragment donation decides which marks share a figure.

**Evidence.** In 1,403 fretted rings, the law ends the ring before the reference does. In 1,078
of them the reference rings on to the next strike on the ring's own string, and in 325 to its
one-bar cap.

Figure **M** shows the inconsistency. Two bars with identical notes in the ringing voice import
differently: string 5 (strings count from the lowest) at fret 8 rings 3/8 in one bar and 3/4 in
the other. In both bars, the fret 8 and the open string 4 that follows it form a two-note fragment
that a grip seam closes. The donation tests the open string 4 against the *closing* figure's whole
grip. In the second bar, that closing figure later grows string 4 at fret 3, six beats on, so the
open string "contradicts" and stays with the fret 8. In the first bar, it does not, so the open
string is donated away and the fret 8 is left alone to stop at its written end. Under the
reference's rings, every repetition of the first bar derives one span shape. Today the repetitions
derive two, the second difference coming from arm (c)'s drone.

**Questions to rule.**
1. May a fragment's donation depend on a statement the closing figure makes after the fragment
   ends? The donation should probably judge against the closer's grip *as stated up to the
   fragment's own notes*, so that repetitions import identically.
2. Should a fretted ring outlive the anchor until its own string is re-struck, while the hand
   still holds the stop, the way the reference rings it? Or is the anchor's "material past the
   marked run never asked" right for fretted notes, as entry K in the ledger argues for the
   fretted-versus-open pair?

**Code.** The donation block in `letRingFigureEnds` (the `closer_grip` built from every member
of the closer), `scope_end`, and the per-figure end.

## (c) Open strings ringing for bars

**What the law does.** An open string takes its phrase's end, the anchor after the whole marked
run's last mark, capped one horizon past that mark. In a let-ring passage with no rest and no
unmarked figure, only the open string's own restrike stops it.

**Evidence.** 307 open rings, and 127 fretted rings, run past the reference's one-bar cap. In
figure **N**, an open string 3 struck in one bar rings for five bars, and its "0" is a member of 8
consecutive spans. Elsewhere in the same song, rings of 29/8 and 27/4 whole notes appear. Of the
rule-A census's 72 carried members that do not ring at their founding under the reference, 59 are
such open strings.

**Question to rule.** How long may an untouched open string be asked to ring? Options: the
phrase, as today; one horizon from its own onset, as the reference does (the lift was once
capped this way); or the phrase capped at that horizon. A real string decays. The question is
what the chart should claim.

**Code.** The open-string lift in `letRingFigureEnds` (`phrase_ends`, and the open ring's
`std::max` with the phrase end).

## (d) Tie extension

**What the law does.** A tie-merged chain is extended like any other marked note, from the
struck note's mark, past the merged written end.

**Evidence.** 260 tie origins ring longer than the reference rings them, because the reference
stops a tied let-ring note at its tie end. In figure **O**, a tied string 5 at fret 6 rings two
whole notes and is a member of a span founded two bars later, after the hand has moved. 12 of
the rule-A census's carried members are such ties. Leaving ties as they are moves 50 fewer
spans than the full reference variant below.

**Question to rule.** Does a tied let-ring chain ask to ring on past its tie end? The ruleset
says Guitar Pro audibly rings it on, but the reference player does not, and Guitar Pro itself was
not checked.

**Code.** The tie merge in `buildChart`, and the application's "a note that absorbed a
same-string merge extends like any other" doctrine.

## Measured span impact (rings patched, spans re-derived, census corpus of 22,339 spans)

| variant | rings changed | spans removed / added |
|---|---|---|
| reference rule (rest, same string, one-bar cap; ties to tie end) | 2,356 | 875 / 1,125 |
| reference rule, tied chains as today | 2,096 | 825 / 1,052 |
| reference rule without the cap | 2,036 | 1,626 / 1,681 |
| every rest in the note's voice stops it | 92 | 23 / 21 |
| event-anchor form | 66 | 17 / 15 |

Each arm's own span impact is not separated beyond the tie row. Measure it per arm when the arm is
ruled.

## Phases

- **Phase 0 — the ground truth (the user's; closes G71-TRUTH).** Steps 1 to 3 of the ground-truth
  section: songs selected, rings authored over recorded reviewed ranges, packages stored under
  the firewall.
- **Phase 1 — diff and fit.** Steps 4 to 6: re-verify the inventory above, diff the import against
  the truth, score the candidate rules, and bring the user one recommended rule per arm with its
  scores and its corpus-wide changes. No production change lands in this phase.
- **Phase 2 — the rulings land.** One change per arm the user rules, each with fixtures for its
  sighted figure and a census re-pin, and `gp-import-rules.md` updated in the same change.
- **Phase 3 — sighting.** The user re-imports the truth songs and a few untouched ones, and checks
  them by eye.

## Execution notes

- Each arm is an independent ruling and lands as its own change, with fixtures for its sighted
  figure and a census re-pin.
- The rulings it touches: the figure law section of `gp-import-rules.md`; the open-string-lift
  ruling; the rests-invisible watch item ("A ring sails through a written rest"); and the
  one-end law, for any arm that stops stack members separately.
- Every importer fixture that uses rests as time filler between a mark and its anchor depends on
  arm (a). The fixtures for the audibility cap, the horizon seam and the conversion report are the
  first to re-read.
