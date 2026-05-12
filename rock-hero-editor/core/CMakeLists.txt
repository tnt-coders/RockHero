add_library(rock_hero_editor_core STATIC)
add_library(rock_hero::editor::core ALIAS rock_hero_editor_core)

find_package(nlohmann_json REQUIRED)
find_package(OpenPSARC REQUIRED)

# Headless editor workflow and project persistence stay testable without owning JUCE widgets.
target_sources(
    rock_hero_editor_core
    PRIVATE src/editor_controller.cpp src/project.cpp src/project_io.cpp
            src/psarc_song_importer.cpp src/rock_song_importer.cpp src/editor_settings.cpp)

target_include_directories(
    rock_hero_editor_core
    PUBLIC include
    PRIVATE include/rock_hero/editor/core)

# EditorSettings stores juce::PropertiesFile in its public header, so consumers need this wrapper.
target_link_libraries(
    rock_hero_editor_core
    PUBLIC rock_hero::common::core rock_hero::common::audio rock_hero::juce_data_structures
    PRIVATE rock_hero::build_policy nlohmann_json::nlohmann_json OpenPSARC::OpenPSARC)

add_subdirectory(tests)
