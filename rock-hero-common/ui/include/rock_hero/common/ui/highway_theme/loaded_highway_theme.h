/*!
\file loaded_highway_theme.h
\brief One highway theme as its directory loaded: identity, colors, string colors and art.
*/

#pragma once

#include <rock_hero/common/ui/highway_theme/highway_texture_set.h>
#include <rock_hero/common/ui/highway_theme/highway_theme.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>
#include <string>

namespace rock_hero::common::ui
{

/*!
\brief Everything one theme directory states, after the loader has validated all of it.

A theme is a directory holding `theme.json` and the art named by common::core::HighwayTexture. The
renderer and the 2D tab lane take the parts separately, so the string colors stay a palette of
their own beside the theme's roles.
*/
struct LoadedHighwayTheme
{
    /*! \brief The theme's directory name, which is its id: `[a-z0-9-]+`. */
    std::string id;

    /*! \brief The name a picker shows. */
    std::string name;

    /*! \brief The theme's color roles. */
    HighwayTheme colors;

    /*! \brief The theme's string colors, which also make it a palette a player can pick. */
    StringColorPalette strings;

    /*! \brief The theme's art, satisfying the texture contract. */
    HighwayTextureSet textures;
};

} // namespace rock_hero::common::ui
