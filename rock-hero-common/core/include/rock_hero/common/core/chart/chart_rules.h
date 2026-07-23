/*!
\file chart_rules.h
\brief Structural validation rules for chart documents.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Largest string count a chart tuning may declare.

Shared with display code so lane-count controls and validation agree on one authority. Capped at
eight while the tab view only defines string colors through the eighth lane; raise this once
ninth-and-beyond lane colors are chosen.
*/
inline constexpr int g_max_chart_strings{8};

/*!
\brief Highest fret a note, slide waypoint, touch position, or fret-hand position may reference.

Frets above the low twenties do not exist on real instruments; the cap leaves headroom for
extended-range hardware without accepting junk data. Shared with import code so fret clamping
and validation agree on one authority.
*/
inline constexpr int g_max_fret{30};

/*! \brief Stable chart validation failure kind. */
enum class ChartErrorCode : std::uint8_t
{
    /*! \brief The document is unreadable or an element is not the expected JSON shape. */
    MalformedDocument,
    /*! \brief Tuning strings are missing or the string count is unusable. */
    InvalidTuning,
    /*! \brief A chord template's arrays disagree with the tuning's string count. */
    InvalidTemplate,
    /*! \brief A note carries an out-of-range string, fret, or position. */
    InvalidNote,
    /*! \brief Notes are not sorted by position and string, or duplicate an onset. */
    UnsortedOrDuplicateNotes,
    /*! \brief A bend or slide payload violates its note's sustain window. */
    InvalidNotePayload,
    /*! \brief A shape span is empty, unsorted, or references a missing template. */
    InvalidShape,
    /*! \brief A fret-hand position entry is out of range or unsorted. */
    InvalidFretHandPosition
};

/*! \brief Chart validation failure with stable code and display diagnostic. */
struct [[nodiscard]] ChartError
{
    /*! \brief Stable failure code. */
    ChartErrorCode code{};

    /*! \brief Display or log diagnostic. */
    std::string message;
};

/*!
\brief Reports whether a grid position names a real place on the tempo map's grid.

Shared with song-level validation (section markers live on the same grid), so the on-grid rule
cannot drift between chart and song documents.

\param position Grid position to test.
\param tempo_map Song tempo map defining the grid.
\return True when the position's measure, beat, and sub-beat offset are all usable.
*/
[[nodiscard]] bool isValidGridPosition(const GridPosition& position, const TempoMap& tempo_map);

/*!
\brief Reports whether a shape span arrives as an arpeggio rather than a strummed chord box.

The arrival rule shared by the highway and tab projections: a span is an arpeggio when fewer
than two notes strike at its start, or when a posture string is still ringing there without
being re-struck (an earlier note's sustain crosses the span start on a template string with no
onset at it) — a strum under held content is picking around it, not a full strum, so the shape
renders as brackets around individual notes rather than one strummed box. A posture string
that is merely silent at the start (a partial strum of the shape) does not make an arpeggio.

\param chart Chart holding the sorted note stream and template table.
\param shape Shape span to classify.
\param tempo_map Song tempo map, for signature-exact sustain-crossing checks.
\return True when the span renders arpeggio-style.
*/
[[nodiscard]] bool chartShapeArrivesAsArpeggio(
    const Chart& chart, const ChartShape& shape, const TempoMap& tempo_map);

/*!
\brief Validates the chart's structural rules against the song's tempo map.

Enforces the corpus-validated rule set: a usable tuning; template arrays matching the string
count; notes sorted by (position, string) with no duplicate onsets, on valid grid positions,
with strings and frets in range; positive sustains; slide offsets strictly positive, ascending,
and within the sustain; bend offsets non-negative, ascending, and within the sustain; shape
spans positive, sorted, and referencing existing templates; and sorted fret-hand positions.

\param chart Chart to validate.
\param tempo_map Song tempo map the chart's positions must lie on.
\return Empty success, or the first violated rule.
*/
[[nodiscard]] std::expected<void, ChartError> validateChartRules(
    const Chart& chart, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
