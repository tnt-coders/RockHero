# Documentation Conventions Audit Plan

Status: in-progress planning note. The project has a canonical
`docs/design/documentation-conventions.md`, but the codebase has never had a full sweep to confirm
every project-owned file actually follows it. This plan scopes that audit and the remediation it
produces. It is a compliance pass against an existing standard, not a change to the standard.

## Goal

Bring all project-owned source, headers, tests, and CMake files into compliance with
`docs/design/documentation-conventions.md`, and leave behind a repeatable way to check compliance
so it does not silently rot again.

## Scope

In scope (per the conventions doc's own scope section):

- `rock-hero-common/`, `rock-hero-editor/`, `rock-hero-game/`
- project-owned `CMakeLists.txt`, test `CMakeLists.txt`, and `.cmake` helpers
- project-owned tests

Out of scope (explicitly excluded by the conventions doc):

- `external/` (Tracktion, JUCE, vendored third-party)
- generated files (`BinaryData.h`, build artifacts under `build/`)
- `project-config/` submodule internals, except where this repo owns wrapper usage

## What To Check

Derived directly from `documentation-conventions.md`; each is a pass/fail the audit records per file.

Header Doxygen coverage:

- Every externally visible declaration (class, struct, enum, public/protected method, public data
  member, free function, non-obvious alias) has a `/*! ... */` Doxygen block.
- Every enumerator of a documented enum has a `\brief` (Doxygen warns on partial enum coverage).
- Every documented function documents every parameter with `\param`, every template parameter with
  `\tparam`, and every non-void return with `\return` (enforced by `WARN_NO_PARAMDOC = YES`).
- `\throws` is present where exceptions are part of the contract.
- Private header members use regular comments (not Doxygen) when they carry non-obvious
  invariants, ownership, threading, caching, or lifecycle rules.

Source (`.cpp`) comment coverage:

- Every non-trivial function/method definition has a regular comment immediately above it.
- Every class/struct defined inside a `.cpp` has a regular class-level comment.
- No Doxygen blocks in `.cpp` files; convert any to regular comments.
- Each `TEST_CASE` has a concise regular comment; test helpers are commented to production level.

Format and style:

- Doxygen blocks use `/*! ... */` with no leading `*`, indented to the declaration, closing `*/`
  at the same indentation.
- Backslash commands (`\brief`, `\param`, ...) rather than at-sign commands.
- Single-line `/*! \brief ... */` when it fits in 100 columns; multi-line otherwise.
- Blank-line rules between `\brief` and body are followed.
- All comment lines (Doxygen and normal) are within 100 columns including markers and indentation.
- Project-owned file-level headers include `\file <name>`.

CMake comment coverage:

- Target purpose, non-obvious public/private dependency choices, compile definitions, wrapper
  targets, generated files, and non-obvious build constraints are explained; no mechanical
  restatements.

Intent bar:

- Apply the "When in Doubt, Comment" low bar: load-bearing constants, dangerous-refactor warnings,
  cross-library bridging casts, externally enforced invariants, and surprising-but-intentional code
  all carry a comment.

## Approach

Audit in module-sized passes so review stays reviewable, in this order per module:

1. Public headers (the highest-value surface and the only Doxygen-extracted one).
2. `.cpp` files (regular-comment coverage and no-Doxygen-in-cpp rule).
3. Test files.
4. `CMakeLists.txt` and `.cmake` helpers.

Suggested module order, most-exercised first:

1. `rock-hero-common/core`
2. `rock-hero-common/audio`
3. `rock-hero-editor/core`
4. `rock-hero-editor/ui`
5. `rock-hero-editor/app`
6. `rock-hero-common/ui` and the `rock-hero-game/*` modules as they gain real code

Record findings per file (compliant / fix-needed with a short note) so the remediation commits can
be scoped per module rather than as one sweeping change.

## Tooling

- Doxygen with `WARN_NO_PARAMDOC = YES` (already configured) is the strongest automated signal for
  missing `\param`/`\tparam`/`\return` and undocumented enumerators. Build the `docs` target and
  treat warnings as the machine-checkable portion of the audit.
- clang-format does **not** wrap or rewrap comments by project rule, so the 100-column comment limit
  and block formatting must be checked by reading, not relied on from the formatter.
- Consider a lightweight script that flags obvious gaps (public declarations with no preceding
  `/*!`, Doxygen blocks inside `.cpp`, at-sign commands, comment lines over 100 columns) to triage
  files before manual review. The script triages; it does not replace the read-through.

## Staging

1. Land this plan.
2. Build the `docs` target and capture the current Doxygen warning set as the initial automated
   gap list.
3. Optionally add the triage script.
4. Remediate module by module in the order above, one reviewable commit per module/layer.
5. Decide whether any automated portion (Doxygen-warnings-as-errors in CI, or the triage script in
   pre-commit) should become a permanent gate so compliance does not regress.

## Acceptance Criteria

- Building the `docs` target produces no `WARN_NO_PARAMDOC` or undocumented-enumerator warnings for
  project-owned headers.
- Every project-owned public header declaration in scope has appropriate Doxygen.
- No Doxygen blocks remain in `.cpp` files; non-trivial `.cpp` definitions and `TEST_CASE`s carry
  regular comments.
- Project-owned CMake files explain target purpose and non-obvious build decisions.
- A decision is recorded on whether/which compliance check becomes a permanent CI or pre-commit
  gate.

## Open Questions

- Should Doxygen warnings become CI-fatal for project-owned headers once the backlog is cleared?
- Is a triage script worth maintaining, or is the Doxygen build plus manual review enough?
- Do the `rock-hero-game/*` modules get audited now (mostly empty) or deferred until they have real
  code, per the documentation-scope guidance in `CLAUDE.md`?
