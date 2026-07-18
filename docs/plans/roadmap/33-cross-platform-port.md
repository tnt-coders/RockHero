# Plan 33 — Cross-Platform Port (Linux + macOS)

Status: Ready (unscheduled — start at user direction) | 2026-07-18 | baseline `master @ e3ce0d1a`

## Goal

Make both executables (`rock-hero`, `rock-hero-editor`) build, run windowed, and pass verification
on Linux (Vulkan) and macOS (Metal), completing the promise of 20-Q1's answer A ("Windows-first
with cross-platform-preserving choices"). The 2026-07-18 investigation (three parallel audits:
shader/render, CI/build, runtime platform-isms) found the discipline held: the codebase already
compiles, tests, packages, and releases on the existing Linux and macOS CI legs, the Conan
dependency layer is fully cross-platform, and every OS-conditional in project code is quarantined
in seven source files. What remains is a short list of runtime seams — shader profiles, backend
enums, native window handles, macOS bundling — plus the genuinely unknown part: first-ever GPU
bring-up and verification on non-Windows machines. The editor 3D preview's embedded child surface
is the single large item and is deliberately the last, separable phase.

## Non-goals

- **No OpenGL backend.** `highway_camera.cpp` builds a [0, 1]-clip-depth projection (D3D
  convention), which is also exactly Vulkan's and Metal's convention but not GL's [-1, 1]. Pinning
  Vulkan on Linux keeps the camera math untouched; a GL fallback would need
  `bgfx::getCaps()->homogeneousDepth` handling and is explicitly out of scope.
- **No feature work and no Windows behavior changes.** Every phase is additive per-platform
  branching; the Windows/D3D11 paths keep their current behavior byte-for-byte.
- **No new dependencies.** The Conan recipes (bgfx with `tools=True`, SDL3) already carry full
  Linux/macOS support; this plan only consumes what they already provide.
- **No CI-runner procurement decisions inside the plan.** Phase 7 lays out the verification
  options (Xvfb + lavapipe in CI vs. real hardware); which machines exist is a user decision
  (33-Q4).
- **The plugin-window shortcut hook is not ported here.** The Win32 `WH_GETMESSAGE` hook that
  steals Space/Ctrl+Z/Ctrl+Y back from misbehaving VST3 editor windows ships as a documented
  no-op off Windows (Phase 5); the macOS (`NSEvent` local monitor) and X11 (event filter)
  equivalents are a recorded follow-up, not v1.

## Constraints

Applicable subset of the roadmap's non-negotiable constraints (see docs/plans/roadmap/00-roadmap.md):

- (a) **Layering**: `common` never depends on editor or game code; backend selection stays in
  `common/ui`'s render seam; bgfx headers never leave `common/ui` implementation files.
- (b) **Public-header minimalism**: new enum values extend existing public headers; no new public
  surface beyond what the phases name.
- (c) **Naming firewall**: the commercial real-guitar game is never named in any repo file.
- (h) **Builds**: on the Windows dev machine all build/test/lint commands go through
  `.agents/rockhero-build.ps1`. Linux/macOS builds run through the existing CI pipeline (the
  `tnt-coders/ci-workflows` reusable workflow) or a plain `cmake --preset debug` on those hosts —
  the presets are already platform-agnostic (Ninja + Conan auto-detected profile). The helper
  script itself is Windows-only and stays that way.
- **Grid-native re-verification rule**: this plan was authored from a three-audit investigation
  dated 2026-07-18; per CLAUDE.md, re-verify the Current state inventory (fresh stamp) before
  execution begins — file:line references below were spot-checked on the baseline but will drift.

Design-doc anchors: docs/design/architecture.md "Technology Stack" and "Fallback Strategy" (the
SDL3+bgfx stack was chosen *as* a cross-platform abstraction; see also plan 20's Gate record),
docs/design/architectural-principles.md "Ports and Adapters" (backend selection is an adapter
concern; the highway scene model stays headless and platform-free).

## Current state inventory

Verified 2026-07-18 on `master @ e3ce0d1a` (three-audit investigation; load-bearing lines
spot-checked directly).

### Already cross-platform (no work)

- **CI**: `.github/workflows/build-linux.yml`, `build-macos.yml`, `build-windows.yml` are three
  near-identical legs delegating to `tnt-coders/ci-workflows/.github/workflows/cpp-cmake-conan-build.yml@master`,
  each with `install-juce-dependencies: true`, `lint: true`, `package: true`, `release: true` —
  full build + test + package on every platform. The render stack is covered headlessly via
  bgfx's Noop backend (`RenderBackend::Noop`, the S5 spike criterion).
- **CMake presets**: only `debug`/`release`/`relwithdebinfo`, all platform-agnostic (Ninja
  generator, Conan provider via `CMAKE_PROJECT_TOP_LEVEL_INCLUDES`, host profile auto-detected).
  No per-platform presets are needed.
- **Conan layer**: `conanfile.txt` pins `sdl/3.4.8` and `bgfx/1.129.8930-495` with
  `bgfx/*:tools=True`. The local bgfx recipe shadow
  (`conan-recipes/recipes/bgfx/cmake/conanfile.py`) already requires `xorg/system`,
  `opengl/system`, `wayland/[>=1.22 <2]` on Linux (the wayland range is a deliberate local fix
  relaxing conancenter's exact pin to coexist with SDL's xkbcommon) and links
  `Cocoa/Metal/QuartzCore/IOKit/CoreFoundation` on macOS. Because `tools=True` builds shaderc
  **from source per platform**, Linux and macOS hosts get a native shaderc binary — SPIR-V and
  Metal output need no Windows toolchain.
- **Shader sources**: backend-agnostic bgfx `.sc` files in `rock-hero-common/ui/shaders/` +
  `varying.def.sc`, shared by both products. Five programs (`color`, `color_fade`,
  `texture_tint`, `glyph`, `texture`), stages `vs`/`fs`.
- **Paths, dialogs, settings, single-instance**: everything goes through JUCE cross-platform APIs
  (`getSpecialLocation`, `FileChooser`, `PropertiesFile` with `osxLibrarySubFolder` already set,
  `moreThanOneInstanceAllowed`). `juce_path.cpp` and `audio_path_util.cpp` already carry correct
  dual `JUCE_WINDOWS`/POSIX branches. No hardcoded backslashes, drive letters, or `%APPDATA%`
  literals exist in runtime code.
- **ASIO**: enters only as `$<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>`
  (`cmake/RockHeroExternalModules.cmake:177`) — on other platforms JUCE compiles its native
  backends (CoreAudio / ALSA / JACK). Runtime device selection is generic JUCE enumeration; no
  code path requires ASIO.
- **Game loop**: plain portable `main()` with `SDL_MAIN_HANDLED`; the per-frame JUCE message
  pump (`rock-hero-game/ui/src/surface/juce_message_pump.cpp`) is already dual-branch —
  `CFRunLoopRunInMode` under `JUCE_MAC`, `juce::detail::dispatchNextMessageOnSystemQueue` for
  Windows **and** Linux (the symbol exists on JUCE's Linux backend; needs a link check, Phase 5).
- **Depth convention**: `rock-hero-common/core/src/highway/highway_camera.cpp:78-83` builds a
  [0, 1]-depth projection — valid unchanged for D3D11, Vulkan, and Metal.
- **DPI**: `game_window.cpp` already uses `SDL_WINDOW_HIGH_PIXEL_DENSITY` and pixel-size-driven
  resize (`SDL_GetWindowSizeInPixels`, display-scale events); written portably, untested off
  Windows.

### The actual gaps (what this plan changes)

1. **Shader compilation is `if(WIN32)`-gated and emits only D3D11.**
   `cmake/RockHeroRenderStack.cmake`: `rock_hero_add_compiled_shader()` (~lines 50-77) wraps one
   shaderc invocation; `rock_hero_stage_highway_shaders()` (~88-129) is gated `if(WIN32)` at
   line 91 and hardcodes `PROFILE s_5_0`, `PLATFORM windows`, output `${staging_dir}/dx11/`
   (~100-111). The gate's rationale (comment ~83-85) is that shaderc's **HLSL** backend needs the
   Windows D3D compiler — true, but over-broad: SPIR-V and Metal profiles compile on any host.
   Non-Windows builds currently stage an **empty** shader tree, so the deployed
   `resources/shaders/` is empty there.
2. **Backend enums have one real value each; the choice is hardcoded at three load sites.**
   - `rock-hero-common/ui/.../render_device.h` (~24-31): `RenderBackend { Direct3D11, Noop }`;
     `RenderDeviceConfig::backend` defaults to `Direct3D11` (~94).
   - `rock-hero-common/ui/src/render/render_device.cpp`: `toBgfxRendererType()` (~27-42) maps
     only those two; `defaultRenderBackend()` (line 77) returns `Direct3D11` unconditionally.
   - `rock-hero-game/core/.../game_resources.h` (~25-29): `ShaderBackend { Direct3D11 }`;
     `game_resources.cpp` `shaderBackendDirectory()` (~18-29) maps it to `"dx11"`.
   - Literal backend spellings: `rock-hero-game/ui/src/surface/highway_shader_loader.cpp` passes
     `core::ShaderBackend::Direct3D11` in five `load_pair` calls (~lines 18-24);
     `rock-hero-editor/ui/src/preview/preview_resources.cpp:39-52` hardcodes
     `.getChildFile("shaders").getChildFile("dx11")`.
   - Consumers of `defaultRenderBackend()`: `rock-hero-game/ui/src/surface/rock_hero_game.cpp:114`
     and `rock-hero-editor/ui/src/preview/preview_surface.cpp:133`.
3. **Native window handle extraction is Win32-only, and bgfx platform data is incomplete for
   X11.** `rock-hero-game/ui/src/surface/game_window.cpp:131-140` queries only
   `SDL_PROP_WINDOW_WIN32_HWND_POINTER` (comment: "other platforms will branch here") and fails
   with `NativeHandleUnavailable` when null. `render_device.cpp:119` sets only
   `init.platformData.nwh`; X11 additionally requires `platformData.ndt` (the `Display*`);
   Wayland requires the surface pointer plus `platformData.type`. macOS needs only `nwh` (the
   `NSWindow*` from SDL's Cocoa property).
4. **macOS packaging is unhandled.** The exe-relative resource resolver
   (`currentExecutableFile.getParentDirectory()/resources`) matches Windows/Linux but not `.app`
   bundles (`Contents/MacOS` vs `Contents/Resources`) — existing watch item "macOS bundle
   resource layout" (docs/tracking/watch-items.md). The game's PackageBuilder bundle output on
   macOS is unverified; the editor uses `juce_add_gui_app`, which does produce a bundle. The
   Linux `.desktop` file + icon theme dir already ship
   (`rock-hero-game/app/resources/rock_hero.desktop`, registered via `package_add_executable`).
5. **Small runtime polish items.**
   - `rock-hero-common/audio/src/device/audio_device_settings.cpp` `deviceTypePreferenceRank()`
     (~97-120) names only Windows backends; CoreAudio/ALSA/JACK fall into the rank-50 "unknown"
     bucket (functionally fine on single-backend OSes; ranking should still name them).
   - `rock-hero-common/audio/src/tracktion/plugin_window.{h,cpp}`: the Win32 keyboard hook
     (`SetWindowsHookExW` etc., ~.cpp:255-458) is `#if JUCE_WINDOWS`-gated with no `#else`; on
     other platforms the shortcut-stealing feature silently doesn't exist — needs an explicit
     recorded no-op stance (Non-goals).
   - `audio_path_util.cpp:45` `vst3DisplayPath` bundle normalization is `_WIN32`-only; macOS
     VST3 bundles share the `Contents/` structure and want the same normalization (nit).
   - `rock-hero-common/ui/tests/test_render_device.cpp:52-53` asserts
     `defaultRenderBackend() == Direct3D11` under `_WIN32` — the assertion must grow
     per-platform expectations when Phase 2 lands.
6. **CI system packages for the windowed stack are unverified on Linux.**
   `install-juce-dependencies: true` installs JUCE's set (X11, ALSA, freetype…); bgfx's
   `xorg/system` + `opengl/system` and SDL3's wayland/xkbcommon dev libs overlap but are not a
   proven superset — wayland/EGL dev headers are the likely gap (flagged in plan 20 ~748-749).
   A fix may need a change in the external `tnt-coders/ci-workflows` repo.
7. **The editor 3D preview is Windows-only by construction.**
   `rock-hero-editor/ui/src/preview/preview_surface.cpp` — the entire native-child mechanism
   (`RegisterClassExW`/`CreateWindowExW`/`MoveWindow`/`DestroyWindow`, the `WM_SETFOCUS`-bouncing
   window proc) is `#if JUCE_WINDOWS`; non-Windows builds compile it out and the preview does
   nothing. Two standing watch items govern the port: "Render child must never hold keyboard
   focus" (per-platform focus invariant, learned 2026-07-18) and "JUCE peer-recreation paths"
   (docs/tracking/watch-items.md, Editor 3D preview section).
8. **Plan-20 memo drift.** Plan 20's 0a gate memo says the Windows CI leg is the only leg; the
   Linux/macOS workflow files have since been added and run the full pipeline. Reconcile in
   Phase 0 (documentation only).

## Dependencies

Upstream: none — every prerequisite (render stack, Conan recipes, CI legs, presets) already
shipped. Coordination:

- **docs/tracking/watch-items.md** — three items graduate into this plan: "Render child must
  never hold keyboard focus" (Phase 8 trigger fires), "macOS bundle resource layout" (Phase 4),
  "editor/ui tests would need the common::ui link" (only if Phase 8 adds preview tests).
- **plan 20 Gate record** — Phase 0 updates its platform-scope memo to acknowledge the CI legs.
- **plan 26 Phase 4 (video settings)** — if a backend override setting is ever user-visible it
  lands there, not here; this plan keeps backend choice compile-time-default + dev override only.

## Phases

Phases 1-3 are independent of each other and may land in any order; Phase 7 needs 1-3 (and 4 on
macOS). Phase 8 is separable and may be deferred indefinitely without blocking the rest.

### Phase 0 — Decisions + memo reconciliation (documentation only)

1. Confirm the open questions 33-Q1..Q4 below (recommendations carried from the investigation).
2. Update plan 20's Gate record platform-scope note: the 0a memo's "Windows CI leg remains the
   only leg" is superseded by the three full CI legs; record whether Linux/macOS legs are
   *supported-and-must-stay-green* (recommended — they already gate today) or experimental.
3. Re-verify this plan's Current state inventory against the tree at execution time (fresh
   stamp), per the constraint above.

Verification: none (docs only).

### Phase 1 — Shader pipeline: per-platform profile table

Rework `rock_hero_stage_highway_shaders()` in `cmake/RockHeroRenderStack.cmake`:

1. Replace the `if(WIN32)` + hardcoded `s_5_0`/`windows`/`dx11` block with a platform table:
   - Windows: `PROFILE s_5_0`, `PLATFORM windows`, dir `dx11/` (unchanged output).
   - Linux: `PROFILE spirv`, `PLATFORM linux`, dir `spirv/`.
   - macOS: `PROFILE metal`, `PLATFORM osx`, dir `metal/`.
   Each host compiles **its own** platform's profile only (shaderc is a host-native binary from
   the Conan bgfx `tools=True` package; `find_program` at ~line 37 already resolves it on any
   host). Keep the HLSL-needs-D3D-compiler comment scoped to the dx11 row so the original
   rationale survives.
2. Keep `rock_hero_add_compiled_shader()` unchanged — it is already profile-parameterized.
3. The staging/deploy plumbing (`ROCK_HERO_STAGING_DIR`/`ROCK_HERO_STAGED_FILES` properties, the
   `copy_directory` in both app CMakeLists, the `install(DIRECTORY)` mirrors) is
   directory-agnostic and needs no change — verify the staged tree lands under the new
   per-platform subdirectory.
4. Watch shaderc flag requirements for Metal (`--platform osx -p metal`) and SPIR-V
   (`--platform linux -p spirv`) against the pinned bgfx revision — exercise both in CI before
   trusting them (Phase 6 wires the check).

Verification: Windows build stages `dx11/` exactly as before (byte-identical outputs); Linux and
macOS CI legs stage non-empty `spirv/`/`metal/` trees (assert non-empty in the build or check the
packaged artifact).

### Phase 2 — Backend enums + runtime selection

1. `rock-hero-common/ui/.../render_device.h` + `render_device.cpp`:
   - Extend `RenderBackend` with `Vulkan` and `Metal` (keep `Noop` untouched — it is the CI
     headless path).
   - Extend `toBgfxRendererType()` accordingly (`bgfx::RendererType::Vulkan`, `::Metal`).
   - Make `defaultRenderBackend()` platform-selective: Windows → `Direct3D11`, Linux → `Vulkan`
     (33-Q1; never GL, see Non-goals), macOS → `Metal`.
2. `rock-hero-game/core/.../game_resources.h` + `game_resources.cpp`:
   - Extend `ShaderBackend` with `Vulkan`/`Metal`; map `shaderBackendDirectory()` to
     `"spirv"`/`"metal"` (matching Phase 1's staging dirs exactly).
3. Un-hardcode the three literal sites:
   - `highway_shader_loader.cpp`: derive the `ShaderBackend` once from
     `common::ui::defaultRenderBackend()` (add a small mapping helper rather than five literals).
   - `preview_resources.cpp`: replace the literal `"dx11"` child dir with the same
     backend-to-directory mapping (place the shared mapping where both products can reach it
     without layering violations — the backend→dir name pairing is defined by this repo's
     staging convention, so a tiny helper beside each enum is acceptable; do not force a new
     common surface if it fights layering).
4. Update `rock-hero-common/ui/tests/test_render_device.cpp:52-53` to assert the per-platform
   expected default (D3D11 on `_WIN32`, Vulkan on `__linux__`, Metal on `__APPLE__`).

Verification: Windows behavior unchanged (build + touched tests); Linux/macOS CI compile the new
enum paths; the Noop headless test still passes on all legs.

### Phase 3 — Native window handles + bgfx platform data

1. `rock-hero-game/ui/src/surface/game_window.cpp` (~131-140): branch the property query —
   - Windows: `SDL_PROP_WINDOW_WIN32_HWND_POINTER` (unchanged).
   - macOS: `SDL_PROP_WINDOW_COCOA_WINDOW_POINTER`.
   - Linux/X11: `SDL_PROP_WINDOW_X11_WINDOW_NUMBER` (a number, not a pointer — mind the cast)
     plus `SDL_PROP_WINDOW_X11_DISPLAY_POINTER` for the display.
   - Wayland (only if 33-Q2 chooses to include it in v1): `SDL_PROP_WINDOW_WAYLAND_*`.
   The window handle type crossing the seam is `void*` today; X11 needs the display to travel
   too — extend the seam with an optional native display pointer (nullptr on Windows/macOS)
   rather than overloading the single handle.
2. `rock-hero-common/ui/src/render/render_device.cpp` (~119): populate
   `init.platformData.ndt` from the new display field (X11), and `platformData.type` if Wayland
   is in scope. macOS and Windows keep `nwh`-only.
3. Recommended 33-Q2 stance: X11 first (SDL's default video driver preference handles selection);
   Wayland rides SDL's XWayland compatibility until explicitly targeted.

Verification: Windows unchanged; on a Linux/macOS machine (or Phase 7's harness) window creation
proceeds past `NativeHandleUnavailable`.

### Phase 4 — Packaging + resources (macOS bundle, Linux verify)

1. Resolve the "macOS bundle resource layout" watch item: give the resource-root resolver a
   bundle-aware branch (JUCE `File::getSpecialLocation(currentApplicationFile)` +
   `Contents/Resources` when running from a bundle; exe-relative `resources/` otherwise). Both
   products' loaders route through their respective resource-root helpers — find them via the
   watch item's pointers and keep one branch per product, tested.
2. Verify the game's PackageBuilder output on macOS actually forms a launchable `.app` (the
   editor's `juce_add_gui_app` already bundles); fix the CMake packaging declarations if not.
3. Linux: confirm the `.desktop` + icon-theme install paths land where the PackageBuilder puts
   them (this shipped for plan 20; verification only).

Verification: packaged artifacts from the macOS CI leg contain the shaders and textures at the
path the resolver reads; a local macOS run (Phase 7) launches from the bundle.

### Phase 5 — Audio + shell polish (small items)

1. `audio_device_settings.cpp` `deviceTypePreferenceRank()`: add `CoreAudio` (macOS) and
   `ALSA`/`JACK` (Linux, ALSA preferred by default) to the ranking table so non-Windows backends
   are ranked intentionally instead of falling into the unknown bucket. Keep ASIO-first on
   Windows unchanged.
2. Build-check `juce_message_pump.cpp`'s Linux branch: confirm
   `juce::detail::dispatchNextMessageOnSystemQueue` links against the vendored JUCE Linux
   backend (expected yes; the `#else` already targets it). If it does not, the Linux branch
   needs JUCE's documented Linux equivalent — investigate in the vendored source, do not guess.
3. `plugin_window.{h,cpp}`: record the explicit stance that the Win32 shortcut-interception hook
   is a Windows-only defense; add a short comment at the `#if JUCE_WINDOWS` block naming the
   macOS/X11 equivalents (`NSEvent` local monitor / X11 event filter) as the follow-up, and note
   it in docs/tracking/backlog.md so the feature gap is discoverable.
4. Optional nit: extend `vst3DisplayPath` (`audio_path_util.cpp:45`) bundle normalization to
   macOS (`__APPLE__`) since VST3 bundles share the `Contents/` structure.

Verification: touched audio tests on Windows; Linux/macOS CI compile + test legs stay green.

### Phase 6 — CI hardening for the windowed stack

1. Ensure the Linux leg has the system packages the windowed stack needs beyond JUCE's set:
   wayland/EGL dev headers for SDL3/bgfx's `xorg/system` + `opengl/system` resolution. This
   likely means a parameter or step change in the external `tnt-coders/ci-workflows` reusable
   workflow — coordinate there; do not fork the pipeline into this repo.
2. Add a cheap staged-shader assertion to the pipeline (per Phase 1) so an empty shader tree on
   any leg fails loudly instead of shipping a package that cannot draw.
3. Per 33-Q4, optionally add a headless *windowed* smoke on the Linux leg: Xvfb + lavapipe
   (Mesa's software Vulkan) can create a real window + swapchain without a GPU. This proves
   init/teardown, not performance. Metal has no CI-viable software path — macOS windowed proof
   stays manual (Phase 7).

Verification: all three CI legs green including the new assertions.

### Phase 7 — Runtime bring-up + witnessed verification (the actual risk)

The equivalent of plan 20's S1/S2 criteria has only ever run on Windows/D3D11. Budget real
debugging time here; this is where unknown-unknowns live (driver quirks, swapchain resize
behavior, vsync semantics, color/depth format defaults).

Per platform (Linux with a real GPU, macOS on real hardware):

1. Game: launch with a dev song — window opens, highway renders, notes scroll in time against
   the clock, correct lane colors (plan 25 Phase 3's witnessed criteria), stable frame pacing
   with vsync on, clean exit. Resize, minimize/restore, and (macOS) fullscreen transitions do
   not lose the swapchain.
2. Audio: device enumeration shows the native backend (CoreAudio/ALSA), the backing track plays,
   live input monitors through a tone (the plan 21 Phase 6 soak subset that does not require
   ASIO-class latency; record measured round-trip latency for the record — no latency *promise*
   is part of this plan).
3. DPI: Retina (macOS) and HiDPI (Linux) render at native pixel density (the
   `SDL_WINDOW_HIGH_PIXEL_DENSITY` path's first real exercise); editor JUCE UI scales correctly.
4. Editor (minus 3D preview): full authoring session smoke on each platform — open a project,
   edit, undo, save, playback.
5. Record results as a witnessed checklist in this plan (the plan 21 Phase 6 soak precedent).

Verification: the witnessed checklist, signed per platform.

### Phase 8 — Editor 3D preview embedding port (separable; may stay deferred)

Port `PreviewSurface`'s native child mechanism:

- **macOS**: child `NSView` hosting the Metal layer (bgfx accepts an `NSView*`/`CAMetalLayer` as
  `nwh`), embedded in the JUCE peer's view hierarchy, bounds-synced like the Win32
  `MoveWindow` path.
- **Linux/X11**: child X window parented to the JUCE peer window, `nwh` = the child window id,
  `ndt` = the display.
- Both must enforce the watch-item invariant: **the render child must never hold keyboard
  focus** (macOS: plain `NSView` already refuses first responder — verify the backend's view
  keeps it that way; X11: never call `XSetInputFocus` on the child). Fold in the JUCE
  peer-recreation watch item while touching this code.
- If preview tests are added, `rock_hero_editor_ui_tests` must gain the `rock_hero::common::ui`
  link (third watch item).

Until this phase lands, the preview simply stays a Windows-only feature: `attach()` already
compiles to a no-op elsewhere, and F3/menu can stay enabled-but-inert or be hidden per a small
UX decision at execution time.

Verification: preview opens, renders, follows playback, and passes the focus checks on each
ported platform; Windows preview unchanged.

## Open questions

- **33-Q1** Linux render backend: (A) Vulkan only (matches the shipped [0,1] depth convention;
  lavapipe gives a CI software path); (B) Vulkan + OpenGL fallback (requires
  `homogeneousDepth`-aware projection math). **R: A.**
- **33-Q2** Linux windowing scope at v1: (A) X11 native (Wayland via XWayland); (B) native
  Wayland + X11 both wired. **R: A** — SDL handles selection, XWayland covers Wayland desktops,
  and native Wayland can land later as a small extension of the same property branch.
- **33-Q3** editor 3D preview: (A) defer Phase 8 — ship game + editor-minus-preview first;
  (B) include it in the initial port push. **R: A** — it is the single large item, cleanly
  separable, and the preview is an authoring convenience, not a product gate.
- **33-Q4** verification depth: (A) manual witnessed runs on real hardware per platform +
  optional Xvfb/lavapipe Linux smoke in CI; (B) self-hosted GPU runners for continuous windowed
  testing. **R: A now** — B is a standing-infrastructure decision that can follow once the port
  exists.

## Final acceptance bundle

- Windows: the sanctioned verification bundle (build, touched tests, user-triggered clang-tidy,
  pre-commit) — Windows outputs byte-identical where phases promise it.
- All three CI legs green including the staged-shader assertions.
- Phase 7's witnessed checklist signed for Linux and macOS.
- README platform-support note updated to reflect the new support state (it currently declares
  Windows-only, added 2026-07-18).
- docs/design/architecture.md "Technology Stack"/platform notes updated if the support statement
  changes (user confirmation first, per CLAUDE.md design-doc rules).
