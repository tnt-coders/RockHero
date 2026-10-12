/*!
\file highway_resources.h
\brief The one table naming the highway's shader programs and texture assets.
*/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace rock_hero::common::core
{

/*!
\brief The shader programs the highway renderer links, and the base name each is built as.

The names are the base names of the committed sources in rock-hero-common/ui/shaders and of the
compiled binaries deployed beside each product (`vs_<name>.bin` / `fs_<name>.bin`). Both products
resolve their programs by walking this enumeration, so the NAME is stated here and nowhere else:
the game's resource paths, the game's shader loader, the editor preview's loader, and the
renderer's own diagnostics all read it from highwayShaderProgramName. A new program still needs
its `.sc` source pair, its slot in the renderer, and a draw call — the table owns the name and the
set, not the wiring (docs/developer/the-3d-highway.md walks the full checklist).

This lives in common/core rather than beside the renderer because the game resolves resource paths
in game/core, which is headless and must not depend on common/ui.
*/
enum class HighwayShaderProgram : std::uint8_t
{
    /*! \brief Flat vertex-color geometry (board furniture, rails, boxes, overlay rects). */
    Color,

    /*! \brief Vertex color with a Z-ramp alpha fade (beat bars fading toward the horizon). */
    ColorFade,

    /*!
    \brief Atlas-textured quads with the reference channel scheme: texture R multiplies the tint
    color, G adds white highlight, B is the alpha mask — one atlas serves every string color.
    */
    TextureTint,

    /*! \brief Glyph-atlas text (fret numbers, section labels). */
    Glyph,

    /*! \brief Plain textured quads modulated by vertex color (fretboard skin, background art). */
    Texture,

    /*!
    \brief The hand-window light: per-fragment soft-edged brightness across the window width,
    driven by per-vertex edge distances (the FHP highlight and its transitions).
    */
    WindowLight,

    /*!
    \brief Repeat-box mute mark: an X that lays chords.png's measured cross-section along arms
    whose distances are computed per fragment in box-local world units, so the measured line
    weights hold exactly on boxes of any width.
    */
    BoxMute,

    /*!
    \brief The accent light: one continuous falloff around a subject silhouette, evaluated per
    fragment from a signed distance field so corners wrap as arcs and nothing bands.
    */
    AccentGlow,

    /*!
    \brief The full-screen present: copies the off-screen scene to the window through the theme's
    render look — an exact copy for the plain look, the pixel look's hard-edged block upscale and
    scanlines otherwise. Screen space, so it carries no far-edge fade.
    */
    Present,

    /*!
    \brief The dither look's present: each virtual pixel ordered-dithered (4x4 Bayer) to the
    nearest color of the theme's own opaque palette. Screen space, no far-edge fade.
    */
    PresentDither,

    /*!
    \brief The halftone look's present: a 45-degree dot screen over the theme's backdrop, with ink
    outlines from a Sobel edge on the scene's luminance. Screen space, no far-edge fade.
    */
    PresentHalftone,
};

/*!
\brief The texture assets the highway renderer uploads, and the file name each has in a theme.

Every highway theme directory carries one file per asset under these names, beside its
`theme.json`; a theme whose art is adapted from other work carries the license notice there too.
Every asset is required: one missing, undecodable or the wrong shape refuses the whole theme,
because a silent fallback would mask a broken theme.
*/
enum class HighwayTexture : std::uint8_t
{
    /*!
    \brief Note-head sheet PNG: heads only, 4x2 cells of 64 px in the reference channel scheme.

    The renderer stacks it above the fixed technique-mark sheet
    (\ref g_highway_mark_sheet_file_name) into the one head atlas it draws from.
    */
    Notes,

    /*!
    \brief Inlay dot cell PNG: one round marker dot.

    Mapped world-square by the renderer at code-derived positions (the isDottedFret law).
    */
    Inlays,

    /*!
    \brief Repeat-box mute mark art PNG: two stacked cells, palm mute above full mute, painted in
    the structural channel scheme (R tint weight, G achromatic lift, B coverage) with no alpha
    channel. The single source of truth for the box marks' line weighting and coverage — the
    renderer measures each mark's cross-section from these pixels at creation and supplies hue and
    opacity itself as vertex color, so retuning a color never requires repainting the art.
    */
    ChordMarks,
};

/*!
\brief Every shader program, in enumeration order, for loaders that resolve the whole set.

A new enumerator above needs a row here too; the name functions below are switches, so the
compiler already refuses a program with no name, and the assertion below refuses a row list that
does not cover the enumeration.
*/
inline constexpr std::array g_highway_shader_programs{
    HighwayShaderProgram::Color,
    HighwayShaderProgram::ColorFade,
    HighwayShaderProgram::TextureTint,
    HighwayShaderProgram::Glyph,
    HighwayShaderProgram::Texture,
    HighwayShaderProgram::WindowLight,
    HighwayShaderProgram::BoxMute,
    HighwayShaderProgram::AccentGlow,
    HighwayShaderProgram::Present,
    HighwayShaderProgram::PresentDither,
    HighwayShaderProgram::PresentHalftone,
};

// A loader walks the array while the renderer indexes handle slots by enumerator value, so a
// missing or misplaced row would leave a slot silently unloaded. Covering the enumeration is
// asserted against the last enumerator; appending a program moves that name, which is the point —
// the compiler stops anyone who adds an enumerator without its row.
static_assert(
    []() consteval {
        for (std::size_t index = 0; index < g_highway_shader_programs.size(); ++index)
        {
            if (g_highway_shader_programs.at(index) != static_cast<HighwayShaderProgram>(index))
            {
                return false;
            }
        }
        return g_highway_shader_programs.size() ==
               static_cast<std::size_t>(HighwayShaderProgram::PresentHalftone) + 1;
    }(),
    "g_highway_shader_programs must list every program in enumeration order");

/*! \brief Every highway texture asset, in enumeration order, for loaders that resolve the set. */
inline constexpr std::array g_highway_textures{
    HighwayTexture::Notes,
    HighwayTexture::Inlays,
    HighwayTexture::ChordMarks,
};

// Same mechanism as the shader list: the row list must cover the enumeration in order.
static_assert(
    []() consteval {
        for (std::size_t index = 0; index < g_highway_textures.size(); ++index)
        {
            if (g_highway_textures.at(index) != static_cast<HighwayTexture>(index))
            {
                return false;
            }
        }
        return g_highway_textures.size() ==
               static_cast<std::size_t>(HighwayTexture::ChordMarks) + 1;
    }(),
    "g_highway_textures must list every texture in enumeration order");

/*!
\brief Array index for one shader program in a per-program array.
\param program Shader program to index.
\return Zero-based index matching the enumeration order.
*/
[[nodiscard]] constexpr std::size_t indexOf(HighwayShaderProgram program) noexcept
{
    return static_cast<std::size_t>(program);
}

/*!
\brief Array index for one texture asset in a per-texture array.
\param texture Texture asset to index.
\return Zero-based index matching the enumeration order.
*/
[[nodiscard]] constexpr std::size_t indexOf(HighwayTexture texture) noexcept
{
    return static_cast<std::size_t>(texture);
}

/*!
\brief Base name shared by a program's committed sources and its compiled binaries.
\param program Shader program to name.
\return Stable base name text.
*/
[[nodiscard]] constexpr std::string_view highwayShaderProgramName(
    HighwayShaderProgram program) noexcept
{
    switch (program)
    {
        case HighwayShaderProgram::Color:
        {
            return "color";
        }
        case HighwayShaderProgram::ColorFade:
        {
            return "color_fade";
        }
        case HighwayShaderProgram::TextureTint:
        {
            return "texture_tint";
        }
        case HighwayShaderProgram::Glyph:
        {
            return "glyph";
        }
        case HighwayShaderProgram::Texture:
        {
            return "texture";
        }
        case HighwayShaderProgram::WindowLight:
        {
            return "window_light";
        }
        case HighwayShaderProgram::BoxMute:
        {
            return "box_mute";
        }
        case HighwayShaderProgram::AccentGlow:
        {
            return "accent_glow";
        }
        case HighwayShaderProgram::Present:
        {
            return "present";
        }
        case HighwayShaderProgram::PresentDither:
        {
            return "present_dither";
        }
        case HighwayShaderProgram::PresentHalftone:
        {
            return "present_halftone";
        }
    }

    // The switch is exhaustive; renaming an out-of-range value would be a lie, not a fallback.
    std::unreachable();
}

/*!
\brief File name a texture asset has inside a theme directory.
\param texture Texture asset to name.
\return Stable file name text.
*/
[[nodiscard]] constexpr std::string_view highwayTextureFileName(HighwayTexture texture) noexcept
{
    switch (texture)
    {
        case HighwayTexture::Notes:
        {
            return "notes.png";
        }
        case HighwayTexture::Inlays:
        {
            return "inlays.png";
        }
        case HighwayTexture::ChordMarks:
        {
            return "chords.png";
        }
    }

    // The switch is exhaustive; renaming an out-of-range value would be a lie, not a fallback.
    std::unreachable();
}

/*!
\brief File name of the fixed technique-mark sheet, deployed under each product's
resources/highway directory.

The technique marks are the same for every theme, so they are product art rather than a
HighwayTexture: each host reads the sheet once and hands it to the renderer beside the theme's
art, and the renderer composes it under the theme's head sheet.
*/
inline constexpr std::string_view g_highway_mark_sheet_file_name = "marks.png";

} // namespace rock_hero::common::core
