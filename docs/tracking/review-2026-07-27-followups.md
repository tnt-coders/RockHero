# Deep-review follow-ups — 2026-07-27

A full-project multi-agent review (19 code finders + 5 doc auditors, each finding adversarially
verified) ran on 2026-07-27. It **ran out of usage credits partway**, so only 8 code findings
completed verification; the rest of the verifiers, the recent-commits adversarial finder, and
**all 5 documentation auditors** died before producing results.

This file preserves what was NOT completed so the review can be finished later without redoing the
finder work.

## What was already fixed (this session)

The 8 adversarially-confirmed findings were resolved in commits on 2026-07-27:

- **Editor preview retry crash** (HIGH): bring-up failure no longer shuts bgfx down; renderer-only
  retry on reopen. `preview_surface.cpp`.
- **Preview stale cursor** (LOW): render child window class now loads `IDC_ARROW`.
- **Preview paused-time policy untested** (MEDIUM test-gap): extracted headless `PreviewTimeModel`
  with unit tests.
- **Marker-dissolution watch item** (fired): retired in `watch-items.md` (accept branch).
- **Non-ASCII package metadata corruption** (HIGH): `package_description.cpp` now decodes song.json
  as UTF-8.
- **Package I/O bypassed the UTF-8 path bridge** (MEDIUM): added `pathFromUtf8`/`utf8FromPath`,
  routed the four sites; fixes an MSVC throw on non-ACP import filenames.
- **song.json swallowed-flush silent success** (part of the HIGH atomic-write finding): now closes
  before the stream-state check.
- **Tone-automation validation duplicated** (LOW simplify): extracted `validateToneAutomationEntries`.

## Confirmed but deliberately deferred

- **Atomic package replace.** `writeWorkspaceToArchive` still truncates the destination `.rock` in
  place, so a crash mid-save destroys the only copy. The review re-confirmed this, but it is
  already scoped as **roadmap plan 10 Phase 5** ("atomic package replace"), with its own
  failure-injection tests, and is documented as a known caveat in the developer guide. The cheap
  half (song.json flush-before-check) was fixed now; the temp-then-rename archive replace stays
  with plan 10. Do not fix ad hoc — implement per that phase's spec.

## Resume the review to recover the rest (cheapest path)

The workflow supports resume: completed finders return cached results instantly and only the
credit-killed agents (verifiers + doc auditors + `session-changes` finder) re-run.

```
Workflow({ scriptPath: "<session>/workflows/scripts/rockhero-deep-review-wf_c6854afd-05a.js",
           resumeFromRunId: "wf_c6854afd-05a" })
```

(Run ID `wf_c6854afd-05a`; same session only. If the session is gone, the finder titles below are
the work list to re-derive from.)

## Documentation sync — NOT DONE (all 5 auditors died)

The doc-sync half of the request was not completed. These still need a pass, verifying every
concrete claim against current code and editing stale ones:

- `docs/design/architecture.md` — module lists, threading/timing chain, render-stack section;
  check whether the 3D highway / editor-preview render architecture is reflected.
- `docs/design/architectural-principles.md`, `coding-conventions.md`, `documentation-conventions.md`
  — do the named patterns still match code organization; spot-check newest headers for drift.
- `docs/developer/` — area tours (esp. the **3D highway tour**, after heavy recent iteration:
  span holds, consumed heads, FHP window motion, arpeggio boxes, vibrato/bend changes), pattern
  catalog, and the procedural checklists' "silent steps" lists. The recent
  `PreviewTimeModel` extraction and the `pathFromUtf8`/`utf8FromPath` helpers are new touchpoints
  the guide may need to name.
- `docs/plans/` lifecycle hygiene and `docs/tracking/backlog.md` currency.

`watch-items.md` was partially synced this session (marker-dissolution retired); the rest of the
registry was not re-verified.

## Unverified code findings (finder output; verification was cut off)

**These are raw finder claims that never passed adversarial verification** — the earlier run
refuted 108 of the findings it *did* verify, so expect a high false-positive rate here. Several
were flagged by the finders' own coverage notes as "deliberately not reported / immaterial."
**Re-verify each against current code before acting.** Grouped and de-duplicated by area.

### 3D highway renderer (efficiency / simplification — recurring across finders)
- `HighwayRenderer::Impl::draw()` is a ~2,200-line function; chord grouping, repeat-box
  classification, and chord membership are recomputed **per frame** from the visible set, though
  they are chart-static. Candidate: precompute chart-static classification in `setViewState`
  (alongside `display_hold_ends`/`sustain_prefix_max`) and index it per frame.
- `draw()` heap-allocates ~20-30 scratch vectors per frame (per-frame allocation churn); consider
  reusing persistent `Impl` buffers cleared per frame.
- Per-frame linear scans over the full sorted note/beat arrays (also raised as "linearly scans
  every beat"); consider cursor/bracketed iteration like the visible-note range already uses.
- Inlay-skin cell UVs may lack the half-texel inset the head atlas uses (bleed risk).
- Atlases upload with no mip chain (minification aliasing at depth); background view transform
  uploaded per frame with no draws (one matrix — likely immaterial, staged for a later phase).
- `makeHighwaySustainPrefixMax` notes-based overload may now be production-dead (the hold-ends
  overload superseded it) — confirm and remove if so. (Also raised by the conformance finder.)
- `highwayFretLineX` int overload duplicates the double overload.
- `windowSampleTimes`/`handWindowMovesWithin` possible duplication.

### 3D highway (correctness / test-gaps — verify carefully)
- `highwayDisplayHoldEnds` "drops the covering shape" in some interleaving — needs a concrete
  repro; the hw-core-math coverage note examined the epsilon-anchoring asymmetry and judged it
  immaterial, so this may be a false positive.
- Visible-range lower bound "splits a same-onset" group in an edge case.
- Box-only repeat strums "vanish instantly"; repeat-box / span-hold display policy has no tests;
  arpeggio-bracket lane-dominance possibly violated; no test coverage for repeat-box visibility.
- `g_highway_onset_match_epsilon`'s documented rationale is **misattributed** vs. the tempo-map
  cursor's actual behavior (the coverage note flagged this as harmless-but-wrong doc text — a
  cheap doc fix in `highway_view_state.h` ~line 398-403).
- Non-finite bend semitones / NaN handling in touch/bend math (robustness).

Note: the four most recent visual-iteration commits (pinned heads, span holds, consumed heads,
vibrato/bend) were **read and judged correct** by the hw-core-math and hw-renderer-notes coverage
notes, but the dedicated `session-changes` adversarial finder died before running — a fresh
adversarial pass on those four commits is still owed.

### Editor core — controller (accretion hotspot; verify)
- Dangling `IEdit*` applied when undo history is trimmed/rewritten (async timing — medium
  confidence, unreproduced).
- Close/exit takeover during an in-flight busy operation.
- Chart edit verbs bypass the session-fault guard.
- **Fret-entry window reads the wall clock directly** — a "Time Must Be a Dependency" violation if
  real; check against `architectural-principles.md`.
- Full view-state rebuild per event (Tempo/etc.) — efficiency; `setState` deep-copies the entire
  `EditorViewState`.
- `IEditorController` has grown to ~88 virtual methods; chart-editing feature slice ~1600 lines
  (god-object pressure — cross-ref `docs/plans/todo/remaining-god-object-decomposition-plan.md`).
- Create-and-nudge leaves the armed lane caret inconsistent.
- Plugin-state-edit observer → undo entry path.

### Editor core — chart editing / import / tone (verify)
- `planMoveNotes` clamps instead of refusing; Alt+click near an occupied slot silently no-ops;
  multi-digit widen fallback pushes a base note; 40-Q2-B truncation during a move.
- `chart_edits` planners have no direct unit tests; three near-identical note-insert flows.
- GP import: mid-song tempo changes silently discarded; lead-in back-extrapolation fallback;
  slide resolution searches unboundedly far; tie-continuation bend folding untested; local
  fraction helpers re-implement `Fraction`.
- Editor lane curve evaluation ignores curve shape; keyboard Insert on a discrete lane.

### Editor UI (tab + shell — mostly efficiency/simplify; verify)
- `paintTabLane` scans all shapes three times; selection-ring overlay iterates the whole set;
  marquee maps live pointer pixels; click-vs-drag disambiguation diverges between surfaces.
- `TabLaneGeometry` stores `bounds_x` but `x()` recomputes; post-extraction delegate shims linger
  in `tab_view`; TabView caret-mask contract untested.
- `EditorView::setState` deep-copies the whole state and ends every controller edit with a full
  rebuild; selection-count chip always laid out; six listener interfaces reduce to ~30 methods;
  tone-marker split/reuse policy lives in the view; no test for transport-strip selection.

### Common audio (verify — several are lifecycle/correctness)
- `~Engine` never resets `m_replace_op` (stale state across teardown).
- Stale cooperative-load continuations pump after cancellation.
- `buildToneRack` failure leaks an orphaned rack.
- `executePluginStep` duplicates the collection logic; no tests for cancellation/teardown paths.
- `mintEmptyTone`'s Doxygen block is stranded (doc nit).

### Game core / shell (verify)
- `setProfileDisplayName` corrupts non-ASCII (same UTF-8 class as the package fix — likely real,
  cheap; check `game_settings`/profile persistence).
- Offline/unreadable scan root indistinguishable from empty; recoverable gain-calibration
  failure/retry.
- Song-select menu cannot scroll (selection can leave the viewport); render backend pinned in two
  independent places; six content-config fields copied field-by-field; SDL3Application loop
  contract; bounded per-frame JUCE drain state.
- **GameShell watch item may be stale** — re-check its trigger against current composition and
  plan 21 status.

### Package (verify)
- Archive duplicate-entry and symlink rejection may lack tests; tone-track round-trip "silently
  rewrites" something (verify round-trip symmetry); failed-save leaves earlier arrangements'
  files (subsumed by the plan-10 atomic-write work).

### Conformance / goal-alignment (verify)
- A platform guard reportedly lacks the mandated why-comment — **the finder's cited path did not
  exist as stated**, so re-locate before acting (candidate: an `audio_path_util` TU; confirm the
  real path). All other enumerated OS guards were reported conformant.
- Goal-alignment observations (judgment, not defects): post-GATE-A effort pooling in highway
  visual polish; the "game audible" milestone driver; production song-launch path rides
  `DevSession`; `LibraryIndexStore` built/tested but not yet wired to a consumer; roadmap status
  board vs. per-app architecture reality. Treat as roadmap discussion, not code fixes.
