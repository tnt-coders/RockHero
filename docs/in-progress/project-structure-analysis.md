# Project Structure Analysis — Full Review

Status: findings report, no changes made. Written 2026-07-03 from a fresh full-repo pass, the
comparative research listed below, and a re-read of every prior structure discussion
(`docs/completed/architecture-patterns-and-naming-review{,-v2}.md`,
`docs/completed/editor-runtime-extraction-plan.md`, `docs/completed/public-header-surface-review.md`,
`docs/completed/editor-structure-dropped-candidates.md`,
`docs/todo/editor-structure-deferred-work.md`,
`docs/todo/remaining-god-object-decomposition-plan.md`, and the two `docs/design/` architecture
documents).

External references reviewed: Charter (the Java chart editor,
`github.com/Lordszynencja/Charter`), Helio Workstation (JUCE C++ sequencer), MuseScore, Mixxx,
the Pitchfork C++ layout spec (`vector-of-bool/pitchfork`), and the package-by-feature vs
package-by-layer literature.

---

## 1. Current State (measured)

Production file counts per directory (headers + sources, excluding tests):

| Directory | Files | Notes |
|---|---|---|
| `common/core/include/...` | 21 | domain + package + infra mixed flat |
| `common/core/src` | 11 | `rock_song_package.cpp` is 1,687 lines |
| `common/audio/include/...` | 29 | ports + values + errors + workflows mixed flat |
| `common/audio/src` | 18 | `engine.cpp` is 5,906 lines (was 4,177 when the decomposition plan was written) |
| `editor/core/include/...` | 36 | was 33 at the last analysis |
| `editor/core/src` | 40 | flat |
| `editor/ui/include/...` | 2 | `editor.h`, `main_window.h` — deliberately small, good |
| `editor/ui/src` | 37 | flat; `editor_view.cpp` is 2,129 lines with 5 embedded classes |
| `editor/audio`, `common/ui`, `game/*` | placeholders | empty |

64 test files across 5 test targets, largest `test_editor_controller_plugins.cpp` (2,663) and
the shared controller harness (1,453).

Dependency graph (verified from CMake): exactly as documented — `common/core` at the root;
`common/audio → common/core`; product `core → common/{core,audio}`; product `ui → product core +
common/audio`; apps link the product aggregate. No violations found; no edges beyond the
documented set. **The dependency graph is not the problem and nothing in this report changes it.**

## 2. What Is Working and Must Not Churn

A fresh pass confirms what the two prior reviews found, and it is worth restating because every
recommendation below is constrained by it:

- **The 3×3 product/library grid is right.** Every alternative examined (below) is worse. The
  grid answers the two questions that govern linkage: *who may depend on this* (library) and
  *which product owns it* (subsystem). It is also the load-bearing structure for the test
  strategy — per-library test targets with private-`src/` reach are why the suite is fast and
  the bug rate is low.
- **Naming discipline is genuinely unusual in its consistency.** ~200 types; the suffix
  vocabulary (`*ViewState`, `*Workflow`, `*Controller`, `I*`, `*Error`/`*ErrorCode`, `*Prompt`,
  `*Snapshot`/`*Result`/`*Request`/`*Progress`, nested `::Listener`) holds with single-digit
  exceptions already catalogued in the v2 review.
- **Private-first headers.** `editor/ui` exporting only `editor.h` + `main_window.h` while 35
  widget files stay in `src/` is the correct shape and should be extended, not reversed.
- **One primary type per header** still holds. Directory pressure is file *count*, not file
  bloat — with three exceptions called out in §6.

## 3. The Core Diagnosis: the Missing Axis Is *Feature*

The grid gives you two axes — product and layer — and both are the right axes for **linkage**.
But the question a human asks when scanning is neither "what may this depend on" nor "which
binary is this in." It is **"where is the timeline code?"** — and that question currently has no
answer in the tree. It has an answer only in file-name prefixes, alphabetically interleaved with
every other feature in four flat pools.

Concretely, the timeline feature today is:

```text
editor/ui/src:    timeline_ruler.{h,cpp}, timeline_cursor.{h,cpp}, grid_spacing_selector.{h,cpp},
                  arrangement_view.{h,cpp}, + TrackViewport/Content/TimelineViewport/CursorOverlay
                  as private classes INSIDE editor_view.cpp
editor/core:      tempo_grid_geometry.{h,cpp}, timeline_geometry.{h,cpp},
                  transport_readout_text.{h,cpp}, arrangement_view_state.h
common/core:      tempo_map.*, timeline.h, fraction.h
tests:            test_editor_view_timeline.cpp, test_tempo_grid_geometry.cpp,
                  test_timeline_geometry.cpp, test_transport_readout_text.cpp, test_tempo_map.cpp
```

Five directories, ~20 files, zero visual grouping. Signal chain, plugin browser, audio-device
settings, input calibration, busy, transport, and project lifecycle each have the same shape
(4–9 files per layer, smeared). The tests already solved this problem *by naming*: the
`test_editor_view_<feature>.cpp` split was, in effect, feature foldering applied to test files.
Production code has not followed.

The prior reviews both said feature grouping was "directionally correct but premature — wait
until the files exist." Fresh finding: **the wait triggers have fired.** `editor/core/src` is at
40 files, `editor/ui/src` at 37, `editor/core/include` grew 33→36, `engine.cpp` grew 41%, and
the tempo arc added an entirely new *kind* of module (§4c) with no designated home. This is the
"becoming difficult to scan" the deferred register said to watch for.

## 4. Emerging and Partially-Emerged Patterns

These are the patterns the code has grown that the documented vocabulary does not yet cover —
the direct cause of "it's hard to reason about where things logically belong."

### 4a. The two-tier controller rule exists but is unwritten

The codebase actually follows a coherent rule, visible only by inference:

- **Features with their own window/dialog lifecycle get their own controller triad:**
  AudioDeviceSettings (`AudioDeviceSettingsController` + `IAudioDeviceSettingsView` +
  `AudioDeviceSettingsViewState`) and InputCalibration (same shape). The dialog owns a
  transactional sub-session, so it earns a sub-controller.
- **Features resident in the main editor window route through the root facade:** transport,
  signal chain, plugin browser, busy, timeline. They get a `*ViewState` slice inside
  `EditorViewState`, optionally a headless `*Workflow`/`*State` policy type, and their intents
  land on `IEditorController`. No sub-controller — deliberately (the extraction plan's
  "one root controller" position).

Because this rule is unwritten, each new feature *looks* like a judgment call and the triad
appears inconsistently applied ("some components don't fit cleanly into the pattern"). The
pattern is fine; the rule needs writing down. Proposed statement: *a feature gets its own
`Controller`/`I*View`/`ViewState` triad if and only if it owns a modal window with its own
apply/cancel lifecycle; everything hosted in the main window is a root-facade feature whose
policy, if nontrivial, is a `Workflow`/`State` type and whose render data is a `ViewState`
slice.*

### 4b. Per-feature completeness varies — and that is correct, but undocumented

Root-facade features legitimately have different subsets: transport = ViewState + widget (no
workflow — policy is trivial); busy = State + Workflow + ViewState + Overlay; signal chain =
Workflow + ViewState + View + placement helper; timeline = geometry modules + ViewState slice +
widgets. Nothing requires every feature to own all five roles. The documentation should say so
explicitly, otherwise every new feature invites "where is its workflow?" doubt.

### 4c. A new, unnamed role has emerged: pure presentation-math/formatting modules

The tempo arc created a cluster with no vocabulary entry and no folder hint in any plan:

- `editor/core`: `tempo_grid_geometry` (free functions + small structs),
  `timeline_geometry`, `transport_readout_text` — public, consumed by `editor/ui`.
- `editor/core/src`: `audio_device_status_text` — same role, private (consumer is the
  controller).
- `editor/ui/src`: `timeline_cursor` (shared drawing/repaint/mode helpers), `text_metrics`,
  `signal_chain_block_layout`, `editor_colors` — the UI-side analog: shared presentation
  helpers that are not components.

These are exactly the files the architecture *wants* to exist (headless presentation math with
headless tests — the 1.4.99 fix lives there), and they are multiplying. They are neither
`Workflow` (no state, no decisions-with-effects) nor `ViewState` nor `Controller`. Proposed
vocabulary: **projection modules** — free-function headers named `<feature>_geometry.h` /
`<feature>_text.h` / `<feature>_layout.h`, living *with their feature*, public only when a
consumer outside the library exists. This retroactively explains the existing four and gives the
next one an obvious home.

### 4d. `EditorView` is quietly re-growing into the next monolith

`editor_view.cpp` is 2,129 lines containing five classes: `CursorOverlay`, `TrackViewport`,
`TrackViewport::Content`, `TimelineViewport`, and `MenuLookAndFeel`. Three of those five are the
timeline feature, trapped as private nested classes. This matters more than any folder question
because the next scheduled work — continuous-scroll playback follow, then note/tab editing on
the highway — lands exactly there. The completed extraction plan deliberately protected
`EditorController` as a root facade; `EditorView` deserves the same *explicit* decision: it is
the shell + listener hub, and feature components should be extracted from it the way workflows
were extracted from the controller. Extracting `TrackViewport`/`Content`/`CursorOverlay`/
`TimelineViewport` into `timeline/track_viewport.{h,cpp}` (private) would cut the file roughly
in half and give the upcoming timeline work a real home. `MenuLookAndFeel` belongs beside
`menu_bar_button` in a shell cluster.

### 4e. `engine.cpp` has grown 41% past its decomposition plan

5,906 lines, 8 ports, one TU. `docs/todo/remaining-god-object-decomposition-plan.md` Part A
already contains the correct, detailed split (impl header + per-port TUs + helper clusters) and
explicitly gates the `src/tracktion/` folder on the split happening. No new analysis needed —
the finding is priority: it is now the largest scannability and incremental-build cost in the
repo, and every tone/undo feature keeps adding to it.

### 4f. Smaller consistencies worth sweeping opportunistically

Confirmed still open from v2 (none new, none urgent): the view-suffix two-tier policy
(`*View` vs `*Panel/*Controls/*Overlay/*Meter`) is still undocumented in
`coding-conventions.md`; `AudioDeviceSettingsWindow`/`InputCalibrationWindow` → `*Dialog` when
touched; `LiveRigSnapshot` → `LiveRigCaptureResult` when touched; `PluginHandle`/`LiveRigPlugin`/
`PluginViewState` unification; `main_window.h` → app target. The error-type file pairs (16
`*_error.{h,cpp}` files) are individually correct but are ~20% of the flat-listing noise —
feature folders absorb them without any merging.

## 5. Comparative Research

**Charter** (the project you linked): layer-first at the top — `data/ gui/ io/ services/ sound/
util/` — with *feature subfolders inside each layer* (`gui/chartPanelDrawers`, `gui/panes`,
`services/editModes`, `services/mouseAndKeyboard`). Its scannability comes almost entirely from
that second level. What not to adopt: `services` is a grab-bag (window listeners next to fret
math), `util` is a dumping ground, and Java package-privacy is its only dependency enforcement —
RockHero's library boundaries are categorically stronger. Notably, Charter's top level *is*
RockHero's library level (`data≈core`, `gui≈ui`, `sound≈audio`); the difference is Charter has
the third level and RockHero doesn't.

**Helio Workstation** — the closest analog (JUCE, C++, one developer, music editor, ~10× the
file count): `Source/Core/<Feature>/` and `Source/UI/<Feature>/` — the same core/ui split with
feature folders inside, and **no namespace-per-folder** (JUCE style). It demonstrates the exact
target shape scaling to a mature product.

**MuseScore**: library-per-feature (`src/engraving`, `src/playback`, `src/notation`, …, ~22
modules). It works, but the costs are visible: a heavyweight `framework/` shared kernel, complex
inter-module wiring, and stub modules to break cycles. This is what "more libraries" grows into,
and it confirms your instinct to reject it at this scale.

**Mixxx**: one big target, ~30 flat feature dirs under `src/`. Simple to scan, weak boundaries;
its test strategy leans integration-heavy — the opposite of this project's stated strength.

**Pitchfork / canonical-structure literature**: prescribes namespace↔directory correspondence as
the ideal for *externally consumed libraries*; it also allows merged headers and explicitly
treats submodules as a very-large-project tool. The package-by-feature vs package-by-layer
literature converges on the hybrid: **layer first, feature second** for codebases with real
layer boundaries. JUCE and Tracktion themselves — the two frameworks this repo vendors — use
deep folder trees (`juce_gui_basics/{components,widgets,windows,...}`,
`tracktion_engine/{model,playback,...}`) with a single namespace each.

## 6. Options Analysis (the three levers you named)

### More nested namespaces — recommend **no** (unchanged)

Namespaces solve name collisions and linkage ownership, not navigation. The project fully
qualifies names by convention, so `rock_hero::editor::core::timeline::visibleTempoGridLines`
would tax every call site for zero collision benefit (there are no collisions). Every reference
codebase that scales the target shape (JUCE, Tracktion, Helio, Mixxx) does folders-without-
namespaces. Keep `testing/` as the lone folder+namespace pair — it earns it because it marks a
*linkage* boundary (test-only targets), which is exactly what namespaces are for.

### More libraries — recommend **no** (unchanged)

Feature-per-library is dishonest about the coupling that actually exists: main-window features
share the root facade, the aggregate `EditorViewState`, and the view hub. Cutting libraries
through that would force artificial interfaces (MuseScore's `framework/` + stubs problem) and
multiply CMake surface. The 3×3 grid also carries the product identity ("editor code cannot
leak into the game") which feature libraries would blur. The only future library-shaped hole is
real: when the game grows, `game/core` simulation vs `game/ui` bgfx rendering — and the existing
grid already has those slots.

### Feature subfolders without matching namespaces — recommend **yes**, and here is the answer to "it seemed messy"

The discomfort was that folders would diverge from namespaces. Two observations dissolve it:

1. **The divergence already exists and nobody minds.** `<rock_hero/editor/core/editor_view_state.h>`
   has four path segments and two namespace segments; file names have never been namespace
   segments. A feature segment (`<rock_hero/editor/core/timeline/tempo_grid_geometry.h>`) is the
   same kind of thing as a file name: a *navigation* coordinate, not an *ownership* coordinate.
2. **The rule that keeps it from being messy is a single sentence:** *namespace = who owns and
   links it (product::library, stable, three levels, never grows); folder = where you find it
   (feature, cheap to reshape, no code meaning).* Two axes, two mechanisms, no overlap. This is
   the documented position of three completed plans ("folders before namespaces") — it has just
   never been stated as the positive rule, only as a prohibition.

## 7. Proposed Target Shape

One rule applied uniformly: **inside every library, group by feature; use the same feature
names across `core`, `ui`, and test files; keep root-level files for the root contract.** A
feature earns a folder at ≥3 files; until then it stays at the library root. One CMake target
per library, unchanged — folders are `target_sources` paths only.

```text
rock-hero-editor/core/include/rock_hero/editor/core/
  (root)            i_editor_controller.h, editor_controller.h, i_editor_view.h,
                    editor_view_state.h, editor_action_id.h
  timeline/         tempo_grid_geometry.h, timeline_geometry.h, transport_readout_text.h,
                    arrangement_view_state.h
  transport/        transport_view_state.h
  signal_chain/     signal_chain_view_state.h, plugin_view_state.h, plugin_block_assignment.h,
                    plugin_display_type.h, signal_chain_block_placement.h
  plugin_browser/   plugin_browser_view_state.h, plugin_candidate_view_state.h
  audio_device/     audio_device_settings_controller.h, i_audio_device_settings_controller.h,
                    i_audio_device_settings_view.h, audio_device_settings_view_state.h
  input_calibration/ input_calibration_controller.h, i_input_calibration_view.h,
                    input_calibration_view_state.h
  busy/             busy_view_state.h
  project/          project.h, project_error.h, i_song_importer.h, song_import_error.h
  settings/         editor_settings.h, i_editor_settings.h, editor_settings_error.h
  tasks/            i_editor_task_runner.h, juce_editor_task_runner.h,
                    i_message_thread_scheduler.h, juce_message_thread_scheduler.h

rock-hero-editor/core/src/    (same feature folders; editor_controller.cpp, editor_action.*,
                               editor_action_availability.*, editor_undo_history.* stay at root
                               as the root facade cluster)

rock-hero-editor/ui/src/
  shell/            editor.cpp, editor_view.{h,cpp}, main_window.cpp, menu_bar_button.*,
                    menu look-and-feel, editor_colors.h
  timeline/         track_viewport.{h,cpp} (extracted from editor_view.cpp), timeline_ruler.*,
                    timeline_cursor.*, arrangement_view.*, grid_spacing_selector.*
  transport/        transport_controls.*
  signal_chain/     signal_chain_view.*, signal_chain_panel.*, signal_chain_block_layout.*
  plugin_browser/   plugin_browser_window.*
  audio_device/     audio_device_settings_view.*, audio_device_settings_window.*
  input_calibration/ input_calibration_window.*
  busy/             busy_overlay.*
  shared/           audio_level_meter.*, text_metrics.*

rock-hero-common/audio/include/rock_hero/common/audio/
  (root)            engine.h, gain.h, scoped_listener.h
  device/           i_audio_device_configuration.h, audio_device_settings.h, audio_device_status.h,
                    audio_device_configuration_error.h
  transport/        i_transport.h, transport_state.h
  song/             i_song_audio.h, song_audio_error.h, audio_normalization.h, i_thumbnail.h,
                    i_thumbnail_factory.h
  plugin/           i_plugin_host.h, plugin_host_error.h, plugin_chain_snapshot.h,
                    plugin_instance_state.h, plugin_chain_limits.h, plugin_catalog_scan_progress.h
  live_rig/         i_live_rig.h, live_rig_error.h
  input/            i_live_input.h, live_input_error.h, input_calibration.h,
                    input_calibration_state.h, input_device_identity.h, i_audio_meter_source.h,
                    audio_meter_snapshot.h

rock-hero-common/audio/src/   per the existing Part A plan: engine_impl.h + per-port TUs;
                              tracktion/ folder only when the split has happened

rock-hero-common/core/include/rock_hero/common/core/
  (root or, if desired, domain/ | package/ | util/ at 21 files this is the least urgent)
```

Points of judgment inside that sketch, flagged rather than hidden:

- `arrangement_view_state.h` reads as timeline (it is the waveform row) — could also argue a
  `project/` home; timeline is where its consumers live.
- Whether calibration value types in `common/audio` sit under `input/` or a separate
  `calibration/` is taste; `input/` keeps the live-input port beside its calibration math.
- `common/core` is below the pain threshold (21 files); grouping it is optional symmetry, not
  need.
- Test *directories* stay flat — the `test_<feature>_*.cpp` naming already does the job there,
  and CTest registration stays trivial.

## 7a. The include/ ↔ src/ Pairing Concern

The strongest objection to subfolders is that the separated `include/`/`src/` layout already
splits a header from its `.cpp`, and mirrored feature folders would deepen two parallel trees
that must be kept in sync. Measured against the actual files, the concern is much smaller than
it feels, and the remainder has a clean rule.

**Most public headers have no `.cpp` at all.** Counted today: 20 of 36 in `editor/core`, 20 of
29 in `common/audio`, 10 of 21 in `common/core` are header-only — every `I*` port, every
`*ViewState`, every value type and enum. For these there is no pair to disconnect; `include/` is
their only home, and the public tree reads as what it is: the API manifest. The "disconnected
pair" problem only exists for the minority with implementations (16 / 9 / 11 respectively).

**The paired minority shrinks further under the existing private-first rule.** A header earns
`include/` only when a consumer outside the library needs it (`public-header-surface-review.md`
already flags `rock_song_importer.h` and others as wrongly public). The steady state is that
header+cpp pairs in `include/` are exactly the real cross-library machinery — the controller,
settings, project, the projection modules consumed by ui — roughly one to three paired files
per feature folder.

**For that remainder, the mirror is strictly more discoverable than today, not less.** The rule
is: identical feature segment, identical basename —
`include/rock_hero/editor/core/timeline/tempo_grid_geometry.h` ↔
`src/timeline/tempo_grid_geometry.cpp`. Today the pairing jump crosses two flat alphabetical
lists of 36 and 40 entries; after foldering it crosses two four-entry folders with the same
name. The mirror needs maintaining only at folder granularity, and only where a feature has
public paired headers at all (several features are header-only or private-only on one side).
IDE header↔source toggling is location-independent either way; the tree-scanning cost is what
changes, and it changes in the folders' favor.

**The alternative was considered and rejected: merged placement** (headers beside sources in
`src/`, Pitchfork's other sanctioned layout, JUCE-module style). It would genuinely maximize
pair adjacency, but this project's `include/` tree is not just navigation — it is the
*enforced* public boundary (`PUBLIC include` vs `PRIVATE src` on the targets means consumers
physically cannot include a private header). Merging would either put `src/` on the public
include path (dissolving the private-first discipline that the header-surface review curates)
or require install/copy machinery, and it would break the `<rock_hero/product/library/...>`
include idiom unless `src/` replicated the full prefix path. Trading compile-time boundary
enforcement for pair adjacency is the wrong trade for a project whose stated top priority is
boundary-driven testability. A middle option — feature folders in `src/` only, flat `include/`
— avoids the mirror entirely but abandons the two directories that are actually past the
scanning threshold (29 and 36 files), so it fails the original goal.

## 8. What This Costs, Honestly

- **Private `src/` moves** (editor/ui, editor/core src): near-zero churn — relative includes
  within the library and CMake source lists. No consumer sees it.
- **Public `include/` moves** (editor/core, common/audio): every `#include` of a moved header
  changes, repo-wide. Mechanical (`rg` + `sed`), but it is real churn and each library should be
  one dedicated, behavior-free commit. Doxygen `\file` paths and the architecture doc's key-file
  list update with it.
- **The two extractions that are *not* pure moves** — timeline components out of
  `editor_view.cpp`, and the engine Part A split — are behavior-preserving refactors with the
  existing test suites as the guard, and should each be their own plan-doc-tracked effort.
- Ongoing cost of the convention itself: one decision per new file ("which feature?"), which is
  exactly the decision the current layout forces you to encode in a filename prefix anyway.

## 9. Recommended Sequence

1. **Adopt the rules on paper first** (one `docs/design/` update, after your confirmation):
   the feature-folder axis and its one-sentence namespace rule (§6), the two-tier controller
   rule (§4a), per-feature role-completeness (§4b), the projection-module vocabulary (§4c), and
   the v2 view-suffix policy that was never landed.
2. **`editor/ui/src` feature folders + the timeline extraction from `editor_view.cpp`** —
   highest value, zero public churn, unblocks the continuous-scroll work with a home ready.
3. **`editor/core/src` feature folders** — zero public churn.
4. **`editor/core/include` feature folders** — first public-churn commit.
5. **`common/audio` Part A engine split** (existing plan), then its **public header grouping**
   as the plan's optional step 5.
6. **`common/core`** grouping only if it still feels needed afterward.

Each step independently valuable; stopping after any step leaves the repo better than before.

## 10. What Not To Do (reaffirmed against fresh analysis)

- No namespace-per-folder; no role namespaces; `testing/` stays the only exception.
- No feature libraries; no change to the 3×3 grid or any dependency edge.
- No role folders (`view_state/`, `ports/`) where they compete with feature folders — the
  suffix already answers "what kind"; the folder should answer "what feature". (`tasks/` and
  `settings/` above are features of the editor runtime, not roles.)
- No splitting `EditorController` (settled); no per-view controllers.
- No renaming sweeps — `*Dialog`, `LiveRigCaptureResult`, plugin-type unification stay
  opportunistic, when touched.
- No moving `engine.cpp` wholesale into a `tracktion/` folder without the seam split.
- No public-header moves mixed into behavior commits.

## 11. Open Decisions

1. Confirm the feature-folder direction and the namespace stance before any files move.
2. Folder manifest sign-off (§7) — especially `arrangement_view_state` placement, `input/` vs
   `calibration/`, and whether `common/core` participates now or later.
3. Whether step 2 (ui folders + timeline extraction) should precede or ride along with the
   continuous-scroll feature work, since they touch the same files.
4. Whether the engine Part A split gets scheduled next (it has grown 41% past its plan) or
   stays parked until tone work next touches it.
