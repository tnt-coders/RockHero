find_package(Catch2 3 REQUIRED)

add_executable(song_tests test_song.cpp)

target_link_libraries(song_tests PRIVATE song Catch2::Catch2WithMain)

add_test(NAME song_tests COMMAND song_tests)
