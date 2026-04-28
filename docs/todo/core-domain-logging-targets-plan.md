# Core Domain And Logging Targets

Status: proposed. Revisit before adding the first project-wide logging facade or before
splitting the current `rock_hero_core` target.

## Goal

Keep the three main project areas (`core`, `audio`, `ui`) while making the `core` area explicit
about two different responsibilities:

- pure Rock Hero domain code
- project-wide diagnostics and logging infrastructure

The desired consumer experience is still simple: normal targets link `rock_hero_core` and receive
both the domain library and logging facade. Tests and tools that need a pure dependency can link
`rock_hero_core_domain` directly.

## Proposed Target Shape

```text
rock_hero_core_domain
    Pure standard-C++ Rock Hero domain model, rules, validation, timing, and scoring.

rock_hero_core_logging
    Project-owned RH_LOG_* facade, real-time logging facade, backend configuration, and selected
    Conan logging-library binding.

rock_hero_core
    INTERFACE umbrella target that links rock_hero_core_domain and rock_hero_core_logging.
```

The dependency direction should be:

```text
rock_hero_core_domain  -> standard C++ and approved pure data/format dependencies only
rock_hero_core_logging -> chosen logging backend, optionally rock_hero_core_domain value types
rock_hero_core         -> rock_hero_core_domain + rock_hero_core_logging
```

`rock_hero_core_domain` must not depend on `rock_hero_core_logging`.

## Why Not Put Logging Directly In Domain

Logging is runtime infrastructure. It can involve global configuration, sinks, files, stderr,
backend threads, flushing, shutdown ordering, allocations, blocking behavior, and drop policies.
Those are not domain-model concerns.

Keeping logging in a sibling core-area target preserves:

- deterministic domain tests
- domain code that is easy to run without side effects
- a narrow place to swap Quill, spdlog, xtr, or another backend
- a clear real-time logging policy for audio-thread diagnostics
- simple linking for normal consumers through the `rock_hero_core` umbrella

## Folder Layout

Keep the current library area, but make submodule ownership visible:

```text
libs/rock-hero-core/
  CMakeLists.txt
  include/rock_hero/core/
    domain/
      song.h
      chart.h
      arrangement.h
      ...
    logging/
      log.h
      rt_log.h
  src/
    domain/
      ...
    logging/
      log.cpp
      rt_log.cpp
  tests/
    domain/
      ...
    logging/
      ...
```

The exact `src/domain/` move can be deferred if it creates noisy churn. The important first step is
the target boundary, not necessarily moving every source file at once.

Public includes should mirror the target split:

```cpp
#include <rock_hero/core/domain/song.h>
#include <rock_hero/core/logging/log.h>
```

The current flat domain headers under `<rock_hero/core/*.h>` can remain temporarily as forwarding
headers if that keeps the migration mechanical and easy to review.

## Public API Sketch

The logging facade should expose project-owned calls/macros only:

```cpp
RH_LOG_TRACE("Transport position {}", position.seconds);
RH_LOG_DEBUG("Loaded edit {}", edit_name);
RH_LOG_INFO("Loaded track {}", path);
RH_LOG_WARN("Plugin scan skipped: {}", reason);
RH_LOG_ERROR("Session replaceTrackAsset failed after audio load");
```

Audio-thread diagnostics should use a separate API with explicit best-effort semantics:

```cpp
RH_RT_LOG_TRY(rock_hero::core::logging::RtEvent::TransportDrift, expected, actual);
```

The real-time path should be non-blocking and drop-allowed. It should not support arbitrary
string construction on the audio thread unless that behavior is explicitly proven safe.

## Macro Policy

Use project-owned macros at call sites, not third-party macros directly.

Reasons:

- preserve file, line, and function metadata
- avoid evaluating disabled log calls where the backend supports it
- allow compile-time stripping of trace/debug calls
- isolate backend-specific macro mechanics
- keep Quill/spdlog/xtr replacement localized to `rock_hero_core_logging`

No project-owned production code should call backend macros such as `LOG_INFO`, `SPDLOG_INFO`,
`QUILL_LOG_INFO`, or `XTR_LOG` directly outside the logging implementation.

## Backend Choice

The current preferred backend candidate is Quill because it balances low-latency design,
maintainability, current Conan availability, and cross-platform expectations better than the
alternatives.

The facade should still avoid baking in Quill concepts at call sites. If later testing favors
spdlog for maturity or xtr for real-time instrumentation, the swap should mostly affect
`rock_hero_core_logging` and dependency configuration.

## CMake Plan

1. Rename the current concrete core target to `rock_hero_core_domain`.
2. Add `rock_hero_core_logging` as a separate static or object-backed target.
3. Recreate `rock_hero_core` as an `INTERFACE` umbrella target:

```cmake
add_library(rock_hero_core INTERFACE)

target_link_libraries(
    rock_hero_core
    INTERFACE
        rock_hero_core_domain
        rock_hero_core_logging)
```

4. Keep normal consumers linked against `rock_hero_core`.
5. Link pure domain tests against `rock_hero_core_domain`.
6. Link logging facade tests against `rock_hero_core_logging`.

If an executable or test explicitly wants no logging runtime, it should link
`rock_hero_core_domain` instead of the umbrella target.

## Migration Plan

1. Add this target split without changing behavior.
2. Move existing core sources and public headers into the `domain` submodule paths.
3. Add an initially minimal `rock_hero_core_logging` target.
4. Add no-op or standard-library-backed logging facade first if a backend decision is not final.
5. Add the selected Conan logger to `conanfile.txt`.
6. Bind the selected logger inside `rock_hero_core_logging`.
7. Replace any ad hoc `std::clog` or backend-specific logging calls with `RH_LOG_*`.
8. Remove temporary flat forwarding headers once all consumers use the domain include path.
9. Add `RH_RT_LOG_TRY` only when there is a concrete audio-thread instrumentation need.
10. Update design docs once the split is implemented, not while it is only proposed.

## Tests To Add

- Domain tests link and pass with only `rock_hero_core_domain`.
- Logging tests prove disabled log calls do not evaluate message arguments when supported by the
  facade contract.
- Logging tests prove the facade forwards severity and message text to a test sink.
- Real-time logging tests, when added, prove full queues drop rather than block.
- CMake verification proves consumers linking `rock_hero_core` receive both domain and logging
  includes.

## Rules For Domain Code

Domain code should return errors, diagnostics, or status values rather than logging directly.

Example:

```cpp
const auto result = validateChart(chart);
if (!result.ok()) {
    RH_LOG_ERROR("Chart validation failed: {}", result.message());
}
```

This keeps domain logic deterministic while still allowing callers to log important failures.

Exceptions can be considered only for cases where the log statement itself is part of an explicit
diagnostic contract and cannot reasonably live at the boundary.

## Open Questions

- Should `rock_hero_core_logging` be static, object, or header-only plus implementation target?
- Should the first backend be Quill immediately, or should the first pass add a no-op facade and
  defer backend selection?
- Should `RH_RT_LOG_TRY` emit typed events only, or should it allow a constrained format string API?
- Should `rock_hero_core` remain the only target linked by apps/audio/ui, or should some targets
  intentionally link narrower dependencies for build hygiene?

## Acceptance Criteria

The split is complete when:

- `rock_hero_core_domain` builds without any logging backend dependency.
- `rock_hero_core_logging` owns all project logging macros and backend integration.
- `rock_hero_core` is an umbrella target that links both subtargets.
- Existing audio, UI, and app consumers can still link `rock_hero_core`.
- Pure domain tests can link `rock_hero_core_domain` and run without logging setup.
- No production code outside `rock_hero_core_logging` uses third-party logging macros directly.
