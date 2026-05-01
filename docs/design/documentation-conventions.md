\page design_documentation_conventions Documentation Conventions

# Scope

This document defines the canonical documentation, comment, and Doxygen style for project-owned
code in Rock Hero.

These rules apply to:

- `apps/`
- `libs/`
- other project-owned source files unless a file is generated or third-party

These rules do not apply to:

- `external/`
- generated files
- vendored third-party content

# General Rules

- Keep the entire rendered comment line at 100 characters or fewer, including indentation and
  comment markers.
- Do not rely on `clang-format` to wrap comments. The formatter preserves comment text so it does
  not rewrite Doxygen blocks into a style that conflicts with this document. Authors and automated
  agents must manually wrap new or edited comments before committing.
- Wrap as late as reasonably possible; do not leave comments prematurely short when they fit
  cleanly on the current line.
- Preserve intentional blank lines between comment paragraphs or sections.
- Normal non-Doxygen comments follow the same 100-character limit.

# CMake Comment Coverage

Project-owned CMake files should be well commented.

This applies to:

- root `CMakeLists.txt`
- module `CMakeLists.txt`
- test `CMakeLists.txt`
- project-owned `.cmake` helper files

Comments in CMake should explain:

- target purpose
- why a dependency is public or private when that is not obvious
- why a compile definition, wrapper target, generated file, or packaging step exists
- any non-obvious build-system constraint, policy, or workaround

Do not add mechanical comments that merely restate the command name. Prefer short comments that
explain intent and architectural role.

Examples:

\code{.cmake}
# Depend on project-owned wrapper aliases rather than raw JUCE/Tracktion module targets.
target_link_libraries(
    rock_hero_audio
    PUBLIC rock_hero::core
    PRIVATE rock_hero::tracktion_core rock_hero::tracktion_engine rock_hero::tracktion_graph)
\endcode

\code{.cmake}
# Copy the canonical fixture into the build tree so the test target owns its runtime inputs.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test-data/drum_loop.wav"
    "${CMAKE_CURRENT_BINARY_DIR}/test-data/drum_loop.wav"
    COPYONLY)
\endcode

# Doxygen Scope

- Document all project-owned headers with Doxygen.
- Do not use Doxygen comments in `.cpp` files while Doxygen extraction is header-focused.
- Convert `.cpp` Doxygen comments into regular comments.
- Convert Doxygen comments on private class members or private methods into regular comments.
- Use explicit `\brief` fields rather than relying on Doxygen autobrief behavior.

# Header Doxygen Coverage

- Document every project-owned header declaration that is visible outside a single `.cpp` file:
  classes, structs, enums, public or protected methods, public data members, free functions, and
  aliases whose purpose is not immediately obvious.
- When documenting an enum, document every enumerator with a `\brief`. Doxygen warns on
  undocumented enumerators of a documented enum, so partial coverage is not allowed.
- Use regular comments for private header members when they carry non-obvious invariants, ownership,
  threading, caching, or lifecycle rules.

# Source Comment Coverage

- Every project-owned function or method definition in a `.cpp` file gets a regular comment
  immediately above it unless it is explicitly trivial.
- Trivial exceptions are defaulted or empty special members with no meaningful behavior, obvious
  one-line local lambdas, generated macro entrypoints such as `START_JUCE_APPLICATION`, and test
  helpers whose names and assertions already explain the purpose.
- `.cpp` method comments should explain why the method exists, lifecycle timing, ownership or
  lifetime effects, framework callback role, threading or realtime constraints, user-visible
  behavior, failure behavior, or why a temporary implementation is acceptable.
- Test `.cpp` files follow the same regular-comment rule. Each `TEST_CASE` should have a concise
  comment immediately above it explaining the behavior, invariant, or regression it verifies.
  Test helper classes, member functions, free functions, and member variables should be commented
  to the same level as production `.cpp` code when they carry test behavior, captured state, fake
  behavior, or non-obvious setup semantics. Keep these as regular comments, not Doxygen comments.
- Avoid mechanical restatements. Prefer comments that add intent:

\code{.cpp}
// Provides JUCE with the generated project name used for app metadata and windows.
const juce::String getApplicationName() override;
\endcode

- For inline implementation comments, comment non-obvious blocks or lines. If it is questionable
  whether something might need clarification, add a useful comment rather than leaving behavior
  potentially unclear.
- Avoid useless comments only when they are clearly useless; bias toward documentation whenever
  clarity is not certain.

# Doxygen Block Format

Project-owned Doxygen comments must use `/*! ... */` comments without leading `*` prefixes.
If a Doxygen comment contains only a `\brief`, keep the entire comment on one line when it fits
within the 100-character line limit:

\code{.cpp}
/*! \brief Reports whether the transport is currently playing. */
\endcode

The full Doxygen block must be indented to the same level as the declaration it documents. Every
line inside the block uses that same indentation, including `\brief`, body paragraphs, tags, and
the closing `*/`. Use a multi-line block when the comment has a body paragraph, `\param`,
`\return`, `\throws`, `\see`, `\note`, or when the single-line form would exceed 100 characters.

Example for a top-level declaration:

\code{.cpp}
/*!
\brief Reads the current transport position for render-cadence cursor drawing.

Like state(), this method is a live message-thread-only read and is not thread-safe. Listener
callbacks report only TransportState changes and do not carry position. UI code that needs smooth
cursor motion should call position() at its own render cadence.

\return Current transport position.
*/
[[nodiscard]] virtual core::TimePosition position() const noexcept = 0;
\endcode

Example for an indented member declaration:

\code{.cpp}
    /*!
    \brief Creates the concrete editor view and installs the initial track thumbnail.

    \param controller Controller that receives all user intents emitted by this view.
    \param transport Read-only transport used by the cursor overlay for live position reads.
    \param thumbnail_factory Factory used immediately to create the initial row thumbnail.
    */
    EditorView(
        IEditorController& controller, const audio::ITransport& transport,
        audio::IThumbnailFactory& thumbnail_factory);
\endcode

# Doxygen Commands

Use backslash commands, not at-sign commands.

Preferred commands include:

- `\file`
- `\brief`
- `\param`
- `\tparam`
- `\return`
- `\throws`
- `\see`
- `\note`

# Required Fields

Document all applicable fields.

- Use `\param` for every parameter of every documented function. Partial parameter documentation
  is not allowed and is enforced by `WARN_NO_PARAMDOC = YES`.
- Use `\tparam` for every template parameter of every documented template. Same enforcement.
- Use `\return` for every documented non-void function. Enforced by `WARN_NO_PARAMDOC = YES`,
  which warns on any documented function missing its return documentation regardless of how
  obvious the return type is.
- Use `\throws` when exceptions are part of the contract.
- Use `\note` for important usage constraints, thread-safety rules, lifetime caveats, or temporary
  implementation shims.

If a field does not apply, omit it.

# Blank-Line Rules

- If a Doxygen block has a verbose description, keep a blank line between `\brief` and the body.
- If a Doxygen block has no verbose description, do not insert a blank line between `\brief` and
  the next Doxygen command.
- If a Doxygen comment has only a `\brief`, use the single-line form unless the line would exceed
  100 characters.
- Keep blank lines between logically separate paragraphs when a description spans multiple sections.

# File Headers

File-level Doxygen headers should include the file name explicitly:

\code{.cpp}
/*!
\file editor_view.h
\brief Concrete JUCE editor view that renders editor state and emits editor intents.
*/
\endcode

Use this pattern for project-owned public headers when a file-level Doxygen header is warranted.
