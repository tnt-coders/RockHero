# Plugin Browser Code Review Findings

Status: completed. Concrete review findings for commit
`5c168ece - Add plugin browser workflow`, plus follow-up findings raised during review of the
subsequent cleanup and lazy-discovery work. The review is complete: all findings below are
resolved, and there are no open code-review follow-ups.

## Resolution Status

- **Resolved:** finding 1 (re-entrant Close-button destruction), finding 2 (legacy file-path
  `AddPlugin`), finding 3 (catalog merge/replace inconsistency), finding 4 (catalog roots in
  editor-core), finding 5 (paint-fence comment nit), finding 6 (catalog ownership split between
  Tracktion and controller), finding 7 (catalog-scale efficiency), and finding 8
  (`PluginCandidate` dual-use leak risk).
- **Accepted model:** plugin catalog refreshes use optimistic filesystem discovery. They list VST3
  candidates quickly without loading plugin binaries. Validation happens only when the user adds a
  selected candidate.
- **Open:** none.

## Final Model

The plugin browser now uses a fast, scalable discovery model:

- Opening the browser reads the host's current lightweight catalog snapshot.
- Rescan walks conventional VST3 roots and records filesystem-backed `.vst3` candidates.
- Rescan does not load every plugin binary or prove every candidate is usable.
- `IPluginHost::addPlugin(const PluginCandidate&)` receives the selected candidate, so the engine
  can use `PluginCandidate::file_path` directly when an optimistic candidate needs validation.
- A corrupt, missing, or incompatible plugin fails at Add time with a typed `PluginHostError`
  instead of slowing down the whole catalog refresh.

This is the intended tradeoff. It keeps catalog refresh proportional to filesystem traversal and
avoids making users with large plugin catalogs pay validation cost for every installed plugin.

## Scope Reviewed

- `common/audio`: `IPluginHost`, `Engine` plugin discovery, known-plugin catalog reads, lazy
  plugin validation, plugin insertion, and plugin-window behavior.
- `editor/core`: plugin-browser view state, controller actions, busy-state transitions, catalog
  refresh scheduling, candidate add behavior, and startup-restore interaction.
- `editor/ui`: `PluginBrowserWindow`, `EditorView` window ownership, listener forwarding, and
  busy-overlay paint fencing.
- Tests around common audio plugin-host behavior, editor controller workflow, and editor UI wiring.

## Review Checklist Status

- **Clean layering:** Pass. Tracktion/JUCE plugin details stay behind
  `common::audio::IPluginHost`. Editor workflow lives in `editor/core`. UI components only render
  state and emit intents.
- **Cheap browser open:** Pass. `ShowPluginBrowser` calls `knownPluginCatalog()` only; it does not
  traverse plugin folders or launch plugin validation.
- **Fast catalog refresh:** Pass. Rescan walks default VST3 locations and returns optimistic
  filesystem candidates without loading each plugin binary.
- **Add-time validation:** Pass. The selected `PluginCandidate` is passed to the host, and the
  engine uses `file_path` directly for first-time validation instead of decoding a path from the
  catalog ID.
- **Lifecycle on project changes:** Pass. `closeProject` and `loadSessionSong` reset
  `m_plugin_browser_visible`, and the user-driven Close path now defers window teardown.
- **Busy-token handling for scan and add:** Pass. Catalog refresh and add continuations re-check
  the captured busy token before mutating state.
- **Startup restore paint fence:** Pass. The not-yet-showing fallback in
  `EditorView::runAfterBusyOverlayPainted` names the reentrancy guard that keeps startup restore
  from blocking on an impossible paint.
- **Tests cover user behavior without over-coupling:** Pass. Controller tests exercise the
  production browser path, and common audio tests cover the plugin-host port contract.

## Findings

### 1. Re-entrant browser window destruction during the Close click

**Status:** Resolved.

**Issue:** the plugin browser window could be destroyed while still inside its own Close-button or
native title-bar close callback. That created a likely use-after-free risk in JUCE's button/window
callback machinery.

**Fix:** browser window teardown is deferred so it cannot run inside the originating event
callback.

### 2. Legacy `AddPlugin` file-path action was unreachable from the UI

**Status:** Resolved.

**Issue:** the old file-picker plugin add path remained after the browser workflow replaced it.
That left dead controller actions, fake scaffolding, and tests for a path users could no longer
reach.

**Fix:** the file-path `AddPlugin` flow, `scanPluginFile` port method, finalizer, and tests were
removed. The production path is now browser selection by catalog ID, followed by adding the
resolved `PluginCandidate`.

### 3. Catalog replacement versus merge inconsistency

**Status:** Resolved.

**Issue:** one path replaced the browser catalog while the removed file-picker path merged into
it, so catalog ownership semantics were inconsistent.

**Fix:** the merge path was deleted with the legacy file-picker flow. Browser open and rescan now
replace the controller's browser snapshot from host-owned catalog data.

### 4. Catalog roots were resolved inside `editor/core`

**Status:** Resolved.

**Issue:** `editor/core` owned environment-variable reads and platform-specific default VST3 root
paths. That placed filesystem/platform policy in the headless editor workflow instead of the audio
adapter boundary.

**Fix:** default root resolution moved behind `IPluginHost::scanPluginCatalog()`. The
Tracktion-backed `Engine` owns platform root selection; editor-core only expresses the user's
rescan intent.

### 5. `runAfterBusyOverlayPainted` startup fallback intent was unclear

**Status:** Resolved.

**Issue:** the fallback path stored a callback in a member and immediately moved it back out, which
looked redundant without the reentrancy context.

**Fix:** the code now comments that clearing the member before invocation is intentional so
reentrant fence requests do not observe a stale callback.

### 6. Catalog ownership split between Tracktion and the controller

**Status:** Resolved.

**Issue:** review raised that the controller snapshot and Tracktion's `KnownPluginList` could drift
if rescan results bypassed the host-owned catalog boundary.

**Fix:** `IPluginHost` is the catalog authority. The engine owns known Tracktion entries plus
filesystem-discovered optimistic candidates, while the controller keeps only a sorted UI snapshot.
The controller asks the host to refresh and then reads `knownPluginCatalog()` back on the message
thread.

### 7. Catalog-scale efficiency

**Status:** Resolved.

**Issue:** the initial scanner-shaped implementation did too much work for larger plugin catalogs.
It also had O(N^2) duplicate checks and recomputed lowercase filter text per row per keystroke.

**Fix:** catalog refresh now discovers `.vst3` filesystem candidates without validating each
plugin binary. Duplicate detection uses `std::unordered_set`, the browser caches lowercase search
text when state is applied, and VST3 bundle recursion remains disabled so bundle internals are not
walked as ordinary folders.

### 8. `PluginCandidate` dual-use leak risk

**Status:** Resolved.

**Issue:** editor-core view state temporarily consumed `common::audio::PluginCandidate` directly.
That made it too easy for future backend-shaped audio fields to leak into editor UI state.

**Fix:** `PluginCandidateViewState` is the editor-core workflow/display record. The controller is
the explicit conversion point from `common::audio::PluginCandidate` to editor state. The audio
boundary still owns `PluginCandidate`, and `IPluginHost::addPlugin` receives that selected
candidate so Add-time validation can use `file_path` without turning the catalog ID into a path
transport mechanism.

## Closed Notes

No separate follow-up plan is retained for this review. Previously listed items such as
user-configurable search paths, favorites/grouping, scan cancellation, or persistent browser
preferences are normal future product work, not unresolved defects in the reviewed implementation.
The current implementation intentionally favors fast optimistic discovery with typed Add-time
failure for invalid individual plugins.
