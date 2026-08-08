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
\brief Highest fret a note, slide waypoint, or fret-hand position may reference.

Frets above the low twenties do not exist on real instruments; the cap leaves headroom for
extended-range hardware without accepting junk data. Shared with import code so fret clamping
and validation agree on one authority. Harmonic nodes are bounded separately by
\ref g_max_harmonic_node, since a node is not a neck position.
*/
inline constexpr int g_max_fret{30};

/*!
\brief Highest harmonic node position accepted, in fret units.

**Not `g_max_fret`.** A fret must be a real position on the neck; a node is anywhere along the
vibrating string, and the nodes a *pinch* uses sit past the neck entirely, over the pickups —
bridge-side nodes climb with the harmonic (the 3rd partial's is at 19.02, the 4th's at 24.0, both
in real Guitar Pro scores), so capping at `g_max_fret` would reject every bridge-side node from
the 6th partial up. 48 is `12 * log2(16)` exactly, the 16th partial's bridge-side node.

Deliberately permissive: the bound's only job is refusing junk, and a tight one could only reject
a legitimate chart — including an import we do not author. There is no low bound beyond
"positive": higher harmonics crowd toward the nut, so nodes below fret 1 are legitimate. The
evidence behind the number (ergonomics, audibility, corpus reach — all ceiling near the 8th
partial) lives in `docs/plans/in-progress/technique-compatibility-and-hardening.md`.
*/
inline constexpr double g_max_harmonic_node{48.0};

/*!
\brief Highest harmonic partial a *notated* node may be snapped onto during import.

**8, taken from Guitar Pro's own output**: measured corpus labels form the unbroken run 12, 7, 5,
4, 3.2, 2.7, 2.4 — exactly partials 2 through 8 — with every remaining value an alternate node of
those same partials. Neither the GP8 manual nor its format states a cap of its own.

A cap is needed because notation writes rounded labels, not measurements, and snapping only works
while true nodes stay farther apart than the label error. The margin collapses as the cap rises:
at 16, Guitar Pro's "2.4" flips to the 15th partial (2.477) instead of the intended 8th (2.312).
Raising this needs the corpus measurement re-run, not just a bigger number; the full numbers live
in `docs/plans/in-progress/technique-compatibility-and-hardening.md`.
*/
inline constexpr int g_max_snapped_partial{8};

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
    /*! \brief A pick-slide note carries other techniques or a non-traveling path. */
    InvalidPickSlide
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
than two notes strike at its start, when a posture string is still ringing there without being
re-struck (an earlier note's sustain crosses the span start on a template string with no onset
at it), or when a tapped note sounds anywhere within the span — a strum under held content is
picking around it, and a held chord under two-hand tapping is sustained through the taps, not a
full strum, so the shape renders as brackets around individual notes rather than one strummed
box. A posture string that is merely silent at the start (a partial strum of the shape) does not
make an arpeggio.

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
spans positive, sorted, and referencing existing templates; sorted fret-hand positions; and, on
pick-slide notes, no other techniques (a saved scrape carries none — the writer omits the
in-memory overrides) plus a non-empty, always-traveling path ending exactly at the sustain
(consecutive neck positions, the start fret included, must strictly differ — a scrape cannot
sit still).

\param chart Chart to validate.
\param tempo_map Song tempo map the chart's positions must lie on.
\return Empty success, or the first violated rule.
*/
[[nodiscard]] std::expected<void, ChartError> validateChartRules(
    const Chart& chart, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
