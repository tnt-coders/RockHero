# Repository Guidelines

## Codex Bootstrap Rules
- At the start of each non-trivial task, read `CLAUDE.md` and apply its project guidance unless it conflicts with higher-priority runtime instructions.
- Treat files in `.claude/agents/*.md` as task-specific playbooks that Codex may apply directly when relevant.
- Current playbooks:
- `.claude/agents/tracktion-engine-expert.md` for Tracktion/JUCE integration, audio-thread safety, engine architecture, and related CMake/submodule work.
- `.claude/agents/doxygen-audio-docs.md` for Doxygen comment authoring/review in audio and engine-facing C++ code.
- These playbooks are guidance artifacts, not executable agent definitions in Codex.

## Project Structure & Module Organization
`apps/` contains executables (`apps/rock-hero-editor`, `apps/rock-hero-game`). `libs/` contains shared static libraries (`libs/audio-engine`, `libs/song`). `docs/` holds Doxygen inputs such as `Doxyfile.in`. Third-party source submodules live under `external/` (`external/tracktion_engine`). `project-config/` remains a root-level submodule providing shared CMake presets, Conan integration, docs theming, and lint targets; its vendored `cmake-conan/` subtree carries its own pytest suite. Root build outputs go to `build/debug` and `build/release` via presets.

## Build, Test, and Development Commands
Initialize submodules before configuring:

```sh
git submodule sync --recursive
git submodule update --init --recursive
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Use `cmake --preset release` and `cmake --build --preset release` for optimized builds. Generate docs with `cmake --build build/debug --target docs` when Doxygen is installed. Run static analysis with `cmake --build build/debug --target clang-tidy` or `clang-tidy-fix`. Apply formatting with `pre-commit run --all-files`.

## Coding Style & Naming Conventions
The project uses C++23. Formatting is enforced by `.clang-format` (Microsoft base style, 4-space indentation, 100-column limit, left-aligned pointers). Naming comes from `.clang-tidy`: types use `CamelCase`, functions/methods use `camelCase`, namespaces and local variables use `lower_case`, private/protected member fields use `m_lower_case`, and macros use `UPPER_CASE`. Keep CMake formatted with `cmake-format` through pre-commit.

## Testing Guidelines
There are no root application tests registered yet, so `ctest` is mainly a guard for future coverage. For tooling changes under `project-config/cmake-conan/`, run the pytest suite from that directory with `pytest -rA`. Add new tests close to the code they validate and follow existing `test_*.py` naming for Python tests.

## Commit & Pull Request Guidelines
Recent commits use short, imperative subjects such as `Added CI workflow` and `Repositioned build badge in README`. Keep subjects concise, capitalized, and focused on one change. Pull requests should describe the user-visible impact, list build/test commands run, and link the relevant issue. Include screenshots only for documentation or UI-facing changes.

## Configuration Notes
Conan is wired through `CMakePresets.json` and `conanfile.txt`. Avoid editing generated `build/` artifacts. If you touch preset or submodule wiring, verify a fresh configure from the repository root.
