# Catch2 covers the shared string-color palette data, the Charter-exact derivation math, the
# highway atlas layout arithmetic, and the tab feature: lane geometry, the layout manifest, and
# the notation paint core's software-rasterized pixel output.
find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(
    rock_hero_common_ui_tests
    test_box_mute_profile.cpp test_highway_atlas.cpp test_render_device.cpp
    test_string_color_palette.cpp test_tab_lane_layout.cpp test_tab_paint_core.cpp)

# The atlas units under test are private to the library, so tests reach their headers through the
# library's src tree; the private atlas header carries bgfx types, so the bgfx wrapper supplies
# its include paths.
target_include_directories(rock_hero_common_ui_tests PRIVATE ../src)

# The box-mute art is measured at renderer creation against a declared authoring contract, so the
# shipped asset itself is part of that contract and gets a test. Point the suite at the source
# tree's texture folder rather than a deployed copy: the contract is a property of the checked-in
# art, and a deploy step that had not run yet would test stale bytes.
target_compile_definitions(
    rock_hero_common_ui_tests
    PRIVATE ROCK_HERO_TEXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources/textures")

# Tests link the library under test plus Catch2's generated main; the paint-core pixel tests
# rasterize through a software JUCE image, so they need the GUI module's scoped initialiser.
target_link_libraries(
    rock_hero_common_ui_tests PRIVATE rock_hero::common::ui rock_hero::bgfx rock_hero::build_policy
                                      rock_hero::juce_gui_basics Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_common_ui_tests ADD_TAGS_AS_LABELS)
