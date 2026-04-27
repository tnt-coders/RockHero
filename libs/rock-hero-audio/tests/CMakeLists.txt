# Catch2 drives both fast public-contract tests and concrete adapter integration tests.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Copy the canonical fixture into the build tree so the test target owns its runtime inputs.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/test-data/drum_loop.wav"
               "${CMAKE_CURRENT_BINARY_DIR}/test-data/drum_loop.wav" COPYONLY)

# Fast public-contract tests exercise audio ports without constructing Tracktion/JUCE adapters.
add_executable(rock_hero_audio_tests test_edit.cpp test_thumbnail_source.cpp test_transport.cpp)

# The fast target includes audio public headers directly and links only their core value types.
target_include_directories(rock_hero_audio_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(rock_hero_audio_tests PRIVATE rock_hero_core Catch2::Catch2WithMain)

# Real-engine adapter coverage lives in a separate binary because it constructs Tracktion/JUCE.
add_executable(rock_hero_audio_integration_tests test_engine.cpp)

# Link the concrete audio library plus JUCE's message-manager support used by EngineTestHarness.
target_link_libraries(rock_hero_audio_integration_tests
                      PRIVATE rock_hero_audio rock_hero_juce_gui_basics Catch2::Catch2WithMain)

# Pass the copied fixture directory to integration tests without hard-coding source-tree paths.
target_compile_definitions(rock_hero_audio_integration_tests
                           PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-data")

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_audio_tests ADD_TAGS_AS_LABELS)
catch_discover_tests(rock_hero_audio_integration_tests ADD_TAGS_AS_LABELS)
