add_library(rock_hero_common_ui STATIC)
add_library(rock_hero::common::ui ALIAS rock_hero_common_ui)

# Placeholder shared UI module reserved for widgets that are genuinely reused by editor and game.
target_sources(rock_hero_common_ui PRIVATE src/placeholder.cpp)

target_include_directories(
    rock_hero_common_ui
    PUBLIC include
    PRIVATE include/rock_hero/common/ui)

# Keep the placeholder target on common core only until shared UI code proves broader needs.
target_link_libraries(
    rock_hero_common_ui
    PUBLIC rock_hero::common::core
    PRIVATE rock_hero::build_policy)
