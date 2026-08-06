/*!
\file box_mute_profile.h
\brief Cross-section profiles for the repeat-box mute marks, measured from chords.png.
*/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace juce
{
class Image;
} // namespace juce

namespace rock_hero::common::ui
{

/*! \brief Number of RGBA samples one mark's cross-section ramp carries. */
inline constexpr std::size_t g_box_mute_ramp_samples = 64;

/*! \brief Why a chords.png measurement failed; the renderer reports it as an invalid asset. */
enum class BoxMuteProfileError : std::uint8_t
{
    /*! \brief The bytes are empty or do not decode as an image. */
    UndecodableImage,

    /*! \brief The image is not a two-cell stack of glyphs the contract can measure. */
    UnanalyzableGlyph,

    /*!
    \brief The image carries a real alpha channel. The contract requires opacity in the coverage
    channel instead, because JUCE premultiplies an alpha-bearing PNG at decode and would silently
    scale every channel of the structural scheme by it.
    */
    AlphaBearingImage,
};

/*!
\brief One mark's measured cross-section: the art's tint weighting and coverage as a function of
distance from the arm centerline, plus the fractions the renderer needs to lay the arms out per
box.

Fractions are of the glyph rect height — the >=50%-coverage bounding box the arms run corner to
corner of — which is the height the renderer maps onto the box, so authored ratios render
exactly.
*/
struct BoxMuteProfile
{
    /*!
    \brief Ramp over centerline distance [0, extent], four bytes per sample to match the RGBA8
    texture it uploads into. R, G, B carry the structural-art channel scheme shared with the note
    atlas — tint weight, achromatic lift, coverage — and the fourth byte is padding the shader
    never reads (the art has no alpha channel, so decode fills it opaque). The final sample is
    forced to zero, so sampling past the extent dissolves cleanly.
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

The authoring contract (the file is the single source of truth for the marks' structure): two
equal cells stacked vertically, each carrying one X glyph in the structural-art channel scheme
shared with the note atlas — R weights the mark's tint, G lifts achromatically toward white, B is
coverage — drawn as it renders.

The image must carry NO alpha channel. JUCE premultiplies an alpha-bearing PNG at decode, which
would silently scale all three signals and, at near-zero alpha, amplify the round trip into values
no art can hold; a PNG without an alpha chunk decodes bit-exact at every level. Opacity therefore
lives in B, normalized so each cell's rim plateau reaches full coverage, and the renderer supplies
the absolute value as vertex alpha. That is what makes the palm mark's rim composite identically
to the frame bar it meets: both read their opacity from the same constant instead of agreeing by
coincidence. No hue is in the art at all, so retuning a color never needs a repaint.

The palm mute (top) runs edge to edge of its cell — arms corner to corner of the whole cell, cut
raw by the cell boundary with no rim on the outer edges, exactly as the repeat panel cuts it. The
full mute (below) is a contained square glyph inset in its cell, arms clipped to that square so
each tip is an axis-aligned corner like the note art's squared tips. Cores may be solid, faint, or
fully hollow, and a rim need not reach full coverage; the stroke edge is always the rim's outer
falloff at half the glyph's peak coverage. The measurement averages perpendicular cross-section
cuts over the arms' outer spans (clear of the crossing and the tips), so what is painted is
exactly what the box marks render.

\param image Decoded chords.png. Must carry no alpha channel, per the contract above.
\return Both profiles, or the measurement failure — the renderer treats any failure as an
        invalid required asset.
*/
[[nodiscard]] std::expected<BoxMuteProfiles, BoxMuteProfileError> measureBoxMuteProfiles(
    const juce::Image& image);

/*!
\brief Decodes chords.png bytes and measures both profiles.

\param png_bytes The chords.png file contents.
\return Both profiles, or the decode or measurement failure.
*/
[[nodiscard]] std::expected<BoxMuteProfiles, BoxMuteProfileError> measureBoxMuteProfiles(
    std::span<const std::byte> png_bytes);

} // namespace rock_hero::common::ui
