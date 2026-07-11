add_library(rock_hero_game_core STATIC)
add_library(rock_hero::game::core ALIAS rock_hero_game_core)

# Resources feature: the typed resolver every game plan loads packaged assets through
# (plan 20 Phase 2's resource-pack convention). Frame-clock feature: pure per-frame playback
# clock consumption and pacing stats for the game loop (plan 20 Phase 3).
target_sources(rock_hero_game_core PRIVATE src/frame_clock/frame_clock.cpp
                                           src/resources/game_resources.cpp)

target_include_directories(
    rock_hero_game_core
    PUBLIC include
    PRIVATE include/rock_hero/game/core)

# PUBLIC because the frame-clock header exposes the shared clock snapshot, extrapolation policy,
# and timeline types (the headless clock surface of common/audio — same precedent as editor/core).
target_link_libraries(
    rock_hero_game_core
    PUBLIC rock_hero::common::core rock_hero::common::audio
    PRIVATE rock_hero::build_policy)

add_subdirectory(tests)
