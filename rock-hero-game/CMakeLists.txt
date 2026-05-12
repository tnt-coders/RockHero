add_subdirectory(core)
add_subdirectory(audio)
add_subdirectory(ui)

add_library(rock_hero_game INTERFACE)
add_library(rock_hero::game ALIAS rock_hero_game)

target_link_libraries(rock_hero_game INTERFACE rock_hero::game::core rock_hero::game::audio
                                               rock_hero::game::ui)
