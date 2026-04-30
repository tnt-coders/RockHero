\page design_coding_conventions Coding Conventions

# Coding Conventions Scope

This document defines project-owned C++ coding rules that are not fully captured by
`.clang-format` or `.clang-tidy`.

These rules apply to:

- `apps/`
- `libs/`
- other project-owned C++ source files unless a file is generated or third-party

These rules do not apply to:

- `external/`
- generated files
- vendored third-party content

# Naming Conventions

Naming is enforced by `.clang-tidy` and should be treated as the source of truth for linted code.

The project naming conventions are:

- types and scoped enum values use `CamelCase`
- functions and methods use `camelCase`
- namespaces, local variables, and parameters use `lower_case`
- private and protected member fields use `m_lower_case`
- macros and classic enum values use `UPPER_CASE`

When a project-owned UI type is the presentation counterpart of a project-owned data or domain
type, name the data type after what it is and name the UI type with the same base name plus
`View`.

Examples:

- `Track` / `TrackView`
- `Clip` / `ClipView`

Do not introduce alternate suffixes such as `Row`, `Lane`, or `Display` for these direct
data-to-view pairs unless the type represents a genuinely different concept from the underlying
data object.

The underlying `readability-identifier-naming` checker uses the value name `camelBack` for
function and method identifiers. In project documentation, refer to that style as `camelCase`.

## Listener Naming

Prefer the scoped name `Listener` for a type that exposes one clear notification surface. Existing
names such as `ITransport::Listener`, `TrackView::Listener`, and `TransportControls::Listener` are
acceptable because the owning type supplies the missing context.

As listener APIs evolve, consider a more specific nested name such as `StatusListener`,
`ClickListener`, or `InteractionListener` if an owning type grows multiple independent listener
contracts or if the generic name starts to obscure cadence, threading, ownership, or event
semantics.

Do not rename listener interfaces only to make every callback name maximally specific. The point is
to preserve clarity as contracts grow, not to preemptively rename scoped listener types that are
already clear.

# Const Correctness

Use `const` wherever practical by default.

Apply `const` to:

- local variables that are not reassigned after initialization
- member functions that do not mutate observable object state
- references and pointer pointees used for read-only access
- range-loop bindings that should not mutate the iterated element

Treat clang-tidy's const-correctness checks as required style, not optional cleanup. When editing
code, declare touched locals `const` at the point of introduction whenever later code only observes
them. Before finishing a change, scan new and modified functions for variables that
`misc-const-correctness` would flag, including test fixtures and RAII harness objects that are
constructed for setup/teardown but then accessed only through const operations.

Do not add top-level `const` to by-value parameters in public declarations. It is not part of the
function type and adds noise to the interface:

\code{.cpp}
void seek(core::TimePosition position);
\endcode

Prefer the same spelling in definitions unless a local implementation detail is made clearer by
adding `const` there.

Use `const T*` for read-only pointed-to objects and use references when null is not a valid state.
Avoid `T* const`, `const T* const`, and similar top-level const pointer spellings in public
interfaces because they are uncommon and easy to misread.

Inside function bodies, use top-level const on pointer variables only when pointer rebinding would
make the local logic harder to reason about. Do not use `const T* const` as a blanket default.

# Namespace Use

Do not use broad namespace directives for project namespaces:

\code{.cpp}
using namespace rock_hero;
using namespace rock_hero::core;
\endcode

Production `.cpp` files should usually define code inside the namespace that owns the implementation.
Module-local test `.cpp` files should also wrap their tests in the namespace of the module under
test. This keeps tests close to the code they verify and avoids treating same-module tests like
external consumers.

# Public Header Organization

Prefer one primary project-owned type per public header under `include/rock_hero/...` when that
type is intended to be referenced directly outside a single implementation file.

This applies especially to:

- public structs and classes used across multiple modules or layers
- view-state and controller-contract types
- port and adapter interfaces

Benefits:

- the type is easy to locate from its name
- Doxygen output stays easier to navigate
- headers stay focused instead of becoming catch-all model bundles

Prefer an owning header to include related type headers rather than redefining several externally
meaningful types in one file. For example, `editor_view_state.h` should include
`track_view_state.h` rather than also defining `TrackViewState`.

This is a default, not an absolute rule. Small private helper types that are tightly coupled to
one owning public type may still live in the same header when splitting them would make the API
harder to read rather than easier.

# Parameter Passing

Use the following rules for function parameters.

Pass by value when the type is:

- a fundamental type
- an enum
- a pointer
- `std::nullptr_t`
- a small trivially copyable project-owned value type

A small project-owned value type means `sizeof(T) <= 16` and
`std::is_trivially_copyable_v<T>`.

Examples:

\code{.cpp}
Track* findTrack(TrackId id) noexcept;
void seek(TimePosition position);
\endcode

Pass larger or non-trivially-copyable read-only inputs by `const&`:

\code{.cpp}
bool hasAsset(const AudioAsset& asset) const;
bool containsTrack(const Track& track) const;
\endcode

When a function stores or takes ownership of a value, pass it by value and move it into storage:

\code{.cpp}
TrackId addTrack(std::string name, std::optional<AudioAsset> audio_asset);
bool commitTrackAudioAsset(TrackId id, AudioAsset audio_asset, TimeRange timeline_range);
\endcode

The corresponding implementation should make the ownership transfer explicit:

\code{.cpp}
track.name = std::move(name);
track.audio_asset = std::move(audio_asset);
\endcode

# Value Type Guardrails

Project-owned value wrappers that are intended to be passed by value should stay small and
trivially copyable. Add `static_assert` checks when a type's cheap-copy assumption is important to
an interface:

\code{.cpp}
static_assert(sizeof(TimePosition) <= 16);
static_assert(std::is_trivially_copyable_v<TimePosition>);
\endcode

If a value type grows beyond these limits, revisit call sites and prefer `const&` for read-only
parameters.

# Catch2 Assertions

Use `REQUIRE` only when the assertion is a precondition for the rest of the test case. Typical
examples are pointer checks before dereference and container-size checks before indexing.

Use `CHECK` for independent behavior and value observations. This lets one failing test case report
all mismatched fields that are still safe to evaluate.

Example:

\code{.cpp}
REQUIRE(session.tracks().size() == 2);
CHECK(session.tracks()[0].name == "Full Mix");
CHECK(session.tracks()[1].name == "Guitar Stem");
\endcode

Use the `_FALSE` variants with the same rule: `REQUIRE_FALSE` for fatal preconditions and
`CHECK_FALSE` for independent observations.

# Test File Organization

Split test files by the first meaningful subject under test rather than by arbitrary size or by
mirroring production file names mechanically.

Examples:

- `test_transport.cpp` for transport-port behavior
- `test_edit.cpp` for edit-port behavior
- `test_engine.cpp` for concrete engine adapter behavior
- `test_session.cpp`, `test_song.cpp`, `test_chart.cpp`, `test_arrangement.cpp` for core model
  types once each subject has enough tests to stand on its own

Prefer keeping related tests together while the subject is still small. Split into separate test
files when one of these becomes true:

- the file stops being easy to scan quickly
- the subject grows distinct invariants or behaviors
- the tests are better described by a narrower file name

Do not split files purely to create one test file per production header if that makes the test
layout more fragmented than the behavior warrants.

# Catch2 Test Name Length

Keep every `TEST_CASE` name at **78 characters or fewer**.

This is a hard limit, not a style preference. CMake's `catch_discover_tests` enumerates tests by
running the test binary with Catch2's `--list-tests` reporter and parsing its output. In the
non-TTY environment used during build-time discovery, Catch2 falls back to an 80-column console
width and word-wraps any test name whose rendered line exceeds it. Each wrapped fragment then
becomes its own `add_test()` registration, so a single `TEST_CASE` ends up split across two or
more entries that all show as skipped in CTest and IDE test runners. Catch2 prefixes test names
with two spaces of indentation in this listing, leaving a 78-character budget for the name
itself.

The limit applies to the literal test-name string only. Tags, fixtures, and the `TEST_CASE`
macro syntax around the name do not count against it.

If the descriptive name does not fit in 78 characters, prefer rewriting it to be tighter rather
than carrying the wrap risk forward. Names should still convey behavior, not just shorten for
the sake of fitting.

# Catch2 Tag Conventions

Every `TEST_CASE` should carry tags that describe both the owning module and the immediate subject
under test.

Current tag shape:

- module tag first: `[audio]`, `[core]`
- subject tag second: `[transport]`, `[edit]`, `[engine]`, `[session]`, `[song]`,
  `[chart]`, `[arrangement]`
- optional classification tags last when needed: for example `[integration]`

Examples:

\code{.cpp}
TEST_CASE("ITransport seek accepts a timeline position value", "[audio][transport]")
TEST_CASE("Replacing a missing track asset fails cleanly", "[core][session]")
TEST_CASE("Engine edit updates state synchronously", "[audio][engine][integration]")
\endcode

Guidelines:

- use the same subject tag across all tests for the same subsystem or type
- add `[integration]` only when a test exercises a real adapter or otherwise runs materially
  slower than normal fast unit-style coverage
- do not invent multiple near-synonyms for the same subject (`[transport]` vs.
  `[i_transport]` vs. `[playback]`)
- prefer stable tags that will still make sense after minor refactors

Tags are used for ad hoc filtering and future tooling integration, so consistency matters more
than perfect taxonomy.
