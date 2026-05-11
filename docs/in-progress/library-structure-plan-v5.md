# Library Structure Plan (v5)

Status: proposed. Supersedes `library-structure-plan.md`,
`library-structure-revised-plan.md`, `library-structure-plan-v3.md`, and
`library-structure-plan-v4.md`.

Do not implement until this plan is accepted as the durable direction. Once accepted, update
`docs/design/architecture.md` and `docs/design/architectural-principles.md` before or alongside
the implementation work.

## Why This Replaces v4

v4 fixed the library boundary model, but it still preserved the `apps/` and `libs/` split. That
left product code in two places:

- executable entry points under `apps/`
- product libraries under `libs/rock-hero-editor/` and `libs/rock-hero-game/`

That split is now more confusing than helpful. The real top-level ownership axis is product
scope:

- shared Rock Hero code
- Rock Hero Editor code
- Rock Hero Game code

v5 makes that axis visible at the repository root and keeps executable startup under an `app/`
subfolder inside each product.

## Problem

Rock Hero currently has three broad libraries under `libs/`:

- `rock-hero-core`
- `rock-hero-audio`
- `rock-hero-ui`

It also has executable areas under `apps/`:

- `apps/rock-hero-editor`
- `apps/rock-hero`

That structure splits by build role first, then product scope second. It worked while the project
was tiny, but it now hides ownership:

- `rock-hero-ui` is editor UI today, not shared UI.
- editor workflow code needs a headless home outside JUCE widgets.
- future game code needs a game-owned home outside common modules.
- the editor app-shell library under `apps/rock-hero-editor` is testable product code, not just
  process startup.

The structure should answer three questions for every file:

1. Which product scope owns this code? Common, editor, or game?
2. Which technical layer does it belong to? Core, audio, UI, or app startup?
3. Is it headless/testable logic or framework-bound adapter/presentation code?

## Goals

- Make product scope visible at the repository root.
- Keep the `core`, `audio`, and `ui` vocabulary under each product scope.
- Keep executable entry points close to their product code but isolated under `app/`.
- Keep app entry points thin.
- Keep headless code testable without GUI, Tracktion, or bgfx runtime dependencies where practical.
- Keep Tracktion hidden behind project-owned audio APIs.
- Avoid broad library-to-library umbrella dependencies.
- Provide strong placement guidance through folders, targets, namespaces, and README files.
- Preserve the user-facing executable names `rock-hero-editor` and `rock-hero`.

## Proposed Top-Level Layout

Eliminate the final `apps/` and `libs/` split. Use top-level product-scope folders:

```text
rock-hero-common/
  CMakeLists.txt
  core/
  audio/
  ui/

rock-hero-editor/
  CMakeLists.txt
  app/
    main.cpp
    resources/
  core/
  audio/
  ui/

rock-hero-game/
  CMakeLists.txt
  app/
    main.cpp
    resources/
  core/
  audio/
  ui/
```

`rock-hero-common/` has no `app/` folder because it is not a product executable.

Every `core`, `audio`, and `ui` cell is created up front as a real target. Empty cells are
dependency-free stubs with a `README.md` that states what belongs there, what must not go there,
allowed dependency directions, and what kind of first real code would justify filling the cell.

## Module Meanings

### `rock-hero-common/core`

Shared, framework-free domain and data behavior used by both products.

Likely responsibilities:

- `Song`, `Arrangement`, `AudioAsset`, timeline value types
- difficulty and validation rules
- shared package and domain primitives
- pure timing and conversion logic used by both products
- pure model code and fixtures that should be runnable without frameworks

This module must not depend on JUCE, Tracktion, SDL, bgfx, UI loops, audio devices, plugin runtime
state, app settings, or product-specific workflow.

### `rock-hero-common/audio`

Shared audio APIs and the default shared audio implementation.

This layer contains an internal two-target split:

```text
rock_hero::common::audio::api
    Audio ports and shared audio-facing contracts.

rock_hero::common::audio::impl
    Current concrete Tracktion/JUCE-backed implementation.

rock_hero::common::audio
    INTERFACE umbrella over api + impl for app-level composition.
```

The `api` target owns ports such as:

- `ITransport`
- `IAudio`
- `IEdit`
- `IThumbnail`
- `IThumbnailFactory`
- `TransportState`
- listener helpers such as `ScopedListener`

The `impl` target owns:

- `Engine`
- Tracktion-backed thumbnail implementation
- Tracktion/JUCE adapter implementation files

Normal libraries and most tests link `rock_hero::common::audio::api`. App executables and concrete
adapter tests may link `rock_hero::common::audio`, which brings in the default implementation.
Normal library code should not link `rock_hero::common::audio::impl` directly.

Tracktion headers remain private to implementation files and private implementation headers.
Public headers expose only project-owned types and carefully chosen forward declarations.

### `rock-hero-common/ui`

Shared UI code used by both products.

This starts as an empty stub. It is only for UI code both applications genuinely consume.

Valid future candidates:

- shared JUCE plugin host windows if both products need them
- shared settings or calibration dialogs if both products need them
- shared look-and-feel code if both products use it

Do not place editor-only JUCE components here. Do not place game-only bgfx rendering here.

### `rock-hero-editor/app`

Editor executable startup and process lifecycle only.

Likely responsibilities:

- `main.cpp`
- JUCE application entry point
- executable target metadata
- app resources that are tied to the shipped editor executable

This folder should not own reusable product behavior. If a class deserves focused tests or stable
dependency rules, it belongs in `rock-hero-editor/core`, `rock-hero-editor/audio`, or
`rock-hero-editor/ui`.

### `rock-hero-editor/core`

Editor-specific headless workflow and policy.

Likely responsibilities:

- editor session workflow
- open, import, save, save-as, publish, close, and exit orchestration
- unsaved-change policy
- selected arrangement workflow
- editor command application
- undo/redo and command history when those exist
- editor state machines tested without JUCE widgets

This module may depend on `common/core` and `common/audio::api`. It must not depend on concrete
JUCE components or `common/audio::impl`.

The current `EditorController` is the seed of this module.

### `rock-hero-editor/audio`

Editor-specific audio behavior that is not part of the shared audio engine.

This starts as an empty stub. Use it only for editor-only audio policy, services, or adapters that
are not just UI drawing and not part of the shared playback engine.

The current Tracktion thumbnail path remains in `common/audio::impl` because it is part of the
engine-backed audio adapter path.

### `rock-hero-editor/ui`

Editor-specific JUCE presentation.

Likely responsibilities:

- concrete editor JUCE components
- editor `DocumentWindow` shell code
- editor menus and toolbar UI
- arrangement and waveform views
- transport controls
- JUCE-backed editor app settings helpers that are not framework-free yet
- presentation-oriented view state consumed by concrete widgets
- gesture handling and intent emission

This module renders already-derived state and emits intents. It does not own save policy,
undo/redo policy, project workflow, or audio backend decisions.

The current editor app-shell library code moves here initially. That includes `main_window.*`,
`editor_settings.*`, and the settings tests. If `EditorSettings` later becomes framework-free or
broader editor workflow policy, move it to `editor/core` in a focused change.

### `rock-hero-game/app`

Game executable startup and process lifecycle only.

Likely responsibilities:

- `main.cpp`
- executable target metadata
- app resources tied to the shipped game executable, such as desktop files and icons
- temporary startup shell code until `game/ui` owns the real gameplay surface

The shipped executable output remains `rock-hero`.

When game rendering exists, reusable window/rendering behavior should move into `game/ui`, leaving
`app/main.cpp` as startup only.

### `rock-hero-game/core`

Game-specific pure gameplay behavior.

Likely responsibilities:

- note matching
- scoring, combo, and streak rules
- calibration math
- gameplay simulation and state machines
- interpretation of explicit pitch/onset observations

This module receives data and returns decisions. It should not depend on `game/audio`; the
dependency direction is `game/audio` producing observations for `game/core`, not the reverse.

### `rock-hero-game/audio`

Game-specific audio analysis and audio-adjacent gameplay plumbing.

Likely responsibilities:

- pitch detection
- onset detection
- input buffer adaptation for analysis
- windowing, framing, FFT helpers, and DSP utilities
- calibration capture plumbing
- gameplay-safe plugin-chain policy if it differs from editor practice mode

This module may depend on `common/core`, `common/audio::api`, and `game/core` as needed. It does
not own scoring rules.

### `rock-hero-game/ui`

Game-specific bgfx/SDL presentation and rendering.

Likely responsibilities:

- bgfx surface integration
- note highway rendering
- game HUD
- visual feedback
- gameplay-surface input handling

This module does not own scoring rules, audio timing policy, or JUCE-bound configuration UI.

## Targets, Namespaces, Include Paths

Each library submodule has a target, namespace, and include path that mirrors product scope and
layer.

- `common/core`: target `rock_hero_common_core`, namespace `rock_hero::common::core`,
  include path `<rock_hero/common/core/...>`.
- `common/audio`: target `rock_hero_common_audio`, namespace `rock_hero::common::audio`,
  include path `<rock_hero/common/audio/...>`.
- `common/ui`: target `rock_hero_common_ui`, namespace `rock_hero::common::ui`,
  include path `<rock_hero/common/ui/...>`.
- `editor/core`: target `rock_hero_editor_core`, namespace `rock_hero::editor::core`,
  include path `<rock_hero/editor/core/...>`.
- `editor/audio`: target `rock_hero_editor_audio`, namespace `rock_hero::editor::audio`,
  include path `<rock_hero/editor/audio/...>`.
- `editor/ui`: target `rock_hero_editor_ui`, namespace `rock_hero::editor::ui`,
  include path `<rock_hero/editor/ui/...>`.
- `game/core`: target `rock_hero_game_core`, namespace `rock_hero::game::core`,
  include path `<rock_hero/game/core/...>`.
- `game/audio`: target `rock_hero_game_audio`, namespace `rock_hero::game::audio`,
  include path `<rock_hero/game/audio/...>`.
- `game/ui`: target `rock_hero_game_ui`, namespace `rock_hero::game::ui`,
  include path `<rock_hero/game/ui/...>`.

`common/audio` also has nested targets:

| Target | Purpose |
|---|---|
| `rock_hero::common::audio::api` | Audio ports and contracts |
| `rock_hero::common::audio::impl` | Default concrete Tracktion/JUCE implementation |
| `rock_hero::common::audio` | App-facing umbrella over `api` and `impl` |

Example includes:

```cpp
#include <rock_hero/common/core/song.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/common/audio/engine.h>
#include <rock_hero/editor/core/editor_controller.h>
#include <rock_hero/editor/ui/editor_view.h>
#include <rock_hero/game/core/scoring.h>
#include <rock_hero/game/audio/pitch_detector.h>
#include <rock_hero/game/ui/note_highway_view.h>
```

Namespace aliases are not part of the canonical style. Use the full nested namespace in moved
code.

## Umbrella Targets And Executables

Each product-scope parent folder defines one INTERFACE umbrella target:

```text
rock_hero::common
rock_hero::editor
rock_hero::game
```

Umbrellas aggregate only their own scope:

```text
rock_hero::common -> common/core + common/audio + common/ui
rock_hero::editor -> editor/core + editor/audio + editor/ui
rock_hero::game   -> game/core + game/audio + game/ui
```

App executables live under each product's `app/` folder and may link scope umbrellas:

```cmake
target_link_libraries(rock_hero_editor_exe PRIVATE
    rock_hero::common
    rock_hero::editor)

target_link_libraries(rock_hero_game_exe PRIVATE
    rock_hero::common
    rock_hero::game)
```

The output names remain:

```cmake
set_target_properties(rock_hero_editor_exe PROPERTIES OUTPUT_NAME "rock-hero-editor")
set_target_properties(rock_hero_game_exe PROPERTIES OUTPUT_NAME "rock-hero")
```

Libraries and tests link narrow submodule targets. They should not link broad parent umbrellas
unless the broad dependency is specifically the behavior under test.

## Dependency Rules

Top-level direction:

```text
common -> no editor or game dependencies
editor -> common only
game   -> common only
app    -> common + matching product scope
```

Forbidden directions:

- `common::*` depending on `editor::*` or `game::*`
- `editor::*` depending on `game::*`
- `game::*` depending on `editor::*`
- a `core` submodule depending on a sibling `ui` submodule
- normal library code linking `common/audio::impl` directly
- reusable behavior depending on an `app/` folder

Layer guidance:

```text
common/core       standard C++ and approved pure format dependencies
common/audio::api common/core only, plus minimal forward declarations where needed
common/audio::impl common/audio::api + Tracktion/JUCE audio wrappers
common/ui         common/core + JUCE GUI only when real shared UI code exists

editor/core       common/core + common/audio::api
editor/audio      common/core + common/audio::api, plus editor-only audio dependencies as needed
editor/ui         editor/core + common/audio::api + editor/audio + JUCE GUI
editor/app        common + editor umbrellas only, plus app metadata/resources

game/core         common/core
game/audio        common/core + common/audio::api + game/core + DSP dependencies as needed
game/ui           game/core + game/audio + common/audio::api + bgfx + SDL
game/app          common + game umbrellas only, plus app metadata/resources
```

The specific links should stay narrower than this guidance whenever practical.

## Window Model

This plan includes one related design decision that must be copied into durable design docs when
the plan is accepted:

- The game's primary gameplay window is bgfx/SDL.
- JUCE windows in the game are separate top-level native peer windows.
- There is no JUCE shell embedding the bgfx surface.
- There is no bgfx-in-JUCE component.

Implications:

- `game/ui` owns the bgfx gameplay surface.
- Shared JUCE windows, when they exist, live in `common/ui`.
- The game `app/` target coordinates process-level event-loop and focus concerns across GUI
  systems.

The current game `main.cpp` contains a temporary JUCE shell window. That is acceptable until the
real `game/ui` surface exists. Once `game/ui` owns rendering/window behavior, `game/app/main.cpp`
should become startup only.

## Stub Conventions

All nine top-level library submodules are created up front. Empty submodules follow these rules:

- Use a real target with one placeholder source file so target type remains stable.
- Add no framework dependency until real code requires it.
- Create the final include root and namespace from the start.
- Include a short `README.md` describing allowed content, forbidden content, dependency rules,
  and expected first real use.
- Do not create test targets for empty stubs.

`common/audio` is not an empty stub. Its `api` and `impl` child targets are created as part of the
audio migration.

## Compatibility Policy

The completed structure should not keep permanent compatibility shims.

Temporary aliases or forwarding headers may be used inside a short-lived migration branch or PR if
they are needed to make a phase reviewable. They must not remain after the relevant phase is
complete.

The final structure uses:

```cpp
rock_hero::common::core
rock_hero::common::audio
rock_hero::editor::core
rock_hero::editor::ui
rock_hero::game::core
```

and includes such as:

```cpp
#include <rock_hero/common/core/song.h>
#include <rock_hero/common/audio/engine.h>
#include <rock_hero/editor/ui/editor_view.h>
```

## Migration Plan

### Phase 1: Accept And Document The Structure

Confirm this plan as the durable direction. Update durable design docs to describe:

- top-level product-scope folders
- `app/` folders for executable startup
- nine `core` / `audio` / `ui` library cells
- `common/audio::api` and `common/audio::impl`
- scope umbrellas and their app-level usage
- narrow library/test linking
- window model
- no permanent compatibility shims

### Phase 2: Create Top-Level Product Folders, Umbrellas, And Stubs

Create:

```text
rock-hero-common/
rock-hero-editor/
rock-hero-game/
```

Create all nine library submodule folders and targets:

```text
common/core
common/audio
common/ui
editor/core
editor/audio
editor/ui
game/core
game/audio
game/ui
```

Create `app/` folders under `rock-hero-editor/` and `rock-hero-game/`.

Create README guardrails in every library submodule. Existing code can remain in old locations
during this phase if needed.

Update the root `CMakeLists.txt` to add these product folders once their CMake files exist. Keep
old `apps/` and `libs/` entries until the corresponding move phases remove them.

### Phase 3: Move `rock-hero-core` Into `rock-hero-common/core`

Move the current core library into `rock-hero-common/core/`.

Update:

- folder location
- CMake target name
- namespace
- include paths
- consumers
- tests

This is expected to be a noisy but mostly mechanical phase.

### Phase 4: Move `rock-hero-audio` Into `rock-hero-common/audio`

Move current audio code into `rock-hero-common/audio/`.

Split the target internally:

- public port and contract headers into `common/audio::api`
- `Engine`, Tracktion thumbnail implementation, and adapter `.cpp` files into
  `common/audio::impl`
- `common/audio` as an INTERFACE umbrella over both

Update consumers:

- library code that only needs ports links `common/audio::api`
- apps and concrete adapter tests link `common/audio`

### Phase 5: Move `rock-hero-ui` Into `rock-hero-editor/ui`

Move concrete editor JUCE presentation code into `rock-hero-editor/ui/`.

This includes:

- concrete editor view
- arrangement view
- transport controls
- editor UI resources
- JUCE widget tests

Do not move headless editor workflow here if it can be separated in Phase 6.

### Phase 6: Extract `editor/core` From `editor/ui`

Move headless editor workflow into `rock-hero-editor/core/`.

The current `EditorController` is the main candidate. Move supporting framework-free contracts and
state with it when they are part of the workflow boundary.

`editor/ui` should depend on `editor/core`, not the reverse. `editor/core` may depend on
`common/audio::api` for transport and audio ports.

### Phase 7: Move Editor App-Shell Code Into `rock-hero-editor/ui`

Move the current static library code from `apps/rock-hero-editor/` into `rock-hero-editor/ui/`.

Initial placement:

- `main_window.*` moves to `editor/ui` because it is a JUCE `DocumentWindow` shell that composes
  concrete editor UI and audio dependencies.
- `editor_settings.*` moves to `editor/ui` because it is currently JUCE-backed app settings
  infrastructure.
- `apps/rock-hero-editor/tests/test_editor_settings.cpp` moves with the code it tests.

After this phase, editor executable startup should live under `rock-hero-editor/app/`, and the
old `apps/rock-hero-editor/` app-shell library target is removed.

If editor settings later become framework-free or turn into broader editor workflow policy, move
them from `editor/ui` to `editor/core` in a separate focused change.

### Phase 8: Move Executable Entry Points Into Product `app/` Folders

Move:

```text
apps/rock-hero-editor/main.cpp -> rock-hero-editor/app/main.cpp
apps/rock-hero/main.cpp        -> rock-hero-game/app/main.cpp
```

Move app-specific resources:

```text
apps/rock-hero/resources/ -> rock-hero-game/app/resources/
```

Any future editor executable resources should live under `rock-hero-editor/app/resources/`.

After this phase, the root `apps/` folder should be empty or removed.

Update root and product CMake files so executable targets are declared from the new `app/` folders.

### Phase 9: Audit Project And Package Persistence

Do not try to split every persistence concept during the initial core move.

Initial rule:

- move current `rock-hero-core` into `common/core`
- then audit project/package code after the structure is in place

Expected final direction:

- shared package primitives and runtime `.rock` behavior remain in `common/core`
- editor workspace/session policy and editor-only `.rhp` workflow state move to `editor/core`

Confirm details against current code before moving individual persistence files.

### Phase 10: Fill Remaining Stubs As Code Arrives

The empty cells remain as guarded stubs until real code arrives.

Expected first fills:

- `common/ui`: shared UI used by both products
- `editor/audio`: editor-only audio behavior outside the shared engine
- `game/core`: scoring, note matching, calibration, simulation
- `game/audio`: pitch/onset detection and analysis plumbing
- `game/ui`: bgfx/SDL gameplay rendering

Framework dependencies are added in the same change that adds real code requiring them.

### Phase 11: Post-Migration API/Impl Split Review

After the full migration is implemented, briefly analyze the codebase to decide whether any other
submodule would benefit from the same `api` / `impl` split used by `common/audio`.

Do not introduce additional splits speculatively during the initial migration.

## Acceptance Criteria

The migration is complete when:

- root-level `rock-hero-common/`, `rock-hero-editor/`, and `rock-hero-game/` folders exist
- root-level `apps/` and `libs/` folders no longer contain project-owned product code
- all nine scope/layer library submodules exist
- every library submodule has a README guardrail
- `common/audio` has `api`, `impl`, and umbrella targets
- parent umbrellas aggregate only their own scope
- editor and game executable targets live under their product `app/` folders
- executable targets link `common + editor` or `common + game`
- libraries and tests link narrow submodule targets
- editor-only code no longer lives in shared-looking modules
- headless editor workflow tests do not require concrete JUCE widgets or Tracktion implementation
- `game/core` does not depend on `game/audio`
- Tracktion remains hidden from public non-audio implementation code
- permanent compatibility aliases and forwarding headers are removed
- the game executable output remains `rock-hero`
- durable design docs describe the implemented structure

## Open Questions

- Exact README template for each submodule.
- Exact placement of individual `.rhp` and `.rock` persistence files after the Phase 9 audit.
- Whether `EditorSettings` should eventually become framework-free and move from `editor/ui` to
  `editor/core`.
- Whether any non-audio module deserves an `api` / `impl` split after the post-migration review.
