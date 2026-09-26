/*!
\file chart_fret_hand.h
\brief The fret-hand window's derived reach: how far each authored placement's hand stretches.
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*! \brief The lowest and highest fretting-hand stops held during one placement's stretch. */
struct HeldFretRange
{
    /*! \brief Lowest fret the hand holds while the placement stands; never the open string. */
    int lowest{1};

    /*! \brief Highest fret the hand holds while the placement stands. */
    int highest{1};

    /*!
    \brief Compares two ranges by their stored fields.
    \param lhs Left-hand range.
    \param rhs Right-hand range.
    \return True when both ranges store equal values.
    */
    friend constexpr bool operator==(const HeldFretRange& lhs, const HeldFretRange& rhs) noexcept =
        default;
};

/*!
\brief Derives the frets the hand holds during every fret-hand placement's stretch — THE ONE fold
over the notes' held stops, which the window width and the inserted placement's default fret both
read.

A placement's STRETCH runs from its own position up to, and excluding, the next placement's; the
last one's runs to the end of the chart, and the first one's also takes everything before it. Each
note holds its stops over intervals of its stored ring: every fret statement holds the finger it
puts down (\ref heldFretAt, so a natural harmonic holds at its node's fret and an open string
holds nothing) from where it is stated until the note's next fret statement or the ring's end, and
a right-hand onset holds its claim over the whole ring. A stop counts for every stretch its interval
overlaps — a finger struck earlier and still down when the stretch begins included — and the
statement at the ring's very end holds for no time at all.

\param notes The note stream.
\param claimed_stops Each note's claimed stop, index-parallel to \p notes: the resolved column
       (\ref chartClaimedStops) wherever the stream has been resolved.
\param placements The placement stream, ascending by position.
\param tempo_map Tempo map placing each statement on the musical grid.

\return One range per placement, index-parallel to \p placements; empty where the stretch holds
        no stop at all.
*/
[[nodiscard]] std::vector<std::optional<HeldFretRange>> deriveHeldFretRanges(
    const std::vector<ChartNote>& notes, const std::vector<std::optional<int>>& claimed_stops,
    const std::vector<FretHandPosition>& placements, const TempoMap& tempo_map);

/*!
\brief \ref deriveHeldFretRanges over a resolved stream: its saved notes and the claims resolved
beside them, the one pairing every resolved reader wants.

\param resolutions The stream's resolutions (\ref chartResolutions).
\param placements The placement stream, ascending by position.
\param tempo_map Tempo map placing each statement on the musical grid.

\return One range per placement, index-parallel to \p placements.
*/
[[nodiscard]] std::vector<std::optional<HeldFretRange>> deriveHeldFretRanges(
    const ChartResolutions& resolutions, const std::vector<FretHandPosition>& placements,
    const TempoMap& tempo_map);

/*!
\brief Derives every fret-hand placement's window width from the stops the notes hold under it.

THE RULE, stated once: a placement's width is `max(4, highest fretting-hand stop held during its
stretch - fret + 1)`, the stops being the ones \ref deriveHeldFretRanges finds.

Only the index finger's fret is authored. A stop BELOW it widens nothing — it stays outside the
window, which is the honest report that the placement is wrong — and nothing narrows a window
below \ref g_min_fret_hand_width.

\param notes The note stream.
\param claimed_stops Each note's claimed stop, index-parallel to \p notes: the resolved column
       (\ref chartClaimedStops) wherever the stream has been resolved.
\param placements The placement stream, ascending by position.
\param tempo_map Tempo map placing each statement on the musical grid.

\return One width per placement, index-parallel to \p placements.
*/
[[nodiscard]] std::vector<int> deriveFretHandWidths(
    const std::vector<ChartNote>& notes, const std::vector<std::optional<int>>& claimed_stops,
    const std::vector<FretHandPosition>& placements, const TempoMap& tempo_map);

/*!
\brief \ref deriveFretHandWidths over a resolved stream, through the resolved
\ref deriveHeldFretRanges.

\param resolutions The stream's resolutions (\ref chartResolutions).
\param placements The placement stream, ascending by position.
\param tempo_map Tempo map placing each statement on the musical grid.

\return One width per placement, index-parallel to \p placements.
*/
[[nodiscard]] std::vector<int> deriveFretHandWidths(
    const ChartResolutions& resolutions, const std::vector<FretHandPosition>& placements,
    const TempoMap& tempo_map);

} // namespace rock_hero::common::core
