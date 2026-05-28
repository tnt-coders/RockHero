# Readability Taxonomy Evaluation Plan

Status: in-progress planning.

## Purpose

Make the codebase easier to scan by making each type's architectural role visible from its name,
folder, and surrounding module. The goal is not cosmetic tidiness. The goal is that a reader can
open a folder and quickly understand which files are ports, adapters, controllers, views,
view-state models, workflows, settings stores, task runners, value objects, or operation DTOs.

This plan evaluates whether readability should be improved through:

- stronger naming conventions,
- role-based subfolders,
- selective public include reorganization,
- private implementation folder grouping,
- or, only where justified, additional namespaces.

This document is not itself a durable design change. If the resulting taxonomy is accepted and
implemented, the durable rules should be copied into `docs/design/coding-conventions.md` and
possibly `docs/design/architecture.md` after confirmation.

## Problem

The existing module structure is architecturally sound, but the flat header/source layout makes
roles harder to see at a glance. For example, in one module a reader may see interfaces, view
state, controllers, errors, settings, DTOs, adapters, and private helpers in the same folder.

The current names are mostly defensible in isolation, but visual grouping is weak:

- `ITransport`, `IPluginHost`, and `ILiveRig` are ports, but sit next to values and adapters.
- `EditorViewState`, `SignalChainViewState`, and `PluginBrowserViewState` are view-state types,
  but sit next to controllers, settings, errors, and ports.
- `EditorController`, `AudioDeviceSettingsController`, and controller interfaces are not grouped.
- UI implementation files mix shell, windows, panels, controls, views, overlays, and shared widgets.
- Private Tracktion adapter helpers are partly split, but `engine.cpp` still hides many roles in
  one file.

For maintainability, the tree should help a human answer these questions quickly:

1. Is this a public boundary or private implementation?
2. Is this pure/headless, audio-adapter, UI presentation, or app composition?
3. Is this type a port, adapter, controller, view, view state, workflow, value, or DTO?
4. Does this name represent what the type does in this project, not just what it contains today?
5. Is this folder grouping durable, or will it become another taxonomy to explain?

## Desired Role Vocabulary

Use this vocabulary as the initial evaluation baseline.

| Role | Naming shape | Folder hint | Notes |
| --- | --- | --- | --- |
| Domain value or aggregate | Unsuffixed noun | `domain/` only if needed | Examples: `Song`, `Arrangement`, `Gain`. |
| Port | `I<Capability>` | `ports/` | Do not add a `Port` postfix to each interface. |
| Port bundle | `<Owner>::<Thing>Ports` | owning type | Good for construction dependency bundles. |
| Adapter | `<Technology><Thing>` or established facade | `adapters/`, `tracktion/` | Keep public `Engine` for now; use `Tracktion*` privately. |
| Controller | `<Feature>Controller` | `controllers/` | Owns workflow policy and backend calls. |
| Controller interface | `I<Feature>Controller` | `ports/` or `controllers/` | Decide by whether callers use it as a boundary port. |
| View state | `<Feature>ViewState` | `view_state/` | JUCE-free render snapshot. |
| View | `<Feature>View` | `views/` | JUCE component for a conceptual surface. |
| Panel | `<Feature>Panel` | `panels/` or feature folder | Persistent bounded editor region. |
| Controls | `<Feature>Controls` | `controls/` or feature folder | Compact command cluster. |
| Window | `<Feature>Window` | `windows/` | Top-level/modeless JUCE host. |
| Dialog | `<Feature>Dialog` | `dialogs/` or feature folder | Modal/transactional apply-cancel workflow host. |
| Overlay | `<Feature>Overlay` | `overlays/` or `shared/` | Transient component over existing content. |
| Workflow/session | `<Feature>Workflow` or `<Feature>Session` | `workflows/` | Headless use-case state/policy, not a UI component. |
| Settings store | `<Feature>Settings` | `settings/` | Durable app/user settings only. |
| Task runner | `<Feature>TaskRunner` | `tasks/` | Async execution abstraction or concrete runner. |
| Importer/factory | `<Format>Importer`, `<Thing>Factory` | `import/`, `factories/` | Current style is acceptable. |
| Snapshot/request/result/progress | `<Thing>Snapshot`, etc. | near owning API | Operation DTOs, not behavioral services. |
| Error | `<Subject>ErrorCode`, `<Subject>Error` | `errors/` if many | Keep near the API that returns the error. |

## Namespace Policy

Do not use namespaces as the first readability tool. The project is already nested by product and
module, such as `rock_hero::editor::core` and `rock_hero::common::audio`. Adding role namespaces
like `rock_hero::editor::core::view_state` would make types noisier without necessarily improving
ownership.

Prefer folders over namespaces when the goal is visual grouping.

Consider a new namespace only when at least one of these is true:

- there is a semantic domain with several cooperating types, not just a role category;
- there is a real collision risk that folder organization does not solve;
- the namespace would appear in code as meaningful language, not just taxonomy;
- the types are normally consumed together as a subdomain.

Avoid namespaces for generic roles:

- no `ports` namespace just because a file is in `ports/`;
- no `view_state` namespace just because a file is in `view_state/`;
- no `controllers` namespace just because a file is in `controllers/`;
- no `views` namespace just because a file is in `views/`.

Private implementation helpers can use existing module namespaces and anonymous namespaces. If a
future private subdomain becomes large enough, prefer a private folder and `detail` only when the
types must appear in headers visible to multiple `.cpp` files.

## Evaluation Method

### 1. Build A Type Inventory

Generate an inventory for production types under:

- `rock-hero-common/core`,
- `rock-hero-common/audio`,
- `rock-hero-common/ui`,
- `rock-hero-editor/core`,
- `rock-hero-editor/audio`,
- `rock-hero-editor/ui`,
- `rock-hero-game/core`,
- `rock-hero-game/audio`,
- `rock-hero-game/ui`,
- `rock-hero-*/app` only for composition-shell types.

For each class, struct, enum, and public alias, record:

- type name,
- current file path,
- public or private,
- owning module,
- apparent role,
- actual role after reading use sites,
- whether the name reveals the role,
- whether the folder reveals the role,
- whether the namespace adds useful information,
- proposed action: keep, rename, move folder, split file, merge, or defer.

### 2. Score Readability Friction

Use a simple 0-3 scale:

| Score | Meaning | Action |
| --- | --- | --- |
| 0 | Role is obvious from name and location | Keep. |
| 1 | Understandable after opening the file | Consider comments or local grouping. |
| 2 | Requires tracing use sites to know the role | Rename or move to a role folder. |
| 3 | Name, role, and folder actively conflict | Prioritize cleanup. |

Examples of score-2 or score-3 symptoms:

- a type name uses `Settings` but represents a transaction/session;
- a `Window` type is only a launcher and not the actual window;
- a UI component owns workflow policy that belongs in core;
- a public header mixes unrelated externally meaningful types;
- a port is discovered only through concrete type behavior;
- a private helper has a generic name but wraps a specific backend concept.

### 3. Decide The Smallest Useful Fix

Apply this decision order:

1. Rename only when the current name misstates the role.
2. Move to a role folder when the name is fine but the folder hides the role.
3. Split a file when unrelated public or private roles are mixed together.
4. Add or change a namespace only when folders and names do not solve the problem.
5. Defer when churn is larger than the readability gain.

Do not combine large folder moves with behavioral refactors unless the behavior change is the
reason the file boundary exists.

## Candidate Folder Taxonomy

These are candidates to evaluate, not pre-approved moves.

### `rock-hero-common/audio/include/rock_hero/common/audio`

Potential public grouping:

```text
ports/
  i_transport.h
  i_song_audio.h
  i_edit.h
  i_plugin_host.h
  i_live_rig.h
  i_live_input.h
  i_audio_meter_source.h
  i_audio_device_configuration.h
  i_audio_device_settings.h
  i_thumbnail.h
  i_thumbnail_factory.h

values/
  gain.h
  input_device_identity.h
  audio_device_status.h
  audio_meter_snapshot.h
  transport_state.h
  input_calibration_state.h

workflows/
  audio_device_settings.h
  input_calibration.h

errors/
  plugin_host_error.h
  live_rig_error.h
  live_input_error.h
  audio_normalization.h

engine.h
```

Questions to answer during evaluation:

- `IAudio` has already been renamed to `ISongAudio`; folder cleanup should preserve that role name.
- Should `audio_normalization.h` stay as an operation API rather than move to `errors/`?
- Should `Engine` remain at the public audio root as the composition-facing facade?
- Should `AudioDeviceSettings` move to `workflows/` even though it also exposes a public port?

### `rock-hero-common/audio/src`

Potential private grouping:

```text
tracktion/
  tracktion_audio_engine_impl.*
  tracktion_engine_behaviour.*
  tracktion_plugin_catalog.*
  tracktion_plugin_window.*
  tracktion_live_input_routing.*
  tracktion_live_rig_document.*
  tracktion_live_rig_gain_plugin.*
  tracktion_meter_reader.*
  tracktion_thumbnail.*
  tracktion_instrument_wave_device_mapping.*

analysis/
  audio_normalization.*

workflows/
  audio_device_settings.*
  input_calibration.*
```

This should happen only as private implementation seams are extracted. Moving `engine.cpp` as one
giant file into `tracktion/` would not make it much easier to understand.

### `rock-hero-editor/core/include/rock_hero/editor/core`

Potential public grouping:

```text
controllers/
  editor_controller.h
  audio_device_settings_controller.h
  i_editor_controller.h
  i_audio_device_settings_controller.h

ports/
  i_editor_view.h
  i_audio_device_settings_view.h
  i_editor_task_runner.h
  i_song_importer.h

view_state/
  editor_view_state.h
  arrangement_view_state.h
  transport_view_state.h
  signal_chain_view_state.h
  plugin_view_state.h
  plugin_candidate_view_state.h
  plugin_browser_view_state.h
  audio_device_settings_view_state.h
  busy_view_state.h

project/
  project.h
  project_error.h

import/
  song_import_error.h

settings/
  editor_settings.h

tasks/
  juce_editor_task_runner.h

commands/
  editor_action_id.h
```

Questions to answer during evaluation:

- Are controller interfaces clearer under `controllers/` or `ports/`?
- Is `IEditorView` a UI port owned by core, or should it live near root view-state contracts?
- Should `JuceEditorTaskRunner` stay public under `tasks/` despite being JUCE-backed?
- Should prompt structs remain inside `editor_view_state.h`, or split once input calibration
  gains its own workflow state?

### `rock-hero-editor/core/src`

Potential private grouping:

```text
controllers/
  editor_controller.cpp
  audio_device_settings_controller.cpp

project/
  project.*
  project_io.*
  project_error.*

import/
  rock_song_importer.*
  psarc_song_importer.*
  song_import_error.*

tasks/
  juce_editor_task_runner.*
  inline_editor_task_runner.*

view_state/
  busy_view_state.*
  audio_device_status_text.*

commands/
  editor_action.*
```

If `EditorController::Impl` is later split, use feature folders inside `controllers/` or
`editor_controller/` rather than leaving unrelated workflow files at the root.

### `rock-hero-editor/ui/include/rock_hero/editor/ui`

Current public UI headers are sparse. Evaluate before moving. Possible grouping if public UI grows:

```text
shell/
  editor.h
  main_window.h
```

Do not create public `views/` or `windows/` folders until those types are actually public.

### `rock-hero-editor/ui/src`

Potential private grouping:

```text
shell/
  editor.*
  editor_view.*
  main_window.*
  menu_bar_button.*
  busy_overlay.*

timeline/
  arrangement_view.*
  cursor_overlay.*
  track_viewport.*

transport/
  transport_controls.*

signal_chain/
  signal_chain_panel.*
  plugin_row_view.*

plugin_browser/
  plugin_browser_window.*

audio_devices/
  audio_device_settings_view.*
  audio_device_settings_window.*

input_calibration/
  input_calibration_window.*
  future input_calibration_view.*

shared/
  audio_level_meter.*
```

This grouping is likely worthwhile because UI roles are visually distinct and the UI source folder
already mixes shell, windows, views, controls, panels, and shared widgets.

## Naming Decisions To Evaluate

Evaluate these names explicitly before any broad folder move:

| Current name | Issue | Candidate action |
| --- | --- | --- |
| `ISongAudio` | Song preparation and active arrangement playback port | Keep. |
| `Engine` | Generic, but established as public audio facade | Keep for now. |
| `AudioDeviceSettings` | Represents staged settings workflow/session | Keep or rename only when touched. |
| `AudioDeviceSettingsWindow` | Public private-header type is launcher, not actual window | Rename when touched. |
| `InputCalibrationPrompt` | Prompt name no longer captures workflow state | Replace during calibration refactor. |
| `PluginHandle` | Overlaps loaded-chain entry concepts | Fold into `PluginChainEntry`. |
| `LiveRigPlugin` | Overlaps loaded-chain entry concepts | Fold into `PluginChainEntry`. |
| `PluginViewState` | Presentation DTO, okay, but source should be unified | Keep name. |
| `IEdit` | Empty future placeholder | Keep documented or remove until behavior exists. |
| `JuceEditorTaskRunner` | Concrete behavior may be message-thread marshalling | Keep, document behavior. |

Do not rename:

- `ITransport`,
- `IPluginHost`,
- `ILiveRig`,
- `ILiveInput`,
- `TransportControls`,
- `SignalChainPanel`,
- `ArrangementView`,
- `BusyOverlay`,
- `PluginBrowserWindow`,
- `EditorViewState` and other established `*ViewState` types.

## Implementation Phases

### Phase 1: Inventory And Taxonomy Confirmation

1. Generate the production type inventory.
2. Classify every type using the role vocabulary.
3. Score readability friction.
4. Produce a short findings table with proposed action per type.
5. Confirm the role vocabulary before changing public include paths.

Deliverable: an updated review document or companion table in `docs/in-progress/`.

### Phase 2: Low-Risk Naming Fixes

Apply names that have high readability value and low design risk.

Candidate work:

- preserve the completed `ISongAudio` rename;
- rename `AudioDeviceSettingsWindow` only if touching that area;
- avoid `Port` postfixes on individual interfaces;
- reserve `Ports` for bundles such as `Editor::AudioPorts`.

Verification:

- full build for affected targets;
- affected unit tests;
- `rg` check for stale names and include paths.

### Phase 3: Public Header Role Folders

Move public headers only after the taxonomy is accepted.

Preferred order:

1. `common/audio/include/.../ports/` for `I*` audio ports.
2. `editor/core/include/.../view_state/` for editor render-state DTOs.
3. `editor/core/include/.../controllers/` for controller contracts and implementations.
4. Other groups only if they clearly improve scanning.

Rules:

- move one module at a time;
- update includes mechanically;
- keep namespaces unchanged unless separately justified;
- avoid mixing header moves with behavior changes;
- update Doxygen `\file` comments when file names or paths change.

### Phase 4: Private Source Folders

Group private implementation files after or alongside real extraction work.

Recommended first private moves:

1. `editor/ui/src` role folders, because the visual UI taxonomy is immediately useful.
2. `editor/core/src` project/import/tasks/controller grouping.
3. `common/audio/src/tracktion` only as Tracktion helpers are extracted from `engine.cpp`.

Do not move a monolithic file into a role folder and call the design cleaner. The folder should
correspond to a real ownership boundary.

### Phase 5: Namespace Audit

After names and folders are improved, audit whether any namespace changes still add value.

Expected default: no new role namespaces.

Possible exceptions:

- a future scoring domain under `rock_hero::game::core` may justify a semantic namespace;
- a future tone graph domain may justify a semantic namespace;
- private multi-file implementation details may justify `detail` in private headers.

### Phase 6: Durable Documentation

Once implemented and validated, update design docs with the accepted taxonomy:

- `docs/design/coding-conventions.md` for naming and role suffix rules;
- `docs/design/architecture.md` for durable folder conventions;
- `docs/design/architectural-principles.md` only if the change affects architectural boundaries.

Do not update `docs/design/` until the user confirms the taxonomy is the intended durable design.

## Verification Checklist

For each implementation slice:

- `rg` finds no stale include path or type name references.
- CMake target source lists include moved files in the new paths.
- Public headers retain valid Doxygen file comments.
- Tests for affected modules build and pass.
- No public header includes concrete `Engine` only for convenience.
- Namespaces remain consistent with product/module ownership.
- The resulting tree is easier to scan than the previous one.

## Exit Criteria

The cleanup is successful when:

- every production type has a documented role classification;
- high-friction role/name mismatches are either fixed or explicitly deferred;
- public ports are visually distinguishable without `Port` postfix noise;
- view-state headers are visually distinguishable from controllers and workflow types;
- UI private source files are grouped by shell, feature, or reusable widget role;
- private Tracktion adapter files are grouped by backend ownership as they are extracted;
- no additional role namespace exists unless it represents a real semantic domain;
- design docs contain the accepted taxonomy after implementation.

## Non-Goals

- Do not rename every type with a role suffix.
- Do not add `Port` to individual port interface names.
- Do not introduce role namespaces as a substitute for folders.
- Do not move public headers before confirming the include-path churn is worthwhile.
- Do not change behavior while performing pure taxonomy moves.
- Do not split `Engine` or `EditorController` just to satisfy folder shape.
- Do not force every window or UI component into one controller pattern.
