find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(rock_hero_core_tests test_session.cpp test_song.cpp)

target_link_libraries(rock_hero_core_tests PRIVATE rock_hero_core Catch2::Catch2WithMain)

catch_discover_tests(rock_hero_core_tests)
