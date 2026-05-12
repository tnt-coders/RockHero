add_library(rock_hero_game_core STATIC)
add_library(rock_hero::game::core ALIAS rock_hero_game_core)

target_sources(rock_hero_game_core PRIVATE src/placeholder.cpp)

target_include_directories(
    rock_hero_game_core
    PUBLIC include
    PRIVATE include/rock_hero/game/core)

target_link_libraries(
    rock_hero_game_core
    PUBLIC rock_hero::common::core
    PRIVATE rock_hero::build_policy)
