add_library(rock_hero_editor_ui STATIC)
add_library(rock_hero::editor::ui ALIAS rock_hero_editor_ui)

target_sources(rock_hero_editor_ui PRIVATE src/editor_settings.cpp src/main_window.cpp)

target_include_directories(
    rock_hero_editor_ui
    PUBLIC include
    PRIVATE include/rock_hero/editor/ui)

target_link_libraries(
    rock_hero_editor_ui
    PUBLIC rock_hero::editor::core rock_hero::editor::audio rock_hero::common::audio::api
           rock_hero::juce_data_structures rock_hero::juce_gui_basics
    PRIVATE rock_hero::ui rock_hero::audio rock_hero::juce_audio_utils rock_hero::build_policy)

add_subdirectory(tests)
