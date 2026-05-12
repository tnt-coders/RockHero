add_library(rock_hero_common_core STATIC)
add_library(rock_hero::common::core ALIAS rock_hero_common_core)

target_sources(rock_hero_common_core PRIVATE src/placeholder.cpp)

target_include_directories(
    rock_hero_common_core
    PUBLIC include
    PRIVATE include/rock_hero/common/core)

target_link_libraries(rock_hero_common_core PRIVATE rock_hero::build_policy)
