/*!
\file highway_theme_fixtures.h
\brief Code-built highway looks, and the shipped themes the source tree holds.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/ui/highway_theme/highway_theme.h>
#include <rock_hero/common/ui/highway_theme/highway_theme_registry.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>
#include <stdexcept>
#include <utility>

namespace rock_hero::common::ui::testing
{

/*!
\brief Eight distinct, saturated string colors, highest string first.

The pixel suites pin lane colors derived from these exact values, so they are stated here once
rather than read from a shipped theme, whose colors change with its design.

\return Purple, green, orange, blue, yellow and red for a guitar's six, then teal and grey.
*/
[[nodiscard]] inline StringColorPalette fixtureStringColors()
{
    return StringColorPalette{
        .colors = {
            0xffd22cf8, // purple, string 1 (high e)
            0xff85e747, // green
            0xffff870a, // orange
            0xff25b2ff, // blue
            0xfff2d706, // yellow
            0xffed0000, // red, string 6 (low E)
            0xff00b5a0, // teal, string 7
            0xffb6b6b6, // grey, string 8
        },
    };
}

/*!
\brief A theme whose every role holds its own color, for renderer suites.
\return Distinct opaque roles, apart from the translucent lanes, capo shade and dim fret number.
*/
[[nodiscard]] inline HighwayTheme fixtureHighwayTheme()
{
    return HighwayTheme{
        .backdrop = 0xff000000,
        .beat_bar = 0xff203050,
        .attack_line = 0xffe0f0ff,
        .lit_lane = 0x404080ff,
        .lit_lane_dotted = 0x40204080,
        .lane_border = 0xff408090,
        .fret_line = 0xff202020,
        .fret_line_lit = 0xffc0c0c0,
        .fret_number_lit = 0xff80d0f0,
        .fret_number_dim = 0x80408090,
        .fret_number_target = 0xffffa020,
        .strike_fretting = 0xffffb040,
        .strike_picking = 0xffffffff,
        .chord_box = 0xff00d0d0,
        .chord_box_dark = 0xff003040,
        .pick_furniture = 0xffc0c8d8,
        .full_mute_mark = 0xff507890,
        .arpeggio = 0xffc040ff,
        .capo_shade = 0xb4000000,
        .capo_bar = 0xffb0b8c0,
        .capo_bar_rim = 0xff101418,
        .inlay_tint = 0xffffffff,
        .label_text = 0xfff0f0f0,
    };
}

/*!
\brief Returns the source tree's folder of built-in themes, which the products deploy.
\return The folder holding one directory per shipped theme.
*/
[[nodiscard]] inline std::filesystem::path builtInThemesDirectory()
{
    return common::core::pathFromUtf8(ROCK_HERO_BUILT_IN_THEMES_DIR);
}

/*!
\brief The shipped built-in themes and no user themes, for hosts that resolve the player's look.

The editor's views take the registry their pickers list and their preview and tab lane resolve
against; it is loaded once per test process.

\return The registry, valid for the life of the test process.
*/
[[nodiscard]] inline const HighwayThemeRegistry& fixtureHighwayThemeRegistry()
{
    static const HighwayThemeRegistry g_registry = [] {
        // An empty user folder path names no folder, so it contributes no themes.
        std::expected<HighwayThemeRegistry, HighwayThemeError> loaded = HighwayThemeRegistry::load(
            HighwayThemeFolders{.built_in = builtInThemesDirectory(), .user = {}});
        if (!loaded.has_value())
        {
            throw std::runtime_error{loaded.error().message};
        }
        return std::move(*loaded);
    }();
    return g_registry;
}

/*!
\brief Returns the source tree's directory of the default theme, whose art the suites measure.
\return The default theme's directory.
*/
[[nodiscard]] inline std::filesystem::path defaultThemeDirectory()
{
    return builtInThemesDirectory() / g_default_highway_theme_id;
}

} // namespace rock_hero::common::ui::testing
