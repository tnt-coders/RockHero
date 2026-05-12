add_library(rock_hero_common_audio_api STATIC)
add_library(rock_hero::common::audio::api ALIAS rock_hero_common_audio_api)

# The shared audio API target owns only project-facing contracts and light helper templates.
target_sources(rock_hero_common_audio_api PRIVATE src/placeholder.cpp)

target_include_directories(
    rock_hero_common_audio_api
    PUBLIC include
    PRIVATE include/rock_hero/common/audio)

# Export JUCE GUI basics because the thumbnail port intentionally mentions JUCE drawing types.
target_link_libraries(
    rock_hero_common_audio_api
    PUBLIC rock_hero::common::core rock_hero::juce_gui_basics
    PRIVATE rock_hero::build_policy)

add_subdirectory(tests)
