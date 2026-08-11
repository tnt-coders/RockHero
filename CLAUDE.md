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

## Simplicity Yields Only to Correctness

The first bar, and it binds every task — not just reviews. **Strive extensively for simplicity of
design. The only thing simplicity yields to is correctness.**

The operational test: whenever a fix, a review finding, or a new feature points at a MORE
COMPLICATED path — a new branch, flag, field, parameter, or helper — stop and scrutinize whether
the EXISTING design is simply flawed, and whether a simpler model falls out that DELETES code
instead of adding it. Adding is a signal to re-examine the design, never a conclusion. Prefer
generalizing the one authority that already exists over introducing a second; treat two places
that must agree by hand as a defect rather than a style question; and when only the more complex
shape is correct, say plainly why the simple one is wrong instead of quietly adding.

**And when simplicity does yield to correctness, that yield is itself a finding.** Needing extra
complexity to be correct is evidence that something in the design underneath may be wrong, so it
immediately warrants a deeper look at the core of that design rather than a shrug and a patch.
Ask what shape would have made the complexity unnecessary — a different decomposition, a datum
stored once instead of derived twice, an illegal state made unrepresentable — and if such a shape
exists, say so even when it is larger than the task at hand. The yield is never a licence to stop
looking; it is the moment to look hardest.

A worked example, because the pattern recurs. A slide junction's head printed a raw fret where the
note's onset head printed a harmonic node. The obvious fix added a second node-or-fret decision at
the junction — a copy of the onset's rule. The simpler model instead generalized the ONE existing
function to take the stop being labeled (`tabNoteHeadText(note, fret_at_head)`), so the junction
reuses the rule rather than restating it: one authority, two call sites, net code removed.

## Design Quality Bar (No Shortcuts)

This bar binds every model and overrides any process rule it conflicts with.

Within the scope of what you are building, always choose the cleanest correct design for the shipped
code — never a lesser design chosen to save yourself work. "It was faster to write," "to avoid a new
file," or "to dodge a CMake / build-list edit" are never acceptable reasons to cut a corner. If the
clean design needs a new source file, a build-list edit, an interface change, or a small refactor of
the code you are already touching, do that. A clean design that needs a source-list change *is* a
determinate reason to edit CMake; the "don't reconfigure CMake" rule forbids reflexive reconfigures,
not the build wiring a correct design requires.

This governs the delivered artifact, not agent process: it is not license to widen scope or bundle
unrelated cleanups (keep the minimal *scope*, but the cleanest *design* within it). Context economy
bounds how much you read; it never bounds how thoroughly you verify what you ship. Be economical in
process; never economical in the design of what you ship. When the cleanest design is materially
more work or risk, do it anyway for small deltas; for large ones, surface the tradeoff to the user
rather than silently taking the lesser option.

## Runtime Performance

This bar binds every model, like the Design Quality Bar above.

Cheap agent time never buys slow shipped code. Runtime cost is a design property, so weigh it while
choosing the design rather than promising to tune it later:

- **Deadline paths** — the audio callback and the per-frame render path — carry hard budgets, and
  their constraints (no locks, no allocation, no blocking work on the audio thread) are stated in
  `docs/design/architecture.md`. There the budget is part of correctness, not an optimization.
- **Everything else** — editor workflows, import, serialization — reads for clarity first. Reach for
  a faster shape where input size makes it matter: per-note and per-sample passes over a whole song,
  or repeated rescans of the timeline.

Never claim a performance result from a `debug` build. Measure through
`.agents/rockhero-build.ps1 -Preset relwithdebinfo` (optimized, with debug info and no LTO), which
is a determinate reason to configure that preset. Report what you measured, not what ought to be
faster.

## Agent Process

How to work, as opposed to what to ship. These rules bind every model — they are written to be
correct for any of them, so there is no per-model profile to reconcile. Task-scoped subagents follow
their task brief instead. Non-Claude agents follow their own harness file (`AGENTS.md`) and ignore
this section.

### Depth over Agent Speed

Optimize for cleanliness, correctness, and runtime performance of the delivered code — never for
finishing the response sooner. Agent wall-clock time and token spend are cheap; a rushed or
shallow solution is expensive. Agent speed is the only speed that is cheap — see
[Runtime Performance](#runtime-performance) for the speed that is not. Keep digging until the root
cause or the full design context is understood before implementing, and do not settle for the first
workable patch when a cleaner design is within reach.

### Context Economy

Read economically, and stop reading economically the moment it risks a wrong or shallow edit.
Economy governs what you *read*; it never governs how thoroughly you *verify*.

- Use `rg -n` first and read the focused line ranges around the matches — but read whole files, full
  path-scoped diffs, and complete design documents whenever a partial read leaves uncertainty about
  invariants, callers, or conventions.
- Prefer `git diff --stat`, `git diff --name-only`, and path-scoped diffs before broader diffs.
- Re-read the relevant `docs/design/*.md` document before any architecture, layering, or convention
  decision, even if it was consulted earlier in the session.
- Batch edits before verifying rather than building after every small change — but verify in smaller
  steps when a change is subtle or spans layers, and build or test mid-task whenever it confirms a
  hypothesis.
- Run quiet targeted tests once the behavioral change is complete; use full logs only after failure.
- Do not reconfigure CMake unless CMake files, target source lists, generated build graph inputs, or
  stale-build errors require it.
- Keep progress updates brief for routine edits, reporting only meaningful findings or blockers.
- Before reporting a task complete, diff every touched file and check the result against each
  requirement in the request.

Build only through `.agents/rockhero-build.ps1`, never reconfigure CMake without a determinate
reason, and keep quiet output as the first resort.

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
plans and do not move through the lifecycle above. Two standing registries live there and are
kept current: `watch-items.md` (accepted-for-now issues, each with a trigger that graduates it to
action — you *monitor* these) and `backlog.md` (small concrete fixes to *do* when there is time);
dated review-followup files beside them are snapshots, not registries. A small
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
behavior changed → tests) — never as a reflexive bundle. This targets verification; it never
withholds it, and a subtle or cross-layer change is itself a determinate reason to check in
smaller steps.

clang-tidy is **on-demand only**: the whole-project `run-clang-tidy` target is slow and saturates
the machine while it runs, so run it only when the user explicitly asks. Do not run it as part of
routine post-change verification, even after a lint-relevant edit. Ship code that follows the
naming and style rules in this file so an eventual clang-tidy pass stays clean, but leave the
invocation to the user.

### Local Verification Does Not Prove CI

Local verification is MSVC on the `debug` preset; CI compiles every change with GCC (Linux), Clang
(macOS), and clang-cl (Windows lint), all at Release with `-Werror`. A clean local build and green
local tests are a weaker signal than they look, because these classes cannot be produced locally at
all:

| Blind spot | Local result (MSVC, debug) | Caught by |
|---|---|---|
| `-Wfloat-equal` | not implemented | GCC, Clang, clang-cl |
| `-Wmissing-designated-field-initializers` | omitted aggregate fields accepted | GCC, Clang |
| `-Wshadow` against an *inherited* base member | silent | GCC |
| `-Wunused-function` on internal-linkage helpers | unused anonymous-namespace functions accepted | GCC, Clang |
| `bugprone-unchecked-optional-access` | partial against the MSVC STL, which is worse than silent: a local run reports some sites and misses others in the same sweep (a large Catch2 suite reported none while CI found four), so a clean local run proves nothing | CI lint |
| `bugprone-use-after-move` | zero findings against the MSVC STL | CI lint |
| Release-only undefined behavior | debug timing and layout hide it | CI Release tests |
| `juce::PNGImageFormat::decodeImage` pixel format | honors the file's color type (libpng path) | macOS CI tests — CoreImage cannot make a 24-bit image, so every PNG decodes to ARGB; the file's real alpha state survives only in the `originalImageHadAlpha` property |
| `ModifierKeys::isPopupMenu()` | true for a right press only | macOS CI tests — `popupMenuClickModifier` expands to `rightButton \| ctrl`, so it is also true for Ctrl+left-click |
| Any clang-tidy check on newly written code (`performance-enum-size`, `cppcoreguidelines-pro-bounds-constant-array-index`, `bugprone-branch-clone`, `misc-const-correctness`, `modernize-use-auto`, `modernize-use-std-numbers`, `readability-identifier-naming`) | never reported: lint is on-demand, so no build runs it | CI lint on all three platforms — for the flow-insensitive classes above, one file reproduces CI without the machine-saturating whole-project target: `clang-tidy -p build/release --extra-arg=-Wno-unknown-warning-option --extra-arg=-Wno-unused-command-line-argument <file>`. Clean per-file output is still not proof: the two dataflow checks above stay unreliable against the MSVC compile database |

No local command reports these, so the check is a reading pass over the diff, not another build.
Before reporting a code change complete, re-read every touched hunk for the constructs that trigger
them — `==` or `!=` on a floating-point type (including a *defaulted* `operator==` on a struct with a
float member **of its own**; a defaulted comparison whose float compare happens inside a standard
library header, as `std::optional<double>` or `std::vector<double>` does, is NOT diagnosed, and a
defaulted comparison is only defined at all once it is odr-used, so an unused one hides until the
first test compares it), aggregate initializers, `std::optional` dereferences, a variable used after
`std::move`, a constructor parameter sharing a base member's name, a file-local helper whose last
caller the change removed, and framework calls whose meaning differs per OS — and resolve each hit
per `docs/design/coding-conventions.md`. The table lists what CI has already caught (or a review
caught pre-CI), not everything it can catch; when a CI failure exposes a blind spot missing from
it, add the row in the same fix.

`-Werror` stops at the first diagnostic, so one reported error is a sample, not the population.
Never fix only the line CI named: classify the diagnostic, `rg` the tree for that construct, and fix
every hit in one change — the compiler stopped before it reached the rest, and each round trip costs
a full pipeline run. A sweep fixes one diagnostic class mechanically; it is not license to refactor
what it touches.

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
utilities for package, file, JSON, ZIP, string, and result-handling behavior — plus the
`juce_data_structures` properties-file types behind `shared/settings_file_options.h` — while
remaining headless and automated-testable; headless testability is the test the grant turns on
(see `docs/design/architectural-principles.md`). Tracktion headers stay isolated to `rock-hero-common/audio`
implementation files. Architecture and layering decisions should remain aligned with
`docs/design/architecture.md` and
`docs/design/architectural-principles.md`, especially around dependency boundaries, adapter design,
framework isolation, and automated-testable structure.

Minimize platform-specific code: limit OS-conditional compilation (`#if`/`#ifdef` on `_WIN32`,
`JUCE_WINDOWS`, and the like) and native OS APIs to the absolutely essential — only what provably
cannot be expressed in an OS-agnostic way — reaching for the stack's abstraction layers (JUCE,
SDL3, bgfx, the C++ standard library) first, and keeping any unavoidable guard confined to one seam
with a comment stating why no generic form exists. The full rule is **Minimize Platform-Specific
Code** in `docs/design/architectural-principles.md`, with the mechanical C++ form in
`docs/design/coding-conventions.md`.

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
