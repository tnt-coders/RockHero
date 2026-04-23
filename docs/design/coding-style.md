\page design_coding_style Coding Style

# Coding Style Scope

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

# Const Correctness

Use `const` wherever practical by default.

Apply `const` to:

- local variables that are not reassigned after initialization
- member functions that do not mutate observable object state
- references and pointer pointees used for read-only access
- range-loop bindings that should not mutate the iterated element

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
bool replaceTrackAsset(TrackId id, AudioAsset audio_asset);
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
