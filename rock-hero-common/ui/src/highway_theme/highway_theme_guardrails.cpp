#include "highway_theme/highway_theme_guardrails.h"

#include "highway_theme/highway_theme_roles.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>

namespace rock_hero::common::ui
{

namespace
{

// Both values are provisional, chosen so the shipped theme passes with margin (its closest pair
// lies 24.4 apart and its lit fret line 85 L* above the unlit one) until the built-ins are sighted
// side by side. They live here alone.
constexpr double g_minimum_delta_e = 15.0;
constexpr double g_minimum_fret_line_lightness_rise = 30.0;

using RoleMember = ArgbColor HighwayTheme::*;

// Two roles the notation tells apart by color alone.
struct DistinctRoles
{
    RoleMember first;
    RoleMember second;
};

constexpr std::array g_distinct_roles{
    // The fretting hand's chord box against a tapped chord's.
    DistinctRoles{.first = &HighwayTheme::chord_box, .second = &HighwayTheme::pick_furniture},
    // An arpeggio's rails against a held shape's.
    DistinctRoles{.first = &HighwayTheme::arpeggio, .second = &HighwayTheme::lane_border},
    // The three states of a fret number.
    DistinctRoles{
        .first = &HighwayTheme::fret_number_lit, .second = &HighwayTheme::fret_number_dim
    },
    DistinctRoles{
        .first = &HighwayTheme::fret_number_lit, .second = &HighwayTheme::fret_number_target
    },
    DistinctRoles{
        .first = &HighwayTheme::fret_number_dim, .second = &HighwayTheme::fret_number_target
    },
    // The two hands' strike pops.
    DistinctRoles{.first = &HighwayTheme::strike_fretting, .second = &HighwayTheme::strike_picking},
};

// A color in CIELAB under the D65 white point.
struct Cielab
{
    double lightness;
    double a;
    double b;
};

// Decodes one sRGB channel byte to linear light.
[[nodiscard]] double linearChannel(const ArgbColor color, const unsigned shift)
{
    const double encoded = static_cast<double>((color >> shift) & 0xFFU) / 255.0;
    return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
}

// Converts the color's RGB, ignoring its alpha, through linear sRGB and CIE XYZ to CIELAB.
[[nodiscard]] Cielab cielabOf(const ArgbColor color)
{
    const double red = linearChannel(color, 16U);
    const double green = linearChannel(color, 8U);
    const double blue = linearChannel(color, 0U);

    // XYZ relative to the D65 white, so white lands on (1, 1, 1).
    const double x = ((0.4124564 * red) + (0.3575761 * green) + (0.1804375 * blue)) / 0.95047;
    const double y = (0.2126729 * red) + (0.7151522 * green) + (0.0721750 * blue);
    const double z = ((0.0193339 * red) + (0.1191920 * green) + (0.9503041 * blue)) / 1.08883;

    // CIELAB's cube root, with its linear segment near black.
    const auto compand = [](const double t) {
        constexpr double delta = 6.0 / 29.0;
        return t > delta * delta * delta ? std::cbrt(t)
                                         : (t / (3.0 * delta * delta)) + (4.0 / 29.0);
    };
    const double fx = compand(x);
    const double fy = compand(y);
    const double fz = compand(z);
    return Cielab{
        .lightness = (116.0 * fy) - 16.0,
        .a = 500.0 * (fx - fy),
        .b = 200.0 * (fy - fz),
    };
}

// The CIE76 color difference: the straight-line distance in CIELAB.
[[nodiscard]] double deltaE(const ArgbColor first, const ArgbColor second)
{
    const Cielab lhs = cielabOf(first);
    const Cielab rhs = cielabOf(second);
    return std::hypot(lhs.lightness - rhs.lightness, lhs.a - rhs.a, lhs.b - rhs.b);
}

// Spells a color's RGB as the theme file does, for diagnostics.
[[nodiscard]] std::string hexOf(const ArgbColor color)
{
    return std::format("#{:06X}", color & 0xFFFFFFU);
}

} // namespace

// Runs the role pairs, the string pairs and the fret-line order, reporting the first failure.
std::expected<void, HighwayThemeError> checkHighwayThemeGuardrails(
    const HighwayTheme& colors, const StringColorPalette& strings)
{
    for (const DistinctRoles& roles : g_distinct_roles)
    {
        const double distance = deltaE(colors.*roles.first, colors.*roles.second);
        if (distance < g_minimum_delta_e)
        {
            return std::unexpected{HighwayThemeError{
                HighwayThemeErrorCode::GuardrailViolated,
                std::format(
                    "{} {} and {} {} lie {:.1f} apart in CIELAB; the notation tells them apart by "
                    "color, which needs at least {}",
                    highwayThemeRoleName(roles.first),
                    hexOf(colors.*roles.first),
                    highwayThemeRoleName(roles.second),
                    hexOf(colors.*roles.second),
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
