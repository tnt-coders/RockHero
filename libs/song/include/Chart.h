/** @file Chart.h
    @brief Chart aggregate: groups all Arrangements for a Song.
*/

#pragma once

#include "Arrangement.h"

#include <vector>

namespace rock_hero
{

/** Groups all playable Arrangements for a Song.

    A Chart contains one or more Arrangements, each covering a distinct
    part/difficulty combination. The rock-hero-game selects one Arrangement
    per session; the editor may display or edit any of them.
*/
struct Chart
{
    std::vector<Arrangement> arrangements;
};

} // namespace rock_hero
