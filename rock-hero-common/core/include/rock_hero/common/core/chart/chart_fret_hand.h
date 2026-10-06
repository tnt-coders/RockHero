/*!
\file chart_fret_hand.h
\brief The fret hand derived from the notes and the authored positions: where the hand sits
(\ref deriveFretHandEvents), and how its window rides a slide-out (\ref deriveFretHandRides).
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

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

    /*! \brief Holds that pin the hand for some time (rule 6, \ref fretHandHolds). */
    long long hold_pins{0};

    /*! \brief Figures the walk cut. */
    long long runs{0};

    /*! \brief Covered figures held still because a stop is still held (rule 6). */
    long long held_by_hold{0};

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

/*! \brief A stop the fretting hand provably holds over `[from, to)` on the whole-note axis. */
struct FretHandHold
{
    /*! \brief Where the hold begins. */
    Fraction from{};

    /*! \brief Where the hold ends, exclusive: an event exactly here finds the stop released. */
    Fraction to{};

    /*! \brief The fret held. */
    int fret{0};
};

/*!
\brief RULE 6, stated once: every stop the fretting hand provably holds, and for how long — what
the derivation keeps covered and what pins it (\ref deriveFretHandEvents), and what an authored
position must cover (\ref validateFretHandCoverage).

Two sources. A RING: a note the fretting hand stops at a fret holds from its last pitched stop —
where its fret channel last arrived somewhere new — to the end of its stored ring, the same end the
span derivation reads. So a slide-then-hold holds its destination, an arrival into the next head
holds nothing, a slide-out holds the stop it slides from until the ring ends (the finger stays
pressed while it slides), and bends and vibrato, which state no fret, keep holding. A dead note
holds like any other: its stored ring is the proof, and only its tail's display differs. A GRIP:
every pressed stop of a span's posture holds across the whole span (\ref deriveChartShapes), from
its front to its close, a member struck after the front included. An open string holds nothing,
and neither does a natural harmonic's ring or node: its node is demanded at the onset only.

\param connections The resolved connections (\ref chartConnections).
\param shapes The spans and postures derived from the same connections (\ref deriveChartShapes).

\return Every hold, in no promised order.
*/
[[nodiscard]] std::vector<FretHandHold> fretHandHolds(
    const ChartConnections& connections, const ChartShapes& shapes);

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
Every stop the hand provably holds (\ref fretHandHolds) is demanded the same way, from where its
hold begins up to, not including, its end, so no window drops a ringing stop or a held grip — the
direction is notes, then grips, then positions — and each also pins the hand while it holds.

Each event's front is the first onset after the latest of the previous figure's last instant, the
latest end among the holds begun before the figure, and the bar line that broke the figure;
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
\param holds The stops the hand holds (\ref fretHandHolds), derived from the same connections.
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
    const ChartConnections& connections, const std::vector<FretHandHold>& holds,
    const TempoMap& tempo_map, int capo, const std::vector<FretHandPosition>& authored,
    FretHandDerivationTally* tally = nullptr);

/*!
\brief The first event fronting strictly AFTER a place: the event before it is the window standing
there, since an event fronting at the place has already arrived.

\param events Events strictly ascending by front.
\param place Place on the whole-note axis.

\return The first event fronting after `place`; `events.begin()` where no window stands there yet.
*/
[[nodiscard]] std::vector<FretHandEvent>::const_iterator firstFretHandEventAfter(
    const std::vector<FretHandEvent>& events, Fraction place);

/*!
\brief The window's ride with every unpitched slide-out: MOTION derived from the slide, never a
position, so it is no hand event and changes none.

The hand travels with a slide-out of a stop it holds (\ref fretHandDemandAt), so the window does: it
arrives at the slide-out's end covering the exit fret, and at the next instant the hand derivation
covers it is back at the window the hand events state there. The ride shifts the standing window the
least distance that covers the exit frets of every slide-out ending at that instant, keeping its
width; a window already covering them does not move. A slide-out stays planted where it has no
room: where the next demand comes before its end or exactly at it, or where a hand event fronts
between its start and its end — that event is the hand moving on, so the drawing has no departure
case. The way back is a ride arrival only where no hand event fronts first; otherwise that event is
the way back.

The board's hand track merges these arrivals with the hand events and eases each as it eases any
arrival, so the ride takes the slide's own segment and unpitched ease. Nothing else reads them.

\param connections The resolved connections the events were derived from.
\param holds The holds the events were derived from (\ref fretHandHolds).
\param tempo_map Tempo map the connections were resolved against.
\param events The hand events (\ref deriveFretHandEvents).

\return The ride arrivals, strictly ascending by front, none fronting where a hand event does.
*/
[[nodiscard]] std::vector<FretHandEvent> deriveFretHandRides(
    const ChartConnections& connections, const std::vector<FretHandHold>& holds,
    const TempoMap& tempo_map, const std::vector<FretHandEvent>& events);

} // namespace rock_hero::common::core
