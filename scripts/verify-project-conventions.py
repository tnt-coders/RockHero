"""Verify mechanical project conventions from docs/design/.

Each convention gets one check function; the script fails when any check reports violations.

Current checks:

- Library roots hold folders only (architectural-principles.md).

Library roots hold folders only. Every source file lives in a feature folder, the library's hub
folder (`engine/`, `controller/`, `main_window/`), or `shared/` — no .h/.cpp may sit directly at
a library's include root or src root. The rule is purely structural, so this check needs no
allowlist: it fails on any file directly at a root.

The single exemption is `placeholder.cpp`, the scaffolding translation unit that gives a
not-yet-implemented library something to compile; it disappears when the library gains real code.

Run from the repository root (pre-commit does this automatically).
"""
from __future__ import annotations

import pathlib
import sys

PRODUCTS = ('common', 'editor', 'game')
LIBRARIES = ('core', 'audio', 'ui')
SOURCE_SUFFIXES = {'.h', '.cpp'}
EXEMPT = {'placeholder.cpp'}


def rootFiles(directory: pathlib.Path) -> list[str]:
    if not directory.is_dir():
        return []
    return sorted(
        entry.name for entry in directory.iterdir()
        if entry.is_file() and entry.suffix in SOURCE_SUFFIXES and entry.name not in EXEMPT
    )


def main() -> int:
    repo = pathlib.Path(__file__).resolve().parent.parent
    violations: list[str] = []

    for product in PRODUCTS:
        for library in LIBRARIES:
            base = repo / f'rock-hero-{product}' / library
            include_root = base / 'include' / 'rock_hero' / product / library
            src_root = base / 'src'
            for label, directory in (('include', include_root), ('src', src_root)):
                for name in rootFiles(directory):
                    violations.append(f'{product}/{library} {label} root: {name}')

    if violations:
        print('Library-root placement violations (see docs/design/architectural-principles.md):')
        for violation in violations:
            print(f'  {violation}')
        print(
            'Library roots hold folders only. Move the file into its feature folder, the\n'
            "library's hub folder (engine/, controller/, main_window/), or shared/."
        )
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
