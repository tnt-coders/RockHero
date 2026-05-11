# Library Structure Plan

Status: proposed. Do not implement until the folder layout, target names, namespace shape, and
compatibility strategy are explicitly accepted.

## Problem

Rock Hero currently has two executables:

- `rock-hero-editor`
- `rock-hero-game`

The reusable code lives under `libs/` as three top-level libraries:

- `rock-hero-core`
- `rock-hero-audio`
- `rock-hero-ui`

That structure was a good early split by technical concern, but it now hides an important second
axis: product scope. Some code in `rock-hero-audio` and `rock-hero-ui` is not truly shared between
the editor and game. It exists because the editor currently needs it.

Examples include:

- JUCE editor views and controls
- editor view state and editor intent contracts
- editor workflow coordination such as open, import, save, publish, close, and exit behavior
- editor-specific waveform thumbnail and proxy presentation paths
- editor-only project prompts and dirty-state policy

The risk is not that editor-specific code exists. The risk is that editor-specific code lives in
libraries whose top-level names make them look shared. Over time, that can create several problems:

- The game may accidentally inherit editor dependencies or editor policy.
- `rock-hero-ui` can become a misleading name when the game UI is likely SDL/bgfx rather than JUCE.
- `rock-hero-audio` can grow editor presentation helpers instead of staying focused on audio
  backend capabilities.
- App-specific workflow may accumulate in common-looking libraries because the app target is kept
  intentionally thin.
- Tests may need heavier framework dependencies than the behavior under test really requires.
- Future refactors become harder because the folder layout no longer communicates ownership.

Putting these libraries under `apps/` would avoid the "common library" impression, but it would
create a different problem. Product behavior that deserves tests and clear dependency boundaries
should not live beside executable startup code. The app targets should remain thin composition
roots, not containers for significant implementation libraries.

## Goal

Preserve the useful `core`, `audio`, and `ui` mental model while also making product scope explicit.

The desired structure should make both questions obvious:

1. Which product area owns this code?
2. Which technical layer does this code belong to?

The answer should be visible in the folder path, target name, namespace, and include path.

## Proposed Solution

Use product-scope parent areas under `libs/`, with `core`, `audio`, and `ui` child modules beneath
each parent only when that product scope needs them.

```text
libs/
  rock-hero-common/
    core/
    audio/
    ui/

  rock-hero-editor/
    core/
    audio/
    ui/

  rock-hero-game/
    core/
    audio/
    ui/
```

The parent directory answers "who owns or consumes this?" The child directory answers "what kind of
responsibility is this?"

Not every parent needs every child. In particular, `rock-hero-common/ui` should probably remain
absent until there is UI code genuinely shared by both applications.

## Module Meanings

### `rock-hero-common/core`

Shared, framework-free domain and data behavior.

Likely responsibilities:

- `Song`
- `Arrangement`
- timeline value types
- difficulty and validation rules
- shared package/domain primitives
- pure timing and conversion logic used by both products

This module should remain the easiest code to test. It should not depend on JUCE, Tracktion, SDL,
bgfx, app settings, UI loops, audio devices, or plugin runtime state.

### `rock-hero-common/audio`

Shared audio backend contracts and implementations that both products are expected to use.

Likely responsibilities:

- transport ports
- shared Tracktion/JUCE audio adapter
- active arrangement playback
- plugin host integration
- tone automation backend
- audio device and realtime infrastructure
- project-owned audio contracts used by both editor and game

This module should stay an audio adapter and infrastructure layer. It should not own editor save
policy, editor prompts, scoring rules, or JUCE component behavior.

### `rock-hero-common/ui`

Shared UI code used by both products.

This should probably not exist at first. The editor UI is JUCE-based, while the game presentation
is expected to use SDL/bgfx. A common UI module should be added only when both products genuinely
share presentation code or framework-free presentation value types.

### `rock-hero-editor/core`

Editor-specific headless workflow and policy.

Likely responsibilities:

- editor session workflow
- open, import, save, save-as, publish, close, and exit orchestration
- unsaved-change policy
- selected arrangement workflow
- editor command application
- undo/redo and command history when those exist
- editor-specific state machines that should be tested without JUCE widgets

This module should not depend on concrete JUCE components. It may depend on common domain and
project-owned audio contracts. If it needs to request side effects, prefer explicit intents,
function seams, or small project-owned ports.

### `rock-hero-editor/audio`

Editor-only audio-facing helpers and adapters.

Likely responsibilities:

- waveform proxy generation paths used only by the editor
- editor thumbnail caching services
- editor-only audio asset inspection helpers
- editor-specific audio preparation behavior that is not part of common playback

This module should be used carefully. If a feature is really a shared playback capability, it
belongs in `rock-hero-common/audio`. If a feature is mostly drawing or widget behavior, it belongs
in `rock-hero-editor/ui`.

The current thumbnail APIs need special review because they combine audio source concerns with
JUCE drawing types. A clean final shape may split them into:

- editor audio code that prepares or caches waveform/proxy data
- editor UI code that draws that data into JUCE components

### `rock-hero-editor/ui`

Editor-specific JUCE presentation.

Likely responsibilities:

- concrete editor JUCE components
- editor menu and toolbar UI
- arrangement and waveform views
- transport controls
- editor view state that is purely presentation-oriented
- mapping user gestures to editor intents

This module should render already-derived state and emit intents. It should not become the owner of
workflow policy, save behavior, undo/redo rules, or audio backend decisions.

### `rock-hero-game/core`

Game-specific pure behavior.

Likely responsibilities:

- note matching
- scoring
- combo and streak rules
- calibration math
- gameplay simulation
- gameplay state machines
- pitch/onset result interpretation

This module should be headless and highly testable. It should receive explicit transport positions,
sample times, detections, and inputs rather than reading clocks or framework state directly.

### `rock-hero-game/audio`

Game-specific audio policy and adapters.

Likely responsibilities:

- game playback session setup
- game-specific routing policy
- calibration capture plumbing
- pitch/input stream adaptation
- gameplay-safe plugin-chain policy if it differs from editor practice mode

This module should not own scoring policy. It should feed explicit audio-derived observations to
`rock-hero-game/core`.

### `rock-hero-game/ui`

Game-specific presentation and rendering.

Likely responsibilities:

- SDL/bgfx window-facing presentation
- note highway rendering
- game HUD
- visual feedback
- game input handling

This module should depend on game state and emit game intents. It should not own scoring rules or
audio timing policy.

## Target And Namespace Shape

Each child module should be a real target. Parent folders should not become large catch-all
libraries.

Recommended target aliases:

```text
rock_hero::common::core
rock_hero::common::audio
rock_hero::common::ui

rock_hero::editor::core
rock_hero::editor::audio
rock_hero::editor::ui

rock_hero::game::core
rock_hero::game::audio
rock_hero::game::ui
```

Recommended concrete CMake target names:

```text
rock_hero_common_core
rock_hero_common_audio
rock_hero_common_ui

rock_hero_editor_core
rock_hero_editor_audio
rock_hero_editor_ui

rock_hero_game_core
rock_hero_game_audio
rock_hero_game_ui
```

Recommended namespaces:

```cpp
rock_hero::common::core
rock_hero::common::audio
rock_hero::common::ui

rock_hero::editor::core
rock_hero::editor::audio
rock_hero::editor::ui

rock_hero::game::core
rock_hero::game::audio
rock_hero::game::ui
```

Recommended public include paths:

```cpp
#include <rock_hero/common/core/song.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/editor/core/editor_session.h>
#include <rock_hero/editor/ui/editor_view.h>
#include <rock_hero/game/core/scoring.h>
#include <rock_hero/game/ui/note_highway_view.h>
```

The current shorter include paths can remain temporarily as forwarding headers during migration if
that keeps the refactor reviewable.

## Dependency Rules

The top-level dependency direction should be:

```text
common -> no editor or game dependencies
editor -> common only
game   -> common only
apps   -> common + their matching product modules
```

There should be no dependencies from:

- `common` to `editor`
- `common` to `game`
- `editor` to `game`
- `game` to `editor`

Layer-specific guidance:

```text
common/core   -> standard C++ and approved pure format dependencies
common/audio  -> common/core + Tracktion/JUCE audio wrappers
common/ui     -> common/core only, unless a real shared UI framework decision exists

editor/core   -> common/core + project-owned common/audio ports
editor/audio  -> common/core + common/audio, with JUCE only when justified by editor waveform work
editor/ui     -> editor/core + editor/audio + common ports + JUCE GUI

game/core     -> common/core
game/audio    -> common/audio + game/core as needed
game/ui       -> game/core + game/audio + SDL/bgfx-facing dependencies
```

The exact direction between `editor/core`, `editor/audio`, and `editor/ui` should be chosen to keep
workflow policy out of widgets and drawing out of backend code.

## Why Not Three Parent Libraries

The proposed parent areas should not become three large targets:

```text
rock_hero::common
rock_hero::editor
rock_hero::game
```

as normal production dependencies.

Optional umbrella targets may be useful for app composition, but they should be interface targets
only and should not hide unintended dependencies. Normal library-to-library dependencies should
link the narrow child target they actually need.

## Why Not Keep Only The Current Three Libraries

Keeping only `rock-hero-core`, `rock-hero-audio`, and `rock-hero-ui` preserves the technical layer
split, but it does not express product scope.

Adding `editor/`, `game/`, and `common/` folders inside those libraries would help browsing, but it
would not create a real boundary if each library remains one CMake target. The editor and game
would still link the same broad target and inherit the same dependencies.

The key boundary should be the target, not just the folder.

## Why Not Put Product Libraries Under `apps`

`apps/` should stay focused on process startup and composition:

- executable entry points
- main windows
- app-local settings
- startup restore
- shutdown behavior
- concrete dependency wiring

Editor-only or game-only product behavior can still be real library code. If it deserves targeted
tests, stable interfaces, and dependency rules, it belongs under `libs/rock-hero-editor/...` or
`libs/rock-hero-game/...`, not beside `main.cpp`.

## Migration Plan

### Phase 1: Accept The Structure

Before moving code, decide and document:

- final folder names
- target alias style
- namespace style
- public include path style
- whether temporary compatibility aliases are allowed
- whether temporary forwarding headers are allowed
- which current APIs are common versus editor-owned

This should be reflected in `docs/design/architecture.md` and
`docs/design/architectural-principles.md` only after the structure is accepted as the durable
direction.

### Phase 2: Create The Parent Layout And Child Targets

Create the new folder layout with CMake targets, but keep code movement minimal.

Suggested initial targets:

```text
libs/rock-hero-common/core
libs/rock-hero-common/audio
libs/rock-hero-editor/core
libs/rock-hero-editor/ui
```

Do not create empty `common/ui`, `editor/audio`, `game/core`, `game/audio`, or `game/ui` targets
until they have real ownership.

### Phase 3: Move Current Common Code

Move the current `rock-hero-core` implementation into `rock-hero-common/core`.

Review these current core concepts carefully:

- pure song, arrangement, timeline, and difficulty types are common
- runtime `.rock` package concepts are likely common
- editor-only `.rhp` workspace state may belong in `rock-hero-editor/core`
- importers should be placed based on whether the game will use them directly or only the editor
  will use them to author projects

Temporary aliases such as `rock_hero::core` may be useful during the migration, but they should
have an explicit removal plan.

### Phase 4: Move Shared Audio Backend Code

Move shared playback, transport, and Tracktion adapter code into `rock-hero-common/audio`.

Keep only genuinely shared audio capabilities here. Do not move editor workflow policy or
presentation-only waveform code into common audio just because it currently talks to the engine.

### Phase 5: Move Editor UI Code

Move the current `rock-hero-ui` JUCE editor components into `rock-hero-editor/ui`.

This should include:

- editor view
- arrangement view
- transport controls
- editor UI resources
- presentation-oriented editor view state
- widget-level listener contracts

The current `rock-hero-ui` target name can remain temporarily as a compatibility alias if the code
move would otherwise be too noisy.

### Phase 6: Extract Editor Workflow From Editor UI

Move headless editor workflow out of the UI target and into `rock-hero-editor/core`.

The current `EditorController` is the main candidate. The final design should avoid making
`editor/core` depend on concrete UI interfaces. Prefer a controller or service that exposes
framework-free editor state and emits side-effect requests through project-owned seams.

This is where future undo/redo, command history, and richer edit policy should live.

### Phase 7: Review Editor Audio Boundaries

Review the current thumbnail and waveform APIs.

If the feature is a shared backend capability, keep it in `rock-hero-common/audio`.

If it is editor-specific waveform data preparation or proxy caching, move it to
`rock-hero-editor/audio`.

If it is drawing into JUCE components, keep it in `rock-hero-editor/ui`.

Avoid APIs that make common audio depend on JUCE widget or drawing concepts unless both products
really need that coupling.

### Phase 8: Add Game Modules As Features Arrive

Create game modules when real game behavior needs them:

```text
libs/rock-hero-game/core
libs/rock-hero-game/audio
libs/rock-hero-game/ui
```

Likely first candidates:

- `rock-hero-game/core` for scoring, note matching, calibration, and simulation
- `rock-hero-game/ui` for SDL/bgfx note highway presentation
- `rock-hero-game/audio` only when gameplay-specific audio policy differs from common playback

## Acceptance Criteria

The migration is successful when:

- editor-only code no longer lives in common-looking targets
- game-only code has a clear home outside the editor and common modules
- common modules contain only intentionally shared code
- app targets remain thin composition roots
- each child module can be tested at the lowest practical dependency level
- no module depends on the sibling product area
- target names, namespaces, and include paths all communicate the same ownership model

## Open Questions

- Should public namespaces migrate fully to `rock_hero::common::core`, or should the current
  `rock_hero::core` namespace remain for common core?
- Should compatibility aliases such as `rock_hero::core`, `rock_hero::audio`, and
  `rock_hero::ui` remain temporarily, and for how long?
- Is `.rhp` project workspace persistence editor-owned, common-owned, or split between common
  package primitives and editor workflow state?
- Should waveform thumbnail generation be split into editor audio data preparation and editor UI
  drawing?
- Should parent umbrella targets exist for app convenience, or should apps link only granular child
  targets?
