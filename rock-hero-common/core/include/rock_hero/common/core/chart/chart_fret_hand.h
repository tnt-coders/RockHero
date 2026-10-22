/*!
\file chart_fret_hand.h
\brief The fret hand derived from the notes and the authored positions: what the notes demand of
the hand (\ref fretHandDemands), where the hand sits (\ref deriveFretHandEvents), how its window
rides a slide-out (\ref deriveFretHandRides), and the two together as a chart revision's hand layer
(\ref fretHandResolutions).
*/

#pragma once

#include <cstddef>
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
    long long instants{0};

    /*! \brief Of those, the instants where at least one fretted note is struck. */
    long long struck_instants{0};

    /*! \brief Pitched keyframe fret statements demanding their fret (hand-position rule 2). */
    long long keyframe_demands{0};

    /*! \brief Pull-offs planting a stop beneath their source, the open string included. */
    long long plants{0};

    /*! \brief Of those, the plants on a pressed fret: the demands of hand-position rule 12. */
    long long pressed_plants{0};

    /*! \brief Instants a fretting finger slides into (the hand-position slide rule). */
    long long slide_arrivals{0};

    /*! \brief Of those, the instants whose sliding fingers travel different distances. */
    long long slide_arrivals_disagreeing{0};

    // Filled by \ref deriveFretHandEvents.

    /*! \brief Holds that pin the hand for some time (hand-position rule 6, \ref fretHandHolds). */
    long long hold_pins{0};

    /*! \brief Figures the walk cut, the figures a slide shift placed included. */
    long long runs{0};

    /*! \brief Covered figures held still because a stop is still held (hand-position rule 6). */
    long long held_by_hold{0};

    /*! \brief Covered one-fret figures held still (hand-position rule 5); holds count first. */
    long long held_on_one_fret{0};

    /*! \brief Figures restating the standing window: nothing emitted (hand-position rule 11). */
    long long restated{0};

    /*! \brief Fronts reaching back before their figure's first instant (hand-position rule 10). */
    long long fronts_reaching_back{0};

    /*!
    \brief Slide arrivals placed by the standing window moved by the travel, with as much of the
    figure as it covers.
    */
    long long slide_shifts{0};

    /*!
    \brief Figures placed by the window a train of slides proves, carried back from its last
    landing along the unlifted finger's stops; a covered one-fret figure rule 5 would have left
    standing included.
    */
    long long slide_carried_back{0};
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

    /*!
    \brief The one-based string the hold is on, counted from the lowest-pitched string (as
    \ref ChartNote::string): every hold is built per string.
    */
    int string{1};
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
\brief One instant the hand must cover: the hull of every demand stated there and of every stop
held across it, and how many fretted notes are struck there.

Only a strike counts toward a chord, so a demand never makes one.
*/
struct FretHandInstant
{
    /*! \brief The instant, on the whole-note axis (\ref ChartConnections::places). */
    Fraction place{};

    /*! \brief The instant's bar, read once from its place (hand-position rule 3's bar test). */
    int measure{1};

    /*! \brief The lowest fret demanded or held at the instant. */
    int lowest{0};

    /*! \brief The highest fret demanded or held at the instant. */
    int highest{0};

    /*! \brief How many fretted notes are struck at the instant. */
    int struck{0};

    /*! \brief Present where a fretting finger slides in (the hand-position slide rule). */
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
\brief Hand-position rule 1, stated once: the fret a note demands of the fretting hand at an offset
along its ring, or nothing.

The fret slot its fretting finger occupies there (\ref fretHandStopAt), pressing a fret or touching
a node, so a natural harmonic demands its node's fret. An onset the PICKING hand makes
(\ref rightHandOnset) demands nothing, and neither does an open string — save one bent anywhere
along its ring, which is bent behind the nut, or behind the capo under one (hand-position rule 13).

THE DEMAND IS A PLAYABLE FRET: one below \ref firstPlayableFret is lifted to it, exactly as every
window is fitted (\ref fittedWindowFret), so the derivation and the hand-position coverage rule read
one number. A pressed fret already lies above the capo; what the lift moves is a behind-the-nut
bend, which demands the first playable fret, and a node nearest a fret at or below the capo.

\param note Note whose demand is asked.
\param offset Whole notes from the note's onset.
\param capo The tuning's capo fret; 0 for none.

\return The demanded fret, at or above the first playable fret, or nothing.
*/
[[nodiscard]] std::optional<int> fretHandDemandAt(const ChartNote& note, Fraction offset, int capo);

/*!
\brief Hand-position rule 6's release, stated once: the first instant at or after \p front at which
a note on one string lets go of the pressed \p held_fret a finger holds there, else \p close.

A note GRIPPING the fret is a fretting-hand note whose channel states it where the judgement begins
— at \p front, or at its onset where it starts later — and it releases the fret at its first
statement of any other stop: the finger slid away. Any other note, of either hand, releases it where
it sounds the string below the held fret, a node and the open string included, or AT it, since two
fingers cannot share one fret on one string. A fret above is another finger and leaves the fret
held, so a tap releases it only where it reaches it: the picking hand sounds a string above the
finger holding it, so a tap at or below proves that finger lifted.

Read from each note's fret statements: the onset, then every fret-stating keyframe but a slide-out,
each in force until the next or the ring's end, and each as the hand that makes it states it — the
fretting hand's node-aware stop (\ref frettingStopAt), or the picking hand's own fret where that
hand stops the string (\ref pickingHandStopsString). A release is a struck note's onset or a
glide's landing.

Searches only the notes that can release before \p close (\ref ChartConnections::string_notes): no
ring crosses the next onset on its own string, so the scan begins at the one note that can still
ring at \p front and ends at the first onset at or past a release already found.

\param connections The resolved connections (\ref chartConnections).
\param string_index The zero-based string the fret is held on (\ref chartStringIndex).
\param held_fret The fret held. MUST be pressed (\ref pressedFret): a hold states only pressed
       stops, since an open string and a natural harmonic hold nothing.
\param front Where the judgement begins on the whole-note axis.
\param close Where it ends: the answer where nothing releases the fret sooner.
\return The earliest release on the whole-note axis, or \p close where none comes before it.
*/
[[nodiscard]] Fraction stringReleasesStopAt(
    const ChartConnections& connections, std::size_t string_index, int held_fret, Fraction front,
    Fraction close);

/*!
\brief Hand-position rule 6, stated once: every stop the fretting hand provably holds, and for how
long — what the derivation keeps covered and what pins it (\ref deriveFretHandEvents), and what an
authored position must cover (\ref validateFretHandCoverage).

Two sources. A RING: a note holds each pressed stop its fret channel states, from that statement
up to the next — a slide holds the stop it leaves up to its arrival, half-open, and the stop it
arrives at from there — and the last to the end of its stored ring. So an arrival into the next head
holds nothing past the ring, a slide-out holds the stop it slides from until the ring ends (the
finger stays pressed while it slides), and bends and vibrato, which state no fret, keep holding. A
dead note holds like any other: its stored ring is the proof, and only its tail's display differs.
A GRIP: every pressed stop of a span's posture is held from the span's front until a note on its
string releases it (\ref stringReleasesStopAt), else to the close. The members' own statements are
the ring holds beside it. An open string holds nothing, and neither does a natural harmonic's ring
or node: its node is demanded at the onset only.

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

The hand-position rules are listed in \ref design_rulesets_hand_positions.

What a note DEMANDS of the hand (\ref fretHandDemandAt) is asked at its onset and at every pitched
keyframe fret statement but a slide-out (hand-position rule 2); a pull-off source also demands the
stop the pull-off plants beneath it (\ref chartPlantedStops), from where it states the stop pulled
from up to the pull-off (hand-position rule 12). Every stop the hand provably holds — each note's
stops in turn, and a span's grip from the span's front — is demanded the same way, from where its
hold begins up to, not including, its end. The demands are merged by instant into the hull each one
states, so a held stop fronting where nothing fretted is struck is still an instant the hand covers.
A demand reached by a fretting finger sliding from a different pressed stop marks its instant a
slide arrival, carrying the travel where every sliding finger agrees on it (the hand-position slide
rule).

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
\brief Visits every stop the fretting hand grips at a place: the one query both the hand-position
coverage rule (\ref uncoveringFretHandPositions) and an authored position's window
(\ref deriveFretHandEvents) read, so the two cannot disagree about what a position must cover.

Two kinds of stop. Every note struck exactly at the place demands its onset's fret
(\ref fretHandDemandAt) — nothing for an open string or a picking-hand onset — so a natural
harmonic's node is gripped at its strike alone. And every hold (\ref fretHandHolds) standing there,
begun at or before it and ending after it: a ring reaching exactly to the place is gone. A pressed
onset that also begins a hold is visited once per kind, which no reader can tell apart.

\param connections The resolved connections the demands were derived from.
\param demands What the notes demand of the hand (\ref fretHandDemands); its capo is the one the
       onsets are read under.
\param place Place on the whole-note axis.
\param visit Called with each gripped stop's fret, where it was struck or began holding, and its
       one-based string, the onsets first, in no further promised order.
*/
void forEachGrippedStop(
    const ChartConnections& connections, const FretHandDemands& demands, Fraction place,
    const std::function<void(int fret, Fraction from, int string)>& visit);

/*!
\brief Derives where the fret hand sits from the notes and the positions the charter authored: the
13-rule figure-segmented floor tracker, superseded wherever an authored position stands.

The hand-position rules are listed in \ref design_rulesets_hand_positions.

THE LAW, in the three sentences a charter is taught: cut the song into figures at boundaries you can
see — a chord being struck, a bar line, or the grip outgrowing the hand; put the hand at each
figure's floor, spanning the grip, never narrower than four; and two exceptions, both of which only
ever REMOVE motion: the hand never moves while a written note still rings, and a one-fret figure the
standing window already covers does not move it — unless a slide leaving it proves where the hand
was (the slide rule below).

What the notes demand of the hand is their own statement (\ref fretHandDemands): the walk covers
every instant it names, a held stop among them from where its hold begins, so no window drops a
ringing stop or a held grip — the direction is notes, then grips, then positions — and each hold
also pins the hand while it holds.

Each event's front is the first onset after the previous figure's last instant and the bar line
that broke the figure that lies inside no hold. It is never later than the figure's first instant
the standing window does not cover, so the last chord of a repeated chain keeps the chain's window.
The window before the first front is the reader's question. A derived event is emitted only where
the window changes.

AN AUTHORED POSITION is a statement the derivation consumes, never a patch on its output. It is
always an event, fronting exactly where it stands, and no figure runs across it. Its window is
`[fret, end fret]` where the end is stated; otherwise the stated fret is the floor and the top is
the highest of every stop gripped at its instant (\ref forEachGrippedStop) and of the figure it
heads, cut at the first later instant reaching below the stated fret — which it never holds, so that
instant widens nothing — never under four frets, so it keeps every stop the hand-position coverage
rule makes it cover. It stands until the chart contradicts it: it takes its own instant whatever
sounds there, and every later instant whose demands fit inside the window, up to the next authored
position or slide arrival. The numbered hand-position rules resume at the first instant that does
not fit, and their fronts never reach back to an authored position.

A SLIDE MOVES THE HAND. A fretting finger's pitched slide from one pressed stop to another — a
keyframe glide, or a glide arriving into the next head — proves the hand moved with it and kept the
finger's place in it. The arrival is a forced boundary like an authored position: no figure and no
authored tenure runs across it, and hand-position rules 5, 6 and 10 neither hold the old window over
it nor reach back across it. The figure the slide lands in is played in the standing window moved
by the travel: its floor is the moved floor and its width rule 8's over the figure from there, so a
six-fret grip sliding onto a lone note arrives four wide. The move stands only where the moved
window covers the arrival — a finger held outside it proves the hand stayed — and only where every
finger sliding into that instant travels the same distance; it reaches as far into the figure as
the moved window covers, and the first instant it does not cover heads a figure of its own. Where it
does not stand, the figure is placed by the ordinary rules. Read backward, the proof runs along the
whole train of slides the finger makes without lifting — a note's keyframes, a glide into the next
head — each landing heading a figure of its own for as long as each ends at an arrival every
sliding finger agrees on. The hand stands where it is unless the train refutes it: from the last
landing back, each landing stands in its own window where the hand stands through the rest of the
train from it, else in the next landing's proven window moved back by the travel, as low inside it
as still covers the figure, at the figure's own width; where that misses the figure, the hand moved
apart from that slide and the train ends there for every figure before. The figure the train leaves
is read the same way, forced from its last instant before the slide. That proof outranks rule 5,
whose one-fret figure states nothing, but not rule 6, whose hold does; a figure the forward move
placed is never carried. The forward move then reads from the carried window and plays the train on.
A slide-out's fret is no stop, so it moves nothing here (\ref deriveFretHandRides).

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
\brief The window's ride with the unpitched slide-outs ending at one instant: MOTION derived from
the slides, never a position, so it is no hand event and changes none (\ref deriveFretHandRides).
*/
struct FretHandRide
{
    /*!
    \brief Where the ride leaves the standing window: the earliest departure among its slide-outs,
    each the instant its last stop is stated (\ref offsetBeforeEnd).
    */
    Fraction departure{};

    /*! \brief Where the slide-outs end, at their rings' end: the instant the ride arrives. */
    Fraction end{};

    /*! \brief The window standing at the departure, which the ride shifts and returns to. */
    FretWindow standing{};

    /*!
    \brief The window the ride travels toward: \ref standing shifted the least distance that covers
    every exit fret, keeping its width, and never equal to it.
    */
    FretWindow ridden{};

    /*!
    \brief The slide-out departing first, whose drawn rail the ride follows: an index into the
    connections' saved notes.
    */
    std::size_t note{0};

    /*!
    \brief Where the window is back at \ref standing: the first instant the hand covers at or after
    \ref end. Absent where a hand event fronts there or first, since that event is the way back, and
    where a later ride arrives first.
    */
    std::optional<Fraction> back{};

    /*!
    \brief Compares two rides by their stored fields.
    \param lhs Left-hand ride.
    \param rhs Right-hand ride.
    \return True when both rides store equal values.
    */
    friend bool operator==(const FretHandRide& lhs, const FretHandRide& rhs) = default;
};

/*!
\brief The window's ride with every unpitched slide-out of a stop the fretting hand holds.

The hand travels with a slide-out of a stop it holds (\ref fretHandDemandAt), so the window does:
from the slide-out's departure it travels on the slide-out's own curve toward the window covering
the exit fret, and it is back at the standing window by the next instant the hand covers. Slide-outs
ending at one instant ride one window, shifted the least distance that covers every exit, keeping
its width; a window already covering them does not move. A slide-out stays planted where it has no
room: where an instant the hand covers, or a hand event, lies strictly between its departure and its
end — an event there is the hand moving on. Either one standing exactly at the end is where the ride
lands, which is how a slide-out into the next onset is ridden. A tap's slide-out is the picking
hand's travel and moves nothing here.

How far along its curve the drawn window follows the ride is presentation: the board's hand track
follows the slide's rail to where its ink is cut and comes to rest in whatever arrives at the end
(\ref makeChartViewState). Nothing else reads the rides.

\param connections The resolved connections the events were derived from.
\param demands The demands the events were derived from (\ref fretHandDemands): the instants a
       ride needs room between, and the capo.
\param events The hand events (\ref deriveFretHandEvents).

\return The rides, strictly ascending by end.
*/
[[nodiscard]] std::vector<FretHandRide> deriveFretHandRides(
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
    std::vector<FretHandRide> rides;
};

/*!
\brief Derives the hand layer from a chart's notes layer and its authored positions.

The pipeline's last stage — notes, then grips, then positions — so it reads the demands the notes
layer already derived (\ref ChartResolutions::hand_demands) rather than deriving them again.

\param notes_layer The notes layer (\ref chartResolutions); its demands' capo is the one the hand
       is derived under.
\param authored The chart's authored positions, valid and strictly ascending.
\param tempo_map Tempo map the notes layer was derived against.

\return The hand events and the slide-out rides.
*/
[[nodiscard]] FretHandResolutions fretHandResolutions(
    const ChartResolutions& notes_layer, const std::vector<FretHandPosition>& authored,
    const TempoMap& tempo_map);

} // namespace rock_hero::common::core
