# Deep-review follow-ups — 2026-07-27 (completed 2026-07-28)

A full-project multi-agent review (19 code finders + 5 doc auditors, each finding adversarially
verified) ran on 2026-07-27. It ran out of usage credits partway, so only 8 code findings completed
verification before the rest of the verifiers, the recent-commits adversarial finder, and all five
documentation auditors died.

The remainder was completed on 2026-07-28 by a **verify-only workflow** that re-verified the
preserved finder claims against current code, ran the owed skeptic pass on the four recent commits,
and added finders for that day's GuitarPro-import rework and for cross-cutting invariants a
per-subsystem finder cannot see. Every confirmed finding has now been fixed. This file is the
closed record.

## Outcome

76 verdicts across the completion run: **41 refuted, 9 already scoped elsewhere, 1 already fixed,
1 uncertain, 24 confirmed** — the same high refute rate the original run showed. The four recent
visual-iteration commits (pinned heads, span holds, consumed heads, vibrato/bend) each got a
dedicated skeptic and came back **clean** (the one suspicious depth line had already been
remediated by the later rework). The cross-cutting finder confirmed the onset epsilon is a single
coherent policy and that the tab and highway projections resolve `GridPosition` to seconds
identically.

## Fixed

**On 2026-07-27** (the 8 originally-verified findings): editor-preview retry crash; preview stale
cursor; extracted headless `PreviewTimeModel` with tests; non-ASCII package metadata (UTF-8
decode); the `pathFromUtf8`/`utf8FromPath` bridge across four sites; song.json flush-before-check;
extracted `validateToneAutomationEntries`; retired the marker-dissolution watch item.

**On 2026-07-28** (the completion run's confirmed findings):

- *Must-fix.* Alt+click onto an occupied chart slot no longer clobbers the existing note to fret 0
  (occupancy gate on the empty-create branch); game profile display names round-trip non-ASCII
  (`fromUTF8`); the GuitarPro importer's local `Fraction` helpers delegate to the value type's
  int64-safe operators so a long score cannot overflow an accumulated global-beat position.
- *Cheap wins.* Discrete-lane landing value snapped in one shared place (keyboard/mouse/nudge all
  agree) and the created lane point rides the caret; `buildToneRack` removes its half-built rack on
  failure (RAII guard); the duplicated missing-plugin skip in `executePluginStep` extracted; the
  shared `globalBeatPosition` helper hoisted to `grid_arithmetic.h`; the duplicate `highwayFretLineX`
  int overload collapsed; the render-device backend default routed through `defaultRenderBackend()`;
  the inlay atlas UVs inset by a half texel; the path case-fold guard given its mandated why-comment;
  a transposed Doxygen block on `mintEmptyTone` fixed; a defensive `hasPendingTransition()` re-check
  added to the deferred undo/redo transition.
- *Medium.* Fret-entry coalescing clock injected via `Services.now_milliseconds` (Time Must Be a
  Dependency); an unreadable/offline scan root distinguished from an empty one so its prior entries
  Reuse instead of churning; the six duplicated `RockHeroGame` content-config members collapsed into
  one stored `Config`.
- *Tests.* Six confirmed gaps filled: chart-edit planners, GP tie-continuation bend folding, TabView
  caret-mask, transport-strip selection chip, live-rig load cancellation/teardown, and archive
  duplicate/zip-slip rejection.

Two review suggestions were deliberately **not** taken as written and are noted here so they are not
re-raised: nesting the private `Game::Config` into the public `RockHeroGame::Config` (would leak the
private type into a public header — the flat public config plus a one-shot adapter in `onInit` is
kept instead); and the platform-guard finding whose cited path did not exist (relocated to
`audio_path_util.cpp`, which now carries the why-comment).

## Documentation sync — done

The doc-sync audit was re-run scoped (5 self-verifying auditors) on 2026-07-27 and its confirmed
findings applied across `architecture.md`, the developer guide (3D-highway tour + package-format
checklist), `architectural-principles.md`, and the plans/tracking registries. One false positive
(`IAudioDeviceSettings` "no longer exists") was rejected.

## Still tracked elsewhere (no action here)

- **Atomic package replace.** `writeWorkspaceToArchive` still truncates the destination `.rock` in
  place, so a crash mid-save destroys the only copy. Re-confirmed real, but already scoped as
  **roadmap plan 10 Phase 5** with its own failure-injection tests. Do not fix ad hoc.
- Per-frame renderer classification/allocation (the classification half was executed 2026-08-10 —
  see `docs/plans/completed/highway-onset-groups-into-the-projection.md`), atlas mips, the
  `makeHighwaySustainPrefixMax` notes-overload removal (**done 2026-08-10** — all three highway
  forwarders were pure pass-throughs and were deleted), and the 88-method `IEditorController` ISP
  pressure are carried by existing plans/watch-items; the song-select menu scroll is carried by
  its `backlog.md` entry (no plan or watch item covers it). See the git history of this file for
  the specific references.
