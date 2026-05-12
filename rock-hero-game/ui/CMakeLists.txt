add_library(rock_hero_game_ui STATIC)
add_library(rock_hero::game::ui ALIAS rock_hero_game_ui)

target_sources(rock_hero_game_ui PRIVATE src/placeholder.cpp)

target_include_directories(
    rock_hero_game_ui
    PUBLIC include
    PRIVATE include/rock_hero/game/ui)

target_link_libraries(
    rock_hero_game_ui
    PUBLIC rock_hero::game::core rock_hero::game::audio rock_hero::common::audio::api
    PRIVATE rock_hero::build_policy)
