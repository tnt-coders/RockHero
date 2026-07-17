add_library(rock_hero_common_ui STATIC)
add_library(rock_hero::common::ui ALIAS rock_hero_common_ui)

# Shared presentation both products consume: the string-color palette data (JUCE-free by design;
# consumers convert colors at their own boundary), the highway feature — the shared 3D
# note-highway renderer and its runtime-rasterized atlases (the 2026-07-11 promotion decision:
# one renderer for the game and the editor preview instead of duplicated thin drawers) — and the
# tab feature: the shared 2D notation paint core, lane layout, and layout manifest (plan 30
# Phase 2). bgfx gets the Tracktion isolation treatment (implementation files only, never a
# public header); JUCE gets the same except the one designated juce_graphics-bearing paint-core
# header allowed by the 30-Q1 amendment (architectural-principles.md "UI Modules").
target_sources(
    rock_hero_common_ui
    PRIVATE src/highway/bgfx_program.cpp
            src/highway/highway_atlas.cpp
            src/highway/highway_renderer.cpp
            src/render/render_device.cpp
            src/string_colors/string_color_palette.cpp
            src/tab/tab_lane_layout.cpp
            src/tab/tab_layout_manifest.cpp
            src/tab/tab_paint_core.cpp)

# The PRIVATE src dir lets library and test translation units reference private headers with
# feature-qualified quoted includes across feature folders.
target_include_directories(
    rock_hero_common_ui
    PUBLIC include
    PRIVATE include/rock_hero/common/ui src)

# common core is public because the palette sizes its extended tier from g_max_chart_strings and
# the highway renderer's public header consumes the shared highway view state. The render stack
# (bgfx) stays PRIVATE per the isolation treatment above; juce_graphics is PUBLIC because the
# designated paint-core header (tab_paint_core.h, 30-Q1) carries juce::Graphics types to its
# consumers.
target_link_libraries(
    rock_hero_common_ui
    PUBLIC rock_hero::common::core rock_hero::juce_graphics
    PRIVATE rock_hero::build_policy rock_hero::bgfx)

add_subdirectory(tests)
