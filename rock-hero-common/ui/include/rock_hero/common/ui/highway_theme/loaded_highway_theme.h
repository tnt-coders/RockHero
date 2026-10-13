/*!
\file loaded_highway_theme.h
\brief One highway theme as its directory loaded: identity, colors, string palette and art.
*/

#pragma once

#include <rock_hero/common/ui/highway_theme/highway_crt.h>
#include <rock_hero/common/ui/highway_theme/highway_texture_set.h>
#include <rock_hero/common/ui/highway_theme/highway_theme.h>
#include <rock_hero/common/ui/highway_theme/tab_shape_inks.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>
#include <string>

namespace rock_hero::common::ui
{

/*!
\brief The string colors one theme file states, named by that theme.

A theme that states `strings` is a palette the player can pin, under its own id and name. A theme
that inherits its strings carries its base's palette whole, so a palette always names the theme
whose file wrote it.
*/
struct HighwayThemePalette
{
    /*! \brief The id of the theme whose file states these colors; a pin stores it. */
    std::string source_id;

    /*! \brief That theme's name, which a picker shows for the palette. */
    std::string name;

    /*! \brief The string colors. */
    StringColorPalette colors;
};

/*!
\brief Everything one theme directory states, after the loader has resolved and validated it.

A theme is a directory holding `theme.json` and the art named by common::core::HighwayTexture; an
inheriting theme takes whatever it leaves out from its base. The renderer and the 2D tab lane take
the parts separately, so the string colors stay a palette of their own beside the theme's roles, and
the lane's span inks beside both.
*/
struct LoadedHighwayTheme
{
    /*! \brief The theme's directory name, which is its id: `[a-z0-9-]+`. */
    std::string id;

    /*! \brief The name a picker shows. */
    std::string name;

    /*! \brief The theme's color roles. */
    HighwayTheme colors;

    /*!
    \brief The theme's string colors: its own when its file states them, which makes the theme a
    palette, otherwise its base's.
    */
    HighwayThemePalette palette;

    /*!
    \brief The 2D tab lane's span inks: the `tab` halves of the theme's chord and arpeggio, which
    resolve with those roles.
    */
    TabShapeInks tab_inks;

    /*!
    \brief The CRT monitor the 3D highway presents its board through, or none: the theme's own
    `crt`, otherwise its base's.
    */
    HighwayCrt crt;

    /*! \brief The theme's art, satisfying the texture contract. */
    HighwayTextureSet textures;
};

} // namespace rock_hero::common::ui
