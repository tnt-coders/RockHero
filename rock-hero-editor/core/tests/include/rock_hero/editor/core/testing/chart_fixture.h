/*!
\file chart_fixture.h
\brief The chart every chart-editing test starts from: one stream the planner tests and the
controller tests build identically.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>

namespace rock_hero::editor::core
{

/*!
\brief Builds a note carrying only the fields the planners read, with the non-DMI position,
bend, and slides fields listed so -Wmissing-designated-field-initializers stays quiet.
\param position Musical onset.
\param string One-based string.
\param fret Fret sounded.
\param sustain Held length in beats; zero by default.
\return The note.
*/
[[nodiscard]] inline common::core::ChartNote makeTestNote(
    common::core::GridPosition position, int string, int fret, common::core::Fraction sustain = {})
{
    return common::core::ChartNote{
        .position = position,
        .string = string,
        .fret = fret,
        .sustain = sustain,
        .bend = {},
        .slides = {},
    };
}

/*!
\brief A six-string chart with a two-note onset at measure 2 beat 1 (strings 1 and 2) and a
sustained note at measure 3 beat 1.

Under the default tempo map (120 BPM, 4/4) measure 2 beat 1 is 2.0s and measure 3 beat 1 is 4.0s.
Notes are pre-sorted by (position, string), as every planner's diff assumes. Built once here so
the planner tests and the controller tests cannot drift onto two charts that merely look alike.

\return The chart.
*/
[[nodiscard]] inline common::core::Chart makeTestChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 3),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5),
        makeTestNote({.measure = 3, .beat = 1}, 1, 7, common::core::Fraction{2}),
    };
    return chart;
}

} // namespace rock_hero::editor::core
