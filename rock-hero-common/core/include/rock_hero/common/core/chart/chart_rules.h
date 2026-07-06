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
    InvalidFretHandPosition,
    /*! \brief A section entry is unnamed or unsorted. */
    InvalidSection
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
\brief Validates the chart's structural rules against the song's tempo map.

Enforces the corpus-validated rule set: a usable tuning; template arrays matching the string
count; notes sorted by (position, string) with no duplicate onsets, on valid grid positions,
with strings and frets in range; positive sustains; slide offsets strictly positive, ascending,
and within the sustain; bend offsets non-negative, ascending, and within the sustain; shape
spans positive, sorted, and referencing existing templates; sorted fret-hand positions and
sections.

\param chart Chart to validate.
\param tempo_map Song tempo map the chart's positions must lie on.
\return Empty success, or the first violated rule.
*/
[[nodiscard]] std::expected<void, ChartError> validateChartRules(
    const Chart& chart, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
