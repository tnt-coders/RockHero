/*!
\file chart_fret_hand.h
\brief The fret hand derived from the notes: where the hand sits (\ref deriveFretHandEvents) and how
far each stored placement's hand stretches (\ref deriveFretHandWidths).
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief One derived hand event: where the hand arrives at a window, standing until the next event.
*/
struct FretHandEvent
{
    /*!
    \brief Where the window takes over, on the whole-note axis
    (\ref ChartConnections::places).
    */
    Fraction front{};

    /*!
    \brief The window: the run's floor fitted onto the board (\ref fittedWindowFret) and the width
    `max(4, run hull)`.
    */
    FretWindow window{};

    /*!
    \brief Compares two events by their stored fields.
    \param lhs Left-hand event.
    \param rhs Right-hand event.
    \return True when both events store equal values.
    */
    friend constexpr bool operator==(const FretHandEvent& lhs, const FretHandEvent& rhs) noexcept =
        default;
};

/*!
\brief What a hand derivation met on its way, counted for the instruments that measure it — the
dataset scorer and the corpus census — and for nothing the product reads.

Every derivation handed a tally ADDS to the counts already in it, so one tally sums a corpus.
*/
struct FretHandDerivationTally
{
    /*! \brief Instants the hand covers. */
    long long groups{0};

    /*! \brief Of those, the instants where at least one fretted note is struck. */
    long long struck_groups{0};

    /*! \brief Pitched keyframe fret statements demanding their fret (rule 2). */
    long long keyframe_demands{0};

    /*! \brief Pull-offs planting a stop beneath their source, the open string included. */
    long long plants{0};

    /*! \brief Of those, the plants on a pressed fret: the rule-12 demands. */
    long long pressed_plants{0};

    /*! \brief Plain rings that pin the hand for some time (rule 6). */
    long long ring_pins{0};

    /*! \brief Figures the walk cut. */
    long long runs{0};

    /*! \brief Covered figures held still because a plain ring sounds (rule 6). */
    long long held_by_ring{0};

    /*! \brief Covered one-fret figures held still (rule 5); a ring hold is counted first. */
    long long held_on_one_fret{0};

    /*! \brief Figures whose window is the standing one, so nothing is emitted (rule 11). */
    long long restated{0};

    /*! \brief Emitted fronts that reach back before their figure's first instant (rule 10). */
    long long fronts_reaching_back{0};
};

/*!
\brief RULE 1, stated once: the fret a note demands of the fretting hand at an offset along its
ring, or nothing.

The fret its fretting finger occupies there (\ref heldFretAt), so a natural harmonic demands its
node's fret. An onset the PICKING hand makes (\ref rightHandOnset) demands nothing, and neither does
an open string — save one bent anywhere along its ring, which is bent behind the nut and puts the
hand at fret 1 (rule 13).

\param note Note whose demand is asked.
\param offset Whole notes from the note's onset.

\return The demanded fret, or nothing.
*/
[[nodiscard]] std::optional<int> fretHandDemandAt(const ChartNote& note, Fraction offset);

/*!
\brief Derives where the fret hand sits from the notes and the positions the charter authored — THE
13-rule figure-segmented floor tracker (`docs/plans/in-progress/fhp-derivation-algorithm.md`, with
plan 60's Phase 1 rulings), superseded wherever an authored position stands.

THE LAW, in the three sentences a charter is taught: cut the song into figures at boundaries you can
see — a chord being struck, a bar line, or the grip outgrowing the hand; put the hand at each
figure's floor, spanning the grip, never narrower than four; and two exceptions, both of which only
ever REMOVE motion: the hand never moves while a written note still rings, and a one-fret figure the
standing window already covers does not move it.

What a note DEMANDS of the hand (\ref fretHandDemandAt) is asked at its onset and at every pitched
keyframe fret statement but a slide-out; a pull-off source's onset also demands the stop the
pull-off plants beneath it (\ref chartPlantedStops), which widens every instant up to the pull-off.

Each event's front is the first onset after the latest of the previous figure's last instant, the
latest end among the plain rings struck before the figure, and the bar line that broke the figure;
the window before the first front is the reader's question. A derived event is emitted only where
the window changes.

AN AUTHORED POSITION is a statement the derivation consumes, never a patch on its output. It is
always an event, fronting exactly where it stands, and no figure runs across it. Its window is
`[fret, end fret]` where the end is stated; otherwise the stated fret is the floor and the top is
the top of the figure it heads, never under four frets. It stands until the chart contradicts it:
it takes its own instant whatever sounds there, and every later instant whose demands fit inside
the window, up to the next authored position. The 13 rules resume at the first instant that does
not fit, and their fronts never reach back to an authored position.

Memoryless per figure and never reads seconds: the bar test is the measure number changing between
consecutive instants.

\param connections The resolved connections (\ref chartConnections): the saved notes, their places,
       the same-string relation and the pull-offs.
\param tempo_map Tempo map the connections were resolved against, placing each instant in its
       measure. A parameter rather than part of the connections, which are a value and could only
       carry a reference that might dangle.
\param capo The tuning's capo fret; 0 for none.
\param authored The chart's authored positions, valid and strictly ascending
       (\ref validateFretHandPositions); empty to ask what the notes alone derive, as an instrument
       scoring the derivation against stored positions must.
\param tally Counts to add what this derivation met to, or null where nobody measures it.

\return The hand events, strictly ascending by front.
*/
[[nodiscard]] std::vector<FretHandEvent> deriveFretHandEvents(
    const ChartConnections& connections, const TempoMap& tempo_map, int capo,
    const std::vector<FretHandPosition>& authored, FretHandDerivationTally* tally = nullptr);

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
puts down (\ref heldFretAt, so a natural harmonic holds at its node's fret and an open string holds
nothing) from where it is stated until the note's next fret statement or the ring's end, and a note
the picking hand stops the string for holds its held stop over the whole ring. A stop counts for
every stretch its interval overlaps — a finger struck earlier and still down when the stretch begins
included — and the statement at the ring's very end holds for no time at all.

\param notes The note stream.
\param held_frets Each note's held fret, index-parallel to \p notes: the complete table's fret
       (\ref chartHeldStops) wherever the stream has been resolved; read only under a note the
       picking hand stops the string for.
\param placements The placement stream, ascending by position.
\param tempo_map Tempo map placing each statement on the musical grid.

\return One range per placement, index-parallel to \p placements; empty where the stretch holds
        no stop at all.
*/
[[nodiscard]] std::vector<std::optional<HeldFretRange>> deriveHeldFretRanges(
    const std::vector<ChartNote>& notes, const std::vector<std::optional<int>>& held_frets,
    const std::vector<FretHandPosition>& placements, const TempoMap& tempo_map);

/*!
\brief \ref deriveHeldFretRanges over a resolved stream: its saved notes and the held stops resolved
beside them (\ref ChartResolutions::held_stops), the one pairing every resolved reader wants.

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
stretch - fret + 1)`, the stops being the ones \ref deriveHeldFretRanges finds — unless the
placement states its end fret (\ref FretHandPosition::end_fret), when the width is exactly
`end_fret - fret + 1`.

A stop BELOW the index finger's fret widens nothing — it stays outside the window, which is the
honest report that the placement is wrong — and nothing narrows a DERIVED window below
\ref g_min_fret_hand_width. An authored window may be narrower; its index finger still leaves the
narrowest window's room under the last fret (\ref normalizeFretHandPosition).

\param notes The note stream.
\param held_frets Each note's held fret, index-parallel to \p notes: the complete table's fret
       (\ref chartHeldStops) wherever the stream has been resolved; read only under a note the
       picking hand stops the string for.
\param placements The placement stream, ascending by position.
\param tempo_map Tempo map placing each statement on the musical grid.

\return One width per placement, index-parallel to \p placements.
*/
[[nodiscard]] std::vector<int> deriveFretHandWidths(
    const std::vector<ChartNote>& notes, const std::vector<std::optional<int>>& held_frets,
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
