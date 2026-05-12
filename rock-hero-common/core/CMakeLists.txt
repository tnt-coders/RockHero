add_library(rock_hero_common_core STATIC)
add_library(rock_hero::common::core ALIAS rock_hero_common_core)

find_package(libzip REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenPSARC REQUIRED)

# Shared framework-free domain library for song/session state and project persistence.
target_sources(
    rock_hero_common_core PRIVATE src/project.cpp src/project_io.cpp src/psarc_importer.cpp
                                  src/rock_importer.cpp src/session.cpp src/song.cpp)

target_include_directories(
    rock_hero_common_core
    PUBLIC include
    PRIVATE include/rock_hero/common/core)

# Keep project-format dependencies private so consumers see only project-owned core headers.
target_link_libraries(
    rock_hero_common_core PRIVATE rock_hero::build_policy libzip::zip nlohmann_json::nlohmann_json
                                  OpenPSARC::OpenPSARC)

add_subdirectory(tests)
