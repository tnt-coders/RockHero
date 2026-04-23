find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(rock_hero_audio_tests test_transport.cpp)

target_link_libraries(rock_hero_audio_tests PRIVATE rock_hero_audio Catch2::Catch2WithMain)

catch_discover_tests(rock_hero_audio_tests)
