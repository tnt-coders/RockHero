# Catch2 powers fast headless tests for session, song, and package behavior.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Keep shared domain and package tests separate from adapter-heavy audio coverage.
add_executable(
    rock_hero_common_core_tests
    test_chart.cpp
    test_session.cpp
    test_song.cpp
    test_arrangement.cpp
    test_application_identity.cpp
    test_difficulty.cpp
    test_fraction.cpp
    test_highway_camera.cpp
    test_highway_projection.cpp
    test_highway_tail.cpp
    test_tempo_map.cpp
    test_tone_track_rules.cpp
    test_tone_track_edits.cpp
    test_tone_track_normalize.cpp
    test_tone_schedule.cpp
    test_timeline.cpp
    test_juce_path.cpp
    test_logger.cpp
    test_rock_song_package.cpp)

# Let focused projection tests include private implementation headers without making them public
# API (the audio tests' established pattern).
target_include_directories(rock_hero_common_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# These tests stay focused on shared domain behavior and native song package rules.
target_link_libraries(
    rock_hero_common_core_tests PRIVATE rock_hero::common::core rock_hero::build_policy
                                        Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest.
catch_discover_tests(rock_hero_common_core_tests)
