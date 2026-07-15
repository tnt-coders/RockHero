\page guide_add_file Adding a Source File

*Applies to: Repo-wide.*

Three decisions place every new file, and two registrations make it real. The placement rules are
owned by \ref design_architectural_principles ("Placement Procedure for New Files") — this page
is the short operational version.

# Placement

1. **Library** — dependency rules decide: touches Tracktion → `rock-hero-common/audio`
   implementation; JUCE UI → a `ui` library; headless → a `core` library; needed by both
   products → `common`.
2. **Folder** — name the user-facing feature the file serves and put it in that feature's folder.
   A mechanism serving two or more features goes in `shared/`; a file that composes features goes
   in the library's hub folder (`engine/`, `controller/`, `main_window/`). Never place a file at
   a library's `include` or `src` root — `scripts/verify-project-conventions.py` (run by
   pre-commit and CI) fails the commit if you do.
3. **Visibility** — start in `src/`. A header moves to `include/` only when a consumer outside
   the library exists.

# Registration — silent steps

1. **CMake.** Source lists are explicit, never globbed: add the file (headers too) to the owning
   library's `CMakeLists.txt` `target_sources`. A forgotten `.cpp` fails the link loudly, but a
   forgotten *header* just quietly vanishes from IDEs and installs.
2. **Tests.** A new `test_*.cpp` must be listed in its `tests/CMakeLists.txt`;
   `catch_discover_tests` registers the cases with CTest from there. An unlisted test file
   compiles nowhere and runs never — no error tells you.

# While you are in there

- Follow the naming table in `CLAUDE.md` (types `CamelCase`, functions `camelCase`, members
  `m_lower_case`) — clang-tidy treats violations as errors, but it runs on demand, not in
  pre-commit, so you will not be told at commit time.
- Public headers need Doxygen per \ref design_documentation_conventions.
- New third-party needs go through the wrapper targets (`rock_hero::juce_*`,
  `rock_hero::tracktion_engine`) or `conanfile.txt` — never raw module links.
