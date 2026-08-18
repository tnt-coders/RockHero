/*!
\file head_art_profile.h
\brief The head art's silhouette, measured from notes.png at load time.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <span>

namespace juce
{
class Image;
} // namespace juce

namespace rock_hero::common::ui
{

/*! \brief Why a notes.png silhouette measurement failed; the renderer reports it as an invalid
    asset. */
enum class HeadArtProfileError : std::uint8_t
{
    /*! \brief The bytes are empty or do not decode as an image. */
    UndecodableImage,

    /*! \brief A head cell does not carry a silhouette the contract can measure. */
    UnanalyzableArt,

    /*!
    \brief The PNG file carries a real alpha channel. The contract requires opacity in the
    coverage channel instead, because JUCE premultiplies an alpha-bearing PNG at decode and would
    silently scale every channel of the structural scheme by it. This is a fact about the file,
    not about the decoded image: macOS decodes every PNG to ARGB whatever the file's color type.
    */
    AlphaBearingImage,
};

/*!
\brief The head art's measured silhouette, in atlas texels of the drawn quad's index space.

Every field is a pure function of the shipped PNG, measured at load so a rebaked atlas can never
leave the accent glow tracing a silhouette the art no longer has — the failure that hand-kept
constants invited, where a mismatch shows up as light with nothing under it.

Extents are fitted to the art's 50%-of-peak COVERAGE contour (coverage is the B channel of the
structural scheme; the shipped file carries no alpha). The threshold is load-bearing: the corner
radius is a strong function of it — fitted to the antialias tail instead, the same art measures
roughly twice the radius — so every field here reads the same contour.

Centres are offsets from the drawn quad's centre, +x right and +y up. They are NOT zero and not
an art defect: odd-sized art inside an even cell cannot be quad-centred by construction, so it
sits half a texel off on both axes (plus any authored banding asymmetry). Modelling this is what
makes a symmetric distance field fit an asymmetric silhouette; without it the glow's ridge lands
up to a texel off the art's edge, bright on bare texture along two edges and buried under the
head along the other two.
*/
struct HeadArtProfile
{
    /*! \brief Rectangle head: half-width of the 50% contour. */
    double half_width_texels{0.0};

    /*! \brief Rectangle head: half-height of the 50% contour. */
    double half_height_texels{0.0};

    /*!
    \brief Rectangle head: corner radius fitted to the 50% contour.

    A DESCRIPTION of the contour, not a construction — the art is a solid rectangle wrapped in a
    one-texel fringe whose corner texel is omitted, so there is no authored radius to read, but as
    a contour description the fit is exact to well under a texel, which is all the field needs.
    */
    double corner_texels{0.0};

    /*! \brief Rectangle head: silhouette centre offset from the quad centre, +x right. */
    double center_x_texels{0.0};

    /*! \brief Rectangle head: silhouette centre offset from the quad centre, +y up. */
    double center_y_texels{0.0};

    /*!
    \brief Node-head diamond: half-span from centre to vertex, equal on both axes.

    Measured from the edge law |x| + |y| = span over the 50% contour, which holds exactly on the
    shipped art — so the glow's rhombus branch is exact here rather than approximate. Handing the
    rectangle's extents to that branch would draw a light far too wide and far too short.
    */
    double node_half_span_texels{0.0};

    /*! \brief Node-head diamond: silhouette centre offset from the quad centre, +x right. */
    double node_center_x_texels{0.0};

    /*! \brief Node-head diamond: silhouette centre offset from the quad centre, +y up. */
    double node_center_y_texels{0.0};
};

/*!
\brief Measures the head silhouettes from a decoded notes.png image.

Reads the standard head cell and the node-head base cell of the atlas grid (the same
width-over-four cell derivation the atlas layout uses). Only the coverage channel is read; any
alpha the decoder attached is ignored, because whether the ART carries alpha is a question about
the file that only the byte overload below can answer.

\param image Decoded notes.png.
\return The measured profile, or the measurement failure — the renderer treats any failure as an
        invalid required asset.
*/
[[nodiscard]] std::expected<HeadArtProfile, HeadArtProfileError> measureHeadArtProfile(
    const juce::Image& image);

/*!
\brief Decodes notes.png bytes, enforces the no-alpha rule, and measures the head silhouettes.

This is the entry point the renderer uses: it is the only place the file's own alpha state is
still known, so it is where an alpha-bearing re-bake is rejected.

\param png_bytes The notes.png file contents.
\return The measured profile, or the decode, alpha, or measurement failure.
*/
[[nodiscard]] std::expected<HeadArtProfile, HeadArtProfileError> measureHeadArtProfile(
    std::span<const std::byte> png_bytes);

} // namespace rock_hero::common::ui
