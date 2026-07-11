# Catch2 covers the shared string-color palette data, the Charter-exact derivation math, and the
# highway atlas layout arithmetic.
find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(rock_hero_common_ui_tests test_highway_atlas.cpp test_render_device.cpp
                                         test_string_color_palette.cpp)

# The atlas units under test are private to the library, so tests reach their headers through the
# library's src tree; the private atlas header carries bgfx types, so the bgfx wrapper supplies
# its include paths.
target_include_directories(rock_hero_common_ui_tests PRIVATE ../src)

# Tests link the library under test plus Catch2's generated main.
target_link_libraries(
    rock_hero_common_ui_tests PRIVATE rock_hero::common::ui rock_hero::bgfx rock_hero::build_policy
                                      Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_common_ui_tests ADD_TAGS_AS_LABELS)
