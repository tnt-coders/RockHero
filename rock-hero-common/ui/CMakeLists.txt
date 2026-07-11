add_library(rock_hero_common_ui STATIC)
add_library(rock_hero::common::ui ALIAS rock_hero_common_ui)

# Shared presentation both products consume: the string-color palette data (JUCE-free by design;
# consumers convert colors at their own boundary) and the highway feature — the shared 3D
# note-highway renderer and its runtime-rasterized atlases (the 2026-07-11 promotion decision:
# one renderer for the game and the editor preview instead of duplicated thin drawers). bgfx and
# JUCE get the Tracktion isolation treatment: implementation files only, never a public header.
target_sources(
    rock_hero_common_ui
    PRIVATE src/highway/bgfx_program.cpp src/highway/highway_atlas.cpp
            src/highway/highway_renderer.cpp src/render/render_device.cpp
            src/string_colors/string_color_palette.cpp)

# The PRIVATE src dir lets library and test translation units reference private headers with
# feature-qualified quoted includes across feature folders.
target_include_directories(
    rock_hero_common_ui
    PUBLIC include
    PRIVATE include/rock_hero/common/ui src)

# common core is public because the palette sizes its extended tier from g_max_chart_strings and
# the highway renderer's public header consumes the shared highway view state. The render stack
# (bgfx) and JUCE stay PRIVATE per the isolation treatment above.
target_link_libraries(
    rock_hero_common_ui
    PUBLIC rock_hero::common::core
    PRIVATE rock_hero::build_policy rock_hero::bgfx rock_hero::juce_graphics)

add_subdirectory(tests)
