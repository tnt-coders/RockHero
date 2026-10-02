add_library(rock_hero_common_audio STATIC)
add_library(rock_hero::common::audio ALIAS rock_hero_common_audio)

# Conan-provided EBU R128 loudness analyzer used by the backing-audio normalization boundary.
find_package(libebur128 CONFIG REQUIRED)

# Shared audio ports, audio-device settings policy, and the default Tracktion/JUCE-backed adapter.
target_sources(
    rock_hero_common_audio
    PRIVATE src/automation/tone_automation_error.cpp
            src/automation/tone_automation_rebuild.cpp
            src/clock/atomic_playback_clock.cpp
            src/clock/atomic_playback_clock.h
            src/clock/playback_clock_extrapolator.cpp
            src/device/audio_device_configuration_error.cpp
            src/device/audio_device_settings.cpp
            src/device/null_audio_device.cpp
            src/device/null_audio_device.h
            src/engine/engine.cpp
            src/engine/engine_clock.cpp
            src/engine/engine_device_config.cpp
            src/engine/engine_impl.h
            src/engine/engine_live_input.cpp
            src/engine/engine_live_rig.cpp
            src/engine/engine_mix.cpp
            src/engine/engine_plugin_host.cpp
            src/engine/engine_song_audio.cpp
            src/engine/engine_tone_automation.cpp
            src/engine/engine_tone_timeline.cpp
            src/engine/engine_transport.cpp
            src/input/live_input_error.cpp
            src/input/live_input_monitor.cpp
            src/input/live_input_monitor_error.cpp
            src/input/live_input_monitoring_status.cpp
            src/live_rig/live_rig_error.cpp
            src/live_rig/tone_document.cpp
            src/live_rig/tone_document.h
            src/live_rig/tone_file.cpp
            src/live_rig/tone_file.h
            src/mix/mix_controls_error.cpp
            src/plugin/plugin_host_error.cpp
            src/plugin/plugin_scan.cpp
            src/plugin/plugin_scan.h
            src/plugin/plugin_state_memento.cpp
            src/plugin/plugin_state_memento.h
            src/plugin/plugin_window_shortcuts.cpp
            src/settings/audio_config_error.cpp
            src/settings/audio_config_store.cpp
            src/shared/audio_path_util.cpp
            src/shared/audio_path_util.h
            src/shared/device_state_xml.cpp
            src/shared/device_state_xml.h
            src/shared/meter_reader.h
            src/song/audio_normalization.cpp
            src/song/audio_transcode.cpp
            src/song/song_audio_error.cpp
            src/tracktion/engine_behaviors.cpp
            src/tracktion/engine_behaviors.h
            src/tracktion/live_rig_gain_plugin.cpp
            src/tracktion/live_rig_gain_plugin.h
            src/tracktion/multi_tone_rack.cpp
            src/tracktion/multi_tone_rack.h
            src/tracktion/monitoring_mode_transition.cpp
            src/tracktion/monitoring_mode_transition.h
            src/tracktion/plugin_dirty_tracking.cpp
            src/tracktion/plugin_dirty_tracking.h
            src/tracktion/plugin_state_hygiene.cpp
            src/tracktion/plugin_state_hygiene.h
            src/tracktion/tone_automation_curve.cpp
            src/tracktion/tone_automation_curve.h
            src/tracktion/tone_branch_gain_plugin.cpp
            src/tracktion/tone_branch_gain_plugin.h
            src/tracktion/plugin_window.cpp
            src/tracktion/plugin_window.h
            src/tracktion/tempo_mirror.cpp
            src/tracktion/tempo_mirror.h
            src/tracktion/tracktion_instrument_wave_device_mapping.cpp
            src/tracktion/tracktion_instrument_wave_device_mapping.h
            src/tracktion/tracktion_thumbnail.cpp
            src/tracktion/tracktion_thumbnail.h
            src/transport/transport_error.cpp)

target_include_directories(
    rock_hero_common_audio
    PUBLIC include
    PRIVATE include/rock_hero/common/audio src)

# Public dependencies are limited to types used by the public audio contracts. Tracktion remains a
# private implementation detail of the concrete adapter. juce_audio_formats, juce_cryptography,
# and libebur128 are normalization-only private dependencies.
target_link_libraries(
    rock_hero_common_audio
    PUBLIC rock_hero::common::audio_calibration rock_hero::common::core rock_hero::juce_gui_basics
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

# The calibration policy (the reference, the pickup table, the capture, the shared sentences) needs
# none of the audio library's dependencies, so it is a library of its own: the audio library, the
# known audio devices and the user guide's generator all link the one copy.
add_library(rock_hero_common_audio_calibration STATIC src/input/input_calibration.cpp
                                                      src/input/pickup_types.cpp)
add_library(rock_hero::common::audio_calibration ALIAS rock_hero_common_audio_calibration)
target_include_directories(
    rock_hero_common_audio_calibration
    PUBLIC include
    PRIVATE include/rock_hero/common/audio)
target_link_libraries(rock_hero_common_audio_calibration PRIVATE rock_hero::build_policy)

# The known audio devices are documentation data: only the user guide's generator reads them (the
# calibration window has no device list), so they live beside the generator, out of the shipped
# library, with the tests that hold their rows to the reference.
add_library(rock_hero_common_audio_known_interfaces STATIC tools/known_interfaces.cpp)
add_library(rock_hero::common::audio_known_interfaces ALIAS rock_hero_common_audio_known_interfaces)
target_include_directories(rock_hero_common_audio_known_interfaces PUBLIC tools)
target_link_libraries(
    rock_hero_common_audio_known_interfaces
    PUBLIC rock_hero::common::audio_calibration
    PRIVATE rock_hero::build_policy)

# The user guide's calibration tables and figures are generated from the code, so the guide never
# restates a figure the code owns. The docs targets build and run the generator first.
if(TARGET docs)
    add_executable(rock_hero_calibration_doc tools/calibration_doc_main.cpp)
    target_link_libraries(rock_hero_calibration_doc
                          PRIVATE rock_hero::common::audio_known_interfaces rock_hero::build_policy)

    set(ROCK_HERO_CALIBRATION_DOC_DIR "${CMAKE_BINARY_DIR}/docs/generated")
    add_custom_command(
        OUTPUT "${ROCK_HERO_CALIBRATION_DOC_DIR}/known_interfaces.md"
               "${ROCK_HERO_CALIBRATION_DOC_DIR}/pickup_types.md"
               "${ROCK_HERO_CALIBRATION_DOC_DIR}/calibration_figures.doxyfile"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${ROCK_HERO_CALIBRATION_DOC_DIR}"
        COMMAND rock_hero_calibration_doc "${ROCK_HERO_CALIBRATION_DOC_DIR}"
        DEPENDS rock_hero_calibration_doc
        COMMENT "Generating the calibration tables and figures"
        VERBATIM)
    add_custom_target(
        rock_hero_calibration_doc_fragments
        DEPENDS "${ROCK_HERO_CALIBRATION_DOC_DIR}/known_interfaces.md"
                "${ROCK_HERO_CALIBRATION_DOC_DIR}/pickup_types.md"
                "${ROCK_HERO_CALIBRATION_DOC_DIR}/calibration_figures.doxyfile")
    add_dependencies(docs rock_hero_calibration_doc_fragments)
    add_dependencies(project_config_docs_for_development rock_hero_calibration_doc_fragments)
endif()

add_subdirectory(tests)
