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

add_subdirectory(tests)
