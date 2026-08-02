/*!
\file box_mute_profile.h
\brief Cross-section profiles for the repeat-box mute marks, measured from chords.png.
*/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace juce
{
class Image;
} // namespace juce

namespace rock_hero::common::ui
{

/*! \brief Number of RGBA samples one mark's cross-section ramp carries. */
inline constexpr std::size_t g_box_mute_ramp_samples = 64;

/*!
\brief One mark's measured cross-section: the art's colors as a function of distance from the
arm centerline, plus the fractions the renderer needs to lay the arms out per box.

Fractions are of the glyph rect height — the >=50%-alpha bounding box the arms run corner to
corner of — which is the height the renderer maps onto the box, so authored ratios render
exactly.
*/
struct BoxMuteProfile
{
    /*!
    \brief Straight (unpremultiplied) RGBA8 ramp over centerline distance [0, extent], ordered
    R, G, B, A per sample; the final sample is fully transparent by construction so sampling
    past the extent dissolves cleanly.
    */
    std::array<std::uint8_t, g_box_mute_ramp_samples * 4> ramp{};

    /*!
    \brief Arm stroke half width as a fraction of the glyph rect height — the outermost
    falling crossing of half the glyph's peak alpha, so faint or hollow cores and rims
    authored below full opacity all anchor to the rim's outer edge.
    */
    double stroke_half_fraction{0.0};

    /*!
    \brief Ramp extent as a fraction of the glyph rect height, ending just past the last
    visible art; extent minus stroke half is the art's visible reach past the stroke edge.
    */
    double extent_fraction{0.0};
};

/*! \brief Both marks' profiles in chords.png row order: palm first, full second. */
struct BoxMuteProfiles
{
    /*! \brief Palm-mute mark profile (top cell). */
    BoxMuteProfile palm;

    /*! \brief Full-mute mark profile (bottom cell). */
    BoxMuteProfile full;
};

/*!
\brief Measures both box-mute mark profiles from a decoded chords.png image.

The authoring contract (the file is the single source of truth for the marks' look): two
equal cells stacked vertically, each carrying one X glyph in final display colors and
straight alpha, drawn as it renders. The palm mute (top) runs edge to edge of its cell —
arms corner to corner of the whole cell, cut raw by the cell boundary with no rim on the
outer edges, exactly as the repeat panel cuts it. The full mute (below) is a contained
square glyph inset in its cell, arms clipped to that square so each tip is an axis-aligned
corner like the note art's squared tips. Cores may be solid, faint, or fully hollow, and
rims may be authored below full opacity; the stroke edge is always the rim's outer falloff
at half the glyph's peak alpha. The measurement averages perpendicular cross-section cuts
over the arms' outer spans (clear of the crossing and the tips), so what is painted is
exactly what the box marks render.

\param image Decoded chords.png in any JUCE pixel format.
\return Both profiles, or empty when the image is not a two-cell stack of analyzable glyphs —
        the renderer treats that as an invalid required asset.
*/
[[nodiscard]] std::optional<BoxMuteProfiles> measureBoxMuteProfiles(const juce::Image& image);

/*!
\brief Decodes chords.png bytes and measures both profiles.

\param png_bytes The chords.png file contents.
\return Both profiles, or empty when the bytes are empty, undecodable, or unanalyzable.
*/
[[nodiscard]] std::optional<BoxMuteProfiles> measureBoxMuteProfiles(
    std::span<const std::byte> png_bytes);

} // namespace rock_hero::common::ui
