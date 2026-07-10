add_library(rock_hero_common_ui STATIC)
add_library(rock_hero::common::ui ALIAS rock_hero_common_ui)

# Shared presentation data both products consume. Deliberately JUCE-free so the game render
# stack can link it without juce_graphics; consumers convert colors at their own boundary.
target_sources(rock_hero_common_ui PRIVATE src/string_colors/string_color_palette.cpp)

# The PRIVATE src dir lets library and test translation units reference private headers with
# feature-qualified quoted includes across feature folders.
target_include_directories(
    rock_hero_common_ui
    PUBLIC include
    PRIVATE include/rock_hero/common/ui src)

# common core is public because the palette sizes its extended tier from g_max_chart_strings, so
# raising the cap is compiler-enforced across every registered preset.
target_link_libraries(
    rock_hero_common_ui
    PUBLIC rock_hero::common::core
    PRIVATE rock_hero::build_policy)

add_subdirectory(tests)
