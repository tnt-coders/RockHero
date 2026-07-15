\page guide_add_port Adding Audio-Engine Capability

*Applies to: Repo-wide — the engine serves both products, and port changes ripple into both.*

Use this checklist when the audio engine gains a new capability: a method on an existing port, or
a whole new port. Worked example from history: `git show 9ba80db3` (the standalone tone-file
surface on `ILiveRig`).

Ports are the project-owned interfaces in
`rock-hero-common/audio/include/rock_hero/common/audio/`; `Engine` implements all of them, with
one implementation file per port under `rock-hero-common/audio/src/engine/`.

# Adding a method to an existing port

## Part A — The compiler walks you through these {#add_port_part_a}

1. Add the pure virtual to the port header (`i_<port>.h`).
2. The build then fails in every implementer, which *is* the checklist: the `override` in
   `engine/engine.h`, and **every test fake implementing the port** — the build finds them all.
   The fakes live in four places: `rock-hero-common/audio/tests/include/.../testing/`, the
   editor core harness
   (`rock-hero-editor/core/tests/include/.../testing/editor_controller_test_harness.h`),
   the editor UI harness
   (`rock-hero-editor/ui/tests/include/rock_hero/editor/ui/testing/editor_view_test_harness.h`),
   and the game's session fakes (`rock-hero-game/core/tests/test_gameplay_session.cpp`). Give
   each fake a real, minimal behavior — not a stub that lies (see the project's stance on code
   that lies about intent in \ref design_architectural_principles, "Typed Boundary Errors").

## Part B — Silent steps: nothing fails if you forget {#add_port_part_b}

1. **Implementation location.** Define the method in the port's own translation unit
   (`engine_live_rig.cpp`, `engine_transport.cpp`, ...), with private state and helper
   declarations in `engine_impl.h` — never inline framework subclasses there; those go in named
   units under `src/tracktion/`.
2. **Typed errors.** A recoverable failure crossing the port must be a domain-owned error value
   (the sibling `*_error.h`/`*_error.cpp`), not a raw framework string.
3. **Threading contract.** Ports are message-thread-only unless the method's Doxygen says
   otherwise; if yours is a documented exception, say so in the header the way
   `engine.h` does for the plugin-scan methods.
4. **New files → CMake.** Any new `.cpp`/`.h` goes into
   `rock-hero-common/audio/CMakeLists.txt` explicitly.
5. **Adapter tests.** Add or extend a `test_*.cpp` under `rock-hero-common/audio/tests/` and list
   it in that `CMakeLists.txt`.
6. **Doxygen** on the port method and the engine override.

# Adding a whole new port

Everything above, plus these — all silent:

1. Create `i_<port>.h` (and errors) in the right feature folder under
   `audio/include/rock_hero/common/audio/`.
2. Add `public INewPort` to the `Engine` inheritance list in `engine/engine.h`, and create the
   port's `engine_<port>.cpp` translation unit (listed in CMake).
3. **Wire the consumers.** Both applications construct one `Engine` and pass it around as port
   references: `rock-hero-editor/app/main.cpp` and `rock-hero-game/app/main.cpp`. A port nobody
   is handed does not exist. Consumer constructors (`EditorController`'s port references,
   `GameplaySession`'s) must be extended to accept it.
4. **Justify the seam.** A port with one production implementation and no fake is pure ceremony;
   if no test needs to fake it yet, ask whether the method belongs on an existing port instead.
