add_library(rock_hero_common_audio STATIC)
add_library(rock_hero::common::audio ALIAS rock_hero_common_audio)

# Conan-provided EBU R128 loudness analyzer used by the backing-audio normalization boundary.
find_package(libebur128 CONFIG REQUIRED)

# Shared audio ports, audio-device settings policy, and the default Tracktion/JUCE-backed adapter.
target_sources(
    rock_hero_common_audio
    PRIVATE src/audio_device_settings.cpp
            src/audio_normalization.cpp
            src/engine.cpp
            src/live_rig_gain_plugin.cpp
            src/live_rig_error.cpp
            src/plugin_host_error.cpp
            src/tracktion_instrument_wave_device_mapping.cpp
            src/tracktion_thumbnail.cpp)

target_include_directories(
    rock_hero_common_audio
    PUBLIC include
    PRIVATE include/rock_hero/common/audio src)

# Public dependencies are limited to types used by the public audio contracts. Tracktion remains a
# private implementation detail of the concrete adapter. juce_audio_formats, juce_cryptography,
# and libebur128 are normalization-only private dependencies.
target_link_libraries(
    rock_hero_common_audio
    PUBLIC rock_hero::common::core rock_hero::juce_gui_basics
    PRIVATE rock_hero::build_policy
            rock_hero::juce_audio_formats
            rock_hero::juce_cryptography
            rock_hero::tracktion_core
            rock_hero::tracktion_engine
            rock_hero::tracktion_graph
            libebur128::ebur128)

add_subdirectory(tests)
