add_library(rock_hero_common_core STATIC)
add_library(rock_hero::common::core ALIAS rock_hero_common_core)

# Shared headless domain library for song/session state and native song package persistence.
target_sources(
    rock_hero_common_core
    PRIVATE src/archive_error.cpp
            src/json.cpp
            src/juce_path.cpp
            src/logger.cpp
            src/logger_error.cpp
            src/session.cpp
            src/song.cpp
            src/package_id.cpp
            src/rock_song_package.cpp
            src/song_package_error.cpp)

target_include_directories(
    rock_hero_common_core
    PUBLIC include
    PRIVATE include/rock_hero/common/core)

# Quill backs the project logging facade; its macros expand at call sites, so it is a public
# dependency of every consumer that logs through <rock_hero/common/core/logger.h>.
find_package(quill REQUIRED)

# Hide Quill's broad LOG_* macro aliases from first-party consumers; project code uses RH_LOG_*.
target_compile_definitions(rock_hero_common_core PUBLIC QUILL_DISABLE_NON_PREFIXED_MACROS=1)

# JSON package helpers expose JUCE core utility types through a project-owned public header.
target_link_libraries(
    rock_hero_common_core
    PUBLIC rock_hero::juce_core quill::quill
    PRIVATE rock_hero::build_policy)

add_subdirectory(tests)
