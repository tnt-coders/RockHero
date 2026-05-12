# Concrete adapter tests construct the real Tracktion/JUCE-backed audio engine.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Copy the canonical fixture into the build tree so the test target owns its runtime inputs.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/test-data/drum_loop.wav"
               "${CMAKE_CURRENT_BINARY_DIR}/test-data/drum_loop.wav" COPYONLY)

add_executable(rock_hero_common_audio_impl_tests test_engine.cpp)

target_link_libraries(
    rock_hero_common_audio_impl_tests PRIVATE rock_hero::common::audio rock_hero::juce_gui_basics
                                              rock_hero::build_policy Catch2::Catch2WithMain)

# Pass the copied fixture directory to integration tests without hard-coding source-tree paths.
target_compile_definitions(rock_hero_common_audio_impl_tests
                           PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-data")

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_common_audio_impl_tests ADD_TAGS_AS_LABELS)
