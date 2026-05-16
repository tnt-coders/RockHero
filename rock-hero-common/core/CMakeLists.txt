add_library(rock_hero_common_core STATIC)
add_library(rock_hero::common::core ALIAS rock_hero_common_core)

# Shared headless domain library for song/session state and native song package persistence.
target_sources(rock_hero_common_core PRIVATE src/archive_error.cpp src/session.cpp src/song.cpp
                                             src/rock_song_package.cpp src/song_package_error.cpp)

target_include_directories(
    rock_hero_common_core
    PUBLIC include
    PRIVATE include/rock_hero/common/core)

# Keep JUCE package-format utilities private so consumers see project-owned core headers.
target_link_libraries(rock_hero_common_core PRIVATE rock_hero::build_policy rock_hero::juce_core)

add_subdirectory(tests)
