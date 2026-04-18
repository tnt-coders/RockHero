add_library(rock_hero_core STATIC)

target_sources(rock_hero_core PRIVATE src/song.cpp)

target_include_directories(
    rock_hero_core
    PUBLIC include
    PRIVATE include/rock_hero/core)

# rock_hero_core depends on standard C++ only — no JUCE, no Tracktion.

add_subdirectory(tests)
