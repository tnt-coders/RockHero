find_package(Catch2 3 REQUIRED)
include(Catch)

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" rock_hero_audio_test_source_dir)

add_executable(rock_hero_audio_tests test_edit.cpp test_engine.cpp test_transport.cpp)

target_link_libraries(rock_hero_audio_tests PRIVATE rock_hero_audio Catch2::Catch2WithMain)

target_compile_definitions(
    rock_hero_audio_tests
    PRIVATE ROCK_HERO_AUDIO_TEST_SOURCE_DIR="${rock_hero_audio_test_source_dir}")

catch_discover_tests(rock_hero_audio_tests)
