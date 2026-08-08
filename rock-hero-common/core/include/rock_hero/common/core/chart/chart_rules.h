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

/*!
\brief Highest harmonic node position accepted, in fret units.

**Not `g_max_fret`.** A fret must be a real position on the neck; a node is anywhere along the
vibrating string, and the nodes a *pinch* uses sit past the neck entirely, over the pickups. Node
positions in fret units are `12 * log2(n / (n - k))` for the harmonic's k-th node, so the bridge-side
node climbs with the harmonic: the 3rd is at 19.02 and the 4th at 24.0 (both observed in real Guitar
Pro scores), and the 12th reaches 43.02. Capping at `g_max_fret` would have rejected every
bridge-side node from the 6th harmonic up.

48 is `12 * log2(16)` exactly — the 16th harmonic's bridge-side node, chosen 2026-08-08 with roughly
double the headroom anything human needs. Three independent lines put the practical ceiling near the
**8th** partial:

- **Ergonomics, the hard limit.** The nut-side node of the nth partial sits at exactly `L / n` from the
  nut, so adjacent nodes are `L / (n(n+1))` apart. On a 647.7 mm scale that is 9.0 mm at the 8th and
  7.2 mm at the 9th — already under a 10-15 mm fingertip — and 2.1 mm at the 17th, where one finger
  blankets half a dozen nodes. No amount of gain defeats that.
- **The literature.** Partials 2-5 are the easily audible ones; above them they are "nearly inaudible
  without the overdrive of an amp", and the "stratospheric" band between frets 2 and 3 is partials 6-9.
- **Real charts.** A 118-file Guitar Pro corpus reaches only about the 8th.

Precision is not the constraint either: one decimal separates every distinct node through the **17th**
harmonic without collision (it first fails at the 18th, where 0.990 and 1.050 both round to 1.0).

Note this is a *distance-to-the-bridge* bound more than a partial bound — even capping at the 9th
partial, its bridge-side node already sits at 38.0 fret units, so a generous number is required either
way.

Deliberately permissive: the bound's only job is refusing junk, and a tight one could only reject a
legitimate chart — including a Guitar Pro import we do not author. Keeping the *picker* short is a
separate UI concern; when harmonic authoring is built it should offer partials 2-8 (the nut-side nodes
plus the named bridge-side ones: 7/19, 5/24, 4/9/16), not all 79 nodes this bound admits.

There is no low bound beyond "positive": nodes below fret 1 are legitimate, since higher harmonics
crowd toward the nut (the 16th harmonic's nearest node is 1.12).
*/
inline constexpr double g_max_harmonic_node{48.0};

/*!
\brief Highest harmonic partial an editor offers for deliberate selection.

Distinct from \ref g_max_harmonic_node, which only refuses junk. This is the *authoring* ceiling.

16, settled 2026-08-08. One decimal of precision separates every distinct node through the 17th
partial without collision, so 16 costs nothing there, and the interaction cost is avoided by the
picker's shape: a per-fret dropdown listing the harmonics available at that fret **sorted from lowest
order to highest**, defaulting to the lowest, puts the impractical ones at the bottom of a short list
rather than flat in a menu of every node on the neck.

Practice sits far below this — the literature calls partials 2-5 the easily audible ones, treats the
9th and 10th as playable-though-challenging, and says beyond the 10th they "are so weak and difficult
to bring out that they are rarely used", while the ergonomics give 5.9 mm between the 10th's nodes
against a 10-15 mm fingertip. Offering them anyway is harmless when they sort last; *inferring* them
is not, which is why import uses \ref g_max_snapped_partial instead.
*/
inline constexpr int g_max_authored_partial{16};

/*!
\brief Highest harmonic partial a *notated* node may be snapped onto during import.

Deliberately lower than \ref g_max_authored_partial, because the two are different problems: choosing
a partial needs no inference, while resolving a coarse conventional label into a physical node does.

**8, taken from Guitar Pro's own output.** The GP8 manual documents the harmonic *types* (A.H., T.H.,
P.H., S.H., natural) but states no maximum order and lists no node positions, and `HarmonicFret` is a
free-form decimal — so neither the application nor its format imposes a cap to inherit. Measured
instead: across a 118-file corpus the nut-side labels form an unbroken run 12, 7, 5, 4, 3.2, 2.7,
2.4 — exactly partials 2 through 8 — and every remaining value (5.8, 8.2, 9.0, 14.7, 19, 24) is an
alternate node of one of those same partials. Nothing needs a 9th. All 13 values resolve to the same
partial at a cap of 8 as at 10, so the tighter cap loses nothing.

Why a cap is needed at all: notation writes labels, not measurements, and rounds them inconsistently.
The 7th appears as "2.7" or "2.8" against a true 2.669, and four GP values match no true node at any
cap (9.0 sits 0.16 from the 5th partial's 8.844). Snapping only works while the nodes stay farther
apart than that error, and the margin collapses as the cap rises: the tightest label wins by **0.180**
fret units at a cap of 8 against its own 0.088 error, but by just **0.011** at 16 — where Guitar Pro's
"2.4" flips to the 15th partial (2.477) instead of the intended 8th (2.312).

Raising this needs that measurement re-run against real scores, not just a bigger number.
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
