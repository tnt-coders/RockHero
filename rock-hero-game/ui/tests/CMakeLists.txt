# Catch2 covers the game surface units, starting with the headless Noop render-device path that
# keeps game rendering testable with no GPU and no window (plan 20 gate criterion S5).
find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(rock_hero_game_ui_tests test_highway_atlas.cpp test_render_device.cpp)

# The surface units under test are private to the library, so tests reach their headers through
# the library's src tree (same pattern as the common/core and common/audio test targets).
target_include_directories(rock_hero_game_ui_tests PRIVATE ../src)

# Tests link the library under test plus Catch2's generated main; the atlas test includes a
# private header that carries bgfx types, so the bgfx wrapper supplies its include paths.
target_link_libraries(
    rock_hero_game_ui_tests PRIVATE rock_hero::game::ui rock_hero::bgfx rock_hero::build_policy
                                    Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_game_ui_tests ADD_TAGS_AS_LABELS)
