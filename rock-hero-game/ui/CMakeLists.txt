add_library(rock_hero_game_ui STATIC)
add_library(rock_hero::game::ui ALIAS rock_hero_game_ui)

# Game surface feature: the SDL window, the bgfx render device, and the L2 frame loop composition
# point (plan 20 gate outcome: SDL owns main; JUCE's queue is drained per frame). The highway
# renderer itself is shared (rock-hero-common/ui highway feature); this library composes it with
# the game's resources. Dev feature + overlay: the --dev-package fixture session and the
# diagnostics overlay content (plan 20 Phase 4).
target_sources(
    rock_hero_game_ui
    PRIVATE src/dev/dev_session.cpp
            src/overlay/diagnostics_overlay.cpp
            src/surface/game_shell.cpp
            src/surface/game_window.cpp
            src/surface/highway_shader_loader.cpp
            src/surface/juce_message_pump.cpp
            src/surface/render_device.cpp)

# The src include path lets sibling translation units reach private feature headers with the
# quoted feature-qualified form ("surface/render_device.h").
target_include_directories(
    rock_hero_game_ui
    PUBLIC include
    PRIVATE include/rock_hero/game/ui src)

# Everything is PRIVATE because no public header of this library exposes another module's types:
# the render stack (SDL3/bgfx) and the JUCE message-thread runtime match the Tracktion isolation
# treatment, and the first-party deps (resource resolver, shared logger) are implementation
# details of the shell. A dependency moves to PUBLIC only when a public header exposes its types.
target_link_libraries(
    rock_hero_game_ui
    PRIVATE rock_hero::game::core
            rock_hero::common::core
            rock_hero::common::audio
            rock_hero::common::ui
            rock_hero::build_policy
            rock_hero::sdl3
            rock_hero::bgfx
            rock_hero::juce_events)

# Shared .sc shader sources (rock-hero-common/ui/shaders — the promotion moved them beside the
# shared renderer) compile at build time into the staging half of the resource-pack convention
# (plan 20 Phase 2); the app deploys the staged tree next to the executable. Only the
# Direct3D 11 profile ships now (gate 0a memo) — another backend is one more block here plus a
# ShaderBackend enumerator, never a redesign. Compilation is Windows-only because shaderc's HLSL
# backend needs the Windows D3D compiler ("HLSL compiler is not supported on this platform"
# elsewhere); non-Windows builds are compile/test hygiene, not shipped products, so they stage an
# empty tree and the deploy still works.
set(rock_hero_shared_shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../rock-hero-common/ui/shaders")
set(rock_hero_game_shader_staging "${CMAKE_CURRENT_BINARY_DIR}/resources/shaders")
set(rock_hero_game_staged_shaders "")
if(WIN32)
    foreach(shader_program IN ITEMS color color_fade texture_tint glyph)
        foreach(shader_stage IN ITEMS vertex fragment)
            if(shader_stage STREQUAL "vertex")
                set(stage_prefix vs)
            else()
                set(stage_prefix fs)
            endif()
            rock_hero_add_compiled_shader(
                OUTPUT
                "${rock_hero_game_shader_staging}/dx11/${stage_prefix}_${shader_program}.bin"
                TYPE
                ${shader_stage}
                SOURCE
                "${rock_hero_shared_shader_dir}/${stage_prefix}_${shader_program}.sc"
                VARYING
                "${rock_hero_shared_shader_dir}/varying.def.sc"
                PROFILE
                s_5_0
                PLATFORM
                windows)
            list(APPEND rock_hero_game_staged_shaders
                 "${rock_hero_game_shader_staging}/dx11/${stage_prefix}_${shader_program}.bin")
        endforeach()
    endforeach()
endif()

# The staging root must exist even when nothing is staged so the app's copy_directory has a
# source on every platform.
file(MAKE_DIRECTORY "${rock_hero_game_shader_staging}")

add_custom_target(rock_hero_game_shaders DEPENDS ${rock_hero_game_staged_shaders})

# The app's deploy step consumes the staged tree. Custom target properties carry the staging dir
# and the staged-file list across directory scopes: unlike a PARENT_SCOPE variable this survives
# subdirectory reordering, and a lookup typo fails loudly (NOTFOUND) instead of silently emptying
# a copy command. The file list is what lets the deploy re-run when a shader recompiles.
set_target_properties(
    rock_hero_game_shaders PROPERTIES ROCK_HERO_STAGING_DIR "${rock_hero_game_shader_staging}"
                                      ROCK_HERO_STAGED_FILES "${rock_hero_game_staged_shaders}")

add_subdirectory(tests)
