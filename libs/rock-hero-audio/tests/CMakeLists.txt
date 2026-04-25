# Catch2 drives the focused audio adapter tests for this module.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Copy the canonical fixture into the build tree so the test target owns its runtime inputs.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/test-data/drum_loop.wav"
               "${CMAKE_CURRENT_BINARY_DIR}/test-data/drum_loop.wav" COPYONLY)

# Real-engine adapter coverage stays in a dedicated per-library test binary.
add_executable(rock_hero_audio_tests test_edit.cpp test_engine.cpp test_transport.cpp)

# Link the concrete audio library plus Catch2's generated main entrypoint.
target_link_libraries(rock_hero_audio_tests PRIVATE rock_hero_audio Catch2::Catch2WithMain)

# Pass the copied fixture directory to tests without hard-coding source-tree paths in C++.
target_compile_definitions(rock_hero_audio_tests
                           PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-data")

# Register the discovered Catch2 cases with CTest.
catch_discover_tests(rock_hero_audio_tests)
