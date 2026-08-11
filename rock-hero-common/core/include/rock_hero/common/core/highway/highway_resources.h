/*!
\file highway_resources.h
\brief The one table naming the highway's shader programs and texture assets.
*/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rock_hero::common::core
{

/*!
\brief The shader programs the highway renderer links, and the base name each is built as.

The names are the base names of the committed sources in rock-hero-common/ui/shaders and of the
compiled binaries deployed beside each product (`vs_<name>.bin` / `fs_<name>.bin`). Both products
resolve their programs by walking this enumeration, so a new program is added here and nowhere
else: the game's resource paths, the game's shader loader, the editor preview's loader, and the
renderer's own diagnostics all read the name from highwayShaderProgramName.

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
};

/*!
\brief The texture assets the highway renderer uploads, and the file name each is deployed as.

The files live flat under rock-hero-common/ui/resources/textures and are deployed per product;
LICENSE.txt beside them explicitly lists which files are Charter-adapted (BSD 3-Clause) — the rest
are original Rock Hero art. Every asset is REQUIRED product content: empty or undecodable bytes
fail renderer creation with a typed error, because a missing texture means a broken install, not a
degradable state.
*/
enum class HighwayTexture : std::uint8_t
{
    /*! \brief Note-head atlas PNG (4x4 grid, reference channel scheme). */
    Notes,

    /*! \brief Fretboard skin PNG (8x4 grid, one 256x512 cell per fret). */
    Inlays,

    /*! \brief Fingering-panel PNG (4x4 grid: barre shapes plus finger name glyphs). */
    Fingering,

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
compiler already refuses a program with no name.
*/
inline constexpr std::array g_highway_shader_programs{
    HighwayShaderProgram::Color,
    HighwayShaderProgram::ColorFade,
    HighwayShaderProgram::TextureTint,
    HighwayShaderProgram::Glyph,
    HighwayShaderProgram::Texture,
    HighwayShaderProgram::WindowLight,
    HighwayShaderProgram::BoxMute,
};

/*! \brief Every highway texture asset, in enumeration order, for loaders that resolve the set. */
inline constexpr std::array g_highway_textures{
    HighwayTexture::Notes,
    HighwayTexture::Inlays,
    HighwayTexture::Fingering,
    HighwayTexture::ChordMarks,
};

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
    }

    return "color";
}

/*!
\brief File name a texture asset is deployed as, inside the flat textures directory.
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
        case HighwayTexture::Fingering:
        {
            return "fingering.png";
        }
        case HighwayTexture::ChordMarks:
        {
            return "chords.png";
        }
    }

    return "notes.png";
}

} // namespace rock_hero::common::core
