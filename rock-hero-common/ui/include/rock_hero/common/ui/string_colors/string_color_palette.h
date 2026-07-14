/*!
\file string_color_palette.h
\brief Shared string-color palette data and Charter-exact color derivation.
*/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <string_view>

namespace rock_hero::common::ui
{

/*!
\brief Packed 0xAARRGGBB color value.

JUCE-free by design so the game render stack can consume the palette without juce_graphics; each
consumer converts at its own boundary (juce::Colour{value}, bgfx vertex color). Every palette
value and every derivation result carries an opaque 0xff alpha.
*/
using ArgbColor = std::uint32_t;

/*! \brief One selectable string-color preset shared by the editor and the game. */
struct StringColorPalette
{
    /*! \brief Stable identifier persisted by product settings; never renamed once shipped. */
    std::string_view id{};

    /*! \brief Six-string-window base colors, lowest lane of the window first. */
    std::array<ArgbColor, 6> standard{};

    /*!
    \brief Colors for lanes below the six-string window, seventh string downward.

    Sized by the chart string cap on purpose: raising common::core::g_max_chart_strings refuses
    to compile until every registered preset defines the new lane colors, so the string-cap
    domain gate is compiler-enforced.
    */
    std::array<ArgbColor, static_cast<std::size_t>(common::core::g_max_chart_strings) - 6U>
        extended{};
};

/*!
\brief Returns the base display color for one string lane.

The six highest lanes take the palette's standard colors, anchored so the sixth-highest lane is
the first standard color: a four-string bass keeps the same low-string colors as a guitar's
bottom four. Lanes below the standard window walk the extended tier downward and cycle it
defensively past its end.

\param displayed_string Lane's string position, 1 = lowest displayed lane.
\param displayed_string_count Total number of displayed lanes.
\param palette Preset supplying the standard and extended tiers.
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

The derivation chain is bit-identical to Charter's fixed multiply/brighten/darken sequence, so
the editor tab lane, the editor 3D preview, and the game highway render identical string styles
from one authority.
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

    /*! \brief Accent glow: the ring brightened twice. */
    ArgbColor accent;

    /*!
    \brief Derives every surface from one base string color through Charter's fixed chain.
    \param base Base string color the surfaces derive from.
    */
    explicit StringLaneStyle(ArgbColor base);
};

/*!
\brief Returns the Charter Classic preset, the default palette both products start with.
\return The Charter Classic palette data the render stack consumes.
*/
[[nodiscard]] const StringColorPalette& charterClassicPalette();

} // namespace rock_hero::common::ui
