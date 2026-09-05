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

**The zero-authored-input invariant (user, 2026-09-05), binding on every phase**: FHPs must
derive sanely from a fresh import with NO manually authored records, and cleanly with NO
fingering information anywhere — imports may carry none, and free-note fingering likely stays
optional forever, so fingerless is the PRIMARY mode, not a degraded one. The invariant
constrains ABSENCE, never INFLUENCE: authored statements and fingering are optional as inputs
but AUTHORITATIVE when present — a stated finger pins its run's anchor outright (it is the
override language, not a hint), and a mid-run finger statement incompatible with the standing
window forces a re-derivation at its note like any other authored statement. What the
invariant demands is that every rule has a fingerless form that stands alone — rule 7's floor
default IS that form (correct alone 88.8% of the time corpus-wide), and a stated finger
substitutes into it. This is the algorithm's own design premise — the 88.03% corpus score was
measured with zero authored inputs and zero fingering, exactly the fresh-import scenario — and
no later phase may introduce a rule that REQUIRES fingering to function.

**Phase 2 — the derived stream, storage only** (gated on 60-Q4; needs Phase 1, not the
span-marker plan). `fhps` leaves the format; the stream derives at read (editor derived-index
pattern; O(n), cheap) and at import for the importer's own consumers. The two
template-independent storage records land — the **free-note finger** (anchor evidence via
`floor − (finger − 1)`, written by GP note-level fingering import) and the **open-stretch
window statement** (record shape only; range-clamped) — but NO authoring verbs ship here.
Nothing user-facing is lost by the deletion: no FHP authoring surface exists today either, and
with sane derivation the sighting reels stop needing to author `fhps` at all; the external
converter migrates in the #78 window.

**Phase 3 — manual authoring + template coupling, RIDING THE SPAN-MARKER PLAN** (gated on
60-Q3/60-Q5; executes together with the span-marker redesign's marker/template phases — user
sequencing ruling 2026-09-05: the manual authoring of FHP markers and span markers come
together, because the marker is ONE shared object, a forced statement boundary serving both
machines, built once). The authoring bundle: the marker verb and its tells (span-marker plan),
the template editor and dictionary (ditto), and this plan's FHP verbs — finger statements over
fretted material, window statements over open/silent stretches (refused mid-ring), all
consumed as derivation inputs that recalculate around, never output patches. The template
coupling lands in the same arc per the scoping law: the fingering pins the run its marker
heads, unsounded stated stops join that run's coverage, contradiction is a per-onset test that
drops the reference to notes-only fallback, references over silence feed nothing; no span
extent is ever consulted. GP chord-diagram fingering assembles into template entries at import
(impossible hands dropped with a report). The derived-default suggestion for unreferenced
grips ships only if 60-Q5 re-confirms the reversal.

## 6. Decision gate G60-RULINGS (60-Q1..Q5)

All five are recorded with their evidence in the design record; Q1 blocks Phase 1, Q4 blocks
Phase 2, Q3+Q5 shape Phase 3, Q2 is product scope:

- **60-Q1** — sign (or amend) the 11-rule algorithm. **Two deliberate considerations precede
  the signing (user, 2026-09-05), with a corrective measurement pass over the corpus running:**
  (a) **tap contamination** — the source game's official charts always extend anchors to
  include tapped notes, while RockHero deliberately excludes taps from the hand window (taps
  are right-hand; the tap stations ride their own extent), so tap-bearing spans are NOT valid
  ground truth for our model; the agreement scores and the width law's supports must be
  re-reported with tap-scope regions corrected, and the deviation recorded as deliberate —
  the evaluation harness must never count tap-driven disagreement against the algorithm.
  (b) **the sub-4 width amendment candidate** — the source game never authors width < 4 even
  for a demonstrably compact hand (a convention floor, consistent with the finding that the
  stored width field carries no reach signal), so the corpus cannot teach sub-4 widths; the
  proposal is to allow a derived width NARROWER than 4 exactly where fingering proves it:
  a complete grip fingering of extent ≤ 3 frets with finger 4 planted at the hull top, since
  a pinky-topped compact grip genuinely bounds reach. Fingerless mode keeps max(4, hull)
  unchanged, per the invariant.
- **60-Q2** — profiles: the measured recommendation is one default, two named parameters, no
  profile UI, no artist presets (artist ICC ≤ 0.25; the knob inventory collapsed under
  measurement); overrule here if the product wants the knobs surfaced anyway.
- **60-Q3** — the fingering model: grip templates referenced from markers + free-note fingers
  as the format's fingering carriers and this derivation's inputs.
- **60-Q4** — the derived stream: delete stored `fhps`; the authored residue is exactly the
  three record kinds.
- **60-Q5** — the derived-default suggestion for unreferenced grips (reverses the killed
  auto-match of 2026-08-31; reconciled via derived styling; needs explicit re-confirmation).

## 7. Sequencing against the span-marker redesign (user ruling 2026-09-05)

Three beats: **Phases 1–2 land first**, before the span-marker plan's build — the basic
derivation must stand alone under the zero-authored-input invariant, and it is the
hand-coupling's prerequisite while needing nothing from the marker work. **Phase 3 merges into
the span-marker plan's execution**: the manual authoring of FHP markers and span markers come
together as one arc, because the marker is one shared object built once. **The hand coupling
closes the sequence** (the span-marker plan's phase, consuming this plan's shift stream through
the census seam-vs-shift readiness gate).

## 8. Final acceptance bundle

Build + tests green with the relink forced; census re-run with the FHP counters re-signed
(fold the #158 stale-baseline re-sign into this pass); the offender worklist walked with the
user until the pins column is explained (FIXED-CLASS / OPINION / SIGHTED per file); the
seam-vs-shift agreement measure produced and handed to the span-marker plan as its coupling
gate input; CI blind-spot reading pass over every touched hunk.
