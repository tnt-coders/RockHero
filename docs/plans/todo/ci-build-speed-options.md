# CI Build Speed Options

Status: deferred. Revisit only if CI time becomes painful enough to justify build-system or
workflow complexity.

## Why This Exists

CI currently builds Rock Hero from source on fresh runners. JUCE and Tracktion are integrated as
the `external/tracktion_engine` submodule, and Rock Hero wraps the required JUCE and Tracktion
modules into project-owned static targets in `cmake/RockHeroExternalModules.cmake`.

That wrapper approach avoids recompiling the same third-party module sources once per first-party
consumer target inside a single build. It does not, by itself, help a cold CI runner. Every new CI
run still has to compile the external wrapper targets unless another cache or prebuilt package is
introduced.

The current build time is acceptable. This document records options to consider later, in roughly
the order they should be tried.

## Measure Before Changing

Before optimizing, collect enough timing data to know which phase is actually slow:

- dependency install
- Conan install/package resolution
- CMake configure/generate
- JUCE helper generation such as `juceaide`
- third-party compilation
- first-party compilation
- link time
- clang-tidy
- docs generation

Useful evidence:

- CI job timing per step
- Ninja output showing long-running compile and link edges
- separate timings for Debug, Release, lint, docs, and release packaging jobs
- whether the slow portion is compile time or final link time

Do not assume Tracktion/JUCE compilation is the bottleneck if Release link time with LTO is the
part that dominates.

## Option 1: Add Compiler Object Caching

Use `ccache` or `sccache` in CI and configure CMake with compiler launchers:

```cmake
CMAKE_C_COMPILER_LAUNCHER=ccache
CMAKE_CXX_COMPILER_LAUNCHER=ccache
```

or the equivalent `sccache` launcher.

This is the lowest-risk first optimization because it does not change how JUCE, Tracktion, or Rock
Hero are packaged. It caches compiled objects keyed by compiler command, source content, flags, and
headers.

Cache keys should include:

- runner OS
- compiler family and version
- build type
- relevant CMake preset or toolchain hash
- `external/tracktion_engine` submodule commit
- `CMakeLists.txt`, `cmake/*.cmake`, and `conanfile.txt` hashes

Expected benefit:

- strong improvement for repeated PR builds and small code changes
- useful for JUCE/Tracktion module wrappers, first-party code, and generated helper targets

Tradeoffs:

- cache size can grow quickly
- cache hit rate depends on stable absolute paths, flags, and compiler versions
- GitHub Actions cache eviction can reduce effectiveness

## Option 2: Cache Conan Downloads And Built Dependencies

The current presets use the project-config Conan integration. If the reusable CI workflow is not
already caching Conan's package store, add or improve that cache.

Cache keys should include:

- runner OS
- compiler family and version
- build type
- `conanfile.txt`
- Conan profile contents
- lockfile contents, if lockfiles are introduced

This helps dependencies that already come from Conan, such as Catch2 and libebur128. It does not
avoid compiling JUCE/Tracktion while those remain
submodule source targets.

Expected benefit:

- faster configure/install setup
- less network variability
- reduced time if any Conan dependency falls back to local build

Tradeoffs:

- does not solve Tracktion/JUCE compilation directly
- cache keys must be strict enough to avoid ABI mismatches

## Option 3: Disable LTO For PR Builds

Release link time can become expensive because LTO moves cross-translation-unit optimization into
the linker. Rock Hero currently routes first-party build policy through `rock_hero::lto_flags`,
which delegates to JUCE's recommended LTO flags.

If link time becomes the bottleneck, add a project option such as:

```cmake
option(ROCK_HERO_ENABLE_LTO "Enable link-time optimization for Rock Hero targets" ON)
```

Then include `rock_hero::lto_flags` only when that option is enabled.

Recommended CI split:

- PR builds: Release or Debug without LTO
- master builds: at least one non-LTO fast validation build
- tags/releases: full Release with LTO

Expected benefit:

- large reduction when final executable/test links dominate CI time
- simpler than changing dependency packaging

Tradeoffs:

- PRs no longer validate LTO-specific link behavior
- final release job still needs the slower optimized build
- build-policy docs and presets should be updated together

## Option 4: Narrow Which Targets Each Job Builds

Avoid making every CI job build every target if the job does not need that artifact.

Examples:

- lint should need configure, generated headers, binary-data headers, and compile commands, not a
  full application link
- docs should need configure and Doxygen inputs, not all executables
- core-only checks can build `rock_hero_core_tests` without building app targets
- release packaging can remain the only job that builds install/package targets

Expected benefit:

- avoids unnecessary application links in lint/docs jobs
- keeps feedback faster when a change is isolated to one library

Tradeoffs:

- more CI workflow branching
- risks missing cross-target breakage unless at least one job still builds everything
- reusable workflow support may need to be extended

## Option 5: Improve Matrix Strategy

If the CI matrix grows, keep heavyweight builds limited to the cases that need them.

Possible split:

- Linux Debug: fast test and clang-tidy baseline
- Linux Release without LTO: broad optimized compile check
- macOS/Windows: compile and smoke-test only
- full Release with LTO: master/tags/nightly

Expected benefit:

- keeps PR latency controlled as supported platforms grow

Tradeoffs:

- less identical coverage on every platform for every PR
- requires clear release-gate policy

## Option 6: Prebuild JUCE/Tracktion As A Project-Owned Conan Package

If source compilation of JUCE/Tracktion remains the bottleneck after caching and LTO changes, build
a Rock Hero-specific Conan package that exports the same wrapper targets currently created by
`RockHeroExternalModules.cmake`.

This should not be treated as a generic upstream `tracktion_engine` package unless the package is
made broad enough to support arbitrary consumers. For Rock Hero, the useful artifact is more likely
to be a package such as:

```text
rock-hero-tracktion-modules/<tracktion-submodule-commit>
```

It would package:

- the JUCE module wrapper libraries Rock Hero consumes
- the Tracktion module wrapper libraries Rock Hero consumes
- exported CMake targets matching the existing `rock_hero::juce_*` and `rock_hero::tracktion_*`
  aliases, or a clearly documented replacement namespace
- usage requirements and compile definitions such as `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`, and
  `JUCE_INCLUDE_OGGVORBIS_CODE=0`
- Ogg/Vorbis linkage requirements

Package identity must account for:

- OS
- architecture
- compiler family and version
- C++ standard library and runtime model
- build type
- Xcode SDK/toolchain on macOS
- Tracktion/JUCE submodule commit
- Rock Hero wrapper CMake behavior
- relevant JUCE feature defines
- Ogg/Vorbis package versions

Expected benefit:

- fastest cold CI builds once binaries are available
- avoids rebuilding stable third-party modules on every runner

Tradeoffs:

- more infrastructure and release management
- package recipes must stay synchronized with wrapper-target behavior
- debugging package/consumer ABI mismatches is harder than source builds
- every supported platform/configuration needs a binary or a fallback build path
- license and source-availability obligations must remain clear because JUCE/Tracktion are AGPL/GPL
  in this project configuration

## Option 7: Move Heavy Third-Party Builds To A Scheduled Binary Pipeline

If Conan packaging is adopted, consider a separate workflow that publishes the third-party wrapper
binaries when one of these changes:

- `external/tracktion_engine` submodule commit
- `cmake/RockHeroExternalModules.cmake`
- wrapper-related CMake policy
- compiler matrix
- JUCE feature flags
- Ogg/Vorbis dependency versions

Normal PR CI would consume those binaries. If a binary is missing, CI can either fail early with a
clear message or build from source as a fallback.

Expected benefit:

- keeps PR workflows simple and fast
- makes expensive third-party rebuilds deliberate

Tradeoffs:

- more moving pieces
- contributors may need access to the package remote
- fallback source builds still need to stay working

## Option 8: Cache Build Trees Only With Care

Caching an entire CMake build directory is usually more fragile than compiler-object caching. Build
directories contain absolute paths, generated files, compiler metadata, and CMake state that can go
stale in subtle ways.

Prefer `ccache`/`sccache` and Conan caches first. Only consider build-tree caching for narrow,
well-understood generated artifacts, and only if the cache can be invalidated aggressively.

Expected benefit:

- may avoid repeated generated-helper work in specific cases

Tradeoffs:

- high stale-state risk
- difficult to debug when the cache is wrong
- can mask configuration problems that a clean CI run should catch

## Recommended Escalation Order

1. Measure CI phase timings.
2. Add `ccache` or `sccache`.
3. Ensure Conan downloads and built packages are cached.
4. Add a non-LTO CI Release preset if link time is the bottleneck.
5. Narrow lint/docs/build target scopes.
6. Revisit matrix coverage if platform count grows.
7. Package the JUCE/Tracktion wrapper layer with Conan only if the simpler options are not enough.

The main principle is to avoid introducing a prebuilt third-party binary supply chain until the
project has clear evidence that normal compiler/dependency caching and LTO policy are insufficient.
