# Library Structure Plan (Revised)

Status: proposed. Supersedes `library-structure-plan.md` once accepted. Do not implement until the
folder layout, target names, namespace shape, and window model are explicitly accepted.

## Problem

Rock Hero currently has three libraries under `libs/`:

- `rock-hero-core`
- `rock-hero-audio`
- `rock-hero-ui`

That structure split by technical concern but it hides product scope. `rock-hero-ui` is entirely
editor JUCE code today, while the game will eventually use SDL/bgfx for its primary surface, so
the name `rock-hero-ui` is already misleading. Editor workflow that should be testable without
JUCE currently lives inside `rock-hero-ui` because there is no headless editor target. Future
game-specific code has no obvious home that is not also "shared."

The risk is not that editor-specific or game-specific code exists. The risk is that the folder,
target, namespace, and include path do not communicate ownership, which lets editor dependencies
leak into shared modules and makes testability harder than it needs to be.

## Goal

The structure should make two questions obvious for every library:

1. What does this code bind to? (no framework, JUCE, bgfx, Tracktion)
2. What scope does this code cover? (shared, editor-only, game-only)

The answer should be visible in the folder path, target name, namespace, and include path. The
structure should also keep headless logic testable without linking a GUI framework.

## Guiding Principle

The split that carries the most weight is **framework binding**: does this code depend on a GUI or
audio framework, or is it pure logic? Framework binding is what determines:

- whether the code is cheap to unit-test
- whether it can be reused across products that pick different frameworks
- whether it belongs near the integration surface or in the testable core

Product scope (editor vs game) is the secondary axis and shows up in library names only when the
scope is genuinely product-specific. Pure technical layer ("audio" vs "ui" vs "core") is not
load-bearing on its own — those words appear in library names only when they help identify the
framework binding.

Two consequences:

- Headless editor workflow and headless game logic each get their own library so they are testable
  without linking a GUI framework.
- "Shared UI" is split by framework: framework-free presentation logic lives in core; shared JUCE
  widgets get their own library named after the framework; cross-framework shared widgets are not
  a real category.

## Proposed Layout

Flat under `libs/`, with all eight libraries created up front. Libraries that have no real code
yet are created as empty stubs (see [Stub Conventions](#stub-conventions)).

```text
libs/
  rock-hero-core/             framework-free, shared
  rock-hero-audio-engine/     Tracktion isolation; transport, plugin host, thumbnail
  rock-hero-juce-ui/          shared JUCE widgets (plugin host window, settings, dialogs)
  rock-hero-editor/           editor workflow, headless
  rock-hero-editor-ui/        editor-specific JUCE views
  rock-hero-game/             gameplay logic, headless
  rock-hero-game-ui/          bgfx gameplay surface
  rock-hero-dsp/              signal processing: pitch and onset detection, DSP utilities
```

Only `rock-hero-core`, `rock-hero-audio-engine` (after rename), and `rock-hero-editor-ui` (after
rename and headless extraction) hold real code immediately. The remaining five start as empty
static libraries with placeholder sources and are filled as their content lands.

The current state maps to this layout as:

- `rock-hero-core` stays
- `rock-hero-audio` → `rock-hero-audio-engine` (rename)
- `rock-hero-ui` → `rock-hero-editor-ui` (rename; editor workflow extracted)
- new `rock-hero-editor` library holds the extracted headless workflow
- `rock-hero-juce-ui`, `rock-hero-game`, `rock-hero-game-ui`, and `rock-hero-dsp` are created as
  empty stubs and filled as shared JUCE widgets, gameplay logic, gameplay rendering, and
  signal-processing code arrives

## Module Meanings

### `rock-hero-core`

Shared, framework-free domain types and behavior.

Holds:

- `Song`, `Arrangement`, `AudioAsset`, timeline value types
- difficulty and validation rules
- pure timing and conversion logic used by both products
- framework-free presentation value types and view-model logic (e.g., timeline-to-pixel math,
  zoom state, transport display models) — these are domain-adjacent, not UI

Excludes JUCE, Tracktion, SDL, bgfx, plugin runtime state, audio devices.

### `rock-hero-audio-engine`

Shared audio runtime; the project-owned face of Tracktion Engine.

Holds:

- engine wrapper (`Engine`), transport, playback session, plugin host integration
- thumbnail interface and Tracktion-backed implementation
- audio device and realtime infrastructure
- project-owned audio contracts used by editor and game

The thumbnail interface deliberately exposes a JUCE-flavored draw seam (`juce::Graphics`,
`juce::Rectangle` forward-declared). This is a soft GUI dependency on consumers, not a link-time
dependency for callers who do not draw thumbnails. Tracktion's `SmartThumbnail` owns both the
proxy data and the rasterization, so there is no clean seam between "prepared data" and "drawing"
worth manufacturing.

Excludes editor save policy, editor prompts, scoring rules, JUCE component behavior, and
gameplay-specific audio policy.

### `rock-hero-juce-ui`

Shared JUCE widgets used by both editor and game. Created as an empty stub and filled as shared
JUCE widgets emerge.

Likely contents when it fills out:

- plugin host window (VST editors are JUCE components; both apps will host plugins)
- shared dialog patterns (file pickers, modal confirmations)
- settings / preferences UI
- calibration setup UI
- shared look-and-feel

Naming the library after the framework (`juce-ui` rather than `ui`) keeps the binding visible.
This is the lesson learned from the original `rock-hero-ui` name becoming misleading.

### `rock-hero-editor`

Editor-specific headless workflow.

Holds:

- editor session workflow
- open, import, save, save-as, publish, close, exit orchestration
- unsaved-change policy
- selected-arrangement workflow
- editor command application
- undo / redo and command history when those exist
- editor state machines that should be tested without JUCE widgets

The current `EditorController` is the seed of this library. The library should not depend on
concrete JUCE components. Side effects requested of the UI are expressed through explicit intents
or project-owned ports.

### `rock-hero-editor-ui`

Editor-specific JUCE presentation.

Holds:

- concrete editor JUCE components
- editor menu and toolbar UI
- arrangement and waveform views
- transport controls
- presentation-oriented editor view state
- mapping user gestures to editor intents

Renders already-derived state and emits intents. Does not own workflow policy, save behavior,
undo / redo rules, or audio backend decisions.

### `rock-hero-game`

Game-specific pure behavior. Created as an empty stub and filled as gameplay code arrives.

Likely contents:

- note matching
- scoring, combo, streak rules
- calibration math
- gameplay simulation and state machines
- pitch / onset result interpretation

Receives explicit transport positions, sample times, detections, and inputs rather than reading
clocks or framework state directly.

### `rock-hero-game-ui`

Game-specific bgfx presentation. Created as an empty stub and filled when bgfx integration and
gameplay rendering begin.

Likely contents:

- bgfx surface integration with the SDL window
- note highway rendering
- game HUD
- visual feedback
- game input handling for the gameplay surface

Excludes scoring rules, audio timing policy, and JUCE-bound configuration UI. Non-gameplay
configuration screens spawned by the game live in `rock-hero-juce-ui` and are opened as separate
JUCE top-level windows (see Window Model).

### `rock-hero-dsp`

Signal processing code that is not part of the playback engine. Created as an empty stub and
filled when pitch / onset detection or other DSP work begins.

Likely contents:

- pitch detection on guitar input
- onset detection
- windowing, framing, FFT helpers, and other DSP utilities shared by analysis paths
- any custom DSP outside Tracktion's plugin chain (resampling helpers, framing utilities)

Distinct from `rock-hero-audio-engine` because it has different dependencies (JUCE DSP, not
Tracktion), different testability profile (deterministic on recorded buffers), and a different
primary consumer (the game). Named after the technique (DSP) rather than the current use case
(analysis) so the library does not need to be renamed if non-analysis DSP code arrives later.

## Targets, Namespaces, Include Paths

Each library is one CMake target with a matching sub-namespace and include path, following the
existing project convention.

| Library                    | CMake target               | Namespace                       | Include path                       |
| -------------------------- | -------------------------- | ------------------------------- | ---------------------------------- |
| `rock-hero-core`           | `rock_hero_core`           | `rock_hero::core`               | `<rock_hero/core/...>`             |
| `rock-hero-audio-engine`   | `rock_hero_audio_engine`   | `rock_hero::audio_engine`       | `<rock_hero/audio_engine/...>`     |
| `rock-hero-juce-ui`        | `rock_hero_juce_ui`        | `rock_hero::juce_ui`            | `<rock_hero/juce_ui/...>`          |
| `rock-hero-editor`         | `rock_hero_editor`         | `rock_hero::editor`             | `<rock_hero/editor/...>`           |
| `rock-hero-editor-ui`      | `rock_hero_editor_ui`      | `rock_hero::editor_ui`          | `<rock_hero/editor_ui/...>`        |
| `rock-hero-game`           | `rock_hero_game`           | `rock_hero::game`               | `<rock_hero/game/...>`             |
| `rock-hero-game-ui`        | `rock_hero_game_ui`        | `rock_hero::game_ui`            | `<rock_hero/game_ui/...>`          |
| `rock-hero-dsp`            | `rock_hero_dsp`            | `rock_hero::dsp`                | `<rock_hero/dsp/...>`              |

Namespace aliases are not used; the verbose form is the canonical form throughout the codebase.
Slight redundancy in fully-qualified names like `rock_hero::audio_engine::Engine` is accepted as
the cost of unambiguous scoping; this matches the pattern Tracktion uses (`tracktion::Engine`).

## Dependency Rules

Top-level direction:

```text
core              standard C++ only
audio-engine      core + Tracktion / JUCE audio (no GUI link required, soft GUI forward-decls only)
juce-ui           core + JUCE GUI
editor            core + audio-engine (no GUI framework)
editor-ui         editor + audio-engine + juce-ui + JUCE GUI
game              core + audio-engine + dsp
game-ui           game + audio-engine + bgfx + SDL
dsp               core + JUCE DSP
apps              their matching product modules + juce-ui as needed
```

Forbidden directions:

- core depending on any other module
- audio-engine depending on editor, game, or any UI module
- editor depending on editor-ui
- game depending on game-ui
- editor depending on game (or any game module)
- game depending on editor (or any editor module)
- juce-ui depending on editor or game (it is shared; product-specific JUCE goes in the product UI
  library)

The editor app links `rock-hero-editor`, `rock-hero-editor-ui`, `rock-hero-juce-ui`, and the
transitive audio targets. The game app links `rock-hero-game`, `rock-hero-game-ui`,
`rock-hero-juce-ui` (for plugin host windows and settings), and the transitive audio targets.

## Window Model

Decided: the game's primary window is bgfx and hosts only the gameplay surface. JUCE windows in
the game are separate top-level native windows (plugin editor windows, settings, calibration).
There is no JUCE-as-shell embedding the bgfx surface, and no bgfx-in-JUCE component.

Implications:

- `rock-hero-game-ui` owns the bgfx window and gameplay rendering only.
- Game-side JUCE windows are constructed against `rock-hero-juce-ui` and live alongside the bgfx
  window as peers, not children.
- Two GUI systems coexist in one process. JUCE owns its own `MessageManager`; SDL owns its own
  event pump. Window focus and input routing across the two are the responsibility of the app
  target.

This matches the editor's expected pattern (JUCE-only top-level windows) and lets the game reuse
the shared JUCE UI without compromising the gameplay surface's frame budget.

## Stub Conventions

Libraries that exist but have no real code yet follow a few rules so they behave like real
libraries from day one and absorb their first real translation unit without churn.

- **Real static libraries, not INTERFACE targets.** Each stub gets a single placeholder source
  file (e.g., `src/placeholder.cpp` containing only the library's namespace declaration). This
  keeps the CMake target type stable when the first real `.cpp` arrives — dependents continue
  to link the target the same way.
- **No framework dependencies until real code needs them.** A stub `rock-hero-game-ui/` does not
  pull bgfx or SDL into the build, a stub `rock-hero-dsp/` does not pull in JUCE DSP, and so on.
  The dependency is added in the same change that introduces the first translation unit that
  needs it.
- **Include path and namespace established at creation time.** Every stub creates its
  `include/rock_hero/<lib>/` directory and declares its namespace, even if no headers exist
  yet. This is what makes the structure legible to code-placement decisions made before any
  real code arrives.
- **Apps do not pre-wire empty libraries.** App targets link only what they actually use today.
  When a stub library gains its first consumed type, the consuming app starts linking it then.
- **No tests for empty libraries.** Per-library test targets follow the same "real code first"
  rule as the libraries themselves.

## Design Decisions And Rationale

### Why framework binding, not a product × layer matrix

A 3 × 3 product × layer grid (common / editor / game × core / audio / ui) provisions up to nine
cells where Rock Hero realistically has four to six. The empty cells either stay empty (`common/ui`,
`editor/audio` were both flagged as probably-not-needed in the original plan) or invite
manufactured code to justify their existence. Naming the load-bearing split directly — "is this
framework-bound?" — collapses the structure to the libraries that actually carry weight.

### Why thumbnail stays in `rock-hero-audio-engine`

The thumbnail interface (`IThumbnail::drawChannels`) exposes a JUCE-flavored draw seam, but the
Tracktion `SmartThumbnail` underneath owns both the proxy data and the rasterization. There is no
clean seam between "headless data prep" and "JUCE drawing" worth manufacturing. The audio engine
exposes a draw method; consumers call it from a JUCE graphics context. The game does not call
`drawChannels` and does not need to link JUCE GUI to consume the rest of `rock-hero-audio-engine`.

### Why JUCE in the game is acceptable

Plugin editors are JUCE components by SDK contract — the game must host JUCE windows to display
VST plugin UIs. Beyond that unavoidable case, reusing JUCE for non-realtime screens (settings,
calibration, file pickers) is cheaper than building duplicate bgfx UI and reuses code already
built for the editor. The cost is a coexisting GUI system, which is well-trodden when the windows
are separate top-level peers.

### Why `rock-hero-audio-engine`, not `rock-hero-audio` or `rock-hero-engine`

"Audio" alone is the same kind of catch-all as the original "ui" — it does not say what kind of
audio code lives there, and signal-processing code is a plausible sibling library with different
dependencies and consumers. "Engine" alone is too broad in game-dev because rendering and
gameplay engines will eventually exist. "Audio-engine" names the role accurately and leaves
sibling DSP code free to live in `rock-hero-dsp` without competing for the parent "audio" concept.

### Why flat under `libs/`

At seven to eight libraries, library names already encode both product scope and framework binding.
Adding `libs/editor/rock-hero-editor-ui/` puts "editor" in the path twice and forces a primary
axis choice (product vs framework) where neither answer is obviously right. Alphabetical sort
groups `rock-hero-audio-*`, `rock-hero-editor-*`, and `rock-hero-game-*` for free. Revisit if the
library count grows past roughly fifteen.

### Why no `rock-hero-ui` shared library

"Shared UI" decomposes into three things, each with a different home:

- framework-free presentation logic → `rock-hero-core`
- shared JUCE widgets → `rock-hero-juce-ui` (named after the framework binding)
- shared bgfx widgets → not realistic; the editor does not use bgfx

A single shared UI library would have to bind to either JUCE or bgfx and would lie about its
scope in either case.

### Why pre-create empty library stubs

The usual YAGNI argument against creating empty libraries — "don't commit to structures you do
not need yet" — does not apply here. The library set in this plan is enumerated precisely
because every entry is known to be needed; the question is only when each one fills up. Given
that, pre-creating stubs converts the plan from a document into structure-as-code, which has
several payoffs:

- **Code-placement signposting.** When new code lands (often suggested by an AI assistant) the
  decision "where does this go?" is answered by the file system, not by re-reading a plan
  document. A folder named `rock-hero-dsp/` with an established include path and namespace is a
  much stronger signal than a doc that says "this library will exist."
- **CMake target, namespace, and include path conventions established before they are needed.**
  Catches configuration issues at low cost while nothing depends on the target yet.
- **Dependency rules enforceable from day one.** If a CMake or lint rule says "no library may
  depend backward toward `core`," the rule can be expressed against real targets rather than
  hypothetical ones.
- **First real translation unit drops in without churn.** Because the stubs are real static
  libraries (see [Stub Conventions](#stub-conventions)), the first real `.cpp` arriving in a
  stub does not change target type, link semantics, or include conventions.

The real cost — empty libraries drifting from intent if nobody is watching — is mitigated by
the plan doc being the source of truth and by the fact that every stub is expected to fill up
soon. If a stub stays empty for an extended period without a clear path to its first content,
that is a signal to revisit the plan, not a problem with pre-creation.

### Why product libraries are not under `apps/`

`apps/` stays focused on process startup and composition: executable entry points, main windows,
app-local settings, startup / shutdown behavior, and concrete dependency wiring. Editor and game
product behavior that deserves targeted tests, stable interfaces, and dependency rules belongs in
`libs/`, not beside `main.cpp`.

## Migration Plan

### Phase 1: Accept the structure

Confirm and document:

- final library, target, namespace, and include path names per the table above
- the flat layout under `libs/`
- the window model (bgfx primary; JUCE windows as top-level peers)
- whether temporary compatibility aliases or forwarding headers are allowed during rename phases

Update `docs/design/architecture.md` and `docs/design/architectural-principles.md` only after the
structure is accepted as the durable direction.

### Phase 2: Create empty library stubs

Create all libraries that do not yet exist, following [Stub Conventions](#stub-conventions). At
the end of this phase the following stubs exist:

- `libs/rock-hero-juce-ui/`
- `libs/rock-hero-editor/`
- `libs/rock-hero-game/`
- `libs/rock-hero-game-ui/`
- `libs/rock-hero-dsp/`

Each stub:

- is registered as a `rock_hero_<name>` static library target in CMake
- contains a single placeholder source file declaring its namespace
- has `include/rock_hero/<name>/` created (no headers yet)
- has no framework dependencies (those arrive with the first real consumer code)
- is not linked by any app target yet

`rock-hero-editor` is created as a stub here even though it will be filled in Phase 5; creating
the target now means the Phase 5 extraction moves code into an existing target rather than
needing to create new build infrastructure at the same time as moving code.

### Phase 3: Rename `rock-hero-audio` to `rock-hero-audio-engine`

Mechanical rename across the repo:

- folder, CMake target, namespace, include path
- update consumers in `rock-hero-ui` and `apps/rock-hero-editor`
- update any references in `docs/design/`

No code moves between libraries in this phase. This is a one-time noisy change that should land in
a single PR.

### Phase 4: Rename `rock-hero-ui` to `rock-hero-editor-ui`

Mechanical rename. After this phase the editor-only nature of the library is visible in the name;
the remaining work is extracting the headless workflow that should not live in the UI target.

### Phase 5: Extract `rock-hero-editor` from `rock-hero-editor-ui`

Move headless editor workflow into the (already-created) `rock-hero-editor` library.
`EditorController` is the main candidate. Move along with it:

- workflow / orchestration types (open, import, save, save-as, publish, close, exit)
- unsaved-change policy
- editor session and selection state that does not draw
- editor command application and any command-history precursors

Update `rock-hero-editor-ui` to depend on `rock-hero-editor` and consume its types. The dependency
points one way: UI depends on editor, never the reverse. Side effects requested of the UI flow
through explicit intents or project-owned ports.

This phase is where the testability win lands. Tests for `rock-hero-editor` should link only
`rock_hero_editor` (plus core / audio-engine fakes as needed) and should not require JUCE GUI.

### Phase 6: Review `.rhp` workspace state placement

Decide whether `.rhp` project workspace persistence is editor-owned, core-owned, or split. The
likely answer: package primitives (`.rock`) stay in core; editor workspace state (window layout,
last-opened project, dirty flag policy) lives in `rock-hero-editor`. Confirm before moving any
existing code.

### Phase 7: Fill stubs as code arrives

The stubs created in Phase 2 are filled as real code lands. There is no required order — each
fills when its first content is needed:

- `rock-hero-juce-ui` fills when the first shared JUCE widget appears (likely candidates: plugin
  host window, settings dialog).
- `rock-hero-game` fills when scoring, note matching, or calibration math is implemented.
- `rock-hero-dsp` fills when pitch or onset detection (or any other non-engine DSP) is
  implemented.
- `rock-hero-game-ui` fills when bgfx integration and gameplay rendering begin.

Each first-content change in a stub may also be the change that introduces the stub's framework
dependency (bgfx, SDL, JUCE GUI, JUCE DSP).

## Acceptance Criteria

The migration is successful when:

- all eight libraries exist under `libs/` with consistent target names, namespaces, and include
  paths, including the stubs that have not yet been filled
- editor-only code no longer lives in a library with a shared-sounding name
- the editor's headless workflow can be unit-tested without linking JUCE GUI
- `rock-hero-audio-engine` contains the runtime audio engine only, not editor workflow or
  presentation policy
- shared JUCE widgets (once they exist) live in a library whose name advertises the framework
  binding
- game-only code, when it lands, has a clear home outside the editor and shared modules
- app targets remain thin composition roots and link only the libraries they actually consume
- each library can be tested at the lowest practical dependency level
- no library depends on a sibling product module
- target names, namespaces, and include paths all communicate the same ownership model

## Open Questions

- Should each stub library carry a short `README.md` describing what it will hold and what it
  must not contain? This is the strongest code-placement signpost beyond the folder/target/
  namespace pattern, especially for AI-assisted code placement. Default leaning: yes, one short
  README per stub, removed or replaced once the stub fills out.
- Should temporary namespace or target aliases (e.g., `rock_hero::audio` aliasing
  `rock_hero::audio_engine`) be allowed during Phase 3 / Phase 4 renames, or should the renames
  land as single atomic PRs with no shims? Default leaning: no shims; one-shot renames.
- Is `.rhp` workspace persistence fully editor-owned, or are some primitives (paths, recent
  projects) shared enough to live in core? Decide in Phase 5.
- When `rock-hero-juce-ui` is created, should the editor's existing shared widget candidates (if
  any) move at the same time, or only the new shared widget that triggered creation?
- Should the editor app's `apps/rock-hero-editor/` and the game app's `apps/rock-hero/` be
  renamed for symmetry (e.g., `apps/rock-hero-game/`), or kept as they are?
- For game-side JUCE windows that are not shared (game-only settings flavor, for example), do
  they live in `rock-hero-juce-ui` despite being game-only, or does a small `rock-hero-game-ui`
  JUCE subset emerge? Default leaning: keep them in `rock-hero-juce-ui` until a real reason to
  split appears.