# Catch2 covers the shared string-color palette data and Charter-exact derivation math.
find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(rock_hero_common_ui_tests test_string_color_palette.cpp)

# Tests link the library under test plus Catch2's generated main.
target_link_libraries(
    rock_hero_common_ui_tests PRIVATE rock_hero::common::ui rock_hero::build_policy
                                      Catch2::Catch2WithMain rock_hero::test_warning_exceptions)

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_common_ui_tests ADD_TAGS_AS_LABELS)
