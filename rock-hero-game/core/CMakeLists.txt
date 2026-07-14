add_library(rock_hero_game_core STATIC)
add_library(rock_hero::game::core ALIAS rock_hero_game_core)

# Resources feature: the typed resolver every game plan loads packaged assets through
# (plan 20 Phase 2's resource-pack convention). Frame-clock feature: pure per-frame playback
# clock consumption and pacing stats for the game loop (plan 20 Phase 3). Diagnostics feature:
# headless dev-diagnostics state, intents, and the chart-source change detector (plan 20 Phase 4).
# Session feature: the headless GameplaySession orchestration that loads a package, preloads the
# tone rig, and drives playback through the shared audio ports (plan 21 Phase 2).
target_sources(
    rock_hero_game_core
    PRIVATE src/diagnostics/diagnostics.cpp src/frame_clock/frame_clock.cpp
            src/resources/game_resources.cpp src/session/gameplay_session.cpp
            src/session/gameplay_session_error.cpp)

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
