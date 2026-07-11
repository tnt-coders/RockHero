add_library(rock_hero_game_core STATIC)
add_library(rock_hero::game::core ALIAS rock_hero_game_core)

# Resources feature: the typed resolver every game plan loads packaged assets through
# (plan 20 Phase 2's resource-pack convention).
target_sources(rock_hero_game_core PRIVATE src/resources/game_resources.cpp)

target_include_directories(
    rock_hero_game_core
    PUBLIC include
    PRIVATE include/rock_hero/game/core)

# Game core depends only on shared domain data and the standard library.
target_link_libraries(
    rock_hero_game_core
    PUBLIC rock_hero::common::core
    PRIVATE rock_hero::build_policy)

add_subdirectory(tests)
