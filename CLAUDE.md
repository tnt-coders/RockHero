# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this
repository.

## Bootstrap Rules

At the start of each non-trivial task in this repository, before proposing changes or writing
code:

1. Read this `CLAUDE.md` and apply its project guidance unless it conflicts with higher-priority
   runtime instructions.
2. Read the four design documents listed under [Reference Documents](#reference-documents) and
   make design, refactoring, layering, coding-style, testing, and documentation decisions with
   those documents in mind unless a higher-priority runtime instruction overrides them.

These documents are the source of truth for architectural and stylistic decisions. Do not rely on
inference from surrounding code when one of them speaks to the question directly.

A "non-trivial task" is anything beyond a one-line typo fix, a trivial rename, or answering a
factual question that needs no code change. When in doubt, read the docs.

## Project Overview

Rock Hero is an early-stage guitar-driven rhythm game (C++23) where players plug in a real guitar
and play along to songs. It features a 3D note highway and VST plugin support.

## Reference Documents

Consult these documents per the [Bootstrap Rules](#bootstrap-rules) above:

- **`docs/design/architecture.md`** — Full system description: technology stack, two-track design,
  threading model, timing and latency chain, gameplay systems, known risks, and fallback strategy.
  Read this to understand the system shape before adding features or proposing structural changes.

- **`docs/design/architectural-principles.md`** — Structural constraints and testability rules.
  Defines module roles, the ports-and-adapters pattern, what belongs in `rock-hero-core` vs.
  adapters, how to treat time and threading, and the decision rules for new code. Consult this
  whenever placing new behavior, designing an interface, or choosing between implementation
  strategies.

- **`docs/design/coding-conventions.md`** — C++ coding rules that are not fully captured by
  clang-format or clang-tidy, including const correctness, parameter passing, and value-type
  guardrails.

- **`docs/design/documentation-conventions.md`** — Doxygen and comment conventions. Defines block
  format, required fields, backslash vs. at-sign commands, and blank-line rules. Follow this when
  writing or reviewing any documentation in public headers.

`docs/design/index.md` is a table of contents that links the above.

## Build Commands

Initialize submodules before the first configure:

```sh
git submodule sync --recursive
git submodule update --init --recursive
```

```sh
cmake --preset debug                              # Configure debug build
cmake --build --preset debug                      # Build debug
cmake --preset release                            # Configure release build
cmake --build --preset release                    # Build release
ctest --preset debug                              # Run tests
cmake --build build/debug --target docs           # Generate Doxygen docs (requires Doxygen)
cmake --build build/debug --target clang-tidy     # Run static analysis
cmake --build build/debug --target clang-tidy-fix # Auto-fix lint issues
pre-commit run --all-files                        # Apply formatting (clang-format + cmake-format)
```

For tooling changes under `project-config/cmake-conan/`, run its own pytest suite:

```sh
cd project-config/cmake-conan && pytest -rA
```

## Architecture

```text
RockHero/
  apps/
    rock-hero-editor/       - Editor app (load/play controls, waveform display)
    rock-hero/              - Game app (note highway, scoring)
  libs/
    rock-hero-audio/        - Tracktion Engine isolation adapter (static library)
    rock-hero-core/         - Song/Chart/Arrangement types + serialization (static library, no JUCE)
    rock-hero-ui/           - JUCE UI components (static library)
  docs/                     - Design docs and Doxygen configuration
  external/tracktion_engine/ - Git submodule: Tracktion Engine + JUCE 8
  project-config/           - Git submodule: CMake presets, Conan 2.x, Doxygen theme, lint
```

Each library is a module with a matching sub-namespace under `rock_hero` and a matching nested
include path: `rock-hero-audio` ↔ `rock_hero::audio` ↔ `<rock_hero/audio/*.h>`, and so on. CMake
target IDs stay underscore-separated (`rock_hero_audio`, `rock_hero_core`, `rock_hero_ui`).

Key files:
- **`libs/rock-hero-audio/include/rock_hero/audio/engine.h`** /
  **`src/engine.cpp`** - Tracktion isolation; all Tracktion API calls live here
- **`libs/rock-hero-core/include/rock_hero/core/`** - `Song`, `Chart`, `Arrangement` types +
  format serialization; standard C++ only
- **`libs/rock-hero-ui/include/rock_hero/ui/`** - Waveform and transport UI components
- **`apps/rock-hero-editor/`** - editor window and app entry point
- **`apps/rock-hero/main.cpp`** - minimal game window entry point
- **`build/debug/`**, **`build/release/`** - generated build artifacts; do not edit

Dependency rules: `rock-hero-audio` owns all Tracktion headers. `rock-hero-core` depends on
standard C++ only. Apps may depend on both libraries. Architecture and layering decisions should
remain aligned with `docs/design/architecture.md` and `docs/design/architectural-principles.md`,
especially around dependency boundaries, adapter design, framework isolation, and
automated-testable structure.

JUCE and Tracktion Engine are integrated as a git submodule (`external/tracktion_engine/`), not via
Conan. Other dependencies are declared in `conanfile.txt` and resolved automatically through
CMake's `find_package` via the `cmake-conan` integration. Each build directory has its own
isolated Conan cache (`build/<preset>/.conan2`).

If you touch preset or submodule wiring, verify a fresh configure from the repository root.

## Coding Conventions

**Formatting** (`.clang-format`): Microsoft base style, 4-space indentation, 100-column limit,
left-aligned pointers. Keep CMake formatted with `cmake-format` through pre-commit.

**Naming** (`.clang-tidy`):

| Construct | Convention |
|---|---|
| Types, scoped enum values | `CamelCase` |
| Functions, methods | `camelCase` |
| Namespaces, local variables, parameters | `lower_case` |
| Class member fields | `m_lower_case` |
| Classic enum values, macros | `UPPER_CASE` |

In `readability-identifier-naming`, clang-tidy spells the function/method style as `camelBack`.
That checker value corresponds to the project convention described here as `camelCase`.

Clang-tidy treats warnings as errors.

Coding-style decisions that are not fully enforced by `.clang-format` or `.clang-tidy` are defined
in `docs/design/coding-conventions.md`. Follow that document for const correctness, parameter
passing, and value-type guardrails. Comment and Doxygen conventions are defined in
`docs/design/documentation-conventions.md`. Follow that document for all project-owned comment
formatting and documentation decisions.

## Documentation Scope

Reassess documentation scope whenever a subsystem grows from a single header/source pair into
multiple cooperating classes or headers, when a subsystem gains nontrivial internal invariants
around threading, ownership, caching, lifecycle, or synchronization, or when a new subsystem is
added that is likely to need internal architecture docs. Analyze the codebase first. If it is
still clearly unnecessary, do not ask. Only ask whether to expand Doxygen coverage when the value
of broader internal generated documentation becomes genuinely uncertain.

## Testing

There are no root application tests registered yet, so `ctest` is mainly a guard for future
coverage. For tooling changes under `project-config/cmake-conan/`, run the pytest suite from that
directory with `pytest -rA`. Add new tests close to the code they validate and follow existing
`test_*.py` naming for Python tests.

## Commit & Pull Request Guidelines

Commit subjects use short, imperative form (e.g., `Added CI workflow`,
`Repositioned build badge in README`). Keep subjects concise, capitalized, and focused on one
change. Pull requests should describe the user-visible impact, list build/test commands run, and
link the relevant issue. Include screenshots only for documentation or UI-facing changes.

## CI

GitHub Actions runs pre-commit checks, CMake/Conan build+test, static analysis, and Doxygen doc
generation. A release job (for `v*` tags) requires all other jobs to pass first.