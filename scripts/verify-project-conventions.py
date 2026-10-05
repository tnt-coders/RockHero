"""Verify mechanical project conventions from docs/design/.

Each convention gets one check function; the script fails when any check reports violations.

Tree-wide checks, over every tracked file:

- Library roots hold folders only (architectural-principles.md).
- No UTF-8 byte-order mark in a tracked text file.
- No stray C0 control byte in a tracked text file.
- Function-local constants do not wear the g_ prefix (.clang-tidy).
- Lines carrying a comment fit the 100-column limit (documentation-conventions.md).

Changed-file checks, over the project C++ files named on the command line, or the staged ones when
none are named (documentation-conventions.md, "Comments State the Current Truth"):

- No date inside a comment.
- No run of full-line `//` comments longer than the ceiling.

These two bind only changed files because the tree is still being swept to the standard: a
tree-wide run would fail every commit on comments the commit never touched. Once the sweep lands
they can move to the tree-wide set like any other check.

PENDING_CHECKS is empty: nothing is written-but-parked today. Pass --pending to run whatever it
holds, which is how to inventory what a sweep must fix before a check moves into CHECKS.
"""
from __future__ import annotations

import pathlib
import re
import subprocess
import sys
from collections.abc import Callable, Iterator
from typing import NamedTuple

PRODUCTS = ('common', 'editor', 'game')
LIBRARIES = ('core', 'audio', 'ui')
SOURCE_SUFFIXES = ('.h', '.cpp')
EXEMPT = {'placeholder.cpp'}

# Project-owned C++ scope, as docs/design/coding-conventions.md defines it: the three product
# trees, excluding external/, generated, and vendored content.
PRODUCT_ROOTS = tuple(f'rock-hero-{product}/' for product in PRODUCTS)
COLUMN_LIMIT = 100

# The longest run of full-line `//` comments documentation-conventions.md allows: a whole
# function's or type's description. Doxygen blocks are reference documentation and are not capped.
COMMENT_BLOCK_LIMIT = 4

# An ISO date, which is history and belongs in a commit message, never in a comment.
COMMENT_DATE = re.compile(r'(?<!\d)\d{4}-\d{2}-\d{2}(?!\d)')

# The encoding prefixes a string or character literal may wear. An identifier run ending at a
# quote that is none of these is a digit separator (`1'000`), not a literal.
ENCODING_PREFIXES = ('', 'u8', 'L', 'u', 'U')

# Every C0 control character except tab and newline. Newline is the line split, so it can never
# appear inside a line; tab is legal in the fixtures and tool files that need it.
STRAY_CONTROL = re.compile(r'[\x00-\x08\x0b-\x1f]')

# A constant declaration whose declarator wears the g_ prefix, indented and without `static`
# anywhere before the initializer — `constexpr static` and friends order the keywords freely, so
# the exemption is a lookahead over the whole declarator rather than a first-token test.
# Indentation and `static` are what separate the scopes: clang-format does not indent namespaces,
# so a namespace-scope constant starts at column 0, and both a class constant and a function
# static must spell `static`. What is left is a plain function local (`auto` included, so
# `auto const g_x` cannot slip past on its leading keyword).
LOCAL_G_CONSTANT = re.compile(
    r'^[ ]+(?![^=;{]*\bstatic\b)(?:const|constexpr|inline|thread_local|auto)\b'
    r'[^=;{]*?\bg_[a-z0-9_]+\s*(?==|\{|\[|;)'
)


class TextFile(NamedTuple):
    """A tracked text file split into lines once, so every line check shares one numbering."""

    path: str
    lines: tuple[str, ...]


class Sources(NamedTuple):
    """Everything the checks read: the tracked text files, split once, and the changed subset."""

    text_files: tuple[TextFile, ...]
    changed_files: tuple[TextFile, ...]


class Check(NamedTuple):
    """One convention: the function that finds violations, plus how to report them."""

    find: Callable[[Sources], list[str]]
    heading: str
    remedy: str


class CommentText(NamedTuple):
    """The comment text on one line, and whether the line is nothing but a `//` comment."""

    number: int
    text: str
    line_comment: bool


def trackedTextFiles(repo: pathlib.Path) -> tuple[TextFile, ...]:
    """Loads every tracked text file as lines.

    Text means no NUL byte and decodable as UTF-8; images, fonts, and submodule gitlinks fall out.
    Lines are split on '\\n' alone after folding CRLF, so line numbers match the editor's even in a
    file carrying a stray control byte, which str.splitlines() would treat as another separator.
    """
    listing = subprocess.run(
        ('git', 'ls-files', '-z'), cwd=repo, capture_output=True, check=True)
    files: list[TextFile] = []
    for name in listing.stdout.decode('utf-8').split('\0'):
        if not name:
            continue
        path = repo / name
        if not path.is_file():
            continue
        data = path.read_bytes()
        if b'\0' in data:
            continue
        try:
            text = data.replace(b'\r\n', b'\n').decode('utf-8')
        except UnicodeDecodeError:
            continue
        files.append(TextFile(path=name, lines=tuple(text.split('\n'))))
    return tuple(files)


def changedFiles(
    repo: pathlib.Path, text_files: tuple[TextFile, ...], named: list[str]
) -> tuple[TextFile, ...]:
    """Selects the project C++ files the changed-file checks cover.

    Named on the command line, they are whatever the caller points at (a worker checking the file
    it just edited); otherwise they are the staged files, which is what a commit is about to
    carry. pre-commit stashes unstaged edits for the hook's duration, so under the hook the tree
    IS the index; a manual run reads the working tree.
    """
    if named:
        paths: list[str] = []
        for name in named:
            try:
                paths.append(pathlib.Path(name).resolve().relative_to(repo).as_posix())
            except ValueError:
                sys.exit(f'{name}: outside the repository')
    else:
        staged = subprocess.run(
            ('git', 'diff', '--cached', '--name-only', '--diff-filter=d', '-z'),
            cwd=repo, capture_output=True, check=True)
        paths = [name for name in staged.stdout.decode('utf-8').split('\0') if name]
    wanted = {path for path in paths if isProjectCppFile(path)}
    tracked = {file.path: file for file in text_files}
    missing = wanted - tracked.keys()
    if missing:
        sys.exit('not tracked: ' + ', '.join(sorted(missing)))
    return tuple(tracked[path] for path in sorted(wanted))


def isProjectCppFile(path: str) -> bool:
    """Reports whether a tracked path is project-owned C++ that the docs' rules bind."""
    return path.startswith(PRODUCT_ROOTS) and path.endswith(SOURCE_SUFFIXES)


def literalPrefix(line: str, index: int) -> str:
    """The identifier run ending just before `index`, classifying the quote or R found there."""
    start = index
    while start > 0 and (line[start - 1].isalnum() or line[start - 1] == '_'):
        start -= 1
    return line[start:index]


def skipQuoted(line: str, index: int, quote: str) -> int:
    """Index just past the literal opening at `index`, honoring backslash escapes."""
    index += 1
    while index < len(line):
        if line[index] == '\\':
            index += 2
            continue
        if line[index] == quote:
            return index + 1
        index += 1
    return len(line)


def commentText(lines: tuple[str, ...]) -> Iterator[CommentText]:
    """Yields the comment text of every line that carries any.

    String and character literals are skipped, so a `//`, `/*` or date inside one never reads as
    a comment; raw string literals carry their closing delimiter across lines. Project Doxygen
    blocks are `/*! ... */` with no leading `*` on body lines, so a body line is recognizable only
    from the block state carried across lines, not from a per-line marker.
    """
    in_block = False
    raw_close: str | None = None
    for number, line in enumerate(lines, 1):
        text: list[str] = []
        seen = in_block
        line_comment = not in_block and raw_close is None and line.lstrip().startswith('//')
        index = 0
        while index < len(line):
            if in_block:
                end = line.find('*/', index)
                if end < 0:
                    text.append(line[index:])
                    break
                text.append(line[index:end])
                index = end + 2
                in_block = False
            elif raw_close is not None:
                end = line.find(raw_close, index)
                if end < 0:
                    break
                index = end + len(raw_close)
                raw_close = None
            elif line.startswith('//', index):
                seen = True
                text.append(line[index + 2:])
                break
            elif line.startswith('/*', index):
                seen = True
                in_block = True
                index += 2
            elif line[index] == '"':
                prefix = literalPrefix(line, index)
                if prefix.endswith('R') and prefix[:-1] in ENCODING_PREFIXES:
                    paren = line.find('(', index)
                    if paren < 0:
                        break
                    raw_close = ')' + line[index + 1:paren] + '"'
                    index = paren + 1
                else:
                    index = skipQuoted(line, index, '"')
            elif line[index] == "'" and literalPrefix(line, index) in ENCODING_PREFIXES:
                index = skipQuoted(line, index, "'")
            else:
                index += 1
        if seen:
            yield CommentText(number=number, text=''.join(text), line_comment=line_comment)


def checkLibraryRootPlacement(sources: Sources) -> list[str]:
    """Library roots hold folders only.

    Every source file lives in a feature folder, the library's hub folder (`engine/`,
    `controller/`, `main_window/`), or `shared/` — no .h/.cpp may sit directly at a library's
    include root or src root. The rule is purely structural, so this check needs no allowlist: it
    fails on any file directly at a root. Derived from the tracked-file index like every other
    check, so an untracked scratch file cannot fail a commit that does not include it.

    The single exemption is `placeholder.cpp`, the scaffolding translation unit that gives a
    not-yet-implemented library something to compile; it disappears when the library gains real
    code.
    """
    roots: dict[str, str] = {}
    for product in PRODUCTS:
        for library in LIBRARIES:
            roots[f'rock-hero-{product}/{library}/include/rock_hero/{product}/{library}'] = (
                f'{product}/{library} include')
            roots[f'rock-hero-{product}/{library}/src'] = f'{product}/{library} src'
    violations: list[str] = []
    for file in sources.text_files:
        directory, _, name = file.path.rpartition('/')
        if name in EXEMPT or not any(name.endswith(suffix) for suffix in SOURCE_SUFFIXES):
            continue
        label = roots.get(directory)
        if label is not None:
            violations.append(f'{label} root: {name}')
    return violations


def checkNoByteOrderMark(sources: Sources) -> list[str]:
    """No tracked text file starts with a UTF-8 byte-order mark.

    A BOM is invisible in most editors but is real file content: it displaces the first token, and
    it is the fingerprint of a PowerShell Get-Content/Set-Content round-trip, which also rewrites
    every non-ASCII character it passes through as mojibake.
    """
    return [
        f'{file.path}:1'
        for file in sources.text_files
        if file.lines and file.lines[0].startswith('﻿')
    ]


def checkNoStrayControlBytes(sources: Sources) -> list[str]:
    """No tracked text file carries a C0 control character other than tab or newline.

    Any other C0 byte is corruption rather than content, and it is silent: a `\\brief` whose
    backslash a write path collapsed into the byte it escapes becomes 0x08, which Doxygen renders
    as plain body text.
    """
    violations: list[str] = []
    for file in sources.text_files:
        for number, line in enumerate(file.lines, 1):
            # Cheap C-level gate: a line of ordinary text has no control character to find.
            if line.isprintable():
                continue
            for match in STRAY_CONTROL.finditer(line):
                byte = ord(match.group())
                violations.append(f'{file.path}:{number}:{match.start() + 1}: byte {byte:#04x}')
    return violations


def checkCommentLineLength(sources: Sources) -> list[str]:
    """Every line carrying a comment in project-owned C++ fits the 100-column limit.

    clang-format sets `ReflowComments: false` on purpose, so the formatter never wraps a comment
    and nothing else enforced the limit documentation-conventions.md states for them.
    """
    violations: list[str] = []
    for file in sources.text_files:
        if not isProjectCppFile(file.path):
            continue
        for comment in commentText(file.lines):
            length = len(file.lines[comment.number - 1])
            if length > COLUMN_LIMIT:
                violations.append(f'{file.path}:{comment.number}: {length} columns')
    return violations


def checkLocalConstantPrefix(sources: Sources) -> list[str]:
    """No function-local constant in project-owned C++ wears the g_ prefix.

    .clang-tidy sets `LocalConstantCase: lower_case` with no `LocalConstantPrefix`, while the
    global, static, and class constant prefixes are all `g_`. A `g_` name on a plain function
    local is therefore a `readability-identifier-naming` error, and CI treats it as fatal —
    invisible locally, because lint is on-demand only.
    """
    violations: list[str] = []
    for file in sources.text_files:
        if not isProjectCppFile(file.path):
            continue
        for number, line in enumerate(file.lines, 1):
            if 'g_' not in line:
                continue
            if LOCAL_G_CONSTANT.match(line):
                violations.append(f'{file.path}:{number}: {line.strip()}')
    return violations


def checkNoDatesInComments(sources: Sources) -> list[str]:
    """No comment in a changed file carries a date.

    A date is history, and history belongs in git. The one comment allowed to carry history
    cites a commit hash instead, so this check needs no exception.
    """
    violations: list[str] = []
    for file in sources.changed_files:
        for comment in commentText(file.lines):
            for match in COMMENT_DATE.finditer(comment.text):
                violations.append(f'{file.path}:{comment.number}: {match.group()}')
    return violations


def checkCommentBlockLength(sources: Sources) -> list[str]:
    """No run of full-line `//` comments in a changed file exceeds the ceiling.

    A code line, a blank line, or a trailing comment on a code line all end a run. Doxygen
    `/*! */` blocks are reference documentation with their own standard and are not counted.
    """
    violations: list[str] = []
    for file in sources.changed_files:
        start = 0
        length = 0
        for comment in commentText(file.lines):
            if comment.line_comment and comment.number == start + length:
                length += 1
                continue
            if length > COMMENT_BLOCK_LIMIT:
                violations.append(f'{file.path}:{start}: {length} lines')
            start, length = (comment.number, 1) if comment.line_comment else (0, 0)
        if length > COMMENT_BLOCK_LIMIT:
            violations.append(f'{file.path}:{start}: {length} lines')
    return violations


CHECKS = (
    Check(
        find=checkLibraryRootPlacement,
        heading='Library-root placement violations (see docs/design/architectural-principles.md):',
        remedy=(
            'Library roots hold folders only. Move the file into its feature folder, the\n'
            "library's hub folder (engine/, controller/, main_window/), or shared/."
        ),
    ),
    Check(
        find=checkNoByteOrderMark,
        heading='UTF-8 byte-order marks in tracked text files:',
        remedy=(
            'Strip the BOM with a byte-level tool: read all bytes, drop the leading EF BB BF,\n'
            'write all bytes. Never round-trip source through PowerShell Get-Content /\n'
            'Set-Content, which is what writes a BOM and mangles non-ASCII text.'
        ),
    ),
    Check(
        find=checkNoStrayControlBytes,
        heading='Stray C0 control bytes in tracked text files:',
        remedy=(
            'Only tab and newline are legal control bytes. Restore the character the byte\n'
            'replaced (0x08 in place of a backslash escape is the usual corruption) and check\n'
            'the write path that produced it.'
        ),
    ),
    Check(
        find=checkLocalConstantPrefix,
        heading='Function-local constants wearing the g_ prefix (see .clang-tidy):',
        remedy=(
            'Rename the local to plain lower_case, or make it static if it really is one. The g_\n'
            'prefix belongs to global, static, and class constants only; on a function local it\n'
            'is a readability-identifier-naming error that fails CI lint.'
        ),
    ),
    Check(
        find=checkCommentLineLength,
        heading=(
            f'Comment lines past {COLUMN_LIMIT} columns '
            '(see docs/design/documentation-conventions.md):'
        ),
        remedy=(
            'Wrap the comment by hand. clang-format sets ReflowComments: false, so the formatter\n'
            'will not do it, and the limit covers indentation and comment markers.'
        ),
    ),
    Check(
        find=checkNoDatesInComments,
        heading='Dates inside comments (see docs/design/documentation-conventions.md):',
        remedy=(
            'History belongs in git: rewrite the comment as the current truth. Where the code\n'
            'cannot be understood without its history, state that history as the reason and\n'
            'cite the commit hash, never a date.'
        ),
    ),
    Check(
        find=checkCommentBlockLength,
        heading=(
            f'Regular comment blocks over {COMMENT_BLOCK_LIMIT} lines '
            '(see docs/design/documentation-conventions.md):'
        ),
        remedy=(
            'Rewrite the comment as the current truth, once: most notes are one line, a whole\n'
            "function's or type's description up to four. A law the code cannot show lives at\n"
            "its owner's header Doxygen or in the developer guide, and this site names the owner."
        ),
    ),
)

# No check is parked today. A check belongs here only while the tree still carries pre-existing
# violations of it, so that adding it to CHECKS would fail every commit until a sweep lands; move
# it into CHECKS in the same change as that sweep.
PENDING_CHECKS: tuple[Check, ...] = ()


def main() -> int:
    repo = pathlib.Path(__file__).resolve().parent.parent
    arguments = sys.argv[1:]
    text_files = trackedTextFiles(repo)
    named = [argument for argument in arguments if not argument.startswith('--')]
    sources = Sources(
        text_files=text_files, changed_files=changedFiles(repo, text_files, named))

    # --pending runs the parked checks as well, which is how to inventory what a sweep must fix.
    checks = CHECKS + PENDING_CHECKS if '--pending' in arguments else CHECKS

    failed = False
    for check in checks:
        violations = check.find(sources)
        if not violations:
            continue
        failed = True
        print(check.heading)
        for violation in violations:
            print(f'  {violation}')
        print(check.remedy)

    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
