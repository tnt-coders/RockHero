add_library(rock_hero_editor_core STATIC)
add_library(rock_hero::editor::core ALIAS rock_hero_editor_core)

# Headless editor workflow policy lives here so it can be tested without JUCE widgets.
target_sources(rock_hero_editor_core PRIVATE src/editor_controller.cpp)

target_include_directories(
    rock_hero_editor_core
    PUBLIC include
    PRIVATE include/rock_hero/editor/core)

target_link_libraries(
    rock_hero_editor_core
    PUBLIC rock_hero::common::core rock_hero::common::audio::api
    PRIVATE rock_hero::build_policy)

add_subdirectory(tests)
