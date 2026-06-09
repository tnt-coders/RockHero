add_library(rock_hero_game_audio STATIC)
add_library(rock_hero::game::audio ALIAS rock_hero_game_audio)

# Placeholder game-audio module reserved for future pitch, onset, and calibration plumbing.
target_sources(rock_hero_game_audio PRIVATE src/placeholder.cpp)

target_include_directories(
    rock_hero_game_audio
    PUBLIC include
    PRIVATE include/rock_hero/game/audio)

# Game audio may bridge shared audio ports to game-core analysis without depending on game UI.
target_link_libraries(
    rock_hero_game_audio
    PUBLIC rock_hero::common::core rock_hero::common::audio rock_hero::game::core
    PRIVATE rock_hero::build_policy)
