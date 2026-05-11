# Library Structure Plan (v3)

Status: proposed. Supersedes both `library-structure-plan.md` (the original) and
`library-structure-revised-plan.md` (v2). Do not implement until the folder layout, target names,
umbrella structure, namespace shape, and window model are explicitly accepted.

## Why This Replaces v2

The revised plan (v2) was right about three things and wrong about one big thing.

It was right about:

- the framework-binding axis (headless vs framework-bound) being the key testability split inside
  each product
- pre-creating empty library stubs so the structure is enforced by the file system rather than
  only described in a doc
- the window model (bgfx primary, JUCE as peer top-level windows in the game)

It was wrong about flattening the layout. By eliminating the `common/` / `editor/` / `game/`
parent grouping, v2 lost three things that matter more than the verbosity savings:

1. **App-level linking became muddled.** With eight flat libraries, each app target had to
   enumerate its dependencies one at a time. Adding a new product-side library required touching
   the app's CMakeLists. With parent-scope umbrellas, each app links one umbrella per scope it
   consumes and never needs to be updated when submodules move around inside.
2. **The folder layout no longer mirrored the mental model.** "Is this editor stuff, game stuff,
   or shared?" is the question developers (and AI assistants placing code) ask first. v1 answered
   it in the folder path; v2 answered it only by reading library names. The folder hierarchy is a
   stronger signpost than the name.
3. **Library naming carried weight it did not need to carry.** v2 chose names like
   `audio-engine`, `juce-ui`, and `dsp` because the names had to advertise framework binding by
   themselves. Under parent scopes, the parent disambiguates. `common::audio` is unambiguously the
   shared audio engine (the only shared audio code that exists is the Tracktion adapter).
   `common::ui` is unambiguously shared JUCE widgets (the only shared UI framework is JUCE).
   `game::audio` is unambiguously game-side DSP. Pushing disambiguation into the parent scope
   resolves several naming debates v2 was still carrying.

The original plan (v1) had the right structural instinct but two gaps that v2 was reacting to:

- It tolerated empty matrix cells in principle while saying "only create when needed," which left
  the actual creation policy ambiguous. v3 makes pre-creation of all needed submodules a
  first-class part of the migration.
- It did not commit to umbrella targets — it said "optional, may be useful, must be INTERFACE
  only." v3 commits to umbrellas as the standard app-linking surface.

v3 keeps v1's hierarchy, keeps v2's framework-binding clarity *within* each parent scope, keeps
v2's stub conventions and window model decisions, and resolves the remaining open naming and
linking questions through the parent-scope disambiguation argument.

## Problem

Rock Hero currently has three libraries under `libs/`:

- `rock-hero-core`
- `rock-hero-audio`
- `rock-hero-ui`

That structure split by technical concern but it hides product scope. `rock-hero-ui` is entirely
editor JUCE code today, while the game will eventually use bgfx for its primary surface, so the
name is already misleading. Editor workflow that should be testable without JUCE currently lives
inside `rock-hero-ui` because there is no headless editor target. Future game-specific code has
no obvious home that is not also "shared."

The risk is not that editor-specific or game-specific code exists. The risk is that the folder,
target, namespace, and include path do not communicate ownership, which lets editor dependencies
leak into shared modules and makes testability harder than it needs to be.

## Goals

The structure should make three questions obvious for every piece of code:

1. **What product scope does this code belong to?** (shared, editor-only, game-only)
2. **What technical layer does it belong to?** (core domain / audio / ui)
3. **Is it framework-bound or headless?** (testable without GUI framework, or not)

The answer should be visible in the folder path, target name, namespace, and include path. The
structure should also keep headless logic testable without linking a GUI framework, and should
make app-level linking a single concise declaration per scope.

## Guiding Principles

**Parent scope answers the product-ownership question.** Three parent folders under `libs/`:
`rock-hero-common/`, `rock-hero-editor/`, `rock-hero-game/`. Code's product ownership is visible
in the folder path before any names are read.

**Submodules answer the technical-layer question.** Within each parent, submodules use a small
consistent vocabulary: `core`, `audio`, `ui`. Not every parent needs every submodule — submodules
are added only when they have real content (or are pre-created as stubs because their content is
known to be coming).

**Within a parent scope, "core" means headless and "audio" / "ui" mean framework-bound.** The
headless-vs-framework split is enforced by which submodule a piece of code lives in. Code in
`*/core` does not depend on a GUI framework; code in `*/audio` and `*/ui` does.

**The parent scope disambiguates names.** `common::audio` is the shared audio engine.
`common::ui` is the shared JUCE widget layer. `game::audio` is game-side DSP. The parent context
makes compound names like `audio-engine`, `juce-ui`, and `dsp` unnecessary.

**Apps link parent umbrellas, not individual submodules.** Each parent has an INTERFACE umbrella
target (`rock_hero::common`, `rock_hero::editor`, `rock_hero::game`) that aggregates its
submodules. Apps link the umbrella(s) for the scopes they consume.

**Pre-create all known submodules as stubs.** The library set is enumerated because every entry
is known to be needed; pre-creating them turns the plan into structure-as-code that guides code
placement from day one.

## Proposed Layout

```text
libs/
  rock-hero-common/
    CMakeLists.txt                  defines rock_hero_common INTERFACE umbrella
    core/        rock_hero::common::core        framework-free, shared
    audio/       rock_hero::common::audio       Tracktion engine isolation
    ui/          rock_hero::common::ui          shared JUCE widgets
  rock-hero-editor/
    CMakeLists.txt                  defines rock_hero_editor INTERFACE umbrella
    core/        rock_hero::editor::core        editor workflow, headless
    ui/          rock_hero::editor::ui          editor JUCE views
  rock-hero-game/
    CMakeLists.txt                  defines rock_hero_game INTERFACE umbrella
    core/        rock_hero::game::core          gameplay logic, headless
    audio/       rock_hero::game::audio         DSP (pitch, onset, game-side signal processing)
    ui/          rock_hero::game::ui            bgfx gameplay surface
```

Eight submodules total, distributed unevenly under three parents. Submodules that have no real
content yet are created as empty stubs (see [Stub Conventions](#stub-conventions)).

Real content at the start of migration:

- `common/core` (current `rock-hero-core`, moved)
- `common/audio` (current `rock-hero-audio`, moved)
- `editor/ui` (current `rock-hero-ui` after editor headless extraction)
- `editor/core` (extracted from current `rock-hero-ui`)

Empty stubs at the start of migration:

- `common/ui` (filled when the first shared JUCE widget exists)
- `game/core` (filled when scoring / note matching / calibration math is implemented)
- `game/audio` (filled when pitch / onset detection is implemented)
- `game/ui` (filled when bgfx gameplay rendering begins)

Cells that do not exist and should not be pre-created:

- `editor/audio` — there is no editor-only audio code today; the waveform thumbnail (the only
  obvious candidate) lives in `common/audio` because Tracktion's `SmartThumbnail` owns both the
  proxy data and the rasterization. If editor-only audio code ever appears, this submodule is
  added at that time.

## Module Meanings

### `rock-hero-common/core`

Shared, framework-free domain types and behavior.

Likely responsibilities:

- `Song`, `Arrangement`, `AudioAsset`, timeline value types
- difficulty and validation rules
- shared package and domain primitives
- pure timing and conversion logic used by both products
- framework-free presentation value types and view-model logic (timeline-to-pixel math, zoom
  state, transport display models) — domain-adjacent, not UI

Should not depend on JUCE, Tracktion, SDL, bgfx, app settings, UI loops, audio devices, or plugin
runtime state. This module is the easiest code to test.

### `rock-hero-common/audio`

Shared audio runtime: the project-owned face of Tracktion Engine.

Likely responsibilities:

- engine wrapper (`Engine`), transport, playback session, plugin host integration
- thumbnail interface and Tracktion-backed implementation
- audio device and realtime infrastructure
- project-owned audio contracts consumed by editor and game

The thumbnail interface deliberately exposes a JUCE-flavored draw seam (`juce::Graphics`,
`juce::Rectangle` forward-declared). This is a soft GUI dependency on consumers, not a link-time
dependency for callers who do not draw thumbnails. Tracktion's `SmartThumbnail` owns both the
proxy data and the rasterization, so no clean seam between "prepared data" and "drawing" exists
to manufacture.

Should not own editor save policy, editor prompts, scoring rules, JUCE component behavior, or
gameplay-specific audio policy.

### `rock-hero-common/ui`

Shared JUCE widgets used by both editor and game. Created as an empty stub and filled as shared
JUCE widgets emerge.

Likely contents when it fills out:

- plugin host window (VST editors are JUCE components; both apps host plugins)
- shared dialog patterns (file pickers, modal confirmations)
- settings / preferences UI
- calibration setup UI
- shared look-and-feel

Naming logic: under `common`, "ui" can only mean JUCE because the editor uses JUCE and the game's
non-gameplay windows use JUCE. The bgfx surface is `game/ui` (game-only). There is no other
shared UI framework, so the parent scope makes the framework binding unambiguous.

### `rock-hero-editor/core`

Editor-specific headless workflow and policy.

Likely responsibilities:

- editor session workflow
- open, import, save, save-as, publish, close, exit orchestration
- unsaved-change policy
- selected-arrangement workflow
- editor command application
- undo / redo and command history when those exist
- editor state machines that should be tested without JUCE widgets

Should not depend on concrete JUCE components. May depend on common domain and project-owned
audio contracts. Side effects requested of the UI flow through explicit intents, function seams,
or small project-owned ports.

The current `EditorController` is the seed of this submodule.

### `rock-hero-editor/ui`

Editor-specific JUCE presentation.

Likely responsibilities:

- concrete editor JUCE components
- editor menu and toolbar UI
- arrangement and waveform views
- transport controls
- presentation-oriented editor view state
- mapping user gestures to editor intents

Renders already-derived state and emits intents. Does not own workflow policy, save behavior,
undo / redo rules, or audio backend decisions.

### `rock-hero-game/core`

Game-specific pure behavior. Created as an empty stub and filled as gameplay code arrives.

Likely contents when it fills out:

- note matching
- scoring, combo, streak rules
- calibration math
- gameplay simulation and state machines
- pitch / onset result interpretation

Receives explicit transport positions, sample times, detections, and inputs rather than reading
clocks or framework state directly. Headless and highly testable by design.

### `rock-hero-game/audio`

Game-specific audio code: signal processing and audio-adjacent gameplay plumbing. Created as an
empty stub and filled when pitch / onset detection or other game-side audio work begins.

Likely contents when it fills out:

- pitch detection on guitar input (YIN, autocorrelation, etc.)
- onset detection
- input buffer adaptation for analysis
- windowing, framing, FFT helpers and other DSP utilities
- calibration capture plumbing
- pitch / input stream adaptation
- gameplay-safe plugin-chain policy if it differs from editor practice mode

Naming logic: within `game`, "audio" means game-side audio code, which is primarily DSP. The
shared audio engine lives in `common/audio`; what's distinctive about the game's audio scope is
the analysis path on input. If non-DSP game audio policy ever emerges (routing, capture
management), it fits here too — the submodule scope is "the game's audio code," not "DSP
specifically."

Feeds explicit audio-derived observations to `game/core`. Does not own scoring policy.

### `rock-hero-game/ui`

Game-specific bgfx presentation and rendering. Created as an empty stub and filled when bgfx
integration and gameplay rendering begin.

Likely contents when it fills out:

- bgfx surface integration with the SDL window
- note highway rendering
- game HUD
- visual feedback
- game input handling for the gameplay surface

Should not own scoring rules, audio timing policy, or JUCE-bound configuration UI. Non-gameplay
configuration screens spawned by the game live in `common/ui` and open as separate JUCE top-level
windows (see [Window Model](#window-model)).

## Targets, Namespaces, Include Paths

Each submodule is one CMake target with a matching nested namespace and include path. Naming
follows the existing project convention that folder, target, namespace, and include path all line
up.

| Submodule              | CMake target                | Namespace                   | Include path                            |
| ---------------------- | --------------------------- | --------------------------- | --------------------------------------- |
| `common/core`          | `rock_hero_common_core`     | `rock_hero::common::core`   | `<rock_hero/common/core/...>`           |
| `common/audio`         | `rock_hero_common_audio`    | `rock_hero::common::audio`  | `<rock_hero/common/audio/...>`          |
| `common/ui`            | `rock_hero_common_ui`       | `rock_hero::common::ui`     | `<rock_hero/common/ui/...>`             |
| `editor/core`          | `rock_hero_editor_core`     | `rock_hero::editor::core`   | `<rock_hero/editor/core/...>`           |
| `editor/ui`            | `rock_hero_editor_ui`       | `rock_hero::editor::ui`     | `<rock_hero/editor/ui/...>`             |
| `game/core`            | `rock_hero_game_core`       | `rock_hero::game::core`     | `<rock_hero/game/core/...>`             |
| `game/audio`           | `rock_hero_game_audio`      | `rock_hero::game::audio`    | `<rock_hero/game/audio/...>`            |
| `game/ui`              | `rock_hero_game_ui`         | `rock_hero::game::ui`       | `<rock_hero/game/ui/...>`               |

Example include directives:

```cpp
#include <rock_hero/common/core/song.h>
#include <rock_hero/common/audio/engine.h>
#include <rock_hero/common/ui/plugin_host_window.h>
#include <rock_hero/editor/core/editor_session.h>
#include <rock_hero/editor/ui/editor_view.h>
#include <rock_hero/game/core/scoring.h>
#include <rock_hero/game/audio/pitch_detector.h>
#include <rock_hero/game/ui/note_highway_view.h>
```

Namespace aliases are not used; the verbose form is canonical throughout the codebase.

## Umbrella Targets

Each parent folder defines one INTERFACE umbrella target that aggregates its submodules:

```cmake
# libs/rock-hero-common/CMakeLists.txt
add_subdirectory(core)
add_subdirectory(audio)
add_subdirectory(ui)

add_library(rock_hero_common INTERFACE)
add_library(rock_hero::common ALIAS rock_hero_common)
target_link_libraries(rock_hero_common INTERFACE
    rock_hero::common::core
    rock_hero::common::audio
    rock_hero::common::ui)

# libs/rock-hero-editor/CMakeLists.txt
add_subdirectory(core)
add_subdirectory(ui)

add_library(rock_hero_editor INTERFACE)
add_library(rock_hero::editor ALIAS rock_hero_editor)
target_link_libraries(rock_hero_editor INTERFACE
    rock_hero::editor::core
    rock_hero::editor::ui)

# libs/rock-hero-game/CMakeLists.txt — same pattern over game submodules
```

Umbrellas are INTERFACE-only. They do not contain implementation. They do not cross-link between
scopes — `rock_hero::editor` does not pull in `rock_hero::common`, because both are linked
explicitly by apps.

App linking is then a two-line declaration per app:

```cmake
# apps/rock-hero-editor/CMakeLists.txt
target_link_libraries(rock_hero_editor_app PRIVATE
    rock_hero::common
    rock_hero::editor)

# apps/rock-hero-game/CMakeLists.txt
target_link_libraries(rock_hero_game_app PRIVATE
    rock_hero::common
    rock_hero::game)
```

When new submodules are added to a parent (e.g., `editor/audio` if editor-only audio code ever
emerges), only the parent's CMakeLists is updated. Apps do not change.

## Dependency Rules

Top-level direction:

```text
common -> no editor or game dependencies
editor -> common only
game   -> common only
apps   -> common + their matching product umbrella
```

Forbidden directions:

- `common::*` depending on anything in `editor::*` or `game::*`
- `editor::*` depending on anything in `game::*`
- `game::*` depending on anything in `editor::*`
- any UI submodule being depended on by its sibling core submodule (UI depends on core, never the
  reverse — this is the headless-vs-framework boundary inside each parent)

Layer-specific guidance:

```text
common/core    standard C++ and approved pure format dependencies
common/audio   common/core + Tracktion / JUCE audio (no GUI link required; soft GUI forward-decls)
common/ui      common/core + JUCE GUI

editor/core    common/core + common/audio (no GUI framework)
editor/ui      editor/core + common/audio + common/ui + JUCE GUI

game/core      common/core + game/audio (game::core consumes pitch / onset observations)
game/audio     common/core + JUCE DSP (no Tracktion, no GUI)
game/ui        game/core + common/audio + bgfx + SDL
```

The editor app links `rock_hero::common` and `rock_hero::editor`. The game app links
`rock_hero::common` and `rock_hero::game`. Apps do not link individual submodules.

## Window Model

Decided: the game's primary window is bgfx and hosts only the gameplay surface. JUCE windows in
the game are separate top-level native windows (plugin editor windows, settings, calibration).
There is no JUCE-as-shell embedding the bgfx surface, and no bgfx-in-JUCE component.

Implications:

- `game/ui` owns the bgfx window and gameplay rendering only.
- Game-side JUCE windows are constructed against `common/ui` and live alongside the bgfx window
  as peers, not children.
- Two GUI systems coexist in one process. JUCE owns its own `MessageManager`; SDL owns its own
  event pump. Window focus and input routing across the two are the responsibility of the app
  target.

This matches the editor's expected pattern (JUCE-only top-level windows) and lets the game reuse
shared JUCE UI without compromising the gameplay surface's frame budget.

## Stub Conventions

Submodules that exist but have no real code yet follow a few rules so they behave like real
libraries from day one and absorb their first real translation unit without churn.

- **Real static libraries, not INTERFACE targets.** Each stub gets a single placeholder source
  file (e.g., `src/placeholder.cpp` containing only the submodule's namespace declaration). This
  keeps the CMake target type stable when the first real `.cpp` arrives — dependents continue to
  link the target the same way.
- **No framework dependencies until real code needs them.** A stub `game/ui/` does not pull bgfx
  or SDL into the build, a stub `game/audio/` does not pull in JUCE DSP, and so on. The
  dependency is added in the same change that introduces the first translation unit that needs
  it.
- **Include path and namespace established at creation time.** Every stub creates its
  `include/rock_hero/<parent>/<submodule>/` directory and declares its namespace, even if no
  headers exist yet. This is what makes the structure legible to code-placement decisions made
  before any real code arrives.
- **Apps do not pre-wire empty submodules.** Apps link parent umbrellas, which propagate to all
  submodules including stubs. This is fine — empty stubs link as no-ops.
- **No tests for empty submodules.** Per-submodule test targets follow the same "real code first"
  rule.

## Design Decisions And Rationale

### Why hierarchical with parent umbrellas

A flat layout of named libraries puts the entire structural taxonomy into library names. Naming
alone has to advertise product scope, technical layer, and framework binding, which is more weight
than a single token can carry. The result is either compound names like `editor-ui` and
`audio-engine` (which work but are noisy) or short names that lose information.

A two-level layout puts each axis where it belongs: the parent folder carries product scope, the
submodule name carries technical layer, and the headless-vs-framework split lives in which
submodule a file is placed under. Compound names disappear. Library names become short and
contextual.

The umbrella targets close the loop by making app-level linking declarative. An app links the
scopes it consumes, not the submodules within those scopes. Adding a new submodule never requires
touching app CMakeLists.

### Why parent scope disambiguates better than naming conventions

The framework-binding axis (introduced in v2) is real and important, but only within a product
scope. The question "is this code JUCE-bound or headless?" is answered the same way for editor
code and game code: the headless half lives in `core/`, the framework-bound half lives in
`audio/` or `ui/`. The parent scope provides the context for which framework is meant.

`common::ui` cannot mean anything except JUCE widgets because JUCE is the only shared UI
framework. `game::ui` cannot mean anything except bgfx because bgfx is the game's chosen
gameplay-surface framework. `common::audio` cannot mean anything except the Tracktion engine
because that is the only shared audio code. `game::audio` cannot mean anything except game-side
DSP because the shared engine is in `common`. The disambiguation is structural.

This makes naming both simpler (no `juce-ui`, `audio-engine`, `dsp` compounds) and more
informative (every name advertises product scope as well as layer).

### Why pre-create empty submodule stubs

The usual YAGNI argument — "do not commit to structures you do not need yet" — does not apply
here. The submodule set is enumerated because every entry is known to be needed; the question is
only when each one fills up.

Pre-creating stubs converts the plan from a document into structure-as-code, which has several
payoffs:

- **Code-placement signposting.** When new code lands (often suggested by an AI assistant), the
  decision "where does this go?" is answered by the file system, not by re-reading a plan
  document.
- **Conventions established before they are needed.** CMake target, namespace, and include path
  conventions are validated while nothing depends on them yet, so configuration issues are caught
  at low cost.
- **Dependency rules enforceable from day one.** Forbidden directions can be expressed against
  real targets rather than hypothetical ones.
- **First real translation unit drops in without churn.** Because the stubs are real static
  libraries, the first real `.cpp` arriving does not change target type, link semantics, or
  include conventions.

The real cost — empty submodules drifting from intent if nobody is watching — is mitigated by the
plan doc being the source of truth and by the fact that every stub is expected to fill up soon.
A stub that stays empty indefinitely is a signal to revisit the plan, not a problem with
pre-creation.

### Why umbrellas are INTERFACE only, not concrete monolithic libraries

Umbrellas should not contain implementation. Each submodule is a real CMake target so that:

- consumers can depend on the narrow surface they need when that distinction is useful (most
  tests, for example, link only one submodule)
- library-level dependency rules remain enforceable per submodule
- the umbrella does not become a vehicle for hidden coupling between unrelated submodules

The umbrella is purely a convenience for app-level composition. It aggregates dependencies that
would otherwise have to be listed one at a time, and nothing more.

### Why the headless workflow submodule is named `core`, not `workflow` or `logic`

Within a parent scope, "core" reads as "the core logic of this product, framework-free." This
matches the existing project convention where `core` carries the meaning of "domain logic without
framework dependencies" rather than "shared between subsystems." Naming each headless submodule
the same word also makes the testability rule trivially memorable: anything in a `core/`
submodule must build and run without a GUI framework.

Alternatives like `workflow` (for editor) and `logic` (for game) break this symmetry. The
symmetry is what makes the rule easy to enforce and easy to teach.

### Why JUCE is acceptable in the game

Plugin editors are JUCE components by SDK contract — the game must host JUCE windows to display
VST plugin UIs. Beyond that unavoidable case, reusing JUCE for non-realtime screens (settings,
calibration, file pickers) is cheaper than building duplicate bgfx UI and reuses code already
built for the editor.

The cost is a coexisting GUI system in the same process, which is well-trodden when the windows
are separate top-level peers. See [Window Model](#window-model).

### Why DSP lives in `game/audio`, not in a dedicated `dsp` library

Pitch detection, onset detection, and other game-side signal processing are game-specific code
paths. They consume the audio engine but are not part of it, and the editor does not use them.
Placing them under `game/audio` reflects the actual product scope: this is the game's audio code.

If the editor ever needs a slice of DSP (e.g., onset detection for arrangement preview), the
right move is to add `editor/audio` at that time with what it specifically needs. Sharing DSP
utilities through `common/audio` would require pulling DSP-only dependencies (e.g., parts of JUCE
DSP) into the shared engine, which is the wrong boundary.

### Why no `editor/audio` cell exists at the start

The only obvious candidate for editor-only audio code is the waveform thumbnail. The thumbnail
exposes `IThumbnail::drawChannels(juce::Graphics&, ...)` because Tracktion's `SmartThumbnail`
owns both the proxy data and the rasterization — there is no clean seam between "headless data
prep" and "JUCE drawing" worth manufacturing. The thumbnail therefore lives in `common/audio`
alongside the engine, with a JUCE-flavored draw seam that callers who do not render thumbnails
never have to invoke.

If editor-only audio code ever does appear, the submodule is added at that time. It is not
pre-created because, unlike the game's eventual audio scope, there is no concrete content known
to be coming.

### Why product libraries are not under `apps/`

`apps/` stays focused on process startup and composition: executable entry points, main windows,
app-local settings, startup / shutdown behavior, and concrete dependency wiring. Editor and game
product behavior that deserves targeted tests, stable interfaces, and dependency rules belongs in
`libs/`, not beside `main.cpp`.

### Why folder hierarchy carries weight beyond CMake targets

Even with umbrellas making linking easy, the folder hierarchy itself does work. It is where
developers look first when deciding where new code belongs, where AI assistants infer placement
when adding code, and where the architecture is most legible at a glance. A flat layout would
relegate the product-scope distinction to library naming, which is read less often than the
folder tree.

## Migration Plan

### Phase 1: Accept the structure

Confirm and document:

- final folder layout, target names, namespace shape, and include paths per the
  [Targets, Namespaces, Include Paths](#targets-namespaces-include-paths) table
- umbrella target naming and INTERFACE-only structure
- the window model (bgfx primary; JUCE windows as peer top-levels)
- whether temporary compatibility aliases or forwarding headers are allowed during rename phases

Update `docs/design/architecture.md` and `docs/design/architectural-principles.md` only after the
structure is accepted as the durable direction.

### Phase 2: Create the hierarchical layout with parent umbrellas and submodule stubs

Create the new folder layout, all CMake targets, and all umbrella INTERFACE targets. At the end
of this phase the file system reflects the full target layout, with existing libraries still in
their old locations.

Specifically:

- Create `libs/rock-hero-common/`, `libs/rock-hero-editor/`, `libs/rock-hero-game/` parent folders
- Create each parent's `CMakeLists.txt` defining its INTERFACE umbrella target
- Create stub submodules:
  - `libs/rock-hero-common/ui/`
  - `libs/rock-hero-editor/core/`
  - `libs/rock-hero-game/core/`
  - `libs/rock-hero-game/audio/`
  - `libs/rock-hero-game/ui/`

Each stub follows [Stub Conventions](#stub-conventions): one placeholder source file declaring
its namespace, an empty `include/rock_hero/<parent>/<submodule>/` directory, no framework
dependencies, and registration with the parent's umbrella.

No existing code moves in this phase.

### Phase 3: Move `rock-hero-core` into `common/core`

Move the existing `libs/rock-hero-core/` to `libs/rock-hero-common/core/`. Update:

- folder location
- CMake target name from `rock_hero_core` to `rock_hero_common_core`
- namespace from `rock_hero::core` to `rock_hero::common::core`
- include paths from `<rock_hero/core/...>` to `<rock_hero/common/core/...>`
- consumers in `libs/rock-hero-audio/`, `libs/rock-hero-ui/`, and both apps

This is one noisy but mechanical PR.

### Phase 4: Move `rock-hero-audio` into `common/audio`

Same pattern as Phase 3. Folder, target, namespace, include path, and consumers are updated to
`common::audio`.

### Phase 5: Move `rock-hero-ui` into `editor/ui`

Same pattern. After this phase the editor-only nature of the existing UI library is visible in
the folder path and target name.

### Phase 6: Extract `editor/core` from `editor/ui`

Move headless editor workflow into the (already-created) `editor/core` stub.
`EditorController` is the main candidate. Move along with it:

- workflow / orchestration types (open, import, save, save-as, publish, close, exit)
- unsaved-change policy
- editor session and selection state that does not draw
- editor command application and any command-history precursors

Update `editor/ui` to depend on `editor/core` and consume its types. The dependency points one
way: UI depends on core, never the reverse. Side effects requested of the UI flow through
explicit intents or project-owned ports.

This phase is where the testability win lands. Tests for `editor/core` should link only
`rock_hero_editor_core` (plus common fakes as needed) and should not require JUCE GUI.

### Phase 7: Review `.rhp` workspace state placement

Decide whether `.rhp` project workspace persistence is editor-owned, common-owned, or split. The
likely answer: package primitives (`.rock`) stay in `common/core`; editor workspace state (window
layout, last-opened project, dirty flag policy) lives in `editor/core`. Confirm before moving any
existing code.

### Phase 8: Fill remaining stubs as code arrives

The stubs created in Phase 2 are filled as real code lands. There is no required order — each
fills when its first content is needed:

- `common/ui` fills when the first shared JUCE widget appears (likely candidates: plugin host
  window, settings dialog, shared file pickers).
- `game/core` fills when scoring, note matching, or calibration math is implemented.
- `game/audio` fills when pitch or onset detection (or any other game-side audio code) is
  implemented.
- `game/ui` fills when bgfx integration and gameplay rendering begin.

Each first-content change in a stub may also be the change that introduces the stub's framework
dependency (bgfx, SDL, JUCE GUI, JUCE DSP).

## Acceptance Criteria

The migration is successful when:

- all submodules in the planned layout exist under their parent folders, including the stubs
  that have not yet been filled
- each parent folder has an INTERFACE umbrella target that aggregates its submodules
- each app links exactly two umbrellas (its product umbrella plus `rock_hero::common`)
- editor-only code no longer lives in a library with a shared-sounding name
- the editor's headless workflow can be unit-tested without linking JUCE GUI
- `common/audio` contains the runtime audio engine only, not editor workflow or presentation
  policy
- game-only code, when it lands, has a clear home under `game/` outside the editor and common
  scopes
- app targets remain thin composition roots
- each submodule can be tested at the lowest practical dependency level
- no submodule depends on a sibling product scope
- target names, namespaces, and include paths all communicate the same ownership model

## Open Questions

- Should each stub submodule carry a short `README.md` describing what it will hold and what it
  must not contain? This is the strongest code-placement signpost beyond the folder / target /
  namespace pattern, especially for AI-assisted code placement. Default leaning: yes, one short
  README per stub, removed or replaced once the stub fills out.
- Should temporary namespace or target aliases (e.g., `rock_hero::core` aliasing
  `rock_hero::common::core`) be allowed during Phases 3 – 5 to keep the move PRs reviewable, or
  should the moves land as single atomic PRs with no shims? Default leaning: no shims; one-shot
  renames per phase.
- Should the editor app's `apps/rock-hero-editor/` and the game app's `apps/rock-hero/` be
  renamed for symmetry (e.g., `apps/rock-hero-game/`), or kept as they are? Not strictly part of
  this plan but worth deciding alongside it.
- When `editor/audio` is eventually needed, do shared DSP utilities (if `editor/audio` and
  `game/audio` end up with overlapping needs) migrate to `common/audio` at that time, or does a
  new shared DSP submodule appear under `common/`? Decide when the case arises; the default
  leaning is "keep them in their respective product scopes until the duplication is concrete and
  costly."