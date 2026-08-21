/*!
\file chart_navigation.h
\brief Pure navigation math for the chart editing surface: caret destinations and time bounds.
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Could another digit reach a fret this one alone cannot?

The provisional/immediate split for the pending fret entry: {1, 2} at the 24-fret cap wait out
the window, everything else settles in the same keystroke. 0 is deliberately immediate — arming
the window for it served only a leading-zero path nobody types, making the open string (the
commonest value on the instrument) wait out the window.

\param value The fret typed so far.

\return True when a following digit could still form a legal fret.
*/
[[nodiscard]] constexpr bool chartFretValueExtendable(const int value) noexcept
{
    return value >= 1 && value * 10 <= common::core::g_max_fret;
}

/*!
\brief The armed caret's published time bounds: its own seconds plus its measure span.

One derivation feeds both the chart-row and lane-row caret view structs and the pending insert's
slot, so the keep-in-view glide and the pending box can never disagree about where the caret is.
*/
struct CaretTimeBounds
{
    /*! \brief The caret's own position in seconds. */
    double seconds{0.0};

    /*! \brief The start of the caret's measure in seconds. */
    double measure_start_seconds{0.0};

    /*! \brief The start of the following measure in seconds. */
    double measure_end_seconds{0.0};
};

/*!
\brief Resolves a grid position to the caret's time bounds.

\param tempo_map Song tempo map the position lies on.
\param position Grid position of the caret.

\return The position's seconds and its measure's span.
*/
[[nodiscard]] CaretTimeBounds caretTimeBounds(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& position);

/*!
\brief The Guitar Pro measure jump: later is the next measure's downbeat; earlier is the current
measure's downbeat when mid-measure, else the previous measure's.

The caret jumps and the time-selection extend both resolve their target through this, so the two
verbs can never land on different positions for the same motion — the same "shared adjacent-line
primitive" law the plain grid step already follows.

\param from Position the jump starts from.
\param later True for the forward direction.

\return The destination downbeat.
*/
[[nodiscard]] common::core::GridPosition measureJumpPosition(
    const common::core::GridPosition& from, bool later) noexcept;

/*!
\brief The chart's first position (measure 1, beat 1) — the Home / chart-start destination.

\return Measure 1, beat 1.
*/
[[nodiscard]] constexpr common::core::GridPosition chartStartPosition() noexcept
{
    return common::core::GridPosition{.measure = 1, .beat = 1, .offset = {}};
}

/*!
\brief The nearest song section starting strictly after (later) or before (earlier) a reference.

\param sections Song sections, sorted by position.
\param reference Position the search starts from.
\param later True for the forward direction.

\return The adjacent section's position, or nullopt when there is none in that direction.
*/
[[nodiscard]] std::optional<common::core::GridPosition> adjacentSectionPosition(
    const std::vector<common::core::SongSection>& sections,
    const common::core::GridPosition& reference, bool later);

} // namespace rock_hero::editor::core
