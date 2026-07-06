add_library(rock_hero_common_audio STATIC)
add_library(rock_hero::common::audio ALIAS rock_hero_common_audio)

# Conan-provided EBU R128 loudness analyzer used by the backing-audio normalization boundary.
find_package(libebur128 CONFIG REQUIRED)

# Shared audio ports, audio-device settings policy, and the default Tracktion/JUCE-backed adapter.
target_sources(
    rock_hero_common_audio
    PRIVATE src/device/audio_device_configuration_error.cpp
            src/device/audio_device_settings.cpp
            src/engine/engine.cpp
            src/engine/engine_device_config.cpp
            src/engine/engine_impl.h
            src/engine/engine_live_input.cpp
            src/engine/engine_live_rig.cpp
            src/engine/engine_plugin_host.cpp
            src/engine/engine_song_audio.cpp
            src/engine/engine_transport.cpp
            src/input/input_calibration.cpp
            src/input/live_input_error.cpp
            src/live_rig/live_rig_error.cpp
            src/live_rig/tone_document.cpp
            src/live_rig/tone_document.h
            src/plugin/plugin_host_error.cpp
            src/plugin/plugin_scan.cpp
            src/plugin/plugin_scan.h
            src/shared/audio_path_util.cpp
            src/shared/audio_path_util.h
            src/shared/meter_reader.h
            src/song/audio_normalization.cpp
            src/song/song_audio_error.cpp
            src/tracktion/engine_behaviors.cpp
            src/tracktion/engine_behaviors.h
            src/tracktion/live_rig_gain_plugin.cpp
            src/tracktion/live_rig_gain_plugin.h
            src/tracktion/monitoring_mode_transition.cpp
            src/tracktion/monitoring_mode_transition.h
            src/tracktion/plugin_dirty_tracking.cpp
            src/tracktion/plugin_dirty_tracking.h
            src/tracktion/tone_branch_gain_plugin.cpp
            src/tracktion/tone_branch_gain_plugin.h
            src/tracktion/plugin_move_index.h
            src/tracktion/plugin_window.cpp
            src/tracktion/plugin_window.h
            src/tracktion/tracktion_instrument_wave_device_mapping.cpp
            src/tracktion/tracktion_instrument_wave_device_mapping.h
            src/tracktion/tracktion_thumbnail.cpp
            src/tracktion/tracktion_thumbnail.h)

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
            rock_hero::juce_audio_devices
            rock_hero::juce_audio_formats
            rock_hero::juce_audio_processors_headless
            rock_hero::juce_core
            rock_hero::juce_cryptography
            rock_hero::juce_data_structures
            rock_hero::tracktion_core
            rock_hero::tracktion_engine
            rock_hero::tracktion_graph
            libebur128::ebur128)

add_subdirectory(tests)
