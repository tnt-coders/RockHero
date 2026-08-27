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
\brief The ring a fixture note gets when the test does not turn on its duration: an eighth of a
beat.

Chosen once, for every suite that hand-builds a chart. Three things make it the right nothing-to-
say value: it is strictly positive, as every stored ring must be (a zero one is refused
structurally — `ChartNote::sustain`); it is under the kept-sustain bound, so presentation drops the
tail and no surface draws anything; and it is shorter than any gap these fixtures use, so it
justifies no legato claim. A test whose meaning depends on the duration — a drawn tail, a hold, a
connection — states its own.
*/
inline constexpr common::core::Fraction g_fixture_sustain{1, 8};

/*!
\brief Builds a note carrying only the fields the planners read, with the non-DMI position and
keyframes fields listed so -Wmissing-designated-field-initializers stays quiet, and the onset bend
spelled out beside them rather than left to its default.
\param position Musical onset.
\param string One-based string.
\param fret Fret sounded.
\param sustain Ring in beats; \ref g_fixture_sustain by default.
\return The note.
*/
[[nodiscard]] inline common::core::ChartNote makeTestNote(
    common::core::GridPosition position, int string, int fret,
    common::core::Fraction sustain = g_fixture_sustain)
{
    return common::core::ChartNote{
        .position = position,
        .string = string,
        .fret = fret,
        .sustain = sustain,
        .bend = 0.0,
        .keyframes = {},
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
