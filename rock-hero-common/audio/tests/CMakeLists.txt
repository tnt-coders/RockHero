# Audio tests cover fast contracts, settings policy, and the concrete Tracktion/JUCE adapter.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Copy the canonical fixture into the build tree so the test target owns its runtime input.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/test-data/drum_loop.wav"
               "${CMAKE_CURRENT_BINARY_DIR}/test-data/drum_loop.wav" COPYONLY)

# Test-only helpers for common-audio contracts. Downstream tests opt in explicitly instead of
# reaching into this test directory by relative include path.
add_library(rock_hero_common_audio_testing INTERFACE)
add_library(rock_hero::common::audio_testing ALIAS rock_hero_common_audio_testing)

target_include_directories(rock_hero_common_audio_testing
                           INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(
    rock_hero_common_audio_testing INTERFACE rock_hero::common::audio rock_hero::juce_audio_devices
                                             rock_hero::juce_audio_formats)

# Keep audio-port contract tests and concrete Engine adapter coverage in one audio test target.
add_executable(
    rock_hero_common_audio_tests
    test_song_audio.cpp
    test_audio_device_settings.cpp
    test_audio_normalization.cpp
    test_audio_transcode.cpp
    test_gain.cpp
    test_input_calibration.cpp
    test_live_rig_gain_plugin.cpp
    test_multi_tone_rack.cpp
    test_plugin_state_hygiene.cpp
    test_tone_automation_curve.cpp
    test_tone_branch_gain_plugin.cpp
    test_thumbnail_source.cpp
    test_transport.cpp
    test_engine.cpp
    test_engine_behaviors.cpp
    test_monitoring_mode_transition.cpp
    test_tracktion_instrument_wave_device_mapping.cpp
    test_plugin_host.cpp
    test_tracktion_plugin_id_preservation.cpp)

# Let focused adapter tests include private implementation helpers without making them public API.
target_include_directories(rock_hero_common_audio_tests
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# Link the common audio module plus Catch2's generated main for headless and adapter tests.
target_link_libraries(
    rock_hero_common_audio_tests
    PRIVATE rock_hero::common::audio rock_hero::common::audio_testing rock_hero::build_policy
            rock_hero::tracktion_engine Catch2::Catch2WithMain)

# Point integration tests at the copied fixture instead of the source-tree file.
target_compile_definitions(rock_hero_common_audio_tests
                           PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-data")

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_common_audio_tests ADD_TAGS_AS_LABELS)
