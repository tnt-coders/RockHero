\page design_documentation_style Documentation Style

## Scope

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

## General Rules

- Keep the entire rendered comment line at 100 characters or fewer, including indentation and
  comment markers.
- Wrap as late as reasonably possible; do not leave comments prematurely short when they fit
  cleanly on the current line.
- Preserve intentional blank lines between comment paragraphs or sections.
- Normal non-Doxygen comments follow the same 100-character limit.

## Doxygen Scope

- Document all project-owned headers with Doxygen.
- Do not use Doxygen comments in `.cpp` files except for rare cases where a file-level note is
  genuinely needed and cannot be expressed better in markdown documentation.
- Convert Doxygen comments that are not in public headers into regular comments.
- Convert Doxygen comments on private class members or private methods into regular comments.
- Use explicit `\brief` fields rather than relying on Doxygen autobrief behavior.

## Doxygen Block Format

Public-header Doxygen comments must use `/*! ... */` blocks without leading `*` prefixes.

The full Doxygen block must be indented to the same level as the declaration it documents. Every
line inside the block uses that same indentation, including `\brief`, body paragraphs, tags, and
the closing `*/`.

Example for a top-level declaration:

\code{.cpp}
/*!
\brief Returns the current transport position in seconds.

Lock-free; safe to call from any thread. The value is backed by a std::atomic and currently
written by the 60 Hz UI timer shim (updateTransportPositionCache). It will be moved to an
audio-thread callback once ASIO input is wired.

\return The cached transport position in seconds.
*/
[[nodiscard]] double getTransportPosition() const noexcept;
\endcode

Example for an indented member declaration:

\code{.cpp}
    /*!
    \brief Creates the waveform display and starts the 60 Hz repaint timer.

    \param engine The AudioEngine whose transport state drives the cursor.
    */
    explicit WaveformDisplay(AudioEngine& engine);
\endcode

## Doxygen Commands

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

## Required Fields

Document all applicable fields.

- Use `\param` for every meaningful function parameter.
- Use `\tparam` for documented template parameters.
- Use `\return` whenever a function returns a value whose meaning is not obvious from the type
  alone.
- Use `\throws` when exceptions are part of the contract.
- Use `\note` for important usage constraints, thread-safety rules, lifetime caveats, or temporary
  implementation shims.

If a field does not apply, omit it.

## Blank-Line Rules

- If a Doxygen block has a verbose description, keep a blank line between `\brief` and the body.
- If a Doxygen block has no verbose description, do not insert a blank line between `\brief` and
  the next Doxygen command.
- Keep blank lines between logically separate paragraphs when a description spans multiple sections.

## File Headers

File-level Doxygen headers should include the file name explicitly:

\code{.cpp}
/*!
\file waveform_display.h
\brief Waveform rendering component with a scrolling playhead cursor.
*/
\endcode

Use this pattern for project-owned public headers when a file-level Doxygen header is warranted.
