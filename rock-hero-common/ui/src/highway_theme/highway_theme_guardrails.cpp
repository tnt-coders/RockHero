#include "highway_theme/highway_theme_guardrails.h"

#include "highway_theme/highway_theme_derived_colors.h"
#include "highway_theme/highway_theme_roles.h"

#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

namespace rock_hero::common::ui
{

namespace
{

// Both values are provisional, chosen so the shipped theme passes with margin (its closest pair,
// the two picking-hand colors, lies 21.1 apart and its lit fret line 85 L* above the unlit one)
// until the built-ins are sighted side by side. They live here alone.
constexpr double g_minimum_delta_e = 15.0;
constexpr double g_minimum_fret_line_lightness_rise = 30.0;

// The lightest a tab ink may be and still carry the white chip label: L* 60 is about APCA Lc 62
// under white, a little past its floor for bold label text.
constexpr double g_maximum_tab_ink_lightness = 60.0;

// One color the notation reads, named as a diagnostic names it: a role by its key, a derived color
// by what it paints.
struct NamedColor
{
    std::string_view name;
    ArgbColor color;
};

// A role's color under its key.
[[nodiscard]] NamedColor roleColor(const HighwayTheme& colors, ArgbColor HighwayTheme::* member)
{
    return NamedColor{.name = highwayThemeRoleName(member), .color = colors.*member};
}

// Spells a color's RGB as the theme file does, for diagnostics.
[[nodiscard]] std::string hexOf(const ArgbColor color)
{
    return std::format("#{:06X}", color & 0xFFFFFFU);
}

} // namespace

// Runs the color pairs, the string pairs and the fret-line order, reporting the first failure.
std::expected<void, HighwayThemeError> checkHighwayThemeGuardrails(
    const HighwayTheme& colors, const TabShapeInks& tab_inks, const StringColorPalette& strings)
{
    const NamedColor chord = roleColor(colors, &HighwayTheme::chord);
    const NamedColor arpeggio = roleColor(colors, &HighwayTheme::arpeggio);
    const NamedColor picking_chord{
        .name = "the picking hand's chord", .color = pickingHandColor(colors.chord)
    };
    const NamedColor picking_arpeggio{
        .name = "the picking hand's arpeggio", .color = pickingHandColor(colors.arpeggio)
    };
    const NamedColor number_lit = roleColor(colors, &HighwayTheme::fret_number_lit);
    const NamedColor number_dim{.name = "the dim fret number", .color = fretNumberDim(colors)};
    const NamedColor number_target = roleColor(colors, &HighwayTheme::fret_number_target);
    const NamedColor tab_chord{.name = "the tab lane's chord", .color = tab_inks.chord};
    const NamedColor tab_arpeggio{.name = "the tab lane's arpeggio", .color = tab_inks.arpeggio};
    const std::array<std::array<NamedColor, 2>, 9> distinct_pairs{{
        // The two kinds of span on each surface, and each kind's two hands.
        {chord, arpeggio},
        {tab_chord, tab_arpeggio},
        {chord, picking_chord},
        {arpeggio, picking_arpeggio},
        {picking_chord, picking_arpeggio},
        // The three states of a fret number.
        {number_lit, number_dim},
        {number_lit, number_target},
        {number_dim, number_target},
        // The two hands' strike pops.
        {roleColor(colors, &HighwayTheme::strike_fretting),
         roleColor(colors, &HighwayTheme::strike_picking)},
    }};

    for (const auto& [first, second] : distinct_pairs)
    {
        const double distance = deltaE(first.color, second.color);
        if (distance < g_minimum_delta_e)
        {
            return std::unexpected{HighwayThemeError{
                HighwayThemeErrorCode::GuardrailViolated,
                std::format(
                    "{} {} and {} {} lie {:.1f} apart in CIELAB; the notation tells them apart by "
                    "color, which needs at least {}",
                    first.name,
                    hexOf(first.color),
                    second.name,
                    hexOf(second.color),
                    distance,
                    g_minimum_delta_e),
            }};
        }
    }

    for (std::size_t first = 0; first < strings.colors.size(); ++first)
    {
        for (std::size_t second = first + 1; second < strings.colors.size(); ++second)
        {
            const double distance = deltaE(strings.colors.at(first), strings.colors.at(second));
            if (distance < g_minimum_delta_e)
            {
                return std::unexpected{HighwayThemeError{
                    HighwayThemeErrorCode::GuardrailViolated,
                    std::format(
                        "strings {} {} and {} {} lie {:.1f} apart in CIELAB; two strings need at "
                        "least {}",
                        first + 1,
                        hexOf(strings.colors.at(first)),
                        second + 1,
                        hexOf(strings.colors.at(second)),
                        distance,
                        g_minimum_delta_e),
                }};
            }
        }
    }

    for (const NamedColor& ink : std::array{tab_chord, tab_arpeggio})
    {
        const double lightness = cielabOf(ink.color).lightness;
        if (lightness > g_maximum_tab_ink_lightness)
        {
            return std::unexpected{HighwayThemeError{
                HighwayThemeErrorCode::GuardrailViolated,
                std::format(
                    "{} {} is L* {:.1f}; its chip prints a white label, which needs at most {}",
                    ink.name,
                    hexOf(ink.color),
                    lightness,
                    g_maximum_tab_ink_lightness),
            }};
        }
    }

    const double rise =
        cielabOf(colors.fret_line_lit).lightness - cielabOf(colors.fret_line).lightness;
    if (rise < g_minimum_fret_line_lightness_rise)
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::GuardrailViolated,
            std::format(
                "fret_line_lit {} is {:.1f} L* lighter than fret_line {}; the hand window is read "
                "from it, which needs at least {}",
                hexOf(colors.fret_line_lit),
                rise,
                hexOf(colors.fret_line),
                g_minimum_fret_line_lightness_rise),
        }};
    }

    return {};
}

} // namespace rock_hero::common::ui
