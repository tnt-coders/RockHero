# Contributing to Rock Hero

Welcome! This page gets you from a fresh clone to a reviewable change. It is written for both human
contributors and coding agents, and it is the first of three documentation tiers:

1. **This file** — install your tools, build, test, and submit.
2. **The [Developer Guide](docs/developer/index.md)** — plain-language introductions to the core
   concepts (ports, the audio engine, editor actions, view state, undo) plus step-by-step
   checklists for the most common kinds of change, framed so nothing is left half-wired.
3. **The [design documents](docs/design/index.md)** — the binding rules for architecture,
   structure, coding style, and documentation. When anything disagrees, these win.

New to the codebase? Start with the [Developer Guide](docs/developer/index.md): its **Core
Concepts** introduce the moving parts in reading order — ports, the audio engine, editor actions,
view state, undo, the gameplay session, and the package format — each tagged with whether it is
editor-only, game-only, or repo-wide.

Rock Hero is two products that share `rock-hero-common`: the **editor**, which is further along,
and the **game**, which is still in its early stages. The editor is largely organized around one
repeating pattern — the editor-action pipeline — and the
[Anatomy of an Editor Action](docs/developer/anatomy-of-an-editor-action.md) walkthrough traces it
end to end, so it is the best first read if you are working editor-side. The game is younger and
still growing its own patterns and concepts; the guide's
[game page](docs/developer/game-development.md) and its repo-wide
[pattern catalog](docs/developer/design-patterns.md) cover the ground a single editor walkthrough
does not.

## Prerequisites

### Required to build

Install these before the [Quick start](#quick-start) — together they are everything you need for a
working build and a running editor or game:

| Tool | Why you need it | Notes |
|---|---|---|
| **Git** | Clone the repository and fetch submodules | Tracktion Engine and JUCE are vendored under `external/` as a submodule, not fetched by Conan. |
| **A C++23 compiler** | Compile the code | **MSVC (Visual Studio 2022) on Windows is the primary, actively tested toolchain.** Linux (GCC/Clang) and macOS (Clang) also build in CI, but MSVC is the only compiler exercised day to day — others may work but are not guaranteed. |
| **CMake 3.20 or newer** | Configure and drive the build | The presets in `CMakePresets.json` use the Ninja generator. |
| **Ninja** | The build backend the presets select | Bundled with Visual Studio and CLion; install it standalone for command-line builds. |
| **Conan 2.x** | Resolve the third-party dependencies in `conanfile.txt` | Runs automatically during `cmake --preset …` through the cmake-conan integration; each build directory keeps its own cache under `build/<preset>/.conan2`. |
| **Python 3** | Runs Conan (and the contributor tooling below) | Conan installs with `pip install conan`. |

Visual Studio 2022 bundles a C++23-capable MSVC toolchain, CMake, and Ninja in a single install; you
still add Git, Python, and Conan yourself.

### Recommended for pull requests

None of the tools in this section are needed for a **functional build** — you can build, run, and
iterate without them. They are needed to get a change **merged**: CI runs each one, and every check
must pass before a pull request can land. Install them before you open a PR.

| Tool | What it does | How to get it |
|---|---|---|
| **pre-commit** | Runs the formatters and the project-convention check in one step. It fetches and manages **clang-format** and **cmake-format** for you, so you do not install those two separately. | `pip install pre-commit`, then `pre-commit install` to run it automatically on each commit. |
| **clang-tidy** | Static analysis for the naming and style rules. Slow and machine-saturating, so it is on-demand rather than part of pre-commit. | Ships with LLVM/Clang; run it with `cmake --build build/debug --target clang-tidy`. |
| **Doxygen** | Generates the API documentation from header comments. | Install from your package manager; build the docs with `cmake --build build/debug --target docs`. |

## Quick start

With the prerequisites installed, from a fresh clone:

```sh
git submodule sync --recursive
git submodule update --init --recursive

cmake --preset debug            # configure (Conan resolves dependencies automatically)
cmake --build --preset debug    # build
ctest --preset debug            # run the unit-test suites
pre-commit run --all-files      # formatting (clang-format + cmake-format) and conventions
```

`release` and `relwithdebinfo` presets exist alongside `debug`. Generated docs build with
`cmake --build build/debug --target docs` (requires Doxygen).

## Before you push

- **Build and test**: the debug build compiles clean and `ctest --preset debug` passes.
- **Formatting and conventions**: `pre-commit run --all-files` is green (it also enforces file
  placement via `scripts/verify-project-conventions.py`).
- **Static analysis**: run `cmake --build build/debug --target clang-tidy` when you need it —
  it is on-demand (see [Prerequisites](#recommended-for-pull-requests) for why). Write to the
  naming/style conventions (see `CLAUDE.md` and `docs/design/coding-conventions.md`) so a pass
  stays clean.
- **Checklists**: if your change matches a Developer Guide recipe (new editor action, port
  method, UI view, package-format field), walk its "silent steps" list — those steps produce no
  compile error when forgotten.
- **Documentation**: public headers carry Doxygen; if you changed any file, function, or step the
  Developer Guide names, update the guide in the same commit.

## Commits and pull requests

- Start every commit with a short, imperative, capitalized subject line focused on one change
  (`Added CI workflow`, `Fixed tone-region snap at zero`).
- Follow the subject with a blank line and a body that explains **what** changed and **why**. A
  descriptive body is preferred over a bare one-line message for anything beyond a trivial change —
  reviewers and future readers should not have to reconstruct your reasoning from the diff.
- Pull requests describe the user-visible impact, list the build/test commands you ran, and link
  the relevant issue. Include screenshots only for documentation or UI-facing changes.
- CI runs pre-commit, build+test, static analysis, and doc generation; all must pass.

## Repository map

| Path | What lives there |
|---|---|
| `rock-hero-common/` | Code shared by both products (domain model, audio engine, shared UI) |
| `rock-hero-editor/` | The chart-authoring editor |
| `rock-hero-game/` | The playable game |
| `docs/design/` | Binding architecture and convention rules |
| `docs/developer/` | The Developer Guide |
| `docs/plans/` | Product roadmap and plan lifecycle (roadmap / in-progress / todo / completed) |
| `docs/tracking/` | Standing registries: `backlog.md`, `watch-items.md` |
| `external/` | Tracktion Engine + JUCE (git submodule) |
| `project-config/` | CMake presets, Conan integration, lint and Doxygen tooling (submodule) |
