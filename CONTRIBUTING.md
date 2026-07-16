# Contributing to Rock Hero

Welcome! This page gets you from a fresh clone to a reviewable change. It is the first of three
documentation tiers:

1. **This file** — build, test, and submit.
2. **The [Developer Guide](docs/developer/index.md)** — plain-language introductions to the core
   concepts (ports, the audio engine, editor actions, view state, undo) plus step-by-step
   checklists for the most common kinds of change, framed so nothing is left half-wired.
3. **The [design documents](docs/design/index.md)** — the binding rules for architecture,
   structure, coding style, and documentation. When anything disagrees, these win.

New to the codebase? Read the Developer Guide's Core Concepts and its
[Anatomy of an Editor Action](docs/developer/anatomy-of-an-editor-action.md) walkthrough first — the
codebase is built from one repeating pattern, and that page teaches it.

## Quick start

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
- **Static analysis**: clang-tidy is *on-demand*, not part of pre-commit — it is slow and
  saturates the machine. Write to the naming/style conventions (see `CLAUDE.md` and
  `docs/design/coding-conventions.md`) so an eventual pass stays clean; run
  `cmake --build build/debug --target clang-tidy` only when you need it.
- **Checklists**: if your change matches a Developer Guide recipe (new editor action, port
  method, UI view, package-format field), walk its "silent steps" list — those steps produce no
  compile error when forgotten.
- **Documentation**: public headers carry Doxygen; if you changed any file, function, or step the
  Developer Guide names, update the guide in the same commit.

## Commits and pull requests

- Commit subjects are short, imperative, capitalized, and focused on one change
  (`Added CI workflow`, `Fixed tone-region snap at zero`).
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
