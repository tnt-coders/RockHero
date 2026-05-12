# Audio tests cover both fast project-owned contracts and the concrete Tracktion/JUCE adapter.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Copy the canonical fixture into the build tree so the test target owns its runtime input.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/test-data/drum_loop.wav"
               "${CMAKE_CURRENT_BINARY_DIR}/test-data/drum_loop.wav" COPYONLY)

add_executable(rock_hero_common_audio_tests test_audio.cpp test_edit.cpp test_thumbnail_source.cpp
                                            test_transport.cpp test_engine.cpp)

target_link_libraries(
    rock_hero_common_audio_tests PRIVATE rock_hero::common::audio rock_hero::build_policy
                                         Catch2::Catch2WithMain)

target_compile_definitions(rock_hero_common_audio_tests
                           PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-data")

catch_discover_tests(rock_hero_common_audio_tests ADD_TAGS_AS_LABELS)
