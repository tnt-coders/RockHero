add_library(rock_hero_game_ui STATIC)
add_library(rock_hero::game::ui ALIAS rock_hero_game_ui)

# Game surface feature: the SDL window, the bgfx render device, and the L2 frame loop composition
# point (plan 20 gate outcome: SDL owns main; JUCE's queue is drained per frame). The highway
# renderer itself is shared (rock-hero-common/ui highway feature); this library composes it with
# the game's resources. Dev feature + overlay: the --dev-package fixture session and the
# diagnostics overlay content (plan 20 Phase 4).
target_sources(
    rock_hero_game_ui
    PRIVATE src/dev/dev_session.cpp src/overlay/diagnostics_overlay.cpp src/surface/game_shell.cpp
            src/surface/game_window.cpp src/surface/highway_shader_loader.cpp
            src/surface/juce_message_pump.cpp)

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

# Shared highway shaders compile into the staging half of the resource-pack convention (plan 20
# Phase 2); the app deploys the staged tree next to the executable. The program list and the
# Windows-only HLSL caveat live in rock_hero_stage_highway_shaders, shared with the editor's
# preview deploy.
rock_hero_stage_highway_shaders(rock_hero_game_shaders
                                "${CMAKE_CURRENT_BINARY_DIR}/resources/shaders")
