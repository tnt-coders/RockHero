# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this
repository.

## Bootstrap Rules

At the first non-trivial repository task in a coding-agent session, read this `CLAUDE.md` and apply
its project guidance unless it conflicts with higher-priority runtime instructions. Reuse that
context for later tasks in the same session instead of re-reading this file every turn.

Read the relevant design documents listed under [Reference Documents](#reference-documents) when a
task makes architecture, layering, coding-style, testing, or documentation decisions that require
them. Re-read only the document(s) needed for the current decision if the prior context may be
stale, the file changed, or the task raises a design question not already covered by current
context.

These documents are the source of truth for architectural and stylistic decisions. Do not rely on
inference from surrounding code when one of them speaks to the question directly.

Trivial renames, formatting-only edits, factual answers, status checks, and narrow mechanical
follow-ups should not trigger the full documentation bootstrap.

## Coding-Agent Efficiency Baseline

For routine coding tasks, minimize context-heavy output and batch verification. This baseline
binds Mythos-class models at full strength; other Claude models adjust it per
[Model Calibration](#model-calibration):

- Use `rg -n` first, then read only focused line ranges around relevant matches.
- Avoid full-file reads of large files and full-repository diffs unless explicitly reviewing or
  diagnosing an issue that needs them.
- Prefer `git diff --stat`, `git diff --name-only`, and path-scoped diffs before broader diffs.
- Batch edits before verification instead of running build/test after every small change.
- Run quiet targeted tests once the behavioral change is complete; use full logs only after failure.
- Do not reconfigure CMake unless CMake files, target source lists, generated build graph inputs, or
  stale-build errors require it.
- Keep progress updates brief for routine edits, reporting only meaningful findings or blockers.

## Design Quality Bar (No Shortcuts)

This bar binds every model, including Mythos, and overrides any efficiency rule it conflicts with.

Within the scope of what you are building, always choose the cleanest correct design for the shipped
code — never a lesser design chosen to save yourself work. "It was faster to write," "to avoid a new
file," or "to dodge a CMake / build-list edit" are never acceptable reasons to cut a corner. If the
clean design needs a new source file, a build-list edit, an interface change, or a small refactor of
the code you are already touching, do that. A clean design that needs a source-list change *is* a
determinate reason to edit CMake; the "don't reconfigure CMake" rule forbids reflexive reconfigures,
not the build wiring a correct design requires.

This governs the delivered artifact, not agent process: the context-economy rules still bound how
much you read and verify, and this is not license to widen scope or bundle unrelated cleanups (keep
the minimal *scope*, but the cleanest *design* within it). Be economical in process; never economical
in the design of what you ship. When the cleanest design is materially more work or risk, do it
anyway for small deltas; for large ones, surface the tradeoff to the user rather than silently taking
the lesser option.

## Model Calibration

The [Coding-Agent Efficiency Baseline](#coding-agent-efficiency-baseline) above is written for
Mythos-class Claude models (Fable / Mythos), which apply it at full strength, exactly as written,
with none of the extra scaffolding below. A session driven by any other Claude model (Opus,
Sonnet, Haiku, or any future non-Mythos model) keeps every environment, build, and safety rule in
this file but works under the adjusted profile in this section. Task-scoped subagents follow
their task brief regardless of model. Non-Claude agents follow their own harness file
(`AGENTS.md`) and ignore this section. The runtime environment names the active model.

### Depth over Agent Speed

Optimize for cleanliness, correctness, and runtime performance of the delivered code — never for
finishing the response sooner. Agent wall-clock time and token spend are cheap; a rushed or
shallow solution is expensive. Keep digging until the root cause or the full design context is
understood before implementing, and do not settle for the first workable patch when a cleaner
design is within reach.

### Loosened Context Economy

Loosen the baseline's context-economy rules wherever they risk a wrong or shallow edit:

- Read whole files, full path-scoped diffs, and complete design documents whenever a partial read
  leaves uncertainty about invariants, callers, or conventions.
- Re-read the relevant `docs/design/*.md` document before any architecture, layering, or
  convention decision, even if it was consulted earlier in the session.
- Verify in smaller batches when a change is subtle or spans layers; building or testing mid-task
  to confirm a hypothesis is acceptable.
- Before reporting a task complete, diff every touched file and check the result against each
  requirement in the request.

The environment rules are not loosened: build only through `.agents/rockhero-build.ps1`, never
reconfigure CMake without a determinate reason, and keep quiet output as the first resort.

### Existing Libraries over Hand-Rolled Algorithms

Before implementing any nontrivial or well-known algorithm from scratch, check what the project
already ships: the C++23 standard library, JUCE and Tracktion Engine
(`external/tracktion_engine/`), and the Conan dependencies in `conanfile.txt`. Prefer an
existing, tested implementation. When nothing available fits, propose adding a suitable library
to the user rather than silently rewriting complex known algorithms (DSP, parsing, containers,
concurrency primitives); hand-roll only trivial logic, or with user approval.

### Full-Coverage Rule

Never drop a user request, especially one sent while work is already in progress:

1. When a message contains multiple requests — and again whenever a new message arrives mid-task
   — enumerate every distinct request into a tracked checklist (the session task list when
   available, otherwise an explicit checklist restated in the response).
2. Treat mid-task messages as additional obligations, never as background commentary; fold their
   points into the checklist before resuming the interrupted work.
3. After a context compaction, rebuild the checklist from the summary plus the latest user
   messages before continuing.
4. Before ending a turn, re-read every user message received since the last completed response
   and confirm each request is done, answered, or explicitly deferred with a stated reason.
   Silently skipping a point is a defect, exactly like a failing test.

### Parallel Instances for Hard Requests

For complex, multi-part, or design-sensitive requests, spawning a few parallel general-purpose
subagent instances — same brief for independent drafts, or split draft/critique briefs — and
reconciling their outputs into a single answer is pre-authorized; the user accepts the extra
usage and latency. Reserve this for genuinely hard problems (architecture decisions, difficult
debugging, multi-constraint designs), not routine edits.

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

Separately, **`docs/developer/`** is the developer guide: plain-language concept introductions, area
tours (the 2D timeline views, the shared 3D highway, the game side), a design-pattern catalog
with code exemplars, and procedural checklists for common changes (new editor action, port
method, UI view, package-format field), with `docs/developer/index.md` as the hub. It is NOT a source of truth for rules — the design
documents above always win — but consult its recipe checklists when making one of the changes
they cover, especially the "silent steps" lists of touchpoints that produce no compile error when
forgotten. `CONTRIBUTING.md` at the repository root is the human-facing entry point that links
the tiers together.

## Documentation Maintenance Rules

Planning documents live in three lifecycle buckets under `docs/plans/` — the stages a single
plan passes through:

- **`docs/plans/roadmap/`** — The maintained product roadmap. `00-roadmap.md` and any plan
  currently being executed are kept aligned with reality; unstarted plans may lag but must be
  re-verified against the current code before execution begins. See
  `docs/plans/roadmap/00-roadmap.md` for ordering, gates, and open decisions.

- **`docs/plans/todo/`** — Deferred plans for work that may happen at some unknown future point.
  Do not keep these continuously synchronized with routine code or design changes. Treat them as
  plans that may be stale until the user chooses to implement that specific plan; at that point,
  re-read the current code and design docs and revise the plan as needed before using it.

- **`docs/plans/in-progress/`** — Plans for work the user is actively engaged in now. Keep these
  aligned with the user's current direction for that active work, and update them when the plan
  itself changes. Routine code edits made while executing the plan do not require touching the
  doc — the doc captures intent, not implementation state.

Separately, **`docs/tracking/`** holds standing registries that never complete — they are not
plans and do not move through the lifecycle above. Two files live there and are kept current:
`watch-items.md` (accepted-for-now issues, each with a trigger that graduates it to action — you
*monitor* these) and `backlog.md` (small concrete fixes to *do* when there is time). A small
fix belongs in `backlog.md`, not a `docs/plans/todo/` plan file; substantial multi-step work
belongs in a `docs/plans/todo/` plan, not the backlog. Before folding an item from anywhere into
either file, re-verify its claims against the current code — a stale registry is worse than none.

Keep `docs/design/` documents aligned with implemented architecture and durable project decisions.
Before making any significant rule or architecture change in `docs/design/`, confirm with the user
that they intend to change the design rather than merely make a local implementation adjustment.

`docs/developer/` is maintained like `docs/design/`: if a change touches any file, function, or step
the developer guide names, update the guide in the same change set.

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
cmake --preset relwithdebinfo                     # Configure optimized build with debug info, no LTO
cmake --build --preset relwithdebinfo             # Build RelWithDebInfo
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

### Agent-Run Builds

Coding agents run builds, tests, and clang-tidy themselves through the helper at
`.agents/rockhero-build.ps1` (usage in `.agents/README.md`). The helper uses
CLion's bundled CMake for configure and runs Ninja through Visual Studio's developer environment,
which keeps agent builds from breaking CLion's include paths — do not configure or build through
other CMake/compiler environments. Batch verification after coherent edit groups rather than
building after every small change, keep the quiet default output, and pass `-Configure` only
after CMake graph changes or stale-Ninja errors. Run build and tests as separate invocations,
each only when there is a determinate reason for that specific check (code changed → build;
behavior changed → tests) — never as a reflexive bundle.

clang-tidy is **on-demand only**: the whole-project `run-clang-tidy` target is slow and saturates
the machine while it runs, so run it only when the user explicitly asks. Do not run it as part of
routine post-change verification, even after a lint-relevant edit. Ship code that follows the
naming and style rules in this file so an eventual clang-tidy pass stays clean, but leave the
invocation to the user.

## Architecture

```text
RockHero/
  rock-hero-common/
    core/                   - Shared headless domain and package behavior
    audio/                  - Shared audio ports plus Tracktion/JUCE implementation
    ui/                     - Shared UI only when both products need it
  rock-hero-editor/
    app/                    - Editor executable startup
    core/                   - Editor-specific headless workflow and policy
    audio/                  - Editor-specific audio behavior outside the shared engine
    ui/                     - Editor-specific JUCE presentation
  rock-hero-game/
    app/                    - Game executable startup and resources
    core/                   - Game-specific pure gameplay behavior
    audio/                  - Game-specific audio analysis and gameplay plumbing
    ui/                     - Game-specific presentation and rendering
  docs/                     - Design docs, developer guide, user docs, plans, Doxygen config
  external/tracktion_engine/ - Git submodule: Tracktion Engine + JUCE 8
  project-config/           - Git submodule: CMake presets, Conan 2.x, Doxygen theme, lint
```

Each product-scope library exposes a matching nested namespace and include path, such as
`rock-hero-editor/ui` to `rock_hero::editor::ui` to `<rock_hero/editor/ui/*.h>`. CMake target IDs
stay underscore-separated, with aliases using the same product-scope shape such as
`rock_hero::editor::ui`.

Key files:
- **`rock-hero-common/audio/include/rock_hero/common/audio/engine/engine.h`** /
  **`rock-hero-common/audio/src/`** - Tracktion isolation; Tracktion API calls live in the engine
  per-port TUs and `src/tracktion/` adapter units, with `engine.cpp` as the assembly file
- **`rock-hero-common/core/include/rock_hero/common/core/`** - `Song`, `Arrangement` types +
  format serialization, grouped into `song/`, `timeline/`, `package/`, `session/`, and `shared/`
  folders; headless code may use narrow JUCE core utilities
- **`rock-hero-editor/core/include/rock_hero/editor/core/`** - Headless editor workflow
- **`rock-hero-editor/ui/include/rock_hero/editor/ui/`** - Editor JUCE components
- **`rock-hero-editor/app/`** - editor executable entry point
- **`rock-hero-game/app/`** - game executable entry point and packaged resources
- **`build/debug/`**, **`build/release/`** - generated build artifacts; do not edit

Dependency rules: `common` code must not depend on `editor` or `game` code. Product libraries may
depend on `common`, but not on each other. `rock-hero-common/core` may use narrow `juce_core`
utilities for package, file, JSON, ZIP, string, and result-handling behavior while remaining
headless and automated-testable. Tracktion headers stay isolated to `rock-hero-common/audio`
implementation files. Architecture and layering decisions should remain aligned with
`docs/design/architecture.md` and
`docs/design/architectural-principles.md`, especially around dependency boundaries, adapter design,
framework isolation, and automated-testable structure.
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

`ctest --preset debug` runs the per-library Catch2 unit suites (common core/audio/ui, game core,
editor core/ui), each registered with `catch_discover_tests`. There are no whole-application
end-to-end tests yet. Separately, for tooling changes under `project-config/cmake-conan/`, run the
pytest suite from that directory with `pytest -rA`. Add new tests close to the code they validate,
following existing Catch2 `test_*.cpp` naming for C++ tests and `test_*.py` naming for Python tests.

## Commit & Pull Request Guidelines

Commit subjects use short, imperative form (e.g., `Added CI workflow`,
`Repositioned build badge in README`): concise, capitalized, and focused on one change. Follow the
subject with a blank line and a descriptive body explaining what changed and why; prefer a body
over a bare one-line message for anything beyond a trivial change. Pull requests should describe
the user-visible impact, list build/test commands run, and link the relevant issue. Include
screenshots only for documentation or UI-facing changes.

## CI

GitHub Actions runs pre-commit checks, CMake/Conan build+test, static analysis, and Doxygen doc
generation. A release job (for `v*` tags) requires all other jobs to pass first.
