/*!
\file chart_shapes.h
\brief The hand-posture derivation: the spans a note stream implies, and the postures they hold.
*/

#pragma once

#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief One hand posture: the fret held on each string while a span runs.

Array index 0 is the lowest-pitched string; a null entry means the string is not part of the
posture. The array is \ref g_max_chart_strings long — the model's own bound on a string number,
not a statement about the tuning — so a chart with fewer strings simply leaves the top slots
empty, and two postures always compare by their held frets alone.

Derived, never authored — the frets an onset's struck members hold plus whatever was still ringing
across it (\ref deriveChartShapes). Chord names and fingerings carry no field here because nothing
writes one; when they are authored they become a dictionary keyed by a posture rather than members
of it.
*/
struct ChartPosture
{
    /*! \brief Fret held per string; nullopt when the string is not part of the posture. */
    std::vector<std::optional<int>> frets;

    /*!
    \brief Compares two postures by their held frets.
    \param lhs Left-hand posture.
    \param rhs Right-hand posture.
    \return True when both hold the same fret on every string.
    */
    friend bool operator==(const ChartPosture& lhs, const ChartPosture& rhs) = default;
};

/*!
\brief Hand-posture span: how long one posture is held, and which posture it is.

One mechanism covers strummed chords, chugged riffs on a held shape, and arpeggios: the notes under
the span are the sounding truth, the span adds the notation layer (box or bracket). Whether it
renders as a chord box or an arpeggio bracket is a further derivation from the notes at its start
(\ref chartShapeArrivals), not a property stored here.
*/
struct ChartShape
{
    /*! \brief Musical start of the span. */
    GridPosition position;

    /*! \brief Span duration in beats; strictly positive. */
    Fraction sustain{};

    /*! \brief Index into the posture table derived alongside (\ref ChartShapes::postures). */
    std::size_t posture{0};

    /*!
    \brief Compares two spans by their stored fields.
    \param lhs Left-hand span.
    \param rhs Right-hand span.
    \return True when both spans hold equal values.
    */
    friend bool operator==(const ChartShape& lhs, const ChartShape& rhs) = default;
};

/*! \brief The spans a note stream implies, with the posture table those spans index. */
struct ChartShapes
{
    /*! \brief Hand-posture spans, sorted by position. */
    std::vector<ChartShape> shapes;

    /*! \brief The postures the spans index, in first-appearance order and deduplicated. */
    std::vector<ChartPosture> postures;
};

/*!
\brief Derives the hand-posture spans and postures a note stream implies.

The chart stores no spans: a span is a statement about the notes under it, so deriving it is the
only way it can never disagree with them. This is that derivation, run once per chart revision
inside \ref chartResolutions and read from there by everything that draws a chord box, an arpeggio
bracket, or a span-implied hold.

Any onset striking two or more FRETTING-hand strings becomes a posture, deduplicated by its fret
vector, and consecutive onsets holding the same articulation merge into one span covering the
strums' own rings — the grouping the tab renders as a chord box over repeated strums. Tap-only
onsets are transparent to the whole derivation: taps are the tapping hand, so they neither form
postures nor close held spans, letting a ringing chord's span cover the taps above it. ANY
articulation difference is a new chord: span continuity compares each string's whole note with only
its position and duration neutralized, so attack (hammer/pull/tap/slap/pop), muting, harmonics,
vibrato, tremolo, emphasis, bends, and slides — and any technique added to \ref ChartNote later —
all split the span, while strum durations never do. The posture table stays deduplicated by frets
alone (the hand posture is identical; techniques render on the notes). A note still ringing through
a chord's onset (tie-held from before, not re-struck) joins the posture on its string; the shared
arrival rule (\ref chartShapeArrivals) then renders the partly-struck span as an arpeggio, while
fully-strummed spans stay chord boxes — no other arpeggio grouping is derived. A span closed by a
following event trims to the minimum-sustain-distance margin before it
(\ref minimumSustainDistanceBeats at the closing onset's measure), floored at the last strum, with
an exact-adjacency fallback when even that would leave no length — the same margin every other
element keeps.

Articulation is read from the PRESENTED notes and span extent from the stored rings, which is the
split the box states: what the chord LOOKS like is what the surfaces draw (a tail the presentation
rules compressed carries a compressed gesture, and two strums that draw identically are one box),
while how far the hand keeps holding is the actual ring behind the picture.

The maintained plain-English spec is "Posture and shape derivation" in
`docs/developer/the-project-lifecycle.md`.

\param saved_notes Note stream in SAVED form, sorted by (position, string); read for its rings.
\param presented_notes The same notes through \ref presentedChartNotes, in the same order and of
                       the same size; read for the articulation two strums are compared by.
\param tempo_map Tempo map supplying the exact beat axis and the meter at each closing onset.

\return The derived spans and the posture table they index.
*/
[[nodiscard]] ChartShapes deriveChartShapes(
    const std::vector<ChartNote>& saved_notes, const std::vector<ChartNote>& presented_notes,
    const TempoMap& tempo_map);

/*!
\brief Classifies every shape span as an arpeggio or a strummed chord box.

The second half of the same derivation, and here beside the first for that reason: \ref
deriveChartShapes says where the hand goes and how long it stays, this says which of the two marks
the notation draws. Neither is authored, so neither has a rule a document could break.

The arrival rule shared by the highway and tab projections: a span is an arpeggio when fewer
than two notes strike at its start, when a posture string is still ringing there without being
re-struck (an earlier note's PRESENTED tail crosses the span start on a posture string with no
onset at it), or when a picking-hand onset — a tap or a pick slide — sounds anywhere within the
span. A strum under held content is picking around it, and a held chord under two-hand tapping is
sustained through the taps rather than fully strummed, so the shape renders as brackets around
individual notes instead of one strummed box. A posture string that is merely silent at the start
(a partial strum of the shape) does not make an arpeggio.

Asked of the PRESENTED stream (\ref presentedChartNotes), like every other fact a surface draws:
the question is what still SOUNDS across the span start, and a dead string's stored ring is timing
rather than sound — E25 is exactly the rule that takes a dead note's tail away, and it lives in
presentation. Everything else the rule reads (positions, strings, attacks) comes through
presentation untouched, and a ring that genuinely crosses a span start is presented whole anyway:
a span starts at a two-note onset, so such a ring runs strictly past its own first binding onset
and rule 1 exempts it.

Answers all the shapes at once because the rule needs to look BACKWARD — to each posture string's
most recent earlier note — and one forward cursor over the sorted notes carries exactly that with
no walking back. The remaining per-shape scans stay local to each span; what this batching removed
is the unbounded backward walk, which reached the first note in the song whenever a posture string
had none and which both projections then paid for every shape on every chart revision.

\param presented_notes Notes as drawn, sorted by (position, string).
\param shapes Hand-posture spans, sorted by position (\ref ChartResolutions::shapes).
\param postures Posture table the spans index (\ref ChartResolutions::postures).
\param tempo_map Song tempo map, for signature-exact sustain-crossing checks.
\return One flag per shape, in `shapes` order: true where the span renders arpeggio-style.
*/
[[nodiscard]] std::vector<bool> chartShapeArrivals(
    const std::vector<ChartNote>& presented_notes, const std::vector<ChartShape>& shapes,
    const std::vector<ChartPosture>& postures, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
