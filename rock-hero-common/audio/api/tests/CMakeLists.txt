# Fast public-contract tests exercise audio ports without constructing Tracktion/JUCE adapters.
find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(rock_hero_common_audio_api_tests test_audio.cpp test_edit.cpp
                                                test_thumbnail_source.cpp test_transport.cpp)

target_link_libraries(
    rock_hero_common_audio_api_tests PRIVATE rock_hero::common::audio::api rock_hero::build_policy
                                             Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_common_audio_api_tests ADD_TAGS_AS_LABELS)
