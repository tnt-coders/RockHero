add_subdirectory(api)
add_subdirectory(impl)

add_library(rock_hero_common_audio INTERFACE)
add_library(rock_hero::common::audio ALIAS rock_hero_common_audio)

target_link_libraries(rock_hero_common_audio INTERFACE rock_hero::common::audio::api
                                                       rock_hero::common::audio::impl)
