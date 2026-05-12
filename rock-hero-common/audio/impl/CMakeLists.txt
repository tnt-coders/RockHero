add_library(rock_hero_common_audio_impl STATIC)
add_library(rock_hero::common::audio::impl ALIAS rock_hero_common_audio_impl)

target_sources(rock_hero_common_audio_impl PRIVATE src/placeholder.cpp)

target_include_directories(
    rock_hero_common_audio_impl
    PUBLIC include
    PRIVATE include/rock_hero/common/audio)

target_link_libraries(
    rock_hero_common_audio_impl
    PUBLIC rock_hero::common::audio::api
    PRIVATE rock_hero::build_policy)
