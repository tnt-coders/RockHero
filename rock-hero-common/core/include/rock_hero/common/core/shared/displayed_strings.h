/*!
\file displayed_strings.h
\brief How many string lanes a surface shows, shared by the 2D tab lane and the 3D highway.
*/

#pragma once

#include <algorithm>

namespace rock_hero::common::core
{

/*!
\brief Returns the number of string lanes to display for a chart.

The editor's "show at least N strings" setting can only ADD empty lanes: the chart's own string
count is a floor, so a five-string bass never loses a lane to a smaller minimum. A chart with no
strings displays nothing, because padding an instrument that is not there has no meaning — and that
zero case is the one the two surfaces used to disagree about, the 3D projection having open-coded a
bare maximum that turned a stringless chart into a stack of empty lanes.

Lives here rather than beside either surface because both need it and neither may depend on the
other: the answer decides which lane every note and posture sits on, and it anchors the shared
string-color palette, so the two surfaces drawing one chart must get the same number.

\param chart_string_count Number of strings the chart's tuning declares.
\param minimum_displayed_strings Display minimum; zero (the game default) adds no lanes.
\return Lane count to display, or zero when the chart has no strings.
*/
[[nodiscard]] constexpr int displayedStringCount(
    int chart_string_count, int minimum_displayed_strings) noexcept
{
    if (chart_string_count <= 0)
    {
        return 0;
    }

    return std::max(chart_string_count, minimum_displayed_strings);
}

} // namespace rock_hero::common::core
