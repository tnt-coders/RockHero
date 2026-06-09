# Catch2 powers fast headless tests for session, song, and package behavior.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Keep shared domain and package tests separate from adapter-heavy audio coverage.
add_executable(
    rock_hero_common_core_tests
    test_session.cpp
    test_song.cpp
    test_arrangement.cpp
    test_difficulty.cpp
    test_timeline.cpp
    test_juce_path.cpp
    test_logger.cpp
    test_rock_song_package.cpp)

# These tests stay focused on shared domain behavior and native song package rules.
target_link_libraries(
    rock_hero_common_core_tests PRIVATE rock_hero::common::core rock_hero::build_policy
                                        Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest.
catch_discover_tests(rock_hero_common_core_tests)
