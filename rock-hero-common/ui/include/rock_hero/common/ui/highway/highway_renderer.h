/*!
\file highway_renderer.h
\brief Shared bgfx renderer for the 3D note highway, consumed by the game and the editor preview.
*/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <rock_hero/common/core/highway/highway_resources.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::common::ui
{

/*! \brief One shader program's compiled stage binaries. */
struct HighwayShaderPair
{
    /*! \brief Compiled vertex-stage binary. */
    std::vector<std::byte> vertex;

    /*! \brief Compiled fragment-stage binary. */
    std::vector<std::byte> fragment;
};

/*!
\brief The compiled shader programs the highway renderer links at creation.

Indexed by common::core::indexOf(HighwayShaderProgram), which is what each program means; the
enumeration itself carries the per-program documentation and the base name the binaries are built
under. Each product fills the set by walking common::core::g_highway_shader_programs through its
own resource loading (the game's resource pack, the editor's deployed preview resources); the
renderer itself never touches the filesystem.
*/
using HighwayShaderSet =
    std::array<HighwayShaderPair, common::core::g_highway_shader_programs.size()>;

/*!
\brief Texture assets the highway renderer uploads at creation.

Indexed by common::core::indexOf(HighwayTexture), which carries the per-asset documentation and the
file name each asset deploys as. Every entry is REQUIRED product content: empty or undecodable
bytes fail create with a typed error, because a missing texture means a broken install, not a
degradable state — the procedural fallbacks this replaces silently masked such failures.
*/
using HighwayTextureSet =
    std::array<std::vector<std::byte>, common::core::g_highway_textures.size()>;

/*! \brief Stable reasons the highway renderer can fail to come up. */
enum class HighwayRendererErrorCode : std::uint8_t
{
    /*! \brief bgfx rejected a shader binary or failed to link a program. */
    ProgramCreationFailed,

    /*! \brief A required texture asset was missing, undecodable, or the wrong shape. */
    TextureAssetInvalid,
};

/*! \brief Typed boundary error for highway renderer startup failures. */
struct [[nodiscard]] HighwayRendererError
{
    /*! \brief Stable failure reason for program branching. */
    HighwayRendererErrorCode code{};

    /*! \brief Human-readable diagnostic for logs and stderr. */
    std::string message;
};

/*! \brief One screen-space rectangle for the overlay view, in pixels from the top-left corner. */
struct HighwayOverlayRect
{
    /*! \brief Left edge in pixels. */
    float left{0.0F};

    /*! \brief Top edge in pixels. */
    float top{0.0F};

    /*! \brief Right edge in pixels. */
    float right{0.0F};

    /*! \brief Bottom edge in pixels. */
    float bottom{0.0F};

    /*! \brief Packed ABGR color (alpha-blended). */
    std::uint32_t abgr{0};
};

/*!
\brief Renders the note highway from the shared headless scene model.

One renderer serves both products, superseding plan 44's duplicated-thin-drawers recommendation:
the game shell and the editor preview each own a bgfx device and feed this renderer their
compiled shaders, view state, and per-frame time. bgfx never appears in this header — the
framework stays isolated to implementation files, the same treatment Tracktion receives in
common/audio.

Lifetime: create only while a bgfx device is live, destroy before bgfx shutdown (every owned GPU
resource dies with this object). All methods run on the bgfx API thread.

The renderer is a pure consumer: it never derives song time itself and draws exactly what the
view state plus the per-frame time arguments describe (plan 25's event feed and technique phases
extend the drawers, not this contract).
*/
class HighwayRenderer
{
public:
    /*!
    \brief Links the shader programs and uploads the highway atlases and textures.

    \param shaders Compiled stage binaries for the highway programs.
    \param textures Texture assets; every member is required, and a missing or invalid one
           fails creation with TextureAssetInvalid.
    \return The renderer, or a typed error naming the failed program or invalid texture
            asset.
    */
    [[nodiscard]] static std::expected<HighwayRenderer, HighwayRendererError> create(
        const HighwayShaderSet& shaders, const HighwayTextureSet& textures);

    /*! \brief Destroys every owned GPU resource; must run before bgfx shutdown. */
    ~HighwayRenderer();

    /*!
    \brief Transfers renderer ownership; the source becomes empty and tears nothing down.
    \param other Renderer losing ownership.
    */
    HighwayRenderer(HighwayRenderer&& other) noexcept;

    /*!
    \brief Replaces this renderer with another; the source becomes empty.
    \param other Renderer losing ownership.
    \return This renderer.
    */
    HighwayRenderer& operator=(HighwayRenderer&& other) noexcept;

    HighwayRenderer(const HighwayRenderer&) = delete;
    HighwayRenderer& operator=(const HighwayRenderer&) = delete;

    /*!
    \brief Replaces the chart content and rebuilds the retained board geometry.

    \param state Seconds-resolved highway content from the shared projection.
    */
    void setViewState(common::core::HighwayViewState state);

    /*!
    \brief EXPERIMENT SCAFFOLDING — advances the accent light to the next candidate.

    The accent light is being sighted in the app rather than argued, so every candidate ships at
    once and the editor cycles them from a keybind. Deleted along with the candidate table once
    the light is chosen and its numbers move inline.

    The GHOST end of the axis carries no candidates: it was sighted and settled, so only the
    accent is still being judged.

    \return Text naming the active accent candidate.
    */
    [[nodiscard]] std::string cycleAccentStyle();

    /*!
    \brief EXPERIMENT SCAFFOLDING — advances the note family's size to the next candidate.

    One of two axes onto the same proportion — a note head's half-height as a fraction of the
    string spacing, measured at 0.471 today and below that in every sample of the reference it is
    being judged against. This axis shrinks the family against fixed spacing; \ref
    cycleStringSpacing widens the spacing around a fixed family. They reach the same numbers and
    look nothing alike, which is why both ship until one is chosen.

    Scales both head metrics (`HighwayMetrics::note_half_width` and `note_half_height`), which
    the head quad, the technique marks, the arpeggio brackets, the sustain tail's width and every
    art-silhouette constant already derive from, so the whole family and its accent light move
    together.

    \return Text naming the active candidate and the ratio it produces.
    */
    [[nodiscard]] std::string cycleFamilyScale();

    /*!
    \brief EXPERIMENT SCAFFOLDING — advances the string spacing to the next candidate.

    The second axis onto the proportion \ref cycleFamilyScale describes. Scales
    `HighwayMetrics::string_distance`, so the string grid, the fret lines spanning it, and every
    element sized from the lanes grow with it — a taller neck rather than smaller furniture.

    Note it also moves a decision still open: a bend's drawn travel is expressed in string gaps,
    so widening the gaps changes what a given bend looks like.

    \return Text naming the active candidate and the ratio it produces.
    */
    [[nodiscard]] std::string cycleStringSpacing();

    /*!
    \brief EXPERIMENT SCAFFOLDING — advances the head's width to the next candidate.

    A third board axis, split from \ref cycleFamilyScale by measurement: the reference's head is
    narrower for its fret slot than ours (45.5% against 57.6%) while matching our height against
    the string pitch, so width moves alone. The sustain tail follows, deriving from the width
    metric; technique marks, arpeggio brackets and node-head diamonds are square art at the
    family size and hold still. The head art stretches anisotropically under this knob — a
    labelled preview; the baked head-w079 atlas variants are the undistorted cross-check.

    \return Text naming the active candidate and the slot fill it produces.
    */
    [[nodiscard]] std::string cycleHeadWidth();

    /*!
    \brief EXPERIMENT SCAFFOLDING — advances the harmonic head to the next candidate.

    Its own axis rather than a row of the two scale cycles, because the question it settles is
    scale-invariant: the harmonic symbol and the diamond it rides scale together, so the symbol
    overhangs its base by the same fraction at every family size and string spacing. Sizes the
    diamond base (and the glow that traces it) and the symbol independently, leaving every other
    technique mark untouched so the family stays consistent while one shape is judged.

    \return Text naming the active candidate.
    */
    [[nodiscard]] std::string cycleHarmonicSize();

    /*!
    \brief Encodes one frame of the highway into the render views.

    \param now_seconds Playback song time for this frame (from the consumer's clock port).
    \param dt_seconds Frame delta in seconds driving camera smoothing.
    \param width Backbuffer width in pixels.
    \param height Backbuffer height in pixels.
    */
    void draw(double now_seconds, double dt_seconds, std::uint32_t width, std::uint32_t height);

    /*!
    \brief Encodes screen-space rectangles onto the overlay view (diagnostics panels).

    Call after \ref draw within the same frame; the rectangles composite over the scene in list
    order with alpha blending.

    \param rects Rectangles in pixel coordinates.
    \param width Backbuffer width in pixels.
    \param height Backbuffer height in pixels.
    */
    void drawOverlayRects(
        std::span<const HighwayOverlayRect> rects, std::uint32_t width, std::uint32_t height);

private:
    struct Impl;

    // Only create() constructs a renderer.
    explicit HighwayRenderer(std::unique_ptr<Impl> impl) noexcept;

    // All bgfx-facing state lives behind this pointer (framework isolation).
    std::unique_ptr<Impl> m_impl;
};

} // namespace rock_hero::common::ui
