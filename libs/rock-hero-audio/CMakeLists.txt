add_library(rock_hero_audio STATIC)

target_sources(rock_hero_audio PRIVATE src/engine.cpp src/tracktion_thumbnail.cpp)

target_include_directories(
    rock_hero_audio
    PUBLIC include
    PRIVATE include/rock_hero/audio)

target_link_libraries(
    rock_hero_audio
    PUBLIC rock_hero_core
    PRIVATE rock_hero_tracktion_core rock_hero_tracktion_engine rock_hero_tracktion_graph)

add_subdirectory(tests)
