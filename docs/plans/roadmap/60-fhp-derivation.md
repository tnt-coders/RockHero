# Plan 60 — FHP Derivation

## 1. Status

Roadmap, not started. Authored 2026-09-05 against `master @ 0d1b7009`. **Decision-gated
(G60-RULINGS)**: 60-Q1..Q5 mirror the five open rulings in the design record,
`docs/plans/in-progress/fhp-derivation-algorithm.md` — the measured 11-rule algorithm, the
tension resolutions, the profiles verdict, and the fingering/template/manual-specification
model are all recorded there and are the source of truth this plan executes. Phase 1 is
buildable the moment 60-Q1 closes. Re-verify the inventory below before execution.

## 2. Goal

Replace the FHP generator with the corpus-measured **11-rule figure-segmented floor tracker**
(88.03% exact agreement with 4,555 professionally reviewed arrangements vs ~70% for the shipped
walk; coverage 99.94%; churn at the authored rate), make the FHP stream **derived at read** from
the notes plus a three-kind authored opinion layer (marker template references, free-note
fingers, open-stretch window statements), and wire the template/fingering coupling — thereby
unblocking the span-marker redesign's hand-coupling phase, which consumes this plan's shift
stream. The derivation chain is one arrow: notes + authored statements → FHPs → spans/rings.

## 3. Non-goals

- **No profile UI** (60-Q2 records the measured recommendation; overrulable there, not here).
- **No heuristic finger derivation, ever** — position is objective (97% expert agreement),
  finger is not (67%); fingers are authored or absent.
- **No authored anticipation** — anchors state on-onset truth; the highway/camera LEAD (~0.8–1.2
  s, fixed ~300–450 ms minimum-jerk move animation) is a projection-layer transform (feeds the
  open camera-timing review, session task #153), never early data.
- **No width overrides** — width = max(4, run hull) is law; the stored width field of the
  ground-truth corpus is ~98% default and carries no reach signal.
- **Not the span-marker/template implementation** — markers, the template editor, the grip
  library, and the dictionary live in `docs/plans/todo/span-marker-redesign.md` and
  `docs/plans/todo/chord-dictionary.md`; this plan only consumes their authored records.
- **Not the let-ring hand coupling itself** — that is the span-marker plan's phase, gated on
  this plan's Phase 1 accuracy.

## 4. Current-state inventory (verified 2026-09-05)

- `generateFretHandPositions` in `rock-hero-editor/core/src/project/gp_chart_builder.cpp`:
  greedy minimal-shift walk + section/0.8 s-rest re-anchoring (`g_fhp_phrase_rest_seconds`) +
  the pinned-finger window union (2026-09-05) + the held-hull slide reshape. Import-only; the
  result is stored in the chart (`fhps`), which drifts from the notes on every later edit.
- The census rig (`test_corpus_census.cpp`, hidden `[.local-corpus]` tag) carries the
  travel-aware pinned-finger counter, the hand-coupling gate counters (spans crossed by an FHP
  shift), and unfretted-arrival counters. Cross-check baseline partly stale (session #158).
- Ground truth: the sighted offender worklist and sightings registry (local scratch, paths held
  outside the repo per the corpus firewall); watch entries J and K in
  `docs/tracking/watch-items.md`; the measured design record above with the expert/analyst
  reports beside the local dataset.
- No fingering exists anywhere in the model; no marker/template storage exists yet.

## 5. Phases

**Phase 1 — the core rebuild** (gated on 60-Q1; independent of everything else). Replace the
walk's body with the 11-rule pass: one O(n) sweep over onset groups with the bounded run scan.
Deletions: the minimal-shift drag core, the boundary/rest re-anchor machinery and
`g_fhp_phrase_rest_seconds`, the pinned-finger union (suppression subsumes it), the held-hull
slide reshape (sub-4 widths disappear; slide keyframes become ordinary coverage events at their
destinations). Net code deletion. Acceptance: forced-relink test run; the census re-run (pins
are certainty — expect the pinned-window count to collapse toward the corpus's ~2-per-400k
residue); the offender worklist re-measured with the walk order re-ranked; user sightings of the
worst prior offenders. **Must land before the span-marker plan's hand-coupling phase** — the
coupling clips spans at FHP shifts, and building it against the old walk would clip wrongly
everywhere; the readiness gate stays the census seam-vs-shift agreement measure.

**Phase 2 — the derived stream** (gated on 60-Q4; needs Phase 1, not the span-marker plan).
`fhps` leaves the format; the stream derives at read (editor derived-index pattern; O(n), cheap)
and at import for the importer's own consumers. The two template-independent authored records
land: the **free-note finger** (anchor evidence via `floor − (finger − 1)`) and the
**open-stretch window statement** (the manual-specification language where no fretted floor
exists; range-clamped, refused mid-ring). Manual FHP specification ships here: markers +
fingering over fretted material, window statements over open/silent stretches — derivation
recalculates around statements as inputs, never output patches. GP note-level fingering imports
onto free notes. Tooling migrates in the #78 converter window: the sighting reels and the
external converter author fingering statements instead of raw `fhps`.

**Phase 3 — the template coupling** (gated on the span-marker plan's template phases and
60-Q3/60-Q5). Marker template references feed derivation per the scoping law: the fingering
pins the run its marker heads, unsounded stated stops join that run's coverage, contradiction
is a per-onset test that drops the reference to notes-only fallback, references over silence
feed nothing; no span extent is ever consulted. GP chord-diagram fingering assembles into
template entries at import (impossible hands dropped with a report). The derived-default
suggestion for unreferenced grips ships only if 60-Q5 re-confirms the reversal.

## 6. Decision gate G60-RULINGS (60-Q1..Q5)

All five are recorded with their evidence in the design record; Q1 blocks Phase 1, Q4 blocks
Phase 2, Q3+Q5 shape Phase 3, Q2 is product scope:

- **60-Q1** — sign (or amend) the 11-rule algorithm.
- **60-Q2** — profiles: the measured recommendation is one default, two named parameters, no
  profile UI, no artist presets (artist ICC ≤ 0.25; the knob inventory collapsed under
  measurement); overrule here if the product wants the knobs surfaced anyway.
- **60-Q3** — the fingering model: grip templates referenced from markers + free-note fingers
  as the format's fingering carriers and this derivation's inputs.
- **60-Q4** — the derived stream: delete stored `fhps`; the authored residue is exactly the
  three record kinds.
- **60-Q5** — the derived-default suggestion for unreferenced grips (reverses the killed
  auto-match of 2026-08-31; reconciled via derived styling; needs explicit re-confirmation).

## 7. Sequencing against the span-marker redesign

Interleaved, not ordered: **Phase 1 lands first** (before the span-marker plan's coupling
phase — it is that phase's prerequisite and needs nothing from it); **Phase 2 is independent**
of the span-marker work; **Phase 3 follows** the span-marker plan's marker/template-editor
phases. The two plans meet at two seams only: the marker as a shared forced boundary (authored
input to both derivations) and the hand-coupling readiness gate (this plan's shift stream, the
span-marker plan's consumer).

## 8. Final acceptance bundle

Build + tests green with the relink forced; census re-run with the FHP counters re-signed
(fold the #158 stale-baseline re-sign into this pass); the offender worklist walked with the
user until the pins column is explained (FIXED-CLASS / OPINION / SIGHTED per file); the
seam-vs-shift agreement measure produced and handed to the span-marker plan as its coupling
gate input; CI blind-spot reading pass over every touched hunk.
