# Tracktion/JUCE adapter library that owns the concrete audio engine integration.
add_library(rock_hero_audio STATIC)
add_library(rock_hero::audio ALIAS rock_hero_audio)

# Keep the target surface small while the module is still centered on transport + thumbnail work.
target_sources(rock_hero_audio PRIVATE src/engine.cpp src/tracktion_thumbnail.cpp)

# Expose namespaced public headers while preserving short includes inside the owning module.
target_include_directories(
    rock_hero_audio
    PUBLIC include
    PRIVATE include/rock_hero/audio)

# Depend on project-owned wrapper aliases rather than raw JUCE/Tracktion module targets.
target_link_libraries(
    rock_hero_audio
    PUBLIC rock_hero::core
    PRIVATE rock_hero::build_policy rock_hero::tracktion_core rock_hero::tracktion_engine
            rock_hero::tracktion_graph)

# Keep the module's adapter tests adjacent to the library they validate.
add_subdirectory(tests)
