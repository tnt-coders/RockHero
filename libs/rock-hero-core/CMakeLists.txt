# Framework-free domain library for session and song value types plus their rules.
add_library(rock_hero_core STATIC)

# Keep the target surface small while the core module remains centered on early song/session state.
target_sources(rock_hero_core PRIVATE src/session.cpp src/song.cpp)

# Expose namespaced public headers while preserving short includes inside the owning module.
target_include_directories(
    rock_hero_core
    PUBLIC include
    PRIVATE include/rock_hero/core)

# rock_hero_core depends on standard C++ only — no JUCE, no Tracktion.

# Keep the module's pure-domain tests adjacent to the library they validate.
add_subdirectory(tests)
