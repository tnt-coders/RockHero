add_library(rock_hero_common_audio_api STATIC)
add_library(rock_hero::common::audio::api ALIAS rock_hero_common_audio_api)

target_sources(rock_hero_common_audio_api PRIVATE src/placeholder.cpp)

target_include_directories(
    rock_hero_common_audio_api
    PUBLIC include
    PRIVATE include/rock_hero/common/audio)

target_link_libraries(
    rock_hero_common_audio_api
    PUBLIC rock_hero::common::core
    PRIVATE rock_hero::build_policy)
