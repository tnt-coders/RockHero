add_library(rock_hero_common_core STATIC)
add_library(rock_hero::common::core ALIAS rock_hero_common_core)

find_package(libzip REQUIRED)
find_package(nlohmann_json REQUIRED)

# Shared framework-free domain library for song/session state and native song package persistence.
target_sources(rock_hero_common_core PRIVATE src/session.cpp src/song.cpp src/rock_song_package.cpp)

target_include_directories(
    rock_hero_common_core
    PUBLIC include
    PRIVATE include/rock_hero/common/core)

# Keep song/archive-format dependencies private so consumers see only project-owned core headers.
target_link_libraries(rock_hero_common_core PRIVATE rock_hero::build_policy libzip::zip
                                                    nlohmann_json::nlohmann_json)

add_subdirectory(tests)
