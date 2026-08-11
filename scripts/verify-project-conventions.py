"""Verify mechanical project conventions from docs/design/.

Each convention gets one check function; the script fails when any check reports violations.

Current checks:

- Library roots hold folders only (architectural-principles.md).
- No UTF-8 byte-order mark in a tracked text file.
- No stray C0 control byte in a tracked text file.
- Function-local constants do not wear the g_ prefix (.clang-tidy).
- Comment lines fit the 100-column limit (documentation-conventions.md).

PENDING_CHECKS is empty: nothing is written-but-parked today. Pass --pending to run whatever it
holds, which is how to inventory what a sweep must fix before a check moves into CHECKS.

Known gap, deliberate: the comment-column check covers comment LINES, not trailing comments on
code lines (none violate today; the compound parse was not worth it until one does).
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
    """Everything the checks read: the tracked text files, split once."""

    text_files: tuple[TextFile, ...]


class Check(NamedTuple):
    """One convention: the function that finds violations, plus how to report them."""

    find: Callable[[Sources], list[str]]
    heading: str
    remedy: str


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


def isProjectCppFile(path: str) -> bool:
    """Reports whether a tracked path is project-owned C++ that the docs' rules bind."""
    return path.startswith(PRODUCT_ROOTS) and path.endswith(SOURCE_SUFFIXES)


def endsInsideBlockComment(line: str, in_block: bool) -> bool:
    """Carries /* */ state across one line.

    String literals are not parsed: a "/*" inside a string would open a phantom block. Nothing in
    the tree does that today, and the tracker is self-checking in practice because a phantom open
    leaks to the end of the file and turns every following line into a comment.
    """
    if '/' not in line:
        return in_block
    index = 0
    while True:
        if in_block:
            index = line.find('*/', index)
            if index < 0:
                return True
            index += 2
            in_block = False
            continue
        open_at = line.find('/*', index)
        line_at = line.find('//', index)
        # A line comment starting first swallows the rest of the line, block markers included.
        if line_at >= 0 and (open_at < 0 or line_at < open_at):
            return False
        if open_at < 0:
            return False
        index = open_at + 2
        in_block = True


def commentLineNumbers(lines: tuple[str, ...]) -> Iterator[int]:
    """Yields the 1-based numbers of the lines whose content is comment text.

    Project Doxygen blocks are `/*! ... */` with no leading `*` on body lines, so a comment line
    is only recognizable from the block state carried across lines, not from a per-line marker.
    """
    in_block = False
    for number, line in enumerate(lines, 1):
        is_comment = in_block or line.lstrip().startswith(('//', '/*'))
        in_block = endsInsideBlockComment(line, in_block)
        if is_comment:
            yield number


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
        if file.lines and file.lines[0].startswith('\ufeff')
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
    """Every comment line in project-owned C++ fits the 100-column limit.

    clang-format sets `ReflowComments: false` on purpose, so the formatter never wraps a comment
    and nothing else enforced the limit documentation-conventions.md states for them.
    """
    violations: list[str] = []
    for file in sources.text_files:
        if not isProjectCppFile(file.path):
            continue
        for number in commentLineNumbers(file.lines):
            length = len(file.lines[number - 1])
            if length > COLUMN_LIMIT:
                violations.append(f'{file.path}:{number}: {length} columns')
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
)

# No check is parked today. A check belongs here only while the tree still carries pre-existing
# violations of it, so that adding it to CHECKS would fail every commit until a sweep lands; move
# it into CHECKS in the same change as that sweep.
PENDING_CHECKS: tuple[Check, ...] = ()


def main() -> int:
    repo = pathlib.Path(__file__).resolve().parent.parent
    sources = Sources(text_files=trackedTextFiles(repo))

    # --pending runs the parked checks as well, which is how to inventory what a sweep must fix.
    checks = CHECKS + PENDING_CHECKS if '--pending' in sys.argv[1:] else CHECKS

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
