add_subdirectory(core)
add_subdirectory(audio)
add_subdirectory(ui)

add_library(rock_hero_editor INTERFACE)
add_library(rock_hero::editor ALIAS rock_hero_editor)

target_link_libraries(rock_hero_editor INTERFACE rock_hero::editor::core rock_hero::editor::audio
                                                 rock_hero::editor::ui)

add_subdirectory(app)
