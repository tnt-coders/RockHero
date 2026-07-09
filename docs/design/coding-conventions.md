\page design_coding_conventions Coding Conventions

# Coding Conventions Scope

This document defines project-owned C++ coding rules that are not fully captured by
`.clang-format` or `.clang-tidy`.

These rules apply to:

- `rock-hero-common/`
- `rock-hero-editor/`
- `rock-hero-game/`
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

- `Arrangement` / `ArrangementView`
- `Clip` / `ClipView`

Do not introduce alternate suffixes such as `Row`, `Lane`, or `Display` for these direct
data-to-view pairs unless the type represents a genuinely different concept from the underlying
data object.

The underlying `readability-identifier-naming` checker uses the value name `camelBack` for
function and method identifiers. In project documentation, refer to that style as `camelCase`.

## Free-Function Verbs: Build vs Look Up

Free functions that produce or retrieve a value follow a small, deliberate grammar. Pick the form
by what the function *does*, not by the type it returns — several of these return a value, so the
return category alone does not decide the name.

- **`make…` / `create…`** — build a composite value or object from its inputs. This includes
  view-state and other projection builders that assemble a render model from a domain model, such
  as `makePluginViewState(PluginChainEntry)`, `makeToneTrackViewState(...)`, and
  `makeMeasureGrid(GpScore)`. Prefer `make…` for pure value assembly; reserve `create…` for
  builders that allocate, own, or have side effects (`createToneRackInstance`, `createFileSink`).
- **`…For(key)`** — retrieve the value that corresponds to a logical *identity* or key, such as
  `toneNameFor(arrangement, ref)` or `projectGridNoteValueFor(project_id)`. Use it for lookups,
  not for composite construction: a scalar name lookup is `toneNameFor`, but building a whole
  view-state is `makeToneTrackViewState`, not `toneTrackViewStateFor`.
- **`…At(position)`** — retrieve the value located at a position in a coordinate space (time,
  measure/beat, pixel, index): `secondsAt(beat)`, `timeSignatureAt(measure)`,
  `toneRegionIdAt(time)`, `hitAt(point)`.
- **`…Of(x)`** — retrieve a value defined by a relationship to `x`: `indexOf`, `placementOf`.

Do not prefix a pure derivation with `get`. Reserve `get…` for a stored-field accessor or a
side-effecting retrieval such as a get-or-create (`getOrCreatePluginFor`).

## Listener Naming

Prefer the scoped name `Listener` for a type that exposes one clear notification surface. Existing
names such as `ITransport::Listener`, `ArrangementView::Listener`, and
`TransportControls::Listener` are acceptable because the owning type supplies the missing context.

As listener APIs evolve, consider a more specific nested name such as `StatusListener`,
`ClickListener`, or `InteractionListener` if an owning type grows multiple independent listener
contracts or if the generic name starts to obscure cadence, threading, ownership, or event
semantics.

Do not rename listener interfaces only to make every callback name maximally specific. The point is
to preserve clarity as contracts grow, not to preemptively rename scoped listener types that are
already clear.

## Listener Interfaces vs Observer Structs

Two notification shapes are in deliberate use; pick by contract, not habit:

- A **nested `Listener` interface** (registered through `ScopedListener`) for multi-event,
  multi-subscriber contracts on ports and views — `ITransport::Listener`,
  `SignalChainView::Listener`.
- An **observer struct of `std::function` members** for single-consumer callback bundles
  installed by one owner — the engine's `PluginEditObserver` and its siblings.

Do not add a listener-list mechanism for a contract with exactly one consumer, and do not grow a
`std::function` struct into an ad hoc multi-subscriber registry; switch shapes when the contract
crosses that line.

## View And Component Suffixes

Use a two-tier suffix policy for project-owned UI component types:

- `*View` names a feature's top-level presentation component (`SignalChainView`,
  `AudioDeviceSettingsView`).
- `*Panel`, `*Controls`, `*Overlay`, `*Meter`, and `*Window` name parts and hosts: sub-components
  inside a view, transient surfaces, and window shells (`SignalChainPanel`, `TransportControls`,
  `BusyOverlay`, `AudioLevelMeter`, `PluginBrowserWindow`).

Keep the tier visible when refactoring: do not promote a part suffix to `View` or demote a
feature's top-level component to a part suffix. The suffix should keep telling the reader which
tier they are looking at.

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
void seek(common::core::TimePosition position);
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
using namespace rock_hero::common::core;
\endcode

Production `.cpp` files should usually define code inside the namespace that owns the
implementation.
Module-local test `.cpp` files should also wrap their tests in the namespace of the module under
test. This keeps tests close to the code they verify and avoids treating same-module tests like
external consumers.

# Switch Case Blocks

Use braces around every non-empty `switch` case body. This keeps local declarations scoped to the
case that owns them and avoids visually inconsistent switches where only declaration-heavy cases
have blocks. Indent `case` labels one level inside the `switch` body and place the opening brace
on the line after the `case` label, matching the project's normal brace style:

\code{.cpp}
switch (decision)
{
    case Decision::Accept:
    {
        applyDecision();
        break;
    }
    case Decision::Cancel:
    {
        resetPrompt();
        break;
    }
}
\endcode

`clang-format` enforces the case-label indentation and brace placement for scoped case blocks,
but it does not add missing braces. Add the braces when writing or touching switch cases.

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
`arrangement_view_state.h` rather than also defining `ArrangementViewState`.

This is a default, not an absolute rule. Small private helper types that are tightly coupled to
one owning public type may still live in the same header when splitting them would make the API
harder to read rather than easier.

# Feature Role Vocabulary

Editor features are built from a small set of named roles. The suffix tells the reader what kind
of type it is; the feature folder tells the reader where it belongs (placement rules live in
\ref design_architectural_principles).

## Two-Tier Controller Rule

A feature gets its own `Controller` / `I*View` / `ViewState` triad if and only if it owns a modal
window with its own apply/cancel lifecycle. Audio-device settings and input calibration are the
current examples: the dialog owns a transactional sub-session, so it earns a sub-controller.

Everything hosted in the main editor window is a root-facade feature:

- its intents land on `IEditorController`
- its policy, when nontrivial, is a headless `*Workflow` or `*State` type
- its render data is a `*ViewState` slice inside the aggregate `EditorViewState`

Do not add per-view controllers for main-window features.

## Role Subsets Are Expected

Features legitimately own different subsets of the roles (workflow/state, view state, projection
modules, widgets, controller triad). Transport has a view-state slice and a widget but no
workflow, because its policy is trivial; busy has state, workflow, view state, and an overlay.
The absence of a role is not a gap to fill. Add a role when behavior demands it, not for
symmetry.

## Projection Modules

Pure presentation math and formatting live in free-function headers named
`<feature>_geometry.h`, `<feature>_text.h`, or `<feature>_layout.h` — for example
`tempo_grid_geometry.h`, `transport_readout_text.h`, `signal_chain_block_layout.h`. They hold
free functions plus small result structs, with no state and no side effects, so they stay
headless and directly testable.

Projection modules live with their feature and stay private to the library until a consumer
outside the library exists.

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
Arrangement* currentArrangement() noexcept;
void seek(TimePosition position);
\endcode

Pass larger or non-trivially-copyable read-only inputs by `const&`:

\code{.cpp}
bool hasAsset(const AudioAsset& asset) const;
bool containsArrangement(const Arrangement& arrangement) const;
\endcode

When a function stores or takes ownership of a value, pass it by value and move it into storage:

\code{.cpp}
bool loadSong(Song song, std::size_t selected_arrangement);
\endcode

The corresponding implementation should make the ownership transfer explicit:

\code{.cpp}
m_song = std::move(song);
m_current_arrangement_index = selected_arrangement;
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

# Recoverable Error Returns

Use `std::expected<T, DomainError>` for project-owned operations that can fail with a recoverable
reason the caller may need to display, log, test, or branch on.

Use these result shapes:

- `[[nodiscard]] std::expected<T, DomainError>` for operations with a success payload
- `[[nodiscard]] std::expected<void, DomainError>` for commands with no success payload
- `std::optional<T>` only for true absence, nullable state, cache miss, or lookup miss
- `[[nodiscard]] std::expected<std::optional<T>, DomainError>` when absence is valid but
  retrieval or parsing can fail

Do not use:

- `bool` as an operation failure result
- `void` for setters or commands that can fail to persist, restore, route, open, or otherwise
  affect user-visible state
- `std::optional<Error>` as an operation result. A result or snapshot struct may carry
  `std::optional<Error>` as a payload field only when another field is the operation status
  discriminant.
- `std::optional<T>` plus `error_code` or `error_message` out-parameters
- bare `std::string` as an error channel

`std::string` is allowed as the diagnostic `message` field inside a typed error, or as final
rendered UI text after a typed error has reached the presentation boundary. Private helpers should
use the owning domain error type. If no owning type exists, add a small domain error rather than
returning `std::string`.

Each public failure domain should own:

- an enum named `<Subject>ErrorCode`
- a value type named `<Subject>Error`
- a `code` member for stable program behavior
- a `message` member for UI display or logs

The enum code is the stable contract. Tests should normally assert `error.code`; assert exact
message text only when the message itself is part of the behavior under test.

Mark public error value types `[[nodiscard]]`. Do not define `operator==` for error value types
unless the error has structured fields that genuinely need equality. Callers should compare
`error.code` explicitly so diagnostic message text does not accidentally become program behavior.

Error value types should use constructors for default and contextual messages:

\code{.cpp}
enum class ProjectErrorCode
{
    MissingProjectPackage,
};

struct [[nodiscard]] ProjectError
{
    ProjectErrorCode code{};
    std::string message;

    explicit ProjectError(ProjectErrorCode error_code);
    ProjectError(ProjectErrorCode error_code, std::string message_text);
};
\endcode

Failure returns should construct the domain error directly at the return site and rely on class
template argument deduction for `std::unexpected` unless deduction would be incorrect:

\code{.cpp}
return std::unexpected{ProjectError{
    ProjectErrorCode::MissingWorkspace,
    "Project workspace does not exist",
}};

return std::unexpected{ProjectError{ProjectErrorCode::MissingProjectDocument}};
\endcode

Propagate same-domain failures by moving the existing error value:

\code{.cpp}
return std::unexpected{std::move(result.error())};
\endcode

Translate cross-domain failures explicitly at the boundary:

\code{.cpp}
return std::unexpected{ProjectError{
    ProjectErrorCode::CouldNotPublishSong,
    package_result.error().message,
}};
\endcode

Cross-domain translation should usually expose the receiving API's coarser operation-level code
and preserve lower-level detail in the message. For example, a project publish failure should
return `ProjectErrorCode::CouldNotPublishSong` rather than leaking every native package or archive
failure mode through the project API. Add nested causes or mirrored code sets only after callers
demonstrably need to branch on the lower-level reason across that boundary.

Lower-level reusable helpers may have their own error domains when the lower-level operation is a
real public boundary. For example, archive helpers report `ArchiveError`, while native song
package APIs translate archive failures into `SongPackageError` before returning to package
callers.

Do not add helper functions that only wrap `std::unexpected` construction. Keep helpers only when
they perform real policy or cross-domain translation.

Add structured fields only when callers need the context programmatically. Otherwise, keep runtime
context in `message` so the error type stays simple.

Use `[[nodiscard]]` on public functions returning `std::expected`, including virtual interface
methods and concrete API members. Private helpers should also use `[[nodiscard]]` when ignoring
the result would be a likely bug.

Bare error enums are acceptable only when every failure reason maps cleanly to one fixed message
and no runtime context is useful. This should be uncommon for public boundaries.

Do not introduce a global project-wide error enum, `AnyError` variant, or polymorphic error
hierarchy unless cross-layer propagation becomes a demonstrated maintenance problem. Prefer small
domain-owned errors near the API that returns them.

Catch specific third-party or standard-library exception types at adapter and IO boundaries when
practical, then convert them to the owning project error type before returning across a
project-owned API. Choose the error code that names the project operation that failed, and include
the external exception text in a project-owned message prefix when it is useful for diagnosis.
Use catch-all handlers only at broad cleanup or boundary points where the operation can fail in
implementation-defined ways.

`std::expected<std::optional<T>, Error>` is acceptable when absence is a valid result but
retrieving or parsing that result can still fail. Use it sparingly and prefer a named result type
if the double wrapper starts to obscure the API.

Explicit discards of error-returning calls are acceptable only in destructor-only cleanup, in
best-effort rollback after a primary error has already been captured, or through a named
best-effort helper with a comment or log explaining why the failure cannot be returned. All other
error-returning results must be handled, propagated, reported, or intentionally routed through
that helper.

# Catch2 Assertions

Use `REQUIRE` only when the assertion is a precondition for the rest of the test case. Typical
examples are pointer checks before dereference and container-size checks before indexing.

Use `CHECK` for independent behavior and value observations. This lets one failing test case report
all mismatched fields that are still safe to evaluate.

Example:

\code{.cpp}
REQUIRE(session.arrangements().size() == 2);
CHECK(session.arrangements()[0].part == Part::Lead);
CHECK(session.arrangements()[1].part == Part::Bass);
\endcode

Use the `_FALSE` variants with the same rule: `REQUIRE_FALSE` for fatal preconditions and
`CHECK_FALSE` for independent observations.

# Test CTAD

In test code, prefer class template argument deduction for short-lived expected values when the
initializer already states the semantic type and deduction is unambiguous and correct. This keeps
assertions focused on behavior instead of repeating template arguments that are already visible in
the wrapped value.

Examples:

\code{.cpp}
CHECK(
    audio.last_audio_asset ==
    std::optional{common::core::AudioAsset{std::filesystem::path{"mix.wav"}}});
CHECK(loaded_duration == std::optional{common::core::TimeDuration{4.0}});
\endcode

Keep explicit template arguments when CTAD would deduce the wrong type, when constructing an empty
wrapper, or when the wrapper type itself is the contract under test.

Example:

\code{.cpp}
CHECK(error_message == std::optional<std::string>{"Could not open project"});
\endcode

Do not rely on CTAD for string-literal optionals unless the intended stored type really is a
pointer.

# Test File Organization

Split test files by the first meaningful subject under test rather than by arbitrary size or by
mirroring production file names mechanically.

Examples:

- `test_transport.cpp` for transport-port behavior
- `test_song_audio.cpp` for song-audio port behavior
- `test_engine.cpp` for concrete engine adapter behavior
- `test_session.cpp`, `test_song.cpp`, `test_arrangement.cpp` for core model types once each
  subject has enough tests to stand on its own

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

- module tag first: `[audio]`, `[core]`, `[ui]`
- subject tag second: `[transport]`, `[audio]`, `[edit]`, `[engine]`, `[session]`, `[song]`,
  `[arrangement]`, `[editor-controller]`
- optional classification tags last when needed: for example `[integration]`

Examples:

\code{.cpp}
TEST_CASE("ITransport seek accepts a timeline position value", "[audio][transport]")
TEST_CASE("Replacing missing arrangement audio fails cleanly", "[core][session]")
TEST_CASE("Engine audio port updates state synchronously", "[audio][engine][integration]")
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
