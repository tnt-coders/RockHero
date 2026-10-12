add_library(rock_hero_common_ui STATIC)
add_library(rock_hero::common::ui ALIAS rock_hero_common_ui)

# Shared presentation both products consume: the string-color palette (JUCE-free by design;
# consumers convert colors at their own boundary), the highway feature — the shared 3D
# note-highway renderer, its runtime-rasterized atlases and the texture contract its art must meet
# (promoted here so one renderer serves the game and the editor preview instead of duplicated thin
# drawers) — the highway theme (the color roles that renderer draws with, and the one loader that
# reads theme directories into a registry), and the tab feature: the shared 2D notation paint
# core, lane layout, and layout manifest. bgfx gets the Tracktion
# isolation treatment (implementation files only, never a public header); JUCE gets the same
# except the one designated juce_graphics-bearing paint-core header that
# architectural-principles.md "UI Modules" allows.
target_sources(
    rock_hero_common_ui
    PRIVATE src/highway/bgfx_program.cpp
            src/highway/box_mute_profile.cpp
            src/highway/head_art_profile.cpp
            src/highway/highway_atlas.cpp
            src/highway/highway_renderer.cpp
            src/highway/highway_texture_contract.cpp
            src/highway/structural_art.cpp
            src/highway_theme/highway_theme_derived_colors.cpp
            src/highway_theme/highway_theme_file.cpp
            src/highway_theme/highway_theme_guardrails.cpp
            src/highway_theme/highway_theme_registry.cpp
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
# designated paint-core header (tab_paint_core.h) carries juce::Graphics types to its consumers.
target_link_libraries(
    rock_hero_common_ui
    PUBLIC rock_hero::common::core rock_hero::juce_graphics
    PRIVATE rock_hero::build_policy rock_hero::bgfx)

# Both product deploys copy the built-in highway themes from here, one directory per theme (shared
# asset sources live under common and deploy per product). The path is published as a target
# property rather than spelled at each deploy site, so moving this directory fails the deploys at
# configure time instead of leaving them copying nothing.
set(rock_hero_common_ui_themes_dir "${CMAKE_CURRENT_SOURCE_DIR}/resources/themes")
set_target_properties(rock_hero_common_ui PROPERTIES ROCK_HERO_THEMES_DIR
                                                     "${rock_hero_common_ui_themes_dir}")

# The highway's fixed art (the technique-mark sheet every theme's heads compose over, and its
# license notice) deploys the same way, published for the same reason.
set(rock_hero_common_ui_highway_art_dir "${CMAKE_CURRENT_SOURCE_DIR}/resources/highway")
set_target_properties(rock_hero_common_ui PROPERTIES ROCK_HERO_HIGHWAY_ART_DIR
                                                     "${rock_hero_common_ui_highway_art_dir}")

add_subdirectory(tests)
