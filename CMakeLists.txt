cmake_minimum_required(VERSION 3.20)
project(
    RockHero
    VERSION 0.0.1
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set project-config specific options
option(PROJECT_CONFIG_ENABLE_DOCS "Enable docs target when Doxygen is available" ON)
option(PROJECT_CONFIG_ENABLE_CLANG_TIDY "Enable clang-tidy target when run-clang-tidy is available"
       ON)

add_subdirectory(project-config)

# --------------------------------------------------------------------------
# Tracktion Engine (brings JUCE 8 via its own modules/juce submodule)
# --------------------------------------------------------------------------
# EXCLUDE_FROM_ALL prevents Tracktion's examples/benchmarks from building as part of the default
# target (they still exist as named targets).
add_subdirectory(external/tracktion_engine EXCLUDE_FROM_ALL)

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
include(CTest)

# --------------------------------------------------------------------------
# Libraries
# --------------------------------------------------------------------------
add_subdirectory(libs/audio-engine)
add_subdirectory(libs/song)

# --------------------------------------------------------------------------
# Applications
# --------------------------------------------------------------------------
add_subdirectory(apps/rock-hero-editor)
add_subdirectory(apps/rock-hero-game)
