# Tracktion/JUCE adapter library that owns the concrete audio engine integration.
add_library(rock_hero_audio STATIC)

# Keep the target surface small while the module is still centered on transport + thumbnail work.
target_sources(rock_hero_audio PRIVATE src/engine.cpp src/tracktion_thumbnail.cpp)

# Expose namespaced public headers while preserving short includes inside the owning module.
target_include_directories(
    rock_hero_audio
    PUBLIC include
    PRIVATE include/rock_hero/audio)

# Depend on project-owned wrapper targets rather than raw JUCE/Tracktion module targets.
target_link_libraries(
    rock_hero_audio
    PUBLIC rock_hero_core
    PRIVATE rock_hero::build_policy rock_hero_tracktion_core rock_hero_tracktion_engine
            rock_hero_tracktion_graph)

# Keep the module's adapter tests adjacent to the library they validate.
add_subdirectory(tests)
