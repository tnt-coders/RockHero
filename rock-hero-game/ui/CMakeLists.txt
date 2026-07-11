add_library(rock_hero_game_ui STATIC)
add_library(rock_hero::game::ui ALIAS rock_hero_game_ui)

# Game surface feature: the SDL window, the bgfx render device, and the L2 frame loop composition
# point (plan 20 gate outcome: SDL owns main; JUCE's queue is drained per frame).
target_sources(
    rock_hero_game_ui PRIVATE src/surface/game_shell.cpp src/surface/game_window.cpp
                              src/surface/juce_message_pump.cpp src/surface/render_device.cpp)

# The src include path lets sibling translation units reach private feature headers with the
# quoted feature-qualified form ("surface/render_device.h").
target_include_directories(
    rock_hero_game_ui
    PUBLIC include
    PRIVATE include/rock_hero/game/ui src)

# Game UI can consume game/audio state for presentation while keeping app startup separate. The
# render stack (SDL3/bgfx) and the JUCE message-thread runtime stay PRIVATE: no public header of
# this library exposes framework types, matching the Tracktion isolation treatment.
target_link_libraries(
    rock_hero_game_ui
    PUBLIC rock_hero::game::core rock_hero::game::audio rock_hero::common::audio
    PRIVATE rock_hero::build_policy rock_hero::sdl3 rock_hero::bgfx rock_hero::juce_events)

# Committed .sc shader sources compile at build time into the staging half of the resource-pack
# convention (plan 20 Phase 2); the app deploys the staged tree next to the executable. Only the
# Direct3D 11 profile ships now (gate 0a memo) — another backend is one more block here plus a
# ShaderBackend enumerator, never a redesign.
set(rock_hero_game_shader_staging "${CMAKE_CURRENT_BINARY_DIR}/resources/shaders")
foreach(shader_stage IN ITEMS vertex fragment)
    if(shader_stage STREQUAL "vertex")
        set(stage_prefix vs)
    else()
        set(stage_prefix fs)
    endif()
    rock_hero_add_compiled_shader(
        OUTPUT
        "${rock_hero_game_shader_staging}/dx11/${stage_prefix}_surface_flat.bin"
        TYPE
        ${shader_stage}
        SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${stage_prefix}_surface_flat.sc"
        VARYING
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc"
        PROFILE
        s_5_0
        PLATFORM
        windows)
endforeach()

add_custom_target(
    rock_hero_game_shaders DEPENDS "${rock_hero_game_shader_staging}/dx11/vs_surface_flat.bin"
                                   "${rock_hero_game_shader_staging}/dx11/fs_surface_flat.bin")

# The app's deploy step consumes the staged tree; expose its location up the game scope.
set(ROCK_HERO_GAME_SHADER_STAGING_DIR
    "${rock_hero_game_shader_staging}"
    PARENT_SCOPE)

add_subdirectory(tests)
