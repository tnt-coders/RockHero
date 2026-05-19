add_library(rock_hero_common_core STATIC)
add_library(rock_hero::common::core ALIAS rock_hero_common_core)

# Shared headless domain library for song/session state and native song package persistence.
target_sources(
    rock_hero_common_core PRIVATE src/archive_error.cpp src/json.cpp src/session.cpp src/song.cpp
                                  src/rock_song_package.cpp src/song_package_error.cpp)

target_include_directories(
    rock_hero_common_core
    PUBLIC include
    PRIVATE include/rock_hero/common/core)

# JSON package helpers expose JUCE core utility types through a project-owned public header.
target_link_libraries(
    rock_hero_common_core
    PUBLIC rock_hero::juce_core
    PRIVATE rock_hero::build_policy)

add_subdirectory(tests)
