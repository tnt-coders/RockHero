/*!
\file highway_texture_set.h
\brief The art one highway theme supplies, as encoded file bytes.
*/

#pragma once

#include <array>
#include <cstddef>
#include <rock_hero/common/core/highway/highway_resources.h>
#include <vector>

namespace rock_hero::common::ui
{

/*!
\brief A theme's texture assets, as the bytes of their PNG files.

Indexed by common::core::indexOf(HighwayTexture), which carries the per-asset documentation and the
file name each asset has inside a theme directory. Every entry is required: the texture contract
(which the theme loader and HighwayRenderer::create both enforce) refuses a set with any asset
missing, undecodable or the wrong shape, because a silent fallback would mask a broken theme.
*/
using HighwayTextureSet =
    std::array<std::vector<std::byte>, common::core::g_highway_textures.size()>;

} // namespace rock_hero::common::ui
