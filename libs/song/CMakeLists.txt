add_library(song STATIC)

target_sources(song PRIVATE src/Song.cpp)

target_include_directories(song PUBLIC include)

# song depends on standard C++ only — no JUCE, no Tracktion.

add_subdirectory(tests)
