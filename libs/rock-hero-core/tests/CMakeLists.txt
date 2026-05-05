# Catch2 powers the fast pure-core tests for session and song behavior.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Keep framework-free domain tests separate from adapter-heavy audio coverage.
add_executable(
    rock_hero_core_tests
    test_session.cpp
    test_song.cpp
    test_arrangement.cpp
    test_chart.cpp
    test_difficulty.cpp
    test_timeline.cpp
    test_project_loader.cpp)

# The package-loader tests use libzip directly to create fixture archives.
target_link_libraries(rock_hero_core_tests PRIVATE rock_hero::core rock_hero::build_policy
                                                   libzip::zip Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest.
catch_discover_tests(rock_hero_core_tests)
