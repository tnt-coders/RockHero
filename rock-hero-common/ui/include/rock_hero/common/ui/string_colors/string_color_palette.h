/*!
\file string_color_palette.h
\brief The string colors a look draws with, and Charter-exact color derivation.
*/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <rock_hero/common/core/chart/chart_rules.h>

namespace rock_hero::common::ui
{

/*!
\brief Packed 0xAARRGGBB color value.

JUCE-free by design so the game render stack can consume colors without juce_graphics; each
consumer converts at its own boundary (juce::Colour{value}, bgfx vertex color). The alpha byte is
the color's opacity: a theme role may carry any, while palette colors and every derivation result
below are opaque.
*/
using ArgbColor = std::uint32_t;

/*!
\brief The string colors one look draws with, one per string the chart format allows.

Ordered in the player's string numbering from the highest string down: entry 0 is string 1 (the
high e), entry 5 the low E, entry 6 the low B and entry 7 the low F#, so each slot names the same
string at every string count. stringLaneColor maps a displayed lane onto its slot. Every color is
opaque; the theme loader refuses a palette that is not.
*/
struct StringColorPalette
{
    /*! \brief One opaque color per string, highest string first. */
    std::array<ArgbColor, static_cast<std::size_t>(common::core::g_max_chart_strings)> colors{};
};

/*!
\brief Returns the base display color for one string lane.

A chart of six strings or fewer takes its slots from the low E upward, so a four-string bass keeps
the colors of a guitar's bottom four; each string below the sixth takes the next slot down. A lane
outside the palette takes the nearest slot.

\param displayed_string Lane's string position, 1 = lowest displayed lane.
\param displayed_string_count Total number of displayed lanes.
\param palette String colors the lane draws with.
\return Base lane color every rendered per-string surface derives from.
*/
[[nodiscard]] ArgbColor stringLaneColor(
    int displayed_string, int displayed_string_count, const StringColorPalette& palette);

/*!
\brief Reproduces java.awt.Color.darker(): each channel scaled by 0.7 and truncated.
\param color Base color; the result is always opaque.
\return Darkened color.
*/
[[nodiscard]] ArgbColor darkerColor(ArgbColor color);

/*!
\brief Reproduces java.awt.Color.brighter(), including the small-channel bump.
\param color Base color; the result is always opaque.
\return Brightened color, bit-identical to the java semantics Charter uses.
*/
[[nodiscard]] ArgbColor brighterColor(ArgbColor color);

/*!
\brief Reproduces Charter's ColorUtils.multiplyColor with truncation.
\param color Base color; the result is always opaque.
\param multiplier Per-channel scale factor.
\return Scaled color with each channel clamped to the byte range.
*/
[[nodiscard]] ArgbColor multiplyColor(ArgbColor color, double multiplier);

/*!
\brief The seven per-string surfaces Charter derives from one base string color.

The derivation chain is bit-identical to Charter's fixed multiply/brighten/darken sequence, with
one deliberate divergence: the accent color brightens hue-preservingly instead of with Charter's
per-channel double brighten, which bleaches saturated hues toward yellow (an orange or green
accent glow would render yellow). All consumers — the editor tab lane, the editor 3D preview, and
the game highway — render identical string styles from this one authority.
*/
struct StringLaneStyle
{
    /*! \brief String line: base multiplied by 0.8. */
    ArgbColor lane;

    /*! \brief Note ring: the lane color brightened. */
    ArgbColor border_inner;

    /*! \brief Note fill: the ring darkened twice. */
    ArgbColor inner;

    /*! \brief Linked-note fill: the fill darkened twice more. */
    ArgbColor linked_inner;

    /*! \brief Sustain fill: base multiplied by 0.66. */
    ArgbColor tail;

    /*! \brief Sustain border: the tail color brightened. */
    ArgbColor tail_edge;

    /*!
    \brief Accent glow: the ring brightened toward Charter's double-brighter gain, but scaled
    uniformly across channels so the hue never shifts, stopping where the hottest channel
    saturates.
    */
    ArgbColor accent;

    /*!
    \brief Derives every surface from one base string color through Charter's fixed chain.
    \param base Base string color the surfaces derive from.
    */
    explicit StringLaneStyle(ArgbColor base);
};

} // namespace rock_hero::common::ui
