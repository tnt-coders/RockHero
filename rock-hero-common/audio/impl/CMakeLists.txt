add_library(rock_hero_common_audio_impl STATIC)
add_library(rock_hero::common::audio::impl ALIAS rock_hero_common_audio_impl)

# Tracktion/JUCE-backed adapter for the shared audio API. This target is composed through the
# parent audio umbrella and should not be linked directly by ordinary library code.
target_sources(rock_hero_common_audio_impl PRIVATE src/engine.cpp src/tracktion_thumbnail.cpp)

target_include_directories(
    rock_hero_common_audio_impl
    PUBLIC include
    PRIVATE include/rock_hero/common/audio)

target_link_libraries(
    rock_hero_common_audio_impl
    PUBLIC rock_hero::common::audio::api
    PRIVATE rock_hero::build_policy rock_hero::tracktion_core rock_hero::tracktion_engine
            rock_hero::tracktion_graph)

add_subdirectory(tests)
