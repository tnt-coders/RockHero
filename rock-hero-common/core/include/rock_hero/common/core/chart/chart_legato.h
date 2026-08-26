/*!
\file chart_legato.h
\brief The connection resolver: what a chart's legato claims resolve to, for every consumer.
*/

#pragma once

#include <cstddef>
#include <limits>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Index sentinel for \ref ChartResolutions::predecessors: nothing earlier on that string.

Named once and shared, because the "nearest earlier note on the same string" relation now has one
producer and more than one consumer.
*/
inline constexpr std::size_t g_no_chart_predecessor{std::numeric_limits<std::size_t>::max()};

/*!
\brief The motion a note's connection claim justifies, or `Unjustified` when nothing does.

The ONE authority for which way a legato connection runs, and the only place the question is
answered — no direction is ever stored, so there is nothing else it could be read from. Validation
is intra-note only; the relational rules a document used to be refused for live here as clauses
instead, because a claim the chart cannot justify is a claim that plays as a plain pick, not a
broken file.

Judged against the RELEASED fret — where the predecessor's finger ends, so a glide hands over its
last waypoint — never against predecessor identity. Four things disqualify a predecessor outright:
none exists, it is a scrape (its travel is the pick's position, so no finger waits at its end —
user ruling 2026-08-20), it is a fret-hand harmonic (a touch holds nothing to hand over), or its
ring has already stopped at this onset (\ref predecessorHoldReaches, strict adjacency). A string
that stopped sounding is a released string, which is why shrinking a tail drops the connection its
neighbour claimed and why a claim after a REST resolves to nothing.

A dead predecessor is an ordinary one: its finger is on the stop, and the muted cluck after it is a
hammer or pull like any other. It is bounded by the same one test, reading the same field — a dead
note stores the duration its damped stroke lasts (only the DRAWN tail goes, E25), so a chug chained
to its restrike connects and a cluck the hand left long before does not. Ruled, reversed and
settled this way on 2026-08-20, because the alternative — disqualifying the dead note outright —
turned every imported muted cluck into a picked note.

Then the released fret picks the direction: above the note is a pull-off, below it a hammer-on. A
pull-off carries no harmonic (it releases onto a plain stopped pitch); a hammer-on needs somewhere
to land, which the direction test already guarantees — a released fret is never negative, so a
hammer's note is stopped at fret 1 or above by construction and needs no second test. Equal frets
justify nothing — there is no connection to record, and inventing one would be inventing data.

`LeftTap` resolves to the hammer motion unconditionally and reads no predecessor at all: it is the
authored statement that the fretting hand strikes the note from nowhere, so no neighbour can
withdraw it. Every other attack is answered as the hypothetical it is — asking about a `Pick` is how
the `H` toggle finds the notes a claim would justify, so this deliberately never short-circuits on
the note's own attack.

Reads the predecessor's STORED fields only, so there is no cascade: resolving one note can never
change what another resolves to.

Deliberately unbounded in time: a hammer-on from a note eight bars back is musically odd, but a
predecessor still holding is a predecessor, and the author asserting the connection is the authority
on whether the notes connect.

The predecessor's own stored ring is the hold datum, read from the note handed in: the chart states
the actual duration the string sounds, so there is no derived length to pass alongside and no
convention to agree with. A caller asking a HYPOTHETICAL hold (the `H` assist, which offers to
author the ring a claim needs) asks it by handing over a predecessor carrying that ring.

\param note Note whose claim is in question.
\param predecessor Nearest earlier note on the same string, or `nullptr` when there is none.
\param tempo_map Song tempo map supplying the beat axis for the hold test.

\return The motion the claim resolves to, or `Unjustified` when nothing justifies one.
*/
[[nodiscard]] LegatoMotion resolveLegato(
    const ChartNote& note, const ChartNote* predecessor, const TempoMap& tempo_map);

/*!
\brief The saved note stream and every connection claim it justifies.

What the connection rules need, and nothing more. \ref resolveLegato reads a predecessor's stored
position, ring, released fret and attack class, so the saved stream plus one forward walk answers
every claim in the chart; nothing presentation derives — the drawn tails, the hand-posture spans,
the holds — can change a verdict here.

That is why this is asked on its own rather than through \ref ChartResolutions. The settle sweep
and the editor's legato verb want only these three vectors, and they run at every caret move,
selection change, seek and playback start; deriving a whole song's presented stream, spans and
holds to read one flag was a full pass over the chart thrown away on every keystroke.

Every vector is index-parallel to the note stream it was built from.
*/
struct ChartConnections
{
    /*!
    \brief Each note in its saved form (\ref savedChartNote): in-memory latents stripped.

    What the RULES judge, and what the presentation is derived from. Its `sustain` is the actual
    duration the string rings, which is what the connection resolver reads and what no surface
    draws directly.
    */
    std::vector<ChartNote> saved_notes;

    /*!
    \brief What each note's connection claim resolves to.

    `Unjustified` for every note that makes no claim, exactly as for a claim nothing justifies: both
    draw and score as plain picks, so display code can read this entry alone for notes in the
    \ref legatoClaimable family and needs no second test.
    */
    std::vector<LegatoMotion> legato;

    /*!
    \brief Each note's nearest earlier note on its own string, or \ref g_no_chart_predecessor.

    The relation the forward walk already established to answer \ref legato, handed out rather than
    kept private: the `H` toggle asks the resolver its own hypothetical per selected note and needs
    the same predecessor, and re-deriving it there was both a restatement of this rule and a
    backward scan per selected note.
    */
    std::vector<std::size_t> predecessors;
};

/*!
\brief Resolves a whole note stream's connections: the saved form, the motions, the predecessors.

One forward walk carrying the most recent note per string, which IS each note's same-string
predecessor when it is reached, so the connection motions cost one pass over the stream rather than
a backward search per note.

\param notes Note stream sorted by (position, string).
\param tempo_map Song tempo map supplying the beat axis.

\return The connections; every vector is index-parallel to `notes`.
*/
[[nodiscard]] ChartConnections chartConnections(
    const std::vector<ChartNote>& notes, const TempoMap& tempo_map);

/*!
\brief Everything a chart revision derives per note, resolved once for every consumer.

The per-note facts each surface needs and none may restate: the connections the saved stream
justifies, the presented form every surface DRAWS and the scorer will read, and how long each note
is held — plus the hand-posture spans the notes imply, which are not per-note but are derived from
the same two streams and are what the holds are answered against.

What is added here over \ref ChartConnections travels together because it is computed together —
the holds need both the saved and the presented forms AND the spans to be answered at all — and
because computing them separately is exactly how the tab lane, the highway, the gameplay build, and
the reader came to disagree about the same chart. The connections are carried rather than restated,
so a consumer of the whole picture still reads them from one place.

Every per-note vector is index-parallel to the note stream it was built from. Consumed once per
chart revision, never per frame.
*/
struct ChartResolutions
{
    /*! \brief The saved stream and the connections it justifies (\ref chartConnections). */
    ChartConnections connections;

    /*!
    \brief Each note as it is DRAWN and scored (\ref presentedChartNotes).

    The tail rules applied to the saved stream: what both painters, hit testing, and the future
    scorer read (\ref NoteViewState is this form resolved to seconds). Same order and size as
    \ref ChartConnections::saved_notes; only tails and the payload riding them differ.
    */
    std::vector<ChartNote> presented_notes;

    /*!
    \brief The hand-posture spans the notes imply (\ref deriveChartShapes).

    Not stored anywhere: a span is a statement about the notes under it, so it is derived here from
    the two streams above and read from here by everything that draws a chord box or an arpeggio
    bracket. \ref shapes indexes \ref postures.
    */
    std::vector<ChartShape> shapes;

    /*! \brief The posture table \ref shapes indexes, in first-appearance order. */
    std::vector<ChartPosture> postures;

    /*! \brief Each note's held length in beats (\ref chartHolds): how long the hand stays down. */
    std::vector<Fraction> holds;
};

/*!
\brief Resolves a whole note stream once: connections, presented form, spans, holds.

The connections come from \ref chartConnections, so the walk that answers them is stated once for
both the callers that want the whole picture and the callers that want a claim.

The spans come from the same stream rather than from a caller, which is what makes them impossible
to disagree with it: a caller holding a stale span list has nowhere to pass it. The hold markers
ride alongside for the same reason — they are the one posture input the note stream cannot carry
(\ref ChartHoldMarker), and a span derived without the chart's own markers would be a second,
quieter picture of the same chart.

\param notes Note stream sorted by (position, string).
\param hold_markers Silently-held shape members from the same chart (\ref Chart::hold_markers).
\param tempo_map Song tempo map supplying the beat axis.

\return The resolutions; every per-note vector is index-parallel to `notes`.
*/
[[nodiscard]] ChartResolutions chartResolutions(
    const std::vector<ChartNote>& notes, const std::vector<ChartHoldMarker>& hold_markers,
    const TempoMap& tempo_map);

/*!
\brief Flattens every legato claim the chart no longer justifies to a plain pick — the settle sweep.

The one relational mutation in the system, and stateless: it judges only the stream it is handed, so
there is no window state, no flagged notes, and no proofs to keep. The editor runs it at every
settle event, the chart normalizer (\ref normalizeChart) as its last stage on every load and
import, and the document writer before emitting — which is what makes the invariant `Unjustified`
cannot survive a settle or reach a file hold everywhere at once instead of per call site.

A `LeftTap` is never touched: its claim is local, so nothing can withdraw it.

One pass is enough, and that is a property of the resolver rather than an assumption: resolution
reads a predecessor's released fret, node, attack class, position and ring, and flattening
`Legato` to `Pick` changes none of them (a scrape is never a claim, so no flatten touches one), so
no flatten can create or destroy another note's justification.

\param notes Note stream sorted by (position, string); flattened in place.
\param tempo_map Song tempo map supplying the beat axis.

\return One \ref ChartRepair::UnjustifiedLegato conversion per flattened claim, in note order, each
        naming the claim's position and string; empty when the stream already satisfied the
        invariant, which is what callers test to know whether it changed.
*/
[[nodiscard]] std::vector<ChartConversion> sweepUnjustifiedLegato(
    std::vector<ChartNote>& notes, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
