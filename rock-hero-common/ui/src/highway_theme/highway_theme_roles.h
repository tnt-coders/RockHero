/*!
\file highway_theme_roles.h
\brief The one table naming every HighwayTheme role, as the theme file spells it.
*/

#pragma once

#include <array>
#include <cstddef>
#include <rock_hero/common/ui/highway_theme/highway_theme.h>
#include <string_view>
#include <utility>

namespace rock_hero::common::ui
{

/*! \brief One color role: its key in a theme file's `colors` and the member it fills. */
struct HighwayThemeRole
{
    /*! \brief The key, which is the member's own name. */
    std::string_view name;

    /*! \brief The HighwayTheme member the key fills. */
    ArgbColor HighwayTheme::* member;
};

/*!
\brief Every role of HighwayTheme, in declaration order.

The loader reads each role through this table and the guardrails name roles from it, so a role's
key is spelled once. A new HighwayTheme member needs its row here; the assertion below refuses a
table that misses one.
*/
inline constexpr std::array g_highway_theme_roles{
    HighwayThemeRole{.name = "backdrop", .member = &HighwayTheme::backdrop},
    HighwayThemeRole{.name = "beat_bar", .member = &HighwayTheme::beat_bar},
    HighwayThemeRole{.name = "attack_line", .member = &HighwayTheme::attack_line},
    HighwayThemeRole{.name = "lit_lane", .member = &HighwayTheme::lit_lane},
    HighwayThemeRole{.name = "lit_lane_dotted", .member = &HighwayTheme::lit_lane_dotted},
    HighwayThemeRole{.name = "lane_line", .member = &HighwayTheme::lane_line},
    HighwayThemeRole{.name = "lane_line_lit", .member = &HighwayTheme::lane_line_lit},
    HighwayThemeRole{.name = "fret_line", .member = &HighwayTheme::fret_line},
    HighwayThemeRole{.name = "fret_line_lit", .member = &HighwayTheme::fret_line_lit},
    HighwayThemeRole{.name = "fret_number_lit", .member = &HighwayTheme::fret_number_lit},
    HighwayThemeRole{.name = "fret_number_target", .member = &HighwayTheme::fret_number_target},
    HighwayThemeRole{.name = "strike_fretting", .member = &HighwayTheme::strike_fretting},
    HighwayThemeRole{.name = "strike_picking", .member = &HighwayTheme::strike_picking},
    HighwayThemeRole{.name = "chord", .member = &HighwayTheme::chord},
    HighwayThemeRole{.name = "arpeggio", .member = &HighwayTheme::arpeggio},
    HighwayThemeRole{.name = "full_mute_mark", .member = &HighwayTheme::full_mute_mark},
    HighwayThemeRole{.name = "capo_shade", .member = &HighwayTheme::capo_shade},
    HighwayThemeRole{.name = "capo_bar", .member = &HighwayTheme::capo_bar},
    HighwayThemeRole{.name = "capo_bar_rim", .member = &HighwayTheme::capo_bar_rim},
    HighwayThemeRole{.name = "inlay_tint", .member = &HighwayTheme::inlay_tint},
    HighwayThemeRole{.name = "label_text", .member = &HighwayTheme::label_text},
};

// Every member is an ArgbColor, so the table covers the struct exactly when it has one row per
// ArgbColor the struct holds and no two rows fill the same member.
static_assert(
    []() consteval {
        for (std::size_t first = 0; first < g_highway_theme_roles.size(); ++first)
        {
            for (std::size_t second = first + 1; second < g_highway_theme_roles.size(); ++second)
            {
                if (g_highway_theme_roles.at(first).member ==
                        g_highway_theme_roles.at(second).member ||
                    g_highway_theme_roles.at(first).name == g_highway_theme_roles.at(second).name)
                {
                    return false;
                }
            }
        }
        return g_highway_theme_roles.size() * sizeof(ArgbColor) == sizeof(HighwayTheme);
    }(),
    "g_highway_theme_roles must name every HighwayTheme member exactly once");

/*!
\brief Returns the theme-file key of one role.
\param member The HighwayTheme member.
\return The role's key; every member has one, which the assertion above guarantees.
*/
[[nodiscard]] constexpr std::string_view highwayThemeRoleName(ArgbColor HighwayTheme::* member)
{
    for (const HighwayThemeRole& role : g_highway_theme_roles)
    {
        if (role.member == member)
        {
            return role.name;
        }
    }

    // The table covers every member; naming one it does not hold would be a lie, not a fallback.
    std::unreachable();
}

} // namespace rock_hero::common::ui
