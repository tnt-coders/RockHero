/*!
\file chart.h
\brief Chart aggregate: groups all Arrangements for a Song.
*/

#pragma once

#include <rock_hero/core/arrangement.h>

#include <vector>

namespace rock_hero::core
{

/*!
\brief Groups all playable Arrangements for a Song.

A Chart contains one or more Arrangements, each covering a distinct part/difficulty
combination. The rock-hero-game selects one Arrangement per session, while the editor may
display or edit any of them.
*/
struct Chart
{
    std::vector<Arrangement> arrangements;
};

} // namespace rock_hero::core
