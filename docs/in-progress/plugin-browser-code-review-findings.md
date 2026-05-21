# Plugin Browser Code Review Findings

Status: in-progress. Concrete review findings for commit
`5c168ece — Add plugin browser workflow`, plus follow-up findings raised during review of the
subsequent cleanup work. This document supersedes the earlier `plugin-browser-code-review.md`
checklist by folding its scope and checklist into the actual findings produced by walking the
code.

## Resolution Status

- **Resolved:** finding 1 (re-entrant Close-button destruction), finding 2 (legacy file-path
  `AddPlugin`), finding 3 (catalog merge/replace inconsistency), finding 4 (catalog roots in
  editor-core), finding 5 (paint-fence comment nit), finding 6 (catalog ownership split between
  Tracktion and controller), finding 8 (PluginCandidate dual-use leak risk).
- **Open:** finding 7 (minor efficiency notes; no action required yet).

## Purpose

Review the plugin browser change set before treating it as settled. The feature is usable enough
for manual testing, but the implementation touches the audio boundary, editor core workflow,
JUCE presentation, busy-state behavior, and startup restore timing. The review goal is to find
design issues, not just local correctness.

## Scope

- `common/audio`: `IPluginHost`, `Engine` plugin scanning, known-plugin catalog reads, plugin
  insertion, and plugin-window behavior.
- `editor/core`: plugin-browser view state, controller actions, busy-state transitions, catalog
  scan scheduling, candidate add behavior, and startup-restore interaction.
- `editor/ui`: `PluginBrowserWindow`, `EditorView` window ownership, listener forwarding, and
  busy-overlay paint fencing.
- Tests around common audio plugin-host behavior, editor controller workflow, and editor UI
  wiring.
- Follow-up documentation in `docs/todo/plugin-browser-followups.md`.

## Review Checklist Status

The original checklist items, with their current verdict against the code:

- **Clean layering** — Pass. Tracktion/JUCE plugin details stay behind
  `common::audio::IPluginHost`. Editor workflow lives in `editor/core`. UI components only
  render state and emit intents.
- **Cheap browser open** — Pass. `ShowPluginBrowser` calls `knownPluginCatalog()` only; no
  filesystem traversal, no plugin scanner launch. Rescan is the only path that walks the default
  plugin folders.
- **Lifecycle on project changes** — Pass. `closeProject` and `loadSessionSong` reset
  `m_plugin_browser_visible`, and the user-driven Close path now defers window teardown (see
  finding 1 resolution note).
- **Busy-token handling for scan and add** — Pass. Both `completePluginCatalogScan` and the
  `beginAddKnownPlugin` continuation re-check `m_current_busy_token` against the captured token
  before mutating state.
- **Startup restore paint fence** — Pass. The `!isShowing()` branch in
  `EditorView::runAfterBusyOverlayPainted` keeps the project-open continuation from blocking on
  an impossible paint and now names its reentrancy guard.
- **Scan failure surfacing** — Pass. `scanPluginLocationsForCandidates` skips per-plugin
  `NoCompatiblePlugin` and `MissingPluginFile` errors so one bad binary cannot mask the rest,
  and surfaces the first real scanner failure when nothing was found.
- **Tests cover user behavior without over-coupling** — Pass. Finding 2's cleanup removed the
  legacy file-path test scaffolding; remaining controller tests exercise the production browser
  path through the `addKnownPlugin` helper.

## Summary

The change set delivers the intended user-facing behavior: the Add Plugin button opens an in-app
searchable catalog backed by `IPluginHost::knownPluginCatalog()`, first browser open is cheap,
broad scans are gated behind explicit Rescan, and project lifecycle correctly closes the browser
on project replacement/close.

Findings below are ordered roughly by severity at the time they were raised. Each finding starts
with a status line so the doc can be read as both an audit trail and an open-work list.

## Findings

### 1. Re-entrant browser window destruction during the Close click (likely UAF)

**Status:** Resolved.

**Where:** `rock-hero-editor/ui/src/plugin_browser_window.cpp:74-76` and `:368-371`;
`rock-hero-editor/ui/src/editor_view.cpp:1141-1163`;
`rock-hero-editor/core/src/editor_controller.cpp:1419-1428`.

**Symptom:** when the user clicked the in-window **Close** button or the native title-bar X, the
`PluginBrowserWindow` was destroyed while still inside that button's own click callback.

Call chain:

1. `m_close_button.onClick` (or `PluginBrowserWindow::closeButtonPressed`) called
   `m_listener.onPluginBrowserClosed()`.
2. `EditorView::onPluginBrowserClosed()` called `m_controller.onPluginBrowserClosed()`.
3. The controller set `m_plugin_browser_visible = false` and called `updateView()` synchronously.
4. `updateView()` → `m_view->setState(...)` → `presentPluginBrowserIfNeeded(visible=false)` →
   `m_plugin_browser_window.reset()`, which destroyed the `PluginBrowserWindow`, its owned
   `Content`, and the originating `juce::TextButton`.
5. Control returned to JUCE's button machinery, which continued to operate on the now-dead
   `Button*` after the lambda returned.

**Fix:** window teardown is now deferred so it cannot run inside its own event callback.

### 2. Legacy `AddPlugin` file-path action is now unreachable from the UI

**Status:** Resolved.

**Where:** `rock-hero-editor/core/include/rock_hero/editor/core/i_editor_controller.h:85`;
`rock-hero-editor/core/src/editor_action.h:159-171`;
`rock-hero-editor/core/src/editor_controller.cpp:955-957, 1405-1407, 1652-1670, 1730-1761`;
`rock-hero-editor/core/tests/test_editor_controller.cpp:1465, 1579, 2025, 2057, 2060, 2092,
2119, 2144, 2168, 2197, 2235, 2275, 3674, 3893, 3916`;
`rock-hero-editor/ui/tests/test_editor_view.cpp:114`.

`EditorView::showAddPluginChooser()` was removed by the original commit, but the entire
file-path code path remained:

- `IEditorController::onAddPluginRequested(std::filesystem::path)` was still in the public
  interface.
- `EditorAction::AddPlugin` was still a variant alternative; `performActionImpl` and
  `completeAddPluginScan` still implemented it; `actionAvailableWhenIdle` and `actionBusyPolicy`
  still routed it.
- A large block of controller tests still drove that path.

**Fix:** the file-path `EditorAction::AddPlugin`, its `IEditorController` method, the
`completeAddPluginScan` finalizer, `Engine::scanPluginFile`, `IPluginHost::scanPluginFile`, and
the corresponding `FakePluginHost` scaffolding are all removed. The string-id action was renamed
from `AddPluginCandidate` to `AddPlugin` and `onPluginCandidateAddRequested` was renamed to
`onAddPluginRequested(std::string)`. Controller tests now exercise the production browser path
through a new `addKnownPlugin` helper.

### 3. Catalog replacement versus merge inconsistency

**Status:** Resolved (by deletion of the merge path).

**Where (at time of finding):** `rock-hero-editor/core/src/editor_controller.cpp:1751` (merge
after file-picker scan) versus `:1825` (replace after Rescan); `:1681` (overwrite from
`knownPluginCatalog` on `ShowPluginBrowser`).

`completePluginCatalogScan` did `m_plugin_catalog = std::move(*state->candidates)` — wholesale
replacement. `completeAddPluginScan` called `mergePluginCatalogCandidates`. Both paths
disagreed about whether the catalog accumulated or reset.

**Fix:** the merge path's only caller (the file-based `completeAddPluginScan`) was removed as
part of finding 2's cleanup, taking `mergePluginCatalogCandidates` with it. The remaining
catalog producers (`ShowPluginBrowser` and `ScanPluginCatalog`) both replace, so there is no
inconsistency left.

**Follow-up:** `ScanPluginCatalog` now calls `IPluginHost::scanPluginCatalog()`, which refreshes
the host-owned default catalog. The controller then reads `knownPluginCatalog()` on the message
thread and replaces its browser snapshot from that host catalog.

### 4. Catalog roots resolved inside `editor/core`

**Status:** Resolved.

**Where (at time of finding):** `rock-hero-editor/core/src/editor_controller.cpp:184-241`
(`environmentPath`, `appendEnvironmentSubpath`, `defaultPluginCatalogRoots`).

`editor/core` previously read environment variables (`_dupenv_s` / `std::getenv`) and embedded
hard-coded paths under `#if defined(_WIN32) / __APPLE__ / else`. Per
`docs/design/architectural-principles.md`, environment- and platform-shaped concerns belong
behind a port so the headless core stays automated-testable.

**Fix:** default VST3 root resolution moved behind the audio boundary. `EditorController` now
expresses only the user intent by calling `IPluginHost::scanPluginCatalog()`. The Tracktion-backed
`Engine` owns platform/environment root selection and still exposes `scanPluginLocations()` for
future user-configurable roots.

### 5. `runAfterBusyOverlayPainted` startup fallback intent

**Status:** Resolved.

**Where:** `rock-hero-editor/ui/src/editor_view.cpp:782-795`.

The `!isShowing()` fallback stores the callback into `m_after_busy_overlay_paint`, then
immediately moves it back out into a local and invokes it. The intermediate member store is only
there so any reentrant `runAfterBusyOverlayPainted` call made during the callback observes an
empty member.

That is intentional, but it reads as redundant motion. Add a one-line comment naming the
reentrancy guard so a future reader does not "simplify" the member assignment away. Alternative
shape: skip the member assignment entirely when `!isShowing()` and just invoke `callback`
locally — same effect, no apparent redundancy.

**Fix:** the fallback now comments that the member is cleared before invoking the callback so
reentrant fence requests observe no stale callback.

### 6. Catalog ownership split between Tracktion and the controller

**Status:** Resolved.

**Where:** `rock-hero-common/audio/src/engine.cpp` (Tracktion `KnownPluginList`) versus
`rock-hero-editor/core/src/editor_controller.cpp` (`m_plugin_catalog`).

Reads always flow through the controller's snapshot, which is the right boundary. The lurking
issue is that Rescan results overwrite `m_plugin_catalog` without round-tripping through the
plugin host, so Tracktion's `KnownPluginList` and the browser catalog can drift. Typical DAW
behavior is that a user-initiated rescan updates the host's known list. If we eventually want
that, the boundary should clarify which side is canonical; for now a comment at
`completePluginCatalogScan` is enough.

**Fix:** `IPluginHost::scanPluginCatalog()` is now the full-catalog refresh port. `Engine`
scans default roots through Tracktion and updates Tracktion's `KnownPluginList`. The controller
then reads `knownPluginCatalog()` on the message thread and keeps only a sorted UI snapshot of
that result.

### 7. Minor efficiency notes (no action required yet)

**Status:** Open (informational; no action required).

- `appendUniquePluginCandidate` is O(N) per insert (O(N²) total). For realistic VST3 counts this
  is fine; if catalog sizes grow, swap to an `std::unordered_set<std::string>` for seen IDs.
- `PluginBrowserWindow::Content::rebuildFilteredIndices` recomputes a lowercased haystack per
  plugin per keystroke. Fine for ≤1k plugins; if it becomes noticeable, cache a precomputed
  lowercase haystack per plugin inside `setState`.
- `scanPluginLocationsForCandidates` correctly disables recursion into VST3 bundle directories
  via `disable_recursion_pending`. No issue, called out only because the path is easy to break
  on later edits.

### 8. `PluginCandidate` dual-use leak risk

**Status:** Resolved.

**Where (at time of finding):**
`rock-hero-editor/core/include/rock_hero/editor/core/plugin_browser_view_state.h` previously
held `std::vector<common::audio::PluginCandidate>` directly.

Raised during review of finding 2's cleanup: the cleanup collapsed
`PluginBrowserCandidateViewState` into `common::audio::PluginCandidate` so editor-core view
state consumed the audio-boundary type directly. The collapse was a defensible code-size win
while the two types were field-for-field identical, but it left no mechanical guard against
future fields on `common::audio::PluginCandidate` (Tracktion or JUCE handles, raw plugin
descriptions, opaque backend payloads) silently shipping into editor-ui state.

**Fix:** `rock-hero-editor/core/include/rock_hero/editor/core/plugin_candidate_view_state.h` defines
`core::PluginCandidateViewState` as the editor-core workflow record. The controller lifts the
audio-boundary catalog through `makePluginCandidateViewState` / `makePluginCandidateViewStates`
helpers in `editor_controller.cpp`, and `PluginBrowserViewState::plugins` now holds
`std::vector<core::PluginCandidateViewState>`. The controller is the single seam, so any future
backend-shaped field added to `common::audio::PluginCandidate` cannot reach editor-ui without
going through that conversion. The previously-extracted `plugin_candidate.h` header is removed
and `PluginCandidate` is folded back into `i_plugin_host.h` because the editor side no longer
needs a slim header to avoid pulling the port in.

Editor-only fields (favorites, match scores, last-used timestamps) now have a natural home on
`PluginCandidateViewState` rather than on the audio-boundary type.

## Suggested Ordering

Remaining open work:

1. Finding 7 (efficiency notes). No action needed until catalog sizes grow.

## Out of Scope

The deferred items already captured in `docs/todo/plugin-browser-followups.md` (persisted
catalog cache, user-configurable plugin search paths, safer scanner process, scan progress and
cancellation, favorites/grouping/search evolution, durable candidate identity) are not repeated
here. The findings above are about the current code shape, not the missing future features.
