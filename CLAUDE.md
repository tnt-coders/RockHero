# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this
repository.

## Project Overview

Rock Hero is an early-stage guitar-driven rhythm game (C++23) where players plug in a real guitar
and play along to songs. It features a 3D note highway and VST plugin support.

## Build Commands

Initialize submodules before the first configure:

```sh
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
    rock-hero-game/         - Game app (note highway, scoring)
  libs/
    rock-hero-audio-engine/ - Tracktion Engine isolation adapter (static library)
    rock-hero-core/         - Song/Chart/Arrangement types + serialization (static library, no JUCE)
    rock-hero-ui/           - JUCE UI components (static library)
  docs/                     - Doxygen configuration
  external/tracktion_engine/ - Git submodule: Tracktion Engine + JUCE 8
  project-config/           - Git submodule: CMake presets, Conan 2.x, Doxygen theme, lint
```

Key files:
- **`libs/rock-hero-audio-engine/include/rock_hero_audio_engine/audio_engine.h`** /
  **`src/audio_engine.cpp`** - Tracktion isolation; all Tracktion API calls live here
- **`libs/rock-hero-core/include/rock_hero_core/`** - `Song`, `Chart`, `Arrangement` types +
  format serialization; standard C++ only
- **`libs/rock-hero-ui/include/rock_hero_ui/`** - Waveform and transport UI components
- **`apps/rock-hero-editor/`** - editor window and app entry point
- **`apps/rock-hero/src/main.cpp`** - minimal game window entry point
- **`build/debug/`**, **`build/release/`** - generated build artifacts; do not edit

Dependency rules: `rock-hero-audio-engine` owns all Tracktion headers. `rock-hero-core` depends
on standard C++ only. Apps may depend on both libraries.

JUCE and Tracktion Engine are integrated as a git submodule (`external/tracktion_engine/`), not via
Conan. Other dependencies are declared in `conanfile.txt` and resolved automatically through
CMake's `find_package` via the `cmake-conan` integration. Each build directory has its own
isolated Conan cache (`build/<preset>/.conan2`).

If you touch preset or submodule wiring, run a fresh configure from the repository root.

## Reference Documents

Before making architectural, testing, or documentation decisions, consult these documents:

- **`docs/ARCHITECTURE.md`** — Full system description: technology stack, two-track design,
  threading model, timing and latency chain, gameplay systems, known risks, and fallback strategy.
  Read this to understand the system shape before adding features or proposing structural changes.

- **`docs/ARCHITECTURAL_PRINCIPLES.md`** — Structural constraints and testability rules. Defines
  library roles, the ports-and-adapters pattern, what belongs in `rock-hero-core` vs. adapters,
  how to treat time and threading, and the decision rules for new code. Consult this whenever
  placing new behavior, designing an interface, or choosing between implementation strategies.

- **`docs/DOCUMENTATION_STYLE.md`** — Doxygen and comment conventions. Defines block format,
  required fields, backslash vs. at-sign commands, and blank-line rules. Follow this when writing
  or reviewing any documentation in public headers.

## Coding Conventions

**Formatting** (`.clang-format`): Microsoft base style, 4-space indentation, 100-column limit,
left-aligned pointers.

**Naming** (`.clang-tidy`):

| Construct | Convention |
|---|---|
| Types, scoped enum values | `CamelCase` |
| Functions, methods | `camelCase` |
| Namespaces, local variables, parameters | `lower_case` |
| Class member fields | `m_lower_case` |
| Classic enum values, macros | `UPPER_CASE` |

Clang-tidy treats warnings as errors.

## CI

GitHub Actions runs pre-commit checks, CMake/Conan build+test, static analysis, and Doxygen doc
generation. A release job (for `v*` tags) requires all other jobs to pass first.
