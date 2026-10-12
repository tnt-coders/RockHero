# Catch2 covers the shared string-color palette, the Charter-exact derivation math, the backdrop's
# clear-color packing, the theme loader and registry over fixture theme directories, the texture
# contract, the highway atlas layout arithmetic, the head-mark decisions, floor-mark rules and glide
# path the 3D draw pass reads, the highway renderer's own headless lifecycle on bgfx's Noop
# backend, and the tab feature: lane geometry, the layout manifest, and the notation paint core's
# software-rasterized pixel output.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Test-only fixtures for shared presentation: code-built string colors and themes the suites of
# both this library and the editor's draw with, stated once and opted into explicitly.
add_library(rock_hero_common_ui_testing INTERFACE)
add_library(rock_hero::common::ui_testing ALIAS rock_hero_common_ui_testing)

target_include_directories(rock_hero_common_ui_testing
                           INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(rock_hero_common_ui_testing INTERFACE rock_hero::common::ui)

# Every shipped built-in theme must load, and its art is measured against declared contracts, so
# the shipped themes are part of what the suites test. The fixtures point at the source tree's
# themes folder rather than a deployed copy: the contracts are properties of the checked-in files,
# and a deploy step that had not run yet would test stale bytes.
target_compile_definitions(
    rock_hero_common_ui_testing
    INTERFACE ROCK_HERO_BUILT_IN_THEMES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources/themes")

add_executable(
    rock_hero_common_ui_tests
    noop_render_device.cpp
    test_bgfx_color.cpp
    test_box_mute_profile.cpp
    test_head_art_profile.cpp
    test_highway_atlas.cpp
    test_highway_floor_geometry.cpp
    test_highway_head_marks.cpp
    test_highway_renderer_smoke.cpp
    test_highway_slide_path.cpp
    test_highway_texture_contract.cpp
    test_highway_theme_derived_colors.cpp
    test_highway_theme_file.cpp
    test_render_device.cpp
    test_string_color_palette.cpp
    test_tab_lane_layout.cpp
    test_tab_paint_core.cpp)

# The atlas units under test are private to the library, so tests reach their headers through the
# library's src tree; the private atlas header carries bgfx types, so the bgfx wrapper supplies
# its include paths.
target_include_directories(rock_hero_common_ui_tests PRIVATE ../src)

# The renderer smoke suite links the SHIPPED shader binaries: bgfx parses and validates a shader
# blob at the API layer on every backend, so the Noop run still proves the deployed pairs are the
# ones the renderer accepts. Stage them through the helper both products use so the program list
# stays stated in exactly one place; compilation is Windows-only (shaderc's HLSL backend), and
# elsewhere this stages an empty tree and the suite skips itself.
rock_hero_stage_highway_shaders(rock_hero_common_ui_test_shaders
                                "${CMAKE_CURRENT_BINARY_DIR}/resources/shaders")
add_dependencies(rock_hero_common_ui_tests rock_hero_common_ui_test_shaders)
target_compile_definitions(
    rock_hero_common_ui_tests
    PRIVATE ROCK_HERO_SHADERS_DIR="${CMAKE_CURRENT_BINARY_DIR}/resources/shaders/dx11")

# Tests link the library under test plus Catch2's generated main; the paint-core pixel tests
# rasterize through a software JUCE image, so they need the GUI module's scoped initialiser, and
# they chart the same standard tuning every other suite does and draw in the same fixture string
# colors, which the two testing targets state once.
target_link_libraries(
    rock_hero_common_ui_tests
    PRIVATE rock_hero::common::ui
            rock_hero::common::ui_testing
            rock_hero::common::core_testing
            rock_hero::bgfx
            rock_hero::build_policy
            rock_hero::juce_gui_basics
            Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_common_ui_tests ADD_TAGS_AS_LABELS)
