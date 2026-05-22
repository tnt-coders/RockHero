add_library(rock_hero_common_audio STATIC)
add_library(rock_hero::common::audio ALIAS rock_hero_common_audio)

# Shared audio ports, audio-device settings policy, and the default Tracktion/JUCE-backed adapter.
target_sources(
    rock_hero_common_audio
    PRIVATE src/audio_device_settings.cpp src/engine.cpp src/live_rig_error.cpp
            src/plugin_host_error.cpp src/tracktion_instrument_wave_device_mapping.cpp
            src/tracktion_thumbnail.cpp)

target_include_directories(
    rock_hero_common_audio
    PUBLIC include
    PRIVATE include/rock_hero/common/audio src)

# Public dependencies are limited to types used by the public audio contracts. Tracktion remains a
# private implementation detail of the concrete adapter.
target_link_libraries(
    rock_hero_common_audio
    PUBLIC rock_hero::common::core rock_hero::juce_gui_basics
    PRIVATE rock_hero::build_policy rock_hero::tracktion_core rock_hero::tracktion_engine
            rock_hero::tracktion_graph)

add_subdirectory(tests)
