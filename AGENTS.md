# Repository Guidelines

## Codex Bootstrap Rules
- At the first non-trivial repository task in a Codex session, read `CLAUDE.md` and apply its
  project guidance unless it conflicts with higher-priority runtime instructions. Reuse that
  context for later tasks in the same session instead of re-reading it every turn.
- Read `docs/design/architecture.md`, `docs/design/architectural-principles.md`,
  `docs/design/coding-conventions.md`, and `docs/design/documentation-conventions.md` when a task
  makes architecture, layering, coding-style, testing, or documentation decisions that require
  them. Re-read only the relevant document(s) if the prior context may be stale, the file changed,
  or the task raises a design question not already covered by current context.
- Trivial renames, formatting-only edits, factual answers, status checks, and narrow mechanical
  follow-ups should not trigger the full documentation bootstrap.
- Treat files in `.codex/agents/*.toml` as Codex subagent profiles that may be used when
  their specialty is relevant.
- Current Codex subagents:
  - `.codex/agents/tracktion-engine-expert.toml` for Tracktion Engine integration, plugin
    hosting, live input routing, automation, engine architecture, and audio-thread safety.
  - `.codex/agents/juce-expert.toml` for JUCE UI, device management, message-thread behavior,
    plugin-hosting primitives, CMake/module integration, and JUCE best practices.
- These profiles are guidance and delegation configuration for Codex.

## Agent Efficiency Rules
Prefer targeted `rg` queries and focused file reads over broad recursive dumps. Do not re-read
files whose contents are already current in the active session unless they changed or the task needs
fresh context. For build/test verification, prefer the quiet `.codex/skills/rockhero-build/`
helper output; add `-FullOutput` only when diagnosing a failure or when full logs are explicitly
needed.

Minimize context-heavy command output. Prefer `git diff --stat`, `git diff --name-only`, and
path-scoped diffs over full-repository diffs unless the task is explicitly a review or the full diff
is needed to diagnose an issue. Use `rg -n` to find the relevant symbols first, then read only the
smallest practical line range around the match instead of whole large source files.

Batch verification after coherent edit groups instead of running build/test after every small
change. For narrow follow-ups, run `git diff --check` after edits and defer targeted tests until the
behavioral change is complete. Do not reconfigure CMake unless CMake files, target source lists,
generated build graph inputs, or stale-build errors require it. Use full build/test logs only after
a quiet command fails or when the user explicitly asks for detailed output.

If a commit hook fails only because `clang-format` or `cmake-format` rewrote files, inspect and
stage the formatting-only edits, then retry the commit without rerunning tests. The prior test
result remains valid because formatting hooks are mechanical and behavior-preserving.

Keep progress updates brief and less frequent for short tasks. For routine edits, report only the
current action and any meaningful finding or blocker; avoid narrating every small inspection step.

## Documentation Maintenance Rules
Do not keep `docs/todo/` planning documents continuously synchronized with routine code or design
changes. Treat them as deferred implementation plans that may be stale until the user chooses to
implement that specific plan; at that point, re-read the current code and design docs and revise the
plan as needed before using it.

Keep `docs/design/` documents aligned with implemented architecture and durable project decisions.
Before making any significant rule or architecture change in `docs/design/`, confirm with the user
that they intend to change the design rather than merely make a local implementation adjustment.

## Project Structure & Module Organization
Product-scope libraries live at the repository root under `rock-hero-common`, `rock-hero-editor`,
and `rock-hero-game`. Each scope owns `core`, `audio`, and `ui` submodules only when needed, with
matching namespaces and include paths such as `rock_hero::editor::ui` and
`<rock_hero/editor/ui/*.h>`. Executable startup lives under the matching product `app/` folder.
`docs/` holds Doxygen inputs such as `Doxyfile.in`. Third-party source submodules live under
`external/` (`external/tracktion_engine`). `project-config/` remains a root-level submodule
providing shared CMake presets, Conan integration, docs theming, and lint targets; its vendored
`cmake-conan/` subtree carries its own pytest suite. Root build outputs go to `build/debug` and
`build/release` via presets.

## Build, Test, and Development Commands
Initialize submodules before configuring:

```sh
git submodule sync --recursive
git submodule update --init --recursive
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Use `cmake --preset release` and `cmake --build --preset release` for optimized builds, or `cmake --preset relwithdebinfo` and `cmake --build --preset relwithdebinfo` for an optimized build with debug info and no LTO. Generate docs with `cmake --build build/debug --target docs` when Doxygen is installed. Run static analysis with `cmake --build build/debug --target clang-tidy` or `clang-tidy-fix`. Apply formatting with `pre-commit run --all-files`.

### Agent Build Environment Notes
In agent PowerShell environments, `cmake` and `ctest` may not be on `PATH`, and plain
`ninja -C build/debug ...` may fail because MSVC's standard-library include environment is not
loaded. The agent-neutral helper `scripts/rockhero-build.ps1` (usage in `scripts/README.md`)
captures the validated workflow: CLion's bundled CMake for configure and Ninja through Visual
Studio's developer environment. Use the helper rather than reproducing those steps by hand. If
Ninja references deleted or renamed files, reconfigure (`-Configure`) before building.

## Coding Style & Naming Conventions
The project uses C++23. Formatting is enforced by `.clang-format` (Microsoft base style, 4-space indentation, 100-column limit, left-aligned pointers). Naming comes from `.clang-tidy`: types use `CamelCase`, functions/methods use `camelCase`, namespaces and local variables use `lower_case`, private/protected member fields use `m_lower_case`, and macros use `UPPER_CASE`. Keep CMake formatted with `cmake-format` through pre-commit.
Architecture and layering decisions should remain aligned with `docs/design/architecture.md` and
`docs/design/architectural-principles.md`, especially around dependency boundaries, adapter design,
framework isolation, and automated-testable structure.
Coding-style decisions that are not fully enforced by `.clang-format` or `.clang-tidy` are defined
in `docs/design/coding-conventions.md`. Follow that document for const correctness, parameter
passing, and value-type guardrails.
Comment and Doxygen conventions are defined in `docs/design/documentation-conventions.md`. Follow that document
for all project-owned comment formatting and documentation decisions.
Reassess documentation scope whenever a subsystem grows from a single header/source pair into
multiple cooperating classes or headers, when a subsystem gains nontrivial internal invariants
around threading, ownership, caching, lifecycle, or synchronization, or when a new subsystem is
added that is likely to need internal architecture docs. Analyze the codebase first. If it is still
clearly unnecessary, do not ask. Only ask whether to expand Doxygen coverage when the value of
broader internal generated documentation becomes genuinely uncertain.

## Testing Guidelines
There are no root application tests registered yet, so `ctest` is mainly a guard for future coverage. For tooling changes under `project-config/cmake-conan/`, run the pytest suite from that directory with `pytest -rA`. Add new tests close to the code they validate and follow existing `test_*.py` naming for Python tests.

## Commit & Pull Request Guidelines
Recent commits use short, imperative subjects such as `Added CI workflow` and `Repositioned build badge in README`. Keep subjects concise, capitalized, and focused on one change. Pull requests should describe the user-visible impact, list build/test commands run, and link the relevant issue. Include screenshots only for documentation or UI-facing changes.

## Configuration Notes
Conan is wired through `CMakePresets.json` and `conanfile.txt`. Avoid editing generated `build/` artifacts. If you touch preset or submodule wiring, verify a fresh configure from the repository root.
