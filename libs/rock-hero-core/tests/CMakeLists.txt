find_package(Catch2 3 REQUIRED)

add_executable(rock_hero_core_tests test_song.cpp)

target_link_libraries(rock_hero_core_tests PRIVATE rock_hero_core Catch2::Catch2WithMain)

add_test(NAME rock_hero_core_tests COMMAND rock_hero_core_tests)
