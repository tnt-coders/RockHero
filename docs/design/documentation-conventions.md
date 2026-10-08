\page design_documentation_conventions Documentation Conventions

# Scope

This document defines the canonical documentation, comment, and Doxygen style for project-owned
code in Rock Hero.

These rules apply to:

- `rock-hero-common/`
- `rock-hero-editor/`
- `rock-hero-game/`
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
    rock_hero_common_audio
    PUBLIC rock_hero::common::core rock_hero::juce_gui_basics
    PRIVATE rock_hero::build_policy rock_hero::tracktion_core rock_hero::tracktion_engine
            rock_hero::tracktion_graph)
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
- When the function is already documented in its header, the definition's comment is a short
  summary of what the function does, so a reader of the `.cpp` need not open the header to follow
  it. It is not a copy of the header's contract, which would drift from it, and not a pointer such
  as "Documented in foo.h", which tells the reader nothing. Implementation reasons the header does
  not carry follow in the body, where they apply.

\code{.cpp}
// Rebuilds the visible note list for the current scroll window.
void HighwayView::rebuildVisibleNotes()
\endcode

- Every class or struct defined entirely inside a `.cpp` file gets a regular class-level comment
  immediately above the definition unless it is explicitly trivial.
- Trivial exceptions are defaulted or empty special members with no meaningful behavior, obvious
  one-line local lambdas, generated macro entrypoints such as `START_JUCE_APPLICATION`, and test
  helpers whose names and assertions already explain the purpose.
- `.cpp` method comments should explain why the method exists, lifecycle timing, ownership or
  lifetime effects, framework callback role, threading or realtime constraints, user-visible
  behavior, failure behavior, or why a temporary implementation is acceptable.
- `.cpp` class and struct comments should explain the helper's file-local role, ownership or
  lifetime model, threading constraints, stored-state invariants, or why the type stays private to
  the translation unit. Keep these as regular comments, not Doxygen comments.
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

- For inline implementation comments, comment non-obvious blocks or lines. See
  [When in Doubt, Comment](#when-in-doubt-comment) for how to apply this in practice.

# When in Doubt, Comment

The bar for "non-obvious" in this project is deliberately low. If a future reader would have to
read surrounding code, trace through types, check framework documentation, or run the program to
understand *why* something is the way it is, that is non-obvious enough to warrant a comment. Do
not require a comment to clear some high subjective threshold of cleverness before it earns its
place.

Specific situations where a comment is expected even when the code looks short or innocuous:

- A constant, buffer size, or loop shape is load-bearing for correctness (for example, it keeps a
  return value inside a smaller type's range, or it prevents a third-party API from misbehaving).
- A seemingly reasonable refactor would silently break the code. Name the dangerous refactor and
  why it would break, so the next author does not attempt it.
- A type, cast, or conversion exists to bridge two libraries with different size, ownership, or
  threading conventions.
- An invariant is enforced elsewhere in the file or module and the local code depends on it.
- A piece of behavior is surprising relative to the function's name or surrounding code.
- The code looks like dead code, an oversight, or an off-by-one, but is intentional.

What still does not deserve a comment:

- Mechanical restatements of what the code does when the names already make it clear.
- References to the current task, ticket, PR, or recent change. Those belong in commit messages.
- Defensive narration of straightforward control flow.

When the call is close, write the comment: a missing comment that costs someone a debugging
session, or that invites a plausible-looking but wrong refactor, is expensive. Keep it as short as
its meaning allows, and rewrite it when the code it describes changes (see the next section).

# Comments State the Current Truth

The section above decides whether a comment exists; this one decides what it says.

- **Rewrite, never append.** When a change leaves a comment wrong or incomplete, rewrite it so it
  reads as if written today by someone who knows the current code. Never add the new reason beside
  the old one; that is how comments grow into essays.
- **History belongs in git.** No dates, rulings, plan, phase or review IDs, attributions, or
  narration of change ("used to", "now", "no longer", "the old X"). The exception is history
  without which the shape of the code cannot be understood, such as a workaround for a framework
  defect or a constraint a shipped format imposes; state it as the reason, undated. References to
  numbered rules stay, because they name rules, not events (see [Citing Rules](#citing-rules)).
- **A reason sits where it applies.** A reason about one line belongs on that line, not in the
  comment above the function.
- **Every claim is true of the code as it is.** Check a comment against the code before keeping
  or editing it.
- **Length follows meaning.** There is no line quota in either direction. A comment never grows to
  look thorough, never restates the code, and is never cut short of a reason the reader needs.
- **Header Doxygen describes the contract; `.cpp` comments describe the implementation.** Doxygen
  says what the entity is, its units, ranges and edge cases, its threading and ownership rules, and
  why it is shaped as it is when a caller needs to know. How the body achieves that belongs in the
  `.cpp`.

These rules are applied as code is touched: a change brings the comments in the code it edits up
to this standard. `scripts/verify-project-conventions.py`, which runs in pre-commit, rejects a date
inside a comment.

# Citing Rules

A bare "rule 8" means nothing to a reader who does not already know which list it counts in. A
numbered list of domain rules that more than one place cites is a **ruleset**, written once in
`docs/design/rulesets/` (\ref design_rulesets), and a comment cites it by the ruleset's name and the
rule's number:

\code{.cpp}
// Hand-position rules 5 and 6 only ever remove motion, and only from an unforced run.
\endcode

- Every citation names its ruleset; a list shares one name ("hand-position rules 5, 6 and 10").
- A rule the ruleset names rather than numbers is cited by that name with the ruleset's
  ("the hand-position slide rule").
- The header Doxygen of the function that implements a ruleset links its document once
  (`\ref design_rulesets_hand_positions`), so a reader of the code has one hop to the list.
- A test asserting one rule names it the same way in its comment.
- Rule numbers are stable; a ruleset's document never renumbers them.

# Doxygen Block Format

Project-owned Doxygen comments must use `/*! ... */` comments without leading `*` prefixes.
If a Doxygen comment contains only a `\brief`, keep the entire comment on one line when it fits
within the 100-character line limit:

\code{.cpp}
/*! \brief Stops the transport and returns the playhead to the loop start. */
\endcode

The one-line form is only available when the entity needs no other tag, so in practice it is for
`void` functions, data members, types, and enumerators. A function that RETURNS something always
needs a `\return` (see Required Fields below), which means it always needs the multi-line form — a
brief-only one-liner on a value-returning function is wrong however well it fits.

The full Doxygen block must be indented to the same level as the declaration it documents. Every
line inside the block uses that same indentation, including `\brief`, body paragraphs, tags, and
the closing `*/`. Use a multi-line block when the comment has a body paragraph, `\param`,
`\return`, `\throws`, `\see`, `\note`, or when the single-line form would exceed 100 characters.

Example for a top-level declaration:

\code{.cpp}
/*!
\brief Reads the current transport position for render-cadence cursor drawing.

Like state(), this method is a current message-thread-only read and is not thread-safe. Listener
callbacks report only TransportState changes and do not carry position. UI code that needs smooth
cursor motion should call position() at its own render cadence.

\return Current transport position.
*/
[[nodiscard]] virtual common::core::TimePosition position() const noexcept = 0;
\endcode

Example for an indented member declaration:

\code{.cpp}
    /*!
    \brief Creates the concrete editor view and installs the thumbnail factory.

    \param controller Controller that receives all user intents emitted by this view.
    \param audio_ports Required audio ports consumed directly by this view.
    */
    EditorView(core::IEditorController& controller, AudioPorts audio_ports);
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

A field says what the signature cannot: units, the valid range, ownership or lifetime, what an
empty or null value means, the sentinel or empty-result case. When a parameter's name and type
already say everything, its field stays and stays short; it is never padded to look thorough.

# Blank-Line Rules

- If a Doxygen block has a verbose description, keep a blank line between `\brief` and the body.
- If a Doxygen block has no verbose description, do not insert a blank line between `\brief` and
  the next Doxygen command.
- If a Doxygen comment has only a `\brief`, use the single-line form unless the line would exceed
  100 characters. Only a construct with nothing else required can have only a `\brief` — a
  value-returning function always carries `\return` (see Required Fields), so it never qualifies.
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
