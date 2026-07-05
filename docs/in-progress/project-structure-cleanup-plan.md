# Project Structure Cleanup Plan

Status: active plan, written 2026-07-04. Phase 0 was approved and executed the same day, then
amended after user review (revision notes below); Phases 1+ are gated on Phase 0.

Revision 2026-07-04 (post-review amendments):

- The multi-TU class rule (§2.4) is **provisional**: it was withdrawn from
  `docs/design/architectural-principles.md` and is codified there only at the Phase 2 checkpoint,
  after the pilot slice proves the shape. The proven, already-practiced rules (feature folders,
  controller tiers, projection modules, suffixes) stay codified from Phase 0.
- Phase completion is an explicit **acceptance gate**: the agent does not run builds (user rule —
  agent-run builds break CLion include paths), so a phase is complete only when the user reports
  `cmake --build`, `ctest`, the clang-tidy target, and `pre-commit run --all-files` green.
- Phase 2 gained a pilot-slice guardrail with abort criteria (§ Phase 2), reflecting that the
  `Impl` split is the riskiest step in the plan, not a routine move.
- Line counts corrected to current measurements (2026-07-04): `editor_controller.cpp` 4,807
  (grew ~35% past the June figure this plan originally carried), `editor_view.cpp` 2,187,
  `engine.cpp` 5,941.

Companion findings: `docs/in-progress/project-structure-analysis.md` (2026-07-03 full-repo
analysis) and the 2026-07-04 coupling deep-dive it led to. This plan restates only the
conclusions; the evidence lives there.

The file catalogue below was re-verified against the working tree on 2026-07-04 (fresh glob of all
four active libraries, embedded-class scan of every multi-type source file, and location of every
concrete undo edit type). Counts: `common/core` 21 public + 11 src, `common/audio` 29 public +
18 src, `editor/core` 36 public + 40 src, `editor/ui` 2 public + 37 src, plus 5 test targets.
Because the one-primary-type-per-header convention holds (exceptions catalogued in §3.6), the
file-level catalogue is the type-level catalogue.

---

## 1. Goal and Definition of Done

Make the repository human-scannable and structurally unambiguous: every file findable by feature,
every placement decidable by rule, no monolithic translation units, and the two real structural
flaws fixed.

Done means all of the following hold:

1. **Feature axis exists everywhere it is needed.** Inside every library, files are grouped by
   feature folder; the same feature names recur across `core`, `ui`, and test file names.
2. **Root directories are honest.** A file at a library root is there because it is cross-feature
   contract (facade, action sum, availability table, undo timeline, aggregate view state) — never
   because nobody decided.
3. **Flaw 1 fixed:** the plugin browser is part of the signal-chain feature (folder, handler TU,
   catalog workflow ownership). No pairwise feature-to-feature conversation remains.
4. **Flaw 2 fixed:** no correct-object-in-monolithic-TU remains. `editor_view.cpp` (2,187),
   `editor_controller.cpp` (4,807), and `engine.cpp` (5,941) are each reduced to their genuinely
   shared core, with per-feature / per-port definitions distributed into feature folders. Soft
   targets: no production file above ~1,500 lines; the controller hub remainder around a third of
   its current size (`rock_song_package.cpp` at 1,687 is the one accepted temporary exception,
   revisited in Phase 5).
5. **No flat production directory above ~15 files**, except directories explicitly accepted by a
   recorded go/no-go decision (currently `common/core/include` at 21 files, decided at Phase 5).
6. **Placement is mechanical.** The new-file decision procedure (§2.3) answers every "where does
   this go" without judgment calls beyond naming the feature.
7. **Docs match reality.** `docs/design/` carries the adopted rules; `architecture.md`'s key-file
   list points at the new paths; each phase ends with green user-run build + tests + clang-tidy +
   pre-commit.

## 1.1 Non-Goals (hard constraints, reaffirmed)

- No change to the 3×3 product/library grid, any CMake target, or any dependency edge.
- No namespace changes; folders are navigation only. `testing/` remains the only folder+namespace
  pair.
- No feature libraries, no role folders, no self-registering module architecture (coupling-table
  verdict; break-even signals that would reopen it are recorded in §6.3).
- The `EditorController` **object** stays whole: one facade, one `EditorAction` sum, one
  availability table, one atomic state push. Only its translation-unit layout changes.
- No renaming sweeps (`*Dialog`, `LiveRigCaptureResult`, plugin-type unification stay
  opportunistic, when touched).
- No behavior changes anywhere in this plan. Extraction commits are behavior-preserving and
  verified by the existing suites; move commits are pure moves.

---

## 2. Rules Adopted (Phase 0 writes these into `docs/design/`)

### 2.1 The two-axis rule

> **Namespace = who owns and links it** (`product::library`, stable, three levels, never grows).
> **Folder = where you find it** (feature, cheap to reshape, no code meaning).

A feature earns a folder at ≥3 files; until then its files sit at the library root. Once a
feature folder exists, every new file for that feature goes in it immediately. `include/` and
`src/` mirror feature names with identical basenames for paired files.

### 2.2 The two-tier controller rule (already followed, now written)

A feature gets its own `Controller` / `I*View` / `ViewState` triad **iff** it owns a modal window
with its own apply/cancel lifecycle (today: audio-device settings, input calibration). Everything
hosted in the main window is a root-facade feature: its policy, if nontrivial, is a
`Workflow`/`State` type; its render data is a `ViewState` slice; its intents land on
`IEditorController`. Features legitimately own different subsets of the five roles
(workflow / state / view-state / projection / widgets); absence of a role is not a gap.

**Projection modules** are the third vocabulary entry: free-function headers of pure
presentation math or formatting (`<feature>_geometry.h`, `<feature>_text.h`,
`<feature>_layout.h`), living with their feature, public only when consumed outside the library.

### 2.3 New-file decision procedure (the no-ambiguity rule)

1. **Library** — decided by the existing dependency rules (needs Tracktion → `common/audio` impl;
   JUCE UI → a `ui` library; headless → a `core` library; both products → `common`).
2. **Feature** — name the user-facing feature the file serves.
   - Exactly one → that feature's folder.
   - Consumed by ≥2 features with no feature semantics of its own (a mechanism, e.g. text
     metrics) → `shared/` in `ui` libraries; library root in `core` libraries. Admission rule for
     `shared/`: ≥2 consumers **and** nameable without any feature word.
   - Composes or joins features (dispatch, availability, deferred actions, undo timeline,
     aggregate view state) → library root, which is reserved for exactly this.
3. **Kind never decides placement.** The suffix (`Workflow`, `ViewState`, `*Error`, `I*`) answers
   "what kind"; the folder answers "which feature". No `ports/`, `errors/`, `view_states/`.
4. **Visibility** — `src/` first; a header moves to `include/` only when a consumer outside the
   library exists.

### 2.4 Multi-TU class rule (provisional — narrows one settled non-goal)

> **Staging note (post-review):** unlike §2.1–2.3 and §2.5, which document already-practiced
> patterns, this rule is unproven in this repository. It lives only in this plan until the
> Phase 2 pilot slice validates it; it is written into
> `docs/design/architectural-principles.md` at the Phase 2 checkpoint, not before.

A deliberately unified coordination object (root facade, engine) may define its member functions
across multiple translation units when its surface slices along a stable axis (feature, port).
Conditions: private state declared once in one private header; each TU is one named slice living
in that slice's folder; slice-local helpers stay in that TU's anonymous namespace; shared private
helpers get their own named unit. Precedent: `juce::Component` (7 TUs, axis = platform) and the
engine Part A plan. The prior "never split `editor_controller.cpp`" non-goal is hereby narrowed:
the **object** stays; the **file** distributes.

### 2.5 View-suffix policy (from the v2 review, never landed)

`*View` for a feature's top-level component; `*Panel` / `*Controls` / `*Overlay` / `*Meter` /
`*Window` for parts and hosts. Recorded in `coding-conventions.md` by Phase 0.

---

## 3. Full Catalogue

Every production file, current → target. `(new)` marks files created by extraction; `(gone)`
marks files whose contents distribute. Tests are listed in §3.5.

### 3.1 `rock-hero-editor/ui` — 39 files (Phase 1)

`include/` (2 files) is already correct and stays flat: `editor.h`, `main_window.h`.
(Opportunistic, decide during Phase 1: `main_window.h` may move to the app target per the
public-header-surface review; not a blocker.)

| Target folder | Files |
|---|---|
| `src/shell/` | `editor.cpp`, `editor_view.{h,cpp}` (shrunk), `main_window.cpp`, `menu_bar_button.{h,cpp}`, `menu_look_and_feel.{h,cpp}` (new, from `editor_view.cpp:1082`), `editor_colors.h` |
| `src/timeline/` | `track_viewport.{h,cpp}` (new, from `editor_view.cpp:442` — `TrackViewport` + nested `Content`), `timeline_viewport.{h,cpp}` (new, from `editor_view.cpp:538`; may ride inside `track_viewport.*` if small — decide at extraction), `cursor_overlay.{h,cpp}` (new, from `editor_view.cpp:343`), `timeline_ruler.{h,cpp}`, `timeline_cursor.{h,cpp}`, `arrangement_view.{h,cpp}`, `grid_spacing_selector.{h,cpp}` |
| `src/transport/` | `transport_controls.{h,cpp}` |
| `src/signal_chain/` | `signal_chain_view.{h,cpp}`, `signal_chain_panel.{h,cpp}`, `signal_chain_block_layout.{h,cpp}`, `plugin_browser_window.{h,cpp}` (browser merge) |
| `src/audio_device/` | `audio_device_settings_view.{h,cpp}`, `audio_device_settings_window.{h,cpp}` |
| `src/input_calibration/` | `input_calibration_window.{h,cpp}` |
| `src/busy/` | `busy_overlay.{h,cpp}` |
| `src/shared/` | `audio_level_meter.{h,cpp}`, `text_metrics.{h,cpp}` |

After extraction, `editor_view.{h,cpp}` contains only the shell: window layout, menu wiring,
listener-hub forwarding to `IEditorController`, `setState` fan-out, paint fences. Expected size
roughly half of today's 2,129 lines.

### 3.2 `rock-hero-editor/core/src` — 40 files (Phase 2)

| Target | Files |
|---|---|
| root (the hub) | `editor_controller.cpp` (shrunk: construction/wiring, project+lifecycle sequencing, deferred-action continuation, undo transaction protocol, busy choreography, `deriveViewState` skeleton), `editor_controller_impl.h` (new: single `Impl` declaration + task-state structs, from `editor_controller.cpp:768–1250`), `editor_action.{h,cpp}`, `editor_action_availability.{h,cpp}`, `editor_undo_history.{h,cpp}` (mechanism only after edit split), `deferred_project_action_state.{h,cpp}` |
| `signal_chain/` | `signal_chain_workflow.{h,cpp}`, `signal_chain_block_placement.cpp`, `plugin_catalog_workflow.{h,cpp}` (browser merge), `plugin_display_type.cpp`, `signal_chain_edits.{h,cpp}` (new: `PluginInsertEdit`, `PluginRemoveEdit`, `PluginMoveEdit`, `PluginPlacementEdit`, `PluginDisplayTypeEdit`, `PluginStateEdit`, `OutputGainEdit`†, from `editor_undo_history.h:157–320`), `signal_chain_handlers.cpp` (new: plugin/browser/placement/gain `runAction` overloads + view-state slice projection, from `editor_controller.cpp` ~2870–3230) |
| `busy/` | `busy_operation_state.{h,cpp}`, `busy_operation_workflow.{h,cpp}`, `busy_view_state.cpp` |
| `input_calibration/` | `input_calibration_workflow.{h,cpp}`, `input_calibration_controller.cpp`, `input_calibration_handlers.cpp` (new, if the extracted handlers exceed ~150 lines; otherwise they stay in the hub) |
| `audio_device/` | `audio_device_settings_controller.cpp`, `audio_device_status_text.{h,cpp}` |
| `project/` | `project.cpp`, `project_error.cpp`, `project_io.{h,cpp}`, `rock_song_importer.{h,cpp}`, `song_import_error.cpp`, `project_handlers.cpp` (new: open/restore/import/save/save-as/publish/close/exit handler bodies + their task-state usage) |
| `settings/` | `editor_settings.cpp`, `editor_settings_error.cpp` |
| `timeline/` | `tempo_grid_geometry.cpp`, `timeline_geometry.cpp`, `transport_readout_text.cpp` |
| `tasks/` | `juce_editor_task_runner.cpp`, `juce_message_thread_scheduler.cpp` |

† `OutputGainEdit` placement: recommended `signal_chain/` (the gain strip lives in the
signal-chain rig UI). Alternative: hub root beside the timeline mechanism. Confirm at Phase 2.

Transport handlers (`onPlayPausePressed`/`onStopPressed`/seek/grid) are small; they stay in the
hub unless they exceed the same ~150-line threshold during extraction.

### 3.3 `rock-hero-editor/core/include` — 36 files (Phase 3, public churn)

| Target folder | Files |
|---|---|
| root (contract) | `i_editor_controller.h`, `editor_controller.h`, `i_editor_view.h`, `editor_view_state.h`, `editor_action_id.h` |
| `timeline/` | `tempo_grid_geometry.h`, `timeline_geometry.h`, `transport_readout_text.h`, `arrangement_view_state.h` (judgment: timeline over project — its consumers are timeline widgets; confirm) |
| `transport/` | `transport_view_state.h` |
| `signal_chain/` | `signal_chain_view_state.h`, `plugin_view_state.h`, `plugin_block_assignment.h`, `plugin_display_type.h`, `signal_chain_block_placement.h`, `plugin_browser_view_state.h` (browser merge), `plugin_candidate_view_state.h` (browser merge) |
| `audio_device/` | `audio_device_settings_controller.h`, `i_audio_device_settings_controller.h`, `i_audio_device_settings_view.h`, `audio_device_settings_view_state.h` |
| `input_calibration/` | `input_calibration_controller.h`, `i_input_calibration_view.h`, `input_calibration_view_state.h` |
| `busy/` | `busy_view_state.h` |
| `project/` | `project.h`, `project_error.h`, `i_song_importer.h`, `song_import_error.h` |
| `settings/` | `editor_settings.h`, `i_editor_settings.h`, `editor_settings_error.h` |
| `tasks/` | `i_editor_task_runner.h`, `juce_editor_task_runner.h`, `i_message_thread_scheduler.h`, `juce_message_thread_scheduler.h` |

### 3.4 `rock-hero-common/audio` — 47 files (Phase 4)

`src/` follows the existing Part A plan in
`docs/todo/remaining-god-object-decomposition-plan.md`: `engine_impl.h` (new), per-port TUs
(`engine_transport.cpp`, `engine_song_audio.cpp`, `engine_plugin_host.cpp`, `engine_live_rig.cpp`,
`engine_live_input.cpp`, `engine_device_configuration.cpp`, … per that plan's manifest), and the
~1,350 lines of anonymous-namespace helpers lifted into named units (plugin-scan, tone-document,
path utils). `src/tracktion/` is created **only after** the split, for the Tracktion adapter
helpers that already exist as units: `tracktion_thumbnail.{h,cpp}`,
`tracktion_instrument_wave_device_mapping.{h,cpp}`, `live_rig_gain_plugin.{h,cpp}`,
`monitoring_mode_transition.{h,cpp}`, `plugin_move_index.h`, plus the new per-port TUs if they
prove Tracktion-heavy. Error `.cpp`s and value impls (`audio_device_settings.cpp`,
`input_calibration.cpp`, `audio_normalization.cpp`) move with their feature groups below.

`include/` grouping (second public-churn commit):

| Target folder | Files |
|---|---|
| root (facade + mechanisms) | `engine.h`, `gain.h`, `scoped_listener.h`, `audio_normalization.h` |
| `device/` | `i_audio_device_configuration.h`, `audio_device_settings.h`, `audio_device_status.h`, `audio_device_configuration_error.h` |
| `transport/` | `i_transport.h`, `transport_state.h` |
| `song/` | `i_song_audio.h`, `song_audio_error.h`, `i_thumbnail.h`, `i_thumbnail_factory.h` |
| `plugin/` | `i_plugin_host.h`, `plugin_host_error.h`, `plugin_chain_snapshot.h`, `plugin_instance_state.h`, `plugin_chain_limits.h`, `plugin_catalog_scan_progress.h` |
| `live_rig/` | `i_live_rig.h`, `live_rig_error.h` |
| `input/` | `i_live_input.h`, `live_input_error.h`, `input_calibration.h`, `input_calibration_state.h`, `input_device_identity.h`, `i_audio_meter_source.h`, `audio_meter_snapshot.h` |

Judgment (confirm at Phase 4): calibration types under `input/` rather than a separate
`calibration/` — keeps the live-input port beside its calibration math. `audio_normalization.h`
at root because both `song` loading and `common/core` semantics touch it; alternative `song/`.

### 3.5 Tests — 5 targets, stay flat

Test directories keep their flat layout; `test_<feature>_*.cpp` naming already provides the
feature axis and keeps CTest registration trivial. Only include paths change (Phases 3–4).
`testing/` fake headers keep their folder+namespace. The controller harness
(`editor_controller_test_harness.h`) and per-feature controller test files
(`test_editor_controller_{busy,plugins,project_lifecycle,…}.cpp`) already mirror the handler-TU
split this plan performs on the production side — no renames needed.

### 3.6 Multi-type files — resolved or accepted

| File | Types | Disposition |
|---|---|---|
| `editor_view.cpp` | `CursorOverlay`, `TrackViewport`, `Content`, `TimelineViewport`, `MenuLookAndFeel` | extracted (Phase 1) |
| `editor_undo_history.h` | `IEdit` + 7 concrete edits + history + result types | concrete edits extracted to `signal_chain_edits.h`; mechanism stays (Phase 2) |
| `editor_controller.cpp` | `Impl` + 6 nested task-state structs + 1 helper struct | `Impl` + task states → `editor_controller_impl.h`; members distribute (Phase 2) |
| `editor_view_state.h` | `EditorViewState` + 2 decision enums + 4 prompt structs | accepted — the prompts/decisions are the aggregate's own vocabulary (root contract) |
| `editor_settings.cpp` | 5 private codec structs | accepted — private serialization detail of one unit |
| `tempo_grid_geometry.cpp` | private `MeasureGridWalker` | accepted — private walker of one unit |
| `busy_operation_workflow.cpp` | private `PaintGate` | accepted — private detail of one unit |
| `engine.cpp` | `Engine::Impl` + ~1,350 lines of helpers | Part A split (Phase 4) |

Free-function (projection) modules, for the record: `tempo_grid_geometry`, `timeline_geometry`,
`transport_readout_text`, `audio_device_status_text`, `editor_action_availability`,
`signal_chain_block_layout`, `text_metrics`, `audio_normalization`, `monitoring_mode_transition`,
`plugin_move_index`. All land with their features above; the vocabulary for them is §2.2.

---

## 4. Phases

Every phase: batch the edits, then one verification pass. **Acceptance gate:** builds are run by
the user, not the agent (agent-run builds break CLion include paths), so a phase is *not complete*
— and the next phase does not start — until the user reports all four commands green:
`cmake --build --preset debug`, `ctest --preset debug`,
`cmake --build build/debug --target clang-tidy`, and `pre-commit run --all-files`. Pure-move
commits never mix with extraction commits; public-churn commits never mix with either. Each phase
leaves the repo strictly better and is a safe stopping point.

### Phase 0 — Rules on paper *(executed 2026-07-04; amended after review)*

Edit `docs/design/architectural-principles.md` (two-axis rule §2.1, shared/ admission — the
multi-TU rule §2.4 stays out of durable docs until the Phase 2 pilot proves it),
`docs/design/coding-conventions.md` (two-tier controller rule §2.2, role subsets, projection
modules, view-suffix policy §2.5), `docs/design/architecture.md` (folder convention note;
key-file list is updated later, in the phase that moves each file). Update
`docs/todo/editor-structure-deferred-work.md`: the `common/audio` grouping items move into this
plan; mark superseded entries. No code.

### Phase 1 — `editor/ui`: extraction + folders *(zero public churn)*

1. Extract `MenuLookAndFeel`, `CursorOverlay`, `TrackViewport`(+`Content`), `TimelineViewport`
   from `editor_view.cpp` into the files listed in §3.1 (behavior-preserving; guarded by
   `test_editor_view_*` suites). One commit per extracted cluster.
2. Create the eight folders; move all 37 src files; update `target_sources` paths and relative
   includes. One pure-move commit.
3. Decide opportunistically: `main_window.h` → app target.

Do this **before** tablature/note-editing work begins — it lands in exactly these files.

### Phase 2 — `editor/core/src`: folders + hub distribution + browser merge *(zero public churn)*

1. Pure moves into the folders of §3.2 (including `plugin_catalog_workflow` → `signal_chain/`).
   One commit.
2. Split `editor_undo_history.h`: concrete edits → `signal_chain/signal_chain_edits.{h,cpp}`.
   One commit.
3. Create `editor_controller_impl.h` (single `Impl` declaration + task-state structs). One
   commit; `editor_controller.cpp` merely loses its inline declarations.
4. **Pilot slice with a checkpoint.** This is the riskiest step in the plan: `Impl`'s private
   state surface (~4,807-line TU today, methods interleaved across project lifecycle, undo,
   signal chain, busy, settings, calibration) becomes visible to multiple TUs. Extract exactly
   one small handler TU first — input calibration — then stop and evaluate before continuing:
   - `editor_controller_impl.h` remained a declaration surface: only moved declarations, no new
     shared helpers, no logic, no state added to make the split work;
   - the pilot TU compiles in isolation and only that feature's controller tests were touched;
   - anonymous-namespace helpers needed by the pilot either moved cleanly with it or were
     promoted to one named private header without dragging unrelated code.
   **Abort criteria:** if the impl header starts accumulating shared helpers/dumping-ground
   content, or a slice cannot be cut without promoting large tangles of cross-feature helpers,
   stop — keep the folders and the already-extracted units, leave the remaining hub monolithic,
   and record the finding here (this also invalidates codifying §2.4 in Phase 2's close-out).
5. If the checkpoint passes, distribute the remaining handler + projection member definitions
   **one feature at a time**, each its own commit verified by that feature's controller test
   file: signal-chain (largest, includes browser intents), project lifecycle, transport
   (threshold rule). The hub keeps what §3.2 lists for root.
6. **Close-out:** with the pilot proven and the distribution green, codify §2.4 into
   `docs/design/architectural-principles.md` (the staged Phase 0 remainder).

### Phase 3 — `editor/core/include` feature folders *(public churn commit #1)*

Move per §3.3; update every `#include` repo-wide (`rg` + scripted replace), Doxygen `\file`
paths, and the `architecture.md` key-file list. One dedicated, behavior-free commit.
Confirm first: `arrangement_view_state.h` → `timeline/`.

### Phase 4 — `common/audio`: engine Part A + include grouping

1. Execute `docs/todo/remaining-god-object-decomposition-plan.md` Part A (impl header, per-port
   TUs, helper lifts), one seam per commit, guarded by `test_engine.cpp` / `test_plugin_host.cpp`
   / `test_transport.cpp` and the editor suites.
2. Create `src/tracktion/` and move the adapter units (§3.4).
3. `include/` grouping per §3.4 — public churn commit #2, same mechanics as Phase 3.
   Confirm first: `input/` vs `calibration/`; `audio_normalization.h` home.

### Phase 5 — `common/core` *(go/no-go decision)*

Only if, after Phases 1–4, the 21-file flat `include/` still reads as noise. Candidate grouping:
`domain/` (song, arrangement, tempo_map, timeline, fraction, difficulty, audio_asset,
audio_normalization, session), `package/` (rock_song_package, song_package_error, archive_io,
archive_error, package_id, workspace_paths), `infra/` (logger, logger_error, json, juce_path,
cancellation_token, application_identity). A `rock_song_package.cpp` (1,687 lines) seam split may
ride along if package work is touched anyway; otherwise it stays an accepted exception.

### Phase 6 — Close out

Move `project-structure-analysis.md` and this plan to `docs/completed/`; final
`architecture.md` key-file sweep; confirm every §1 criterion; record the outcome and the §6.3
break-even signals in a durable place.

---

## 5. Risks and Guards

- **Hidden helper coupling during TU distribution.** An anonymous-namespace helper used by two
  features must be promoted to a named private header, never duplicated. Detect at extraction:
  compile each new TU in isolation.
- **Include-path churn breaking CLion/IDE state.** Public-move commits are single, atomic, and
  announced so the user can re-sync the IDE once per phase.
- **Behavior drift disguised as movement.** Extraction commits may not touch logic; any
  discovered bug is fixed in a separate commit before or after, never inside a move.
- **Scope creep.** Renames, API changes, and Part B (ISP sub-interfaces) are out of scope; if a
  smell demands action mid-phase, it gets its own note and waits.
- **State growth after pressure relief.** The multi-TU split removes file-size pressure on the
  hub; the guard is the break-even watch list (§6.3), checked whenever a new feature lands.

## 6. Decisions

### 6.1 Resolved by this plan (sign-off = approving Phase 0)

1. Feature folders without namespaces, per §2.1.
2. Browser folded into signal chain.
3. Controller non-goal narrowed per §2.4 (object whole, TUs distributed) — approved as direction;
   §2.4 itself stays provisional until the Phase 2 pilot, per the revision note.
4. Tests stay flat.
5. Root files are the cross-feature contract, listed explicitly in §3.

### 6.2 Confirm at the named phase (small, flagged inline)

`main_window.h` → app target (P1) · `TimelineViewport` own files vs riding (P1) ·
`OutputGainEdit` home (P2) · calibration/transport handler-TU threshold (P2) ·
`arrangement_view_state.h` → `timeline/` (P3) · `input/` vs `calibration/`, `audio_normalization.h`
home (P4) · `common/core` go/no-go (P5).

### 6.3 Break-even signals (would reopen the module question — watch, don't act)

`ActionConditions` past ~25 booleans; `EditorEditContext` past ~6 domains; a second unmergeable
pairwise feature conversation; features becoming optional/third-party; hub still growing after
the TU split.
