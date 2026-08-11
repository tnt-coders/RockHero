/*!
\file chart_legato.h
\brief The connection resolver: what a chart's legato claims resolve to, for every consumer.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief The motion a note's connection claim justifies, or `Unjustified` when nothing does.

The ONE authority for which way a legato connection runs, and the only place the question is
answered — no direction is ever stored, so there is nothing else it could be read from. Validation
is intra-note only; the relational rules a document used to be refused for live here as clauses
instead, because a claim the chart cannot justify is a claim that plays as a plain pick, not a
broken file.

Judged against the RELEASED fret — where the predecessor's finger ends, so a glide hands over its
last waypoint and a scrape its slide-out's end — never against predecessor identity. Three things
disqualify a predecessor outright: none exists, it is a fret-hand harmonic (a touch holds nothing to
hand over), or it is no longer holdable at this onset. Past the kept-sustain bound a disconnected
tail is a proven release, which is why shrinking a tail drops the connection its neighbour claimed.

Then the released fret picks the direction: above the note is a pull-off, below it a hammer-on. A
pull-off carries no harmonic (it releases onto a plain stopped pitch), and a hammer-on needs
somewhere to land (a fret, or a node to strike). Equal frets justify nothing — there is no
connection to record, and inventing one would be inventing data.

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

\param note Note whose claim is in question.
\param predecessor Nearest earlier note on the same string, or `nullptr` when there is none.
\param predecessor_effective_sustain That predecessor's held length, span-extended
       (\ref chartEffectiveSustains) — a span implies its strum is held.
\param tempo_map Song tempo map supplying the beat axis for the hold test.

\return The motion the claim resolves to, or `Unjustified` when nothing justifies one.
*/
[[nodiscard]] LegatoMotion resolveLegato(
    const ChartNote& note, const ChartNote* predecessor, Fraction predecessor_effective_sustain,
    const TempoMap& tempo_map);

/*!
\brief Everything a chart revision derives per note, resolved once for every consumer.

The three per-note facts each surface needs and none may restate: the saved form the display and the
rules both judge, the effective hold the span convention implies, and the resolved connection
motion.
They travel together because they are computed together — the resolutions need the saved forms and
the holds to be answered at all — and because computing them separately is exactly how the tab lane,
the highway, the gameplay build, and the reader came to disagree about the same chart.

Every vector is index-parallel to the note stream it was built from. Consumed once per chart
revision, never per frame.
*/
struct ChartResolutions
{
    /*! \brief Each note in its saved form (\ref savedChartNote): in-memory latents stripped. */
    std::vector<ChartNote> saved_notes;

    /*! \brief Each note's effective held length in beats (\ref chartEffectiveSustains). */
    std::vector<Fraction> effective_sustains;

    /*!
    \brief What each note's connection claim resolves to.

    `Unjustified` for every note that makes no claim, exactly as for a claim nothing justifies: both
    draw and score as plain picks, so display code can read this entry alone for notes in the
    \ref legatoClaimable family and needs no second test.
    */
    std::vector<LegatoMotion> legato;
};

/*!
\brief Resolves a whole note stream in one pass: saved forms, effective holds, connection motions.

One forward walk carrying the most recent note per string, which IS each note's same-string
predecessor when it is reached, so the resolutions cost one pass over the stream rather than a
backward search per note.

\param notes Note stream sorted by (position, string).
\param shapes Hand-posture spans the notes play under; a span implies its strum is held, which the
       hold test judges against.
\param tempo_map Song tempo map supplying the beat axis.

\return The per-note resolutions, index-parallel to `notes`.
*/
[[nodiscard]] ChartResolutions chartResolutions(
    const std::vector<ChartNote>& notes, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map);

/*!
\brief Flattens every legato claim the chart no longer justifies to a plain pick — the settle sweep.

The one relational mutation in the system, and stateless: it judges only the stream it is handed, so
there is no window state, no flagged notes, and no proofs to keep. The editor runs it at every
settle event, the reader at load, and the document writer before emitting — which is what makes the
invariant `Unjustified` cannot survive a settle or reach a file hold everywhere at once instead of
per call site.

A `LeftTap` is never touched: its claim is local, so nothing can withdraw it.

One pass is enough, and that is a property of the resolver rather than an assumption: resolution
reads a predecessor's released fret, node, position and hold, and flattening `Legato` to `Pick`
changes none of them, so no flatten can create or destroy another note's justification.

\param notes Note stream sorted by (position, string); flattened in place.
\param shapes Hand-posture spans the notes play under, for the hold test.
\param tempo_map Song tempo map supplying the beat axis.

\return One human-readable conversion note per flattened claim, in note order; empty when the stream
        already satisfied the invariant, which is what callers test to know whether it changed.
*/
[[nodiscard]] std::vector<std::string> sweepUnjustifiedLegato(
    std::vector<ChartNote>& notes, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map);

} // namespace rock_hero::common::core
