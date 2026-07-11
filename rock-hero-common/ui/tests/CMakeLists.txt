# Catch2 covers the shared string-color palette data and Charter-exact derivation math.
find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(rock_hero_common_ui_tests test_string_color_palette.cpp)

# Tests link the library under test plus Catch2's generated main.
target_link_libraries(rock_hero_common_ui_tests
                      PRIVATE rock_hero::common::ui rock_hero::build_policy Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_common_ui_tests ADD_TAGS_AS_LABELS)

# GCC's -Wfloat-equal fires through Catch2's assertion decomposition on every exact
# floating-point check; exact equality in tests is a deliberate round-trip contract, so the
# warning carries no signal here. Clang and MSVC never warn through the decomposition.
target_compile_options(rock_hero_common_ui_tests PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-float-equal>)
