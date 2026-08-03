# Deep-review follow-ups — 2026-08-03 (completed 2026-08-03)

A multi-agent deep review (7 finder dimensions — dead code, duplication, efficiency,
design/conventions, documentation, tests, completeness — each finding adversarially verified by a
dedicated skeptic) ran on 2026-08-03 over everything after commit `813fcb95` (itself the previous
review's cleanup): the four slide-gesture and theming commits (`6b5c9894`, `4d33abbf`,
`182faedb`, `81ed25bf`) plus the then-uncommitted roadmap edit and the untracked plan 55. Every confirmed finding is now fixed; this file is the closed record. The full
item-by-item plan — 25 items with quoted anchors, target shapes, per-item verification, and the
executor protocol — lives in this file's git history at commit `e59f47d8`.

## Outcome

42 raw findings, every one CONFIRMED or ADJUSTED by its verifier (none refuted), deduplicating to
25 items. The efficiency dimension came back clean (no deadline-path code changed; the import-path
shapes are sorted single passes with log-n lookups), as did layering, the CI blind-spot
constructs, const correctness, and naming across the touched hunks. No remnant of the excised
SlideIn notation or of the reverted pick-slide borrowed-vocabulary experiment.

## Fixed

**Commit `e59f47d8`** (items R1–R10, R13, R15, R18–R25):

- *Real defects.* The dead test helper `hasFretHandPositionAtFret` — a GCC/Clang `-Werror`
  `-Wunused-function` CI break invisible to local MSVC; CLAUDE.md's blind-spot table gained the
  row and its reading-pass list the construct. `resolveSlideOutExits` skipped the documented "no
  room → planted" rule on the departure path; restructured around named `has_next`/`has_room`
  predicates applied ahead of the figure choice. The scoop window was unbounded against
  `slide_out`, so a flags-20 note could place its waypoint at or past the trail-off end and fail
  chart validation; the symmetric halving clause was added, and the "strictly before any payload"
  claim narrowed to the slide chain and trail-off end after verifying `chart_rules.cpp` orders
  bends only against the sustain.
- *Consolidation.* The quadruplicated placement-merge loop → `insertPlacementIfAbsent` /
  `upsertPlacement`; the duplicated window-anchor clamp → `windowAnchorCovering`; the
  thrice-spelled two-fret-minimum rule → `widenedToMinimumTravel` over
  `g_minimum_slide_travel_frets`; the duplicated active-placement lookup → `firstPlacementAfter`.
- *Tests.* Scoop clamp arms engaged strictly (cap, floor, sustain extension); the planted
  in-window scoop asserted to fabricate nothing; dip-replace and restore-yield pinned; bend+scoop
  coexistence; the flags-20 combination; the upward departure ride; and the planted, song-end,
  and no-room-beats-departure trail-off edges.
- *Documentation.* Plan 55's forbidden-pointer bullet rewritten intrinsically before it ever
  entered git (plus the 55-Q2 rewording and per-phase verification); plan 55 registered in the
  roadmap's Decisions-needed, execution order (item 26b), and status board with row order fixed;
  plan 54's stale "future `IGameSettings`" claims, wrong line citation, and superseded inventory
  stamp corrected; plan 45's withdrawn Phase 6 reduced to a pointer at plan 54, the single owner;
  plan 25 gained the theme-color coordination rule it was cited for; the lifecycle guide's
  normalization preamble now names all four passes and the moved-head date is unified on
  2026-07-28; the todo FHP plan flux-noted where it stated the superseded never-moves rule; the
  backlog gained the GP-derived-saves re-import entry.

**Commit `533978b5`** (items R11, R12, R14, R16, R17 — the coverage remainder): each new case
mutation-verified — the arm it targets temporarily broken, the test observed to fail, the source
restored byte-identical before committing. Killed mutants: the chain-waypoint halving arm
(flags 17 through a degenerate shift gap); the `keptStrictlyAfterLastWaypoint` floor in the crush
fallback (a legato chain inheriting its landing's trail-off, end stepping to 9/8); the
`g_max_fret` clamp on an upward exit (fret 28 + 4 would leave the neck); the chord-mate skip in
the next-note scan (opposite-direction chord mates). The five trail-off TEST_CASEs folded into
one "chooses the trail-off window figure" TEST_CASE, one SECTION per figure, sharing only the
syncs vector per Catch2 SECTION semantics.

## Decisions and deviations worth keeping

- **R3's regression fixture**: a 32nd-note coincident-end figure reaches the departs-and-no-room
  state directly, so the tie-merged hold-exempt fixture the plan proposed was unnecessary.
- **R4's bend half**: `chart_rules.cpp` orders bend points only against the sustain, never
  against slide waypoints, so the spec claim was narrowed and the combination pinned by a
  characterization test rather than inventing bend semantics. The `slide_out` half was a real
  fix.
- **Same-direction chord trail-offs have zero diagnostic power** (found when a mutant survived):
  the mates share the active window and the release travel, so the exit anchor is identical
  whichever mate wins the shared instant. Ownership is observable only when the mates scrape
  apart (flags 4 against flags 8) — that is the committed fixture, and it pins that the FIRST
  member owns the instant. Do not re-test this area with a same-direction chord expecting it to
  catch anything.
- **Exit-merge yield-vs-replace is unpinned — no fixture found.** The natural collision cases are
  suppressed by the planted rule (a trail-off end coinciding with an onset never emits an exit),
  but a collision with another string's *fabricated* placement (e.g. a scoop restore at a
  mid-beat instant) was not ruled out. `insertPlacementIfAbsent` stands because it matches the
  documented "fabricated exits yield to real placements" contract.
- **Deliberately skipped**: R6's optional `restoredWindowAt` sibling (the designated-init
  construction at both sites is clearer than a call would be); R22's optional roadmap item-15
  append (plan 25 and item 26a both carry the rule — a third copy would itself be duplication);
  redundant re-tests of the crush fallback's max-floor and strict-compress arms (already pinned
  by the planted edge case and the ride/release figures).

## Still tracked elsewhere (no action here)

- Re-importing GP-derived saves that predate the scoop/trail-off model — recorded in
  `docs/tracking/backlog.md`.
- The pre-existing "source"/"source corpus" wording retro-scrub — pending a separate user
  decision and deliberately untouched by this review.
