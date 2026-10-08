/*!
\file chart_fret_hand.h
\brief The fret hand derived from the notes and the authored positions: what the notes demand of
the hand (\ref fretHandDemands), where the hand sits (\ref deriveFretHandEvents), how its window
rides a slide-out (\ref deriveFretHandRides), and the two together as a chart revision's hand layer
(\ref fretHandResolutions).
*/

#pragma once

#include <functional>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief The resolved connections the hand derivation reads (`chart_legato.h`).

Declared rather than included, as are \ref ChartShapes and \ref ChartResolutions below:
`chart_legato.h` includes this header for \ref FretHandDemands, so the include runs one way only,
and every signature here takes them by reference, which needs no more than the name.
*/
struct ChartConnections;

/*! \brief The spans and postures \ref fretHandHolds reads (`chart_shapes.h`). */
struct ChartShapes;

/*! \brief The notes layer \ref fretHandResolutions reads (`chart_legato.h`). */
struct ChartResolutions;

/*!
\brief What a hand derivation met on its way, counted for the instruments that measure it — the
dataset scorer and the corpus census — and for nothing the product reads.

Two derivations fill it, each its own counts: the notes' demands (\ref fretHandDemands) and the
walk over them (\ref deriveFretHandEvents). Every derivation handed a tally ADDS to the counts
already in it, so one tally sums a corpus.
*/
struct FretHandDerivationTally
{
    // Filled by \ref fretHandDemands.

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

    /*! \brief Instants a fretting finger slides into (the slide rule). */
    long long slide_arrivals{0};

    /*! \brief Of those, the instants whose sliding fingers travel different distances. */
    long long slide_arrivals_disagreeing{0};

    // Filled by \ref deriveFretHandEvents.

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

    /*! \brief Slide arrivals that shifted the standing window by the travel. */
    long long slide_shifts{0};
};

/*!
\brief A stop the fretting hand provably holds over `[from, to)` on the whole-note axis
(\ref fretHandHolds).
*/
struct FretHandHold
{
    /*! \brief Where the hold begins. */
    Fraction from{};

    /*! \brief Where the hold ends, exclusive: an event exactly here finds the stop released. */
    Fraction to{};

    /*! \brief The fret held. */
    int fret{0};
};

/*! \brief Fretting fingers sliding into one instant: how far the hand travels with them. */
struct FretHandSlideIn
{
    /*!
    \brief The frets the hand travels with the sliding fingers; empty where they travel different
    distances, which proves no single shift.
    */
    std::optional<int> travel{};
};

/*!
\brief One instant the hand must cover (the plan's "coverage group"): the hull of every demand
stated there and of every stop held across it, and how many fretted notes are struck there.

Only a strike counts toward a chord, so a demand never makes one.
*/
struct FretHandInstant
{
    /*! \brief The instant, on the whole-note axis (\ref ChartConnections::places). */
    Fraction place{};

    /*! \brief The bar the instant lies in, read once from its place (rule 3's bar test). */
    int measure{1};

    /*! \brief The lowest fret demanded or held at the instant. */
    int lowest{0};

    /*! \brief The highest fret demanded or held at the instant. */
    int highest{0};

    /*! \brief How many fretted notes are struck at the instant. */
    int struck{0};

    /*! \brief Present where a fretting finger slides into this instant (the slide rule). */
    std::optional<FretHandSlideIn> slide{};
};

/*!
\brief The notes' statement to the fretting hand: every stop it holds, every instant it must cover,
and the capo both were read under.

Derived once per notes layer (\ref ChartResolutions::hand_demands) and read by the hand walk
(\ref deriveFretHandEvents), the slide-out rides (\ref deriveFretHandRides), the coverage gate
(\ref uncoveringFretHandPositions) and the editor's redundancy walk. A function of the notes, the
spans, the capo and the tempo map — never of the authored positions, which is why a position edit
leaves it standing.
*/
struct FretHandDemands
{
    /*! \brief The tuning's capo fret the demands were derived under; 0 for none. */
    int capo{0};

    /*!
    \brief Every stop the fretting hand provably holds (\ref fretHandHolds), ascending by where each
    hold begins.
    */
    std::vector<FretHandHold> holds;

    /*!
    \brief Index-parallel to \ref holds: `hold_reach[k]` is the latest end among `holds[0..k]`, so
    the holds standing at a place are found by walking back from the last one begun there.
    */
    std::vector<Fraction> hold_reach;

    /*! \brief Every instant the hand covers, strictly ascending by place. */
    std::vector<FretHandInstant> instants;
};

/*!
\brief RULE 1, stated once: the fret a note demands of the fretting hand at an offset along its
ring, or nothing.

The fret its fretting finger occupies there (\ref heldFretAt), so a natural harmonic demands its
node's fret. An onset the PICKING hand makes (\ref rightHandOnset) demands nothing, and neither does
an open string — save one bent anywhere along its ring, which is bent behind the nut, or behind the
capo under one (rule 13).

THE DEMAND IS A PLAYABLE FRET: one below \ref firstPlayableFret is lifted to it, exactly as every
window is fitted (\ref fittedWindowFret), so the derivation and the coverage rule read one number. A
pressed fret already lies above the capo; what the lift moves is a behind-the-nut bend, which
demands the first playable fret, and a node nearest a fret at or below the capo.

\param note Note whose demand is asked.
\param offset Whole notes from the note's onset.
\param capo The tuning's capo fret; 0 for none.

\return The demanded fret, at or above the first playable fret, or nothing.
*/
[[nodiscard]] std::optional<int> fretHandDemandAt(const ChartNote& note, Fraction offset, int capo);

/*!
\brief RULE 6, stated once: every stop the fretting hand provably holds, and for how long — what
the derivation keeps covered and what pins it (\ref deriveFretHandEvents), and what an authored
position must cover (\ref validateFretHandCoverage).

Two sources. A RING: a note holds each pressed stop its fret channel states, from that statement
up to the next — a slide holds the stop it leaves up to its arrival, half-open, and the stop it
arrives at from there — and the last to the end of its stored ring. So an arrival into the next head
holds nothing past the ring, a slide-out holds the stop it slides from until the ring ends (the
finger stays pressed while it slides), and bends and vibrato, which state no fret, keep holding. A
dead note holds like any other: its stored ring is the proof, and only its tail's display differs.
A GRIP: a span's member struck after its front (\ref deriveChartShapes) holds its pressed stop from
the front up to its strike, after which its ring holds what it states. An open string holds
nothing, and neither does a natural harmonic's ring or node: its node is demanded at the onset only.

\param connections The resolved connections (\ref chartConnections).
\param shapes The spans and postures derived from the same connections (\ref deriveChartShapes).
\param capo The tuning's capo fret; 0 for none. Each stop is read through \ref fretHandDemandAt.

\return Every hold, in no promised order.
*/
[[nodiscard]] std::vector<FretHandHold> fretHandHolds(
    const ChartConnections& connections, const ChartShapes& shapes, int capo);

/*!
\brief Derives what the notes demand of the fretting hand: the stops it holds (\ref fretHandHolds)
and every instant it must cover.

What a note DEMANDS of the hand (\ref fretHandDemandAt) is asked at its onset and at every pitched
keyframe fret statement but a slide-out (rule 2); a pull-off source's onset also demands the stop
the pull-off plants beneath it (\ref chartPlantedStops), which widens every instant up to the
pull-off (rule 12). Every stop the hand provably holds — each note's stops in turn, and a span
member's from the span's front — is demanded the same way, from where its hold begins up to, not
including, its end. The demands are merged by instant into the hull each one states, so a held stop
fronting where nothing fretted is struck is still an instant the hand covers. A demand reached by
a fretting finger sliding from a different pressed stop marks its instant a slide arrival, carrying
the travel where every sliding finger agrees on it (the slide rule).

\param connections The resolved connections (\ref chartConnections).
\param shapes The spans and postures derived from the same connections (\ref deriveChartShapes);
       empty where no span is known, so the rings are the only holds.
\param capo The tuning's capo fret; 0 for none.
\param tempo_map Tempo map the connections were resolved against, placing each instant in its
       measure.
\param tally Counts to add what this derivation met to, or null where nobody measures it.

\return The holds, the instants, and the capo they were derived under.
*/
[[nodiscard]] FretHandDemands fretHandDemands(
    const ChartConnections& connections, const ChartShapes& shapes, int capo,
    const TempoMap& tempo_map, FretHandDerivationTally* tally = nullptr);

/*!
\brief Visits every stop the fretting hand grips at a place: the one query both the coverage rule
(\ref uncoveringFretHandPositions) and an authored position's window (\ref deriveFretHandEvents)
read, so the two cannot disagree about what a position must cover.

Two kinds of stop. Every note struck exactly at the place demands its onset's fret
(\ref fretHandDemandAt) — nothing for an open string or a picking-hand onset — so a natural
harmonic's node is gripped at its strike alone. And every hold (\ref fretHandHolds) standing there,
begun at or before it and ending after it: a ring reaching exactly to the place is gone. A pressed
onset that also begins a hold is visited once per kind, which no reader can tell apart.

\param connections The resolved connections the demands were derived from.
\param demands What the notes demand of the hand (\ref fretHandDemands); its capo is the one the
       onsets are read under.
\param place Place on the whole-note axis.
\param visit Called with each gripped stop's fret and where it was struck or began holding, the
       onsets first, in no further promised order.
*/
void forEachGrippedStop(
    const ChartConnections& connections, const FretHandDemands& demands, Fraction place,
    const std::function<void(int fret, Fraction from)>& visit);

/*!
\brief Derives where the fret hand sits from the notes and the positions the charter authored — THE
13-rule figure-segmented floor tracker (`docs/plans/in-progress/fhp-derivation-algorithm.md`, with
plan 60's Phase 1 rulings), superseded wherever an authored position stands.

THE LAW, in the three sentences a charter is taught: cut the song into figures at boundaries you can
see — a chord being struck, a bar line, or the grip outgrowing the hand; put the hand at each
figure's floor, spanning the grip, never narrower than four; and two exceptions, both of which only
ever REMOVE motion: the hand never moves while a written note still rings, and a one-fret figure the
standing window already covers does not move it.

What the notes demand of the hand is their own statement (\ref fretHandDemands): the walk covers
every instant it names, a held stop among them from where its hold begins, so no window drops a
ringing stop or a held grip — the direction is notes, then grips, then positions — and each hold
also pins the hand while it holds.

Each event's front is the first onset after the latest of the previous figure's last instant, the
latest end among the holds begun before the figure, and the bar line that broke the figure;
the window before the first front is the reader's question. A derived event is emitted only where
the window changes.

AN AUTHORED POSITION is a statement the derivation consumes, never a patch on its output. It is
always an event, fronting exactly where it stands, and no figure runs across it. Its window is
`[fret, end fret]` where the end is stated; otherwise the stated fret is the floor and the top is
the highest of the figure it heads and every stop gripped at its instant (\ref forEachGrippedStop),
never under four frets, so it keeps every stop the coverage rule makes it cover. It stands until the
chart contradicts it: it takes its own instant whatever sounds there, and every later instant whose
demands fit inside the window, up to the next authored position. The 13 rules resume at the first
instant that does not fit, and their fronts never reach back to an authored position.

A SLIDE MOVES THE HAND. A fretting finger's pitched slide from one pressed stop to another — a
keyframe glide, or a glide arriving into the next head — proves the hand moved with it and kept the
finger's place in it: the window at the slide's arrival is the standing one shifted by the travel,
fret and top together. The arrival is a forced boundary like an authored position: no figure and no
authored tenure runs across it, and rules 5, 6 and 10 neither hold the old window over it nor reach
back across it. The shift stands only where the shifted window still covers everything demanded and
held at the arrival — a finger held outside it proves the hand stayed — and an instant where
fingers slide different distances proves no single shift; either way the window is placed by the
ordinary rules there. A slide-out's fret is no stop, so it shifts nothing here
(\ref deriveFretHandRides).

Memoryless per figure and never reads seconds: the bar test is the measure number changing between
consecutive instants.

\param connections The resolved connections (\ref chartConnections): the saved notes and their
       places, whose onsets the fronts land on.
\param demands What the notes demand of the hand (\ref fretHandDemands), derived from the same
       connections; its capo is the one every window is fitted under.
\param tempo_map Tempo map the connections were resolved against, placing each authored position
       and bar line. A parameter rather than part of the connections, which are a value and could
       only carry a reference that might dangle.
\param authored The chart's authored positions, valid and strictly ascending
       (\ref validateFretHandPositions); empty to ask what the notes alone derive, as an instrument
       scoring the derivation against stored positions must.
\param tally Counts to add what this derivation met to, or null where nobody measures it.

\return The hand events, strictly ascending by front.
*/
[[nodiscard]] std::vector<FretHandEvent> deriveFretHandEvents(
    const ChartConnections& connections, const FretHandDemands& demands, const TempoMap& tempo_map,
    const std::vector<FretHandPosition>& authored, FretHandDerivationTally* tally = nullptr);

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
\param demands The demands the events were derived from (\ref fretHandDemands): the instants a
       ride needs room before, and the capo.
\param events The hand events (\ref deriveFretHandEvents).

\return The ride arrivals, strictly ascending by front, none fronting where a hand event does.
*/
[[nodiscard]] std::vector<FretHandEvent> deriveFretHandRides(
    const ChartConnections& connections, const FretHandDemands& demands,
    const std::vector<FretHandEvent>& events);

/*!
\brief The hand layer of a chart revision: where the hand sits and how its window rides, derived
from the notes layer (\ref ChartResolutions) and the authored positions.

A layer of its own because the positions are the one input the notes layer never reads: a position
edit re-derives this and leaves the notes layer standing.
*/
struct FretHandResolutions
{
    /*!
    \brief The hand events (\ref deriveFretHandEvents), authored positions among them, strictly
    ascending by front. Not index-parallel to the notes.
    */
    std::vector<FretHandEvent> events;

    /*!
    \brief How the window rides the unpitched slide-outs (\ref deriveFretHandRides): motion beside
    \ref events, read by the board's hand track alone, so it is no chip and no position.
    */
    std::vector<FretHandEvent> rides;
};

/*!
\brief Derives the hand layer from a chart's notes layer and its authored positions.

The pipeline's last stage — notes, then grips, then positions — so it reads the demands the notes
layer already derived (\ref ChartResolutions::hand_demands) rather than deriving them again.

\param chart The notes layer (\ref chartResolutions); its demands' capo is the one the hand is
       derived under.
\param authored The chart's authored positions, valid and strictly ascending.
\param tempo_map Tempo map the notes layer was derived against.

\return The hand events and the slide-out rides.
*/
[[nodiscard]] FretHandResolutions fretHandResolutions(
    const ChartResolutions& chart, const std::vector<FretHandPosition>& authored,
    const TempoMap& tempo_map);

} // namespace rock_hero::common::core
