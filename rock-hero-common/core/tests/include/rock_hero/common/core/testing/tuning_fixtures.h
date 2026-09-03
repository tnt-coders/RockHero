/*!
\file tuning_fixtures.h
\brief The tunings test fixtures chart, stated once for every suite that builds a chart.
*/

#pragma once

#include <string>
#include <vector>

namespace rock_hero::common::core::testing
{

/*!
\brief The standard six-string guitar tuning, lowest string first.

The one statement of it: five suites across four libraries hand-wrote this same array, and a
fixture tuning that differs by layer is a difference no test means. The array IS the string count
(`ChartViewState::stringCount` and `ChartTuning::strings` alike), so naming the strings is also how
a fixture sizes the lane.

\return Open-string pitch names: E2, A2, D3, G3, B3, E4.
*/
[[nodiscard]] inline std::vector<std::string> standardTuning()
{
    return {"E2", "A2", "D3", "G3", "B3", "E4"};
}

} // namespace rock_hero::common::core::testing
