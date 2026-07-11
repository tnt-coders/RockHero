# Watch items — game render stack (plans 20/21/25)

**Status: standing watch list.** Captured 2026-07-10 from the three-lens review of the first game
code (plan 20 Phases 1–2) so accepted-for-now items do not silently fade. Each entry names its
trigger — the moment it stops being acceptable — and what to do then. Check this list at the
start of plan 21, plan 25 Phase 3, and plan 20 Phase 4.

## 1. GameShell is a composition point living in game/ui — trigger: plan 21 start

`GameShell::run` constructs concrete adapters, chooses the backend, composes the resources root,
and owns the frame loop — several of architectural-principles.md § "UI Modules" move-to-app
triggers at once. Plan 20 Phase 1 explicitly sanctioned the placement at today's size. When the
audio engine joins (plan 21), decide deliberately: either inject composed dependencies from
`app/`, or accept the shell as the game's hub object and extract the headless frame-step policy
(quit/resize/frame-limit sequencing, transport reads) into `game/core` so loop decisions become
unit-testable. Do not let the shell grow by accretion without making this call.

## 2. bgfx handle ownership at scale — trigger: plan 25 Phase 3

`UniqueBgfxHandle` (rock-hero-game/ui/src/surface/bgfx_handle.h) exists and owns the shader
stage handles. When Phase 3 multiplies live handles (vertex/index buffers, textures, uniforms),
wrap them all — and respect the destructor-ordering trap the wrapper's header documents: handle
members must not sit directly on the type whose destructor body calls `bgfx::shutdown()`
(members destroy after the body). The clean shapes are a `SceneRenderer` object declared after
the `RenderDevice`, or restructuring device ownership so the bgfx instance is the
first-declared member. Project rule (already in force): never pass `destroyShaders=true` /
`destroyTextures=true` — bgfx consumes inputs on some failure paths but not others.

## 3. NSIS and the empty resource directories — trigger: next installer inspection

`install(DIRECTORY DESTINATION ...)` creates empty `resources/{fonts,sfx,textures}` under
`cmake --install`, but whether the NSIS-packaged artifact preserves empty directories is
unverified. `GameResources::create` only checks the root today, so nothing breaks either way —
but verify once when inspecting a packaged installer (dovetails with the Windows CI installer
work), and re-check the moment a resolver method starts requiring one of those subdirectories.

## 4. shaderc include tracking — trigger: first project-owned shared .sh shader include

`rock_hero_add_compiled_shader` tracks the .sc source and varying.def.sc but not includes;
today's only include dir is immutable Conan package content, so rebuilds are correct. When a
shared project-owned `.sh` include appears under `rock-hero-game/ui/shaders/`, switch the custom
command to shaderc's `--depends` output via `DEPFILE` (bgfx's own `bgfxToolUtils.cmake`
demonstrates the parse).

## 5. Stale files in the deployed resources tree — trigger: resource renames become common

`copy_directory` (build tree) and `install(DIRECTORY)` (install tree) are additive: a renamed or
removed staged `.bin` lingers beside the executable across incremental builds. A clean build
resets it. Acceptable until resource renames become routine; then make the deploy prune (copy
fresh into a scratch dir + `copy_directory_if_different`, or delete-before-copy).

## 6. CMakeConfigDeps generator — trigger: next Conan provider revision

The classic CMakeDeps generator declares no executable targets, which is why shaderc is located
via `find_program`. Conan's newer CMakeConfigDeps generator honors the recipe's `.exe` component
metadata and would make `bgfx::shaderc` a real executable target (and bgfx's packaged
`bgfxToolUtils.cmake` usable). Weigh migrating when next revising the cmake-conan provider — a
provider decision, not a game-CMake one.

## 7. Interactive drag-resize stalls rendering — accepted, trigger: user-visible complaint

Win32 runs a modal loop inside `DefWindowProc` during title-bar drag/resize, freezing the polled
frame loop for the drag's duration (last frame stretches; the resize lands on release).
Fundamental to polled loops on Windows; every SDL game without a message-hook workaround behaves
this way. Escape hatch if it ever matters: `SDL_SetWindowsMessageHook` / an SDL event watcher
that redraws from inside the modal loop.

## 8. BX_CONFIG_DEBUG re-export — trigger: relying on bgfx asserts from project TUs

bgfx compiles its own debug asserts per config, but whether the Conan CMakeDeps package
re-exports `BX_CONFIG_DEBUG` to RockHero TUs (affecting header-inline `BX_ASSERT`s compiled into
project code) is untraced. Irrelevant today — project code calls no asserting inline bx code —
but verify before designing anything that expects bgfx debug asserts to fire from our TUs.
