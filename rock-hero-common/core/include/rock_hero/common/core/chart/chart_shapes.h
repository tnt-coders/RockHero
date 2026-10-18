/*!
\file chart_shapes.h
\brief The hand-posture derivation: the spans a note stream implies, and the postures they hold.
*/

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/head_margins.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <span>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief The resolved connections this derivation reads (`chart_legato.h`).

Declared rather than included: the connections carry the shapes (\ref ChartResolutions), so the
include runs one way only and a reference needs no more than the name.
*/
struct ChartConnections;

/*!
\brief One hand posture: the stop held on each string while a span runs.

Array index 0 is the lowest-pitched string; a null entry means the string is not part of the
posture. The array is \ref g_max_chart_strings long — the model's own bound on a string number,
not a statement about the tuning — so a chart with fewer strings simply leaves the top slots
empty.

Derived, never authored — the stops an onset's struck members hold (\ref ChartStop: a fret
pressed, the open string, or a harmonic node touched), plus the stops a right-hand onset says the
fretting hand is holding under it (\ref deriveChartShapes), and every ring still sounding where the
span is founded, struck before it or not, fretted or open: it is part of the sounding shape. A
natural harmonic's ring states no grip and never joins. A stop carries no provenance here on
purpose:
the posture is what the hand holds, so two spans holding an identical grip stay one deduplicated
posture however each was learned — while a node grip and a fret grip printing the same number are
two postures, because they are two grips. Chord names and fingerings carry no field here because
nothing writes one; when they are authored they become a dictionary keyed by a posture rather than
members of it.
*/
struct ChartPosture
{
    /*!
    \brief THE GRIP: the stop the hand holds per string; nullopt where it holds none.

    What every rule and every display reads — founding, extent, class, contradiction, the tap's
    held default, the census's carry rows, the bracket's digits.
    */
    std::vector<std::optional<ChartStop>> stops;

    /*!
    \brief Compares two postures by their grip.
    \param lhs Left-hand posture.
    \param rhs Right-hand posture.
    \return True when both hold the same stop on every string.
    */
    friend bool operator==(const ChartPosture& lhs, const ChartPosture& rhs) = default;

    /*!
    \brief Orders two postures by their grip — the posture's own identity, so a table keyed on it
           deduplicates by exactly what \ref operator== compares.
    \param lhs Left-hand posture.
    \param rhs Right-hand posture.
    \return Ordering by grip; partial because a stop's is (\ref ChartStop).
    */
    friend std::partial_ordering operator<=>(const ChartPosture& lhs, const ChartPosture& rhs) =
        default;
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
    /*!
    \brief Musical start of the span — its FRONT, which is not always where the walk noticed it.

    THE DATING RULE: a span dates from its EARLIEST MEMBER ONSET NOT COVERED by a preceding span. An
    accumulation's members arrive one at a time, and the statement began where the first of them was
    struck — so the rails run from there and the later members arrive inside it, rather than the
    mark starting at whichever arrival happened to reach the threshold. A member's onset stands in
    for the beginning of the STATEMENT it makes: a restrike of a stop still held began its
    statement earlier where another member of the figure sounds at the restrike (re-articulation)
    — though no span dates behind the coverage frontier, since a beginning an emitted span already
    fronted is spent up to that span's close, so a restrike of a stop the closed span held dates
    the next span from where the last one ended — and a slid finger's began at the landing. A
    WHOLE-GRIP STROKE standing alone reads none of that — it is a span boundary (the absorption
    rule), and the box it founds dates from the stroke itself, never from a lone note before it
    that happened to hold one of its stops.

    The second half is what keeps spans from overlapping: a ring whose onset lies inside a span
    already emitted is CARRIED, and a carry never backdates. That is one comparison with two
    consequences, and \ref deriveChartShapes spends it once — a carry that dates a span is a
    founding member and BOUNDS it, while one crossing in from covered ground is a tail that joins
    nothing. Landings and death survivors are covered by construction, which is why a successor
    starts exactly where its predecessor ended.

    A place on the whole-note axis, because every reader measures from it: a span's close, its
    statements' reach and the instants it covers are all this place plus a whole-note distance.
    */
    Fraction place{};

    /*!
    \brief THE MUSICAL CLOSE, in whole notes from \ref place; always strictly positive.

    **The instant the span's statement actually ended, and nothing about how it is drawn.** Two
    arms, and the distinction is the whole field: where an EVENT closed the span — a contradiction,
    a growth split, any close at a slot — the close is that CLOSING EVENT'S OWN ONSET, because the
    hand demonstrably moved there; where the statement simply RAN OUT, the close is the shape's own
    reach (the continuity law's minimum). Storing the reach unconditionally would claim grip past a
    proven hand move, and storing the closing onset unconditionally would claim grip through proven
    silence, so the close is the EARLIER of the two.

    SHAPE RULE 6'S MARGIN IS NOT IN HERE. It is a DISPLAY rule, applied once where the view state
    is built (\ref makeChartViewState) from \ref closing_onset_place and \ref stated_extent beside
    this. Storing it instead would put display spacing inside every seam: a growth split would
    close the predecessor one display margin before the successor's own start, leaving spans that
    abut musically a margin apart in the data, which every reader of a figure's seams would then
    have to merge back across.

    THE POSTURE TRUTH CRITERION, which this field is what enforces: **no span claims a stop the hand
    abandoned while it ran.** A span's posture is a per-span set that only ever GROWS (growth IS
    accumulation), so the two ways it could come to lie are by outliving a member and by admitting
    one the front predates — and the extent law below and Law A together forbid both. The first
    member whose statement stops bounds the whole span — the trailing edge — and the dating floor
    bounds the front by the end of each stated string's last FOREIGN sound (Law A's clamp, the
    leading edge), an invariant growth keeps by refusing: a strike on a string that audibly sounded
    another stop since the front breaks the span rather than joining it, since the bracket would
    otherwise print the new fret back over that sound. So every fret a bracket prints was held for
    every instant the bracket covers. A long accumulation bracket is therefore true BY CONSTRUCTION,
    not by measurement.

    THE INVARIANT (shape rule 3): **every span is strictly positive**, since every member sounds. A
    span runs as long as every sounding member goes on stating its stop (THE CONTINUITY LAW, shape
    rule 3), and every member's own chain reaches past the span start, so the extent can only reach
    the start itself where no member sounds at all. It is an invariant of the arithmetic rather than
    a case: the close is the earlier of two instants that are both at or after the start, so it can
    only answer the start itself where the reach does.

    TRAVEL does not shorten a span to its own start; the split happens at the LANDING: a chord slide
    keeps the fingers planted, so the rings run continuously and the continuity law itself covers
    the transit — the span states the departing grip, COVERS the glide, and ends where the new grip
    is established, which is exactly where the successor span opens. The two tile with no gap
    between them, and because the close carries no display margin they tile in the stored data too
    and not only in the walk's own reasoning.
    */
    Fraction sustain{};

    /*!
    \brief How far the span's own STATEMENTS reach, in whole notes from \ref place.

    The last instant an EVENT stated this span — its final strum, or the slot whose holds opened or
    grew it — capped at the musical close, and zero on a span no event ever stated. Shape rule 6's
    display trim floors on it, because a span's furniture may not retreat behind its own last
    statement: in a fast enough passage the closing onset crowds inside the margin, and a box
    trimmed blindly would stop before the strum it is drawn over.

    Published rather than re-derived beside the trim, for the reason every other span fact here is:
    answering it means knowing WHICH SLOTS this statement covers, and this walk is the only thing
    that does. A projection-side scan for "the last onset inside the extent" would count a tap,
    which states nothing of the shape, so it would be the same rule written twice and free to
    disagree.
    */
    Fraction stated_extent{};

    /*!
    \brief The place of the SOUNDING onset that closed this span, where one did.

    The head shape rule 6's display trim keeps its distance from. Present only where that head cut a
    live statement, so it always sits AT the close: a span whose rings died before the event arrived
    keeps its own length and is not pulled back from a head it never reached.

    Empty on the closes that have no head to clear. A span that RAN OUT before any event cut it has
    no closing head, and a slot of HELD FINGERS sounds nothing to keep a distance from — there the
    shape being replaced ends exactly where the new one starts, which is also what keeps a landing
    successor tiled onto its predecessor.
    */
    std::optional<Fraction> closing_onset_place{};

    /*! \brief Index into the posture table derived alongside (\ref ChartShapes::postures). */
    std::size_t posture{0};

    /*!
    \brief True when some SOUNDING of this span was not the shape WHOLE.

    Shape rule 5's class rule in ONE comparison: a slot striking fewer strings than the shape
    SOUNDS is the shape's members arriving SEPARATELY, so the span is an arpeggio for its whole
    length — the span is one statement, and its class is HOW that statement's members arrive. A
    derived grip of fewer than three members never sounds in parts: its partial sounding breaks it
    instead, like any box's.

    Asked of every slot inside the span that sounds anything, ITS OWN START INCLUDED. The start is
    not a second case: a span whose opening slot strikes fewer strings than its shape sounds is a
    span whose posture CARRIES a string into that start without an onset at it — the carried start,
    answered here rather than re-derived one slot earlier and off a different stream. A
    partial restrike, a lone re-pick and a carried start are one fact at three widths, and the walk
    answers all three with the one count.

    A LANDING SUCCESSOR (\ref landing_opened) has no fourth width, and deliberately gets no constant
    `true` saying that nothing struck at a landing means its members arrive separately. A LANDING IS
    NOT A SOUNDING, and neither is a DEATH — nothing is struck at either because the surviving rings
    simply carry on — so there is no sounding of the shape to be partial, and the walk's own guard
    already says it: a slot that sounds nothing is no sounding of the shape, and a boundary has no
    slot at all. Such a constant would be honest only if a successor could never be strummed, which
    shape rule 4 allows, so it would make a chord sliding into chords arrive an arpeggio at every
    landing. A successor classifies by the ordinary triggers like any other span.

    An ACCUMULATION needs no clause here either, and that is worth stating because it looks like it
    should: its opening slot strikes fewer strings than the shape sounds BY DEFINITION — the rings
    it overlapped into are the rest — so this one count answers it at the founding, and every
    accumulation is an arpeggio by construction rather than by a rule of its own.

    Carried here rather than re-derived beside the arrival rule: answering it needs to know WHICH
    SLOTS this statement covers, and this walk is the only thing that does. What a reader can see is
    the span's WINDOW, and the closing onset sits exactly ON its end whenever an event closed the
    span — so a window re-derived from the extent cannot tell a slot the statement RODE from the
    slot that CLOSED it. Measured against the corpus: of the 296 spans a lone re-pick appears in, 48
    hold that re-pick only at the span's own end, and 39 of those print as boxes — the onset there
    CLOSED the span rather than continuing it. A re-derived window has no way to tell the two apart;
    the walk never has to ask, because riding the slot is what it did.

    A ring sounding into the founding, fretted or open, is a member like any other; a chimed ring
    struck before the span is none, prints in no bracket, and so classes nothing.
    */
    bool sounds_in_parts{false};

    /*!
    \brief True when a LANDING opened this span: the one onset-less open (shape rule 4).

    The one span nothing states at its own start. Every other span is opened by an EVENT — a strum —
    that puts the statement at an instant; a landing successor's members are rings struck under the
    statement BEFORE it, whose travels arrived at the boundary the hand slid to. Nothing happens at
    its start except the previous statement ending and the landed grip standing: mere ring-out opens
    NOTHING, so this field has exactly one cause and is named for it.

    Display keys the opening mark's deferral on the same fact through \ref bracket_place (a
    landing states no ink; the first interior sounding fills it), and the chord name changes here
    once names exist. Published rather than inferred because no reader may substitute a test of its
    own: `last_stated_place` stops answering the moment an interior re-pick states the successor, so
    that proxy is wrong in both directions. Its consumers are the census (the sanctioned instrument)
    and the tests; production display keys on \ref bracket_place. Declared here so that is read
    as deliberate rather than discovered.
    */
    bool landing_opened{false};

    /*!
    \brief The place this span's one OPENING MARK draws at (shape rule 4); absent where it draws
           none.

    Every span an EVENT states — a strum, or a shape hold opening its own span — carries its own
    FRONT here (\ref place), because that is the statement's own extent and the rails run from it.
    An ACCUMULATION is no exception and needs no clause: its front is where its earliest uncovered
    member's statement began — that member's onset, or the landing a glide brought it to — so the
    bracket starts there and the later members' heads arrive under it. A LANDING SUCCESSOR, a hold
    opening at a landing included, carries its first INTERIOR sounding instead: the whole grip slid
    in, so its landing states nothing the predecessor's mark did not, and THE INK FOLLOWS THE
    SOUND. One that never sounds interiorly carries nothing and draws no mark at all — the rails
    and the chord name changing there are its whole statement.

    ONE field with one write rule, which is what makes those cases one law rather than a branch on
    \ref landing_opened: the seed happens where a span opens and the fill happens at the first
    sounding, so the second only ever lands where the first did not.

    Consulted only where a BRACKET actually draws — an ARPEGGIO-classified span (\ref
    chartShapeArrivals). A box-class span states itself with its strums' own boxes, so its anchor is
    never read, and that is the ordinary disposition of a landing successor rather than a corner of
    one.

    The WALK publishes it because the walk is what knows which slots this statement covers. A
    re-scan of the note stream for "the first sounding at or after the span's start" would be that
    grouping question asked a second time, against an extent the closing trim has already
    shortened.
    */
    std::optional<Fraction> bracket_place{};

    /*!
    \brief True where a charter's unnamed shape hold states this span (shape rule 9): only its edges
    come from the hold — it fronts at the hold's start — and every other field is read from its
    notes like a derived span's.
    */
    bool hold_stated{false};

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
    /*! \brief Hand-posture spans, sorted by place. */
    std::vector<ChartShape> shapes;

    /*! \brief The postures the spans index, in first-appearance order and deduplicated. */
    std::vector<ChartPosture> postures;
};

/*!
\brief Derives the hand-posture spans a note stream implies — shape rules 1 to 4, the grip-tenure
law — obeying the charter's shape holds (shape rule 9; \ref design_rulesets_shapes).

One idea: a span is the statement "the hand holds this grip, from here to here", and the walk keeps
the EVIDENCE — what each string is doing — in one per-string table that outlives every span. Every
span it publishes carries its musical close, never a display value: shape rule 6's margin lives
wholly at the projection.

A hold changes only the edges (shape rule 9). Inside an unnamed hold the span the notes found is
the hold's, fronted at its start, and only a hand conflict breaks it; it closes at the first hand
conflict inside, or at the first instant at or after the hold's end where the walk would close a
span standing there, and never short of that end. A hold whose notes found nothing
publishes an empty span over its extent; one whose span closes at its own start holds nothing and
publishes none. Every other edge is a wall: a span standing across it closes there, and no span
dates its front behind it; a hold's end is a wall only while its span reaches it, since the hold's
wall is its published end. A none hold publishes no arpeggio span inside it. Handed no holds, the
walk derives from the notes alone.

A SHIFT SLIDE never reaches the landed span's emission test: its arrival stands at the ring's own
end (\ref arrivesIntoNextHead) and a landing opens a successor only where the ring runs strictly
past it, so what the test governs is a charter's own crowded landing.

\param connections The resolved connections, whose `saved_notes` is the stored stream this reads
       (rings are facts and are never written) and whose `arrives_into` tells a SLIDE-OUT from a
       shift slide's ARRIVAL at every end statement (\ref arrivesIntoNextHead): a slide-out takes
       the finger off the board where an arrival lands it on a stop. Handed over whole rather than
       as two vectors, because they are index-parallel and passing them apart is a mismatch waiting
       to happen. The hold-under table it reads (\ref chartPlantedStops, never read bare: every
       site asks \ref gripStatement) is a derivation OF the connections, so it is asked here rather
       than handed in and no caller can pass a table built from another revision.
\param margins The margin before every head of `connections.saved_notes` (\ref HeadMargins), built
       once per revision because every drawn rule keeps the same distances; the landed span's
       emission test reads it at the closing head.
\param shape_holds The chart's shape holds (\ref Chart::shape_holds), structurally valid
       (\ref validateShapeHolds): ascending and never overlapping.
\param tempo_map Tempo map the connections were resolved against, placing the holds' edges.

\return The spans and their posture table (\ref ChartShapes).
*/
[[nodiscard]] ChartShapes deriveChartShapes(
    const ChartConnections& connections, const HeadMargins& margins,
    std::span<const ShapeHold> shape_holds, const TempoMap& tempo_map);

/*!
\brief One travel of a note's fret channel: where it leaves a stop and where it next LANDS.

Both moments come off one walk of the channel's statements — the onset, then every fret-stating
keyframe — and are offsets from the note's onset. The landing is where a new statement is
established and where a span hands over (shape rule 4).
*/
struct FretTravel
{
    /*! \brief The last offset at which the channel still states the stop it leaves. */
    Fraction departure{};

    /*! \brief The offset at which the channel lands: comes to rest or turns back. */
    Fraction arrival{};

    /*! \brief The stop it lands on. */
    int fret{0};
};

/*!
\brief The whole of what a fret channel says from one offset onward (\ref statedStopFrom): which
stop the finger is on there, and the travel it makes away from it.

One answer rather than two queries, because a caller that had to name the stop itself would be a
second statement of this very fact, free to disagree with the channel.
*/
struct StatedStop
{
    /*!
    \brief The stop the channel states at the queried offset — a fret pressed, the open string, or a
    node touched (\ref ChartStop); empty exactly where it is MID-TRAVEL, since a finger between
    stops is on no stop at all, so a string caught there is a member of nothing.
    */
    std::optional<ChartStop> stop{};

    /*! \brief The travel the channel is on, or is about to make; absent where it never leaves. */
    std::optional<FretTravel> travel{};

    /*!
    \brief The offset at which the stated stop's own statement BEGAN inside the channel: zero where
    the channel never left the onset's stop, and the travel's arrival where a glide landed on it.

    Mid-travel it names where the statement being travelled into will begin, the only honest answer
    while the finger is on no stop at all. The span walk dates a slid string's grip by it, with no
    second record of the landing kept anywhere, exactly as the same re-ask caps that string's
    coverage there.
    */
    Fraction stated_from{};
};

/*!
\brief THE channel reader: the stop a note's fret channel states from an offset onward, and the
travel it makes next — the one authority on where a fret channel lands (shape rule 4).

A travel goes on only while each fret it states lies further in its direction: a fret it glides PAST
is a point on the path, so a continuous multi-fret glide stays one travel to its end. Anything else
LANDS it — a restated fret is a rest, and a fret back the way it came is a TURN, where the listener
hears the chord the glide turned on as the chord landed on even though the finger holds it for no
time. The slide-out (\ref slideOutKeyframe) is no statement about where the finger is: the channel
goes on stating the stop it slides from. An arrival into the next head is read like any landing.
A fretting-hand harmonic states its node, never the fret 0 beneath it (\ref frettingStopAt).

Asked from a landing's own offset, it answers the travel after it, so every landing of a channel is
read by asking again from the last one.

\param note Note whose fret channel is read.
\param from Whole notes from the note's onset.
\param arrives_into_next_head Whether the note's last keyframe arrives into the next head on its
       string (\ref arrivesIntoNextHead) rather than sliding out.

\return The stop stated at \p from, the travel the channel is on or makes next, and where the
        stated stop's statement began.
*/
[[nodiscard]] StatedStop statedStopFrom(
    const ChartNote& note, Fraction from, bool arrives_into_next_head);

/*!
\brief The grip a pull-off source states beneath the fret it sounds, where it states one.

THE ONE AUTHORITY for the hold-under law (shape rule 7), shared by the span derivation and the
importer's let-ring figure walk. A pull-off proves a finger on its landing stop AT THE RELEASE and
nothing about any earlier instant, so a DERIVED landing stop states nothing by itself: a source
states the fret it sounds. The one thing the derivation may say is that a finger ADDED ABOVE a stop
the string is demonstrably already at moves nothing — there the source states that stop, the fret
it sounds is the ornament riding above it, and the grip under it neither breaks nor is rewritten.
The evidence is the caller's, because proof is a relation between this landing stop and the stop
under judgment at one site, never a property of the note.

Never under a harmonic played over a pressed stop (\ref harmonicOverPressedStop), which states
that pressed stop: the node its head prints is measured from it, so it is the grip the figure
needs, and a landing on the finger waiting beneath it is a new statement.

Never where the source's own path sweeps the stop (\ref travelsThroughFret): it rides a held stop
only while its whole path stays above it, since a fret stated at or below that stop would have
sounded the stop instead. A slid source whose landing stop lies inside its travel therefore states
what it sounds, exactly as the same notes plainly picked would.

\param note The source note.
\param planted The stop \p note's pull-off lands on (\ref chartPlantedStops); absent where none.
\param down The stop the string is demonstrably at where \p note speaks — what it still sounds, the
       standing grip's entry; absent where nothing is down.
\return \p down where \p note states it as its grip; empty where \p note states the fret it sounds.
*/
[[nodiscard]] std::optional<ChartStop> gripStatement(
    const ChartNote& note, const std::optional<int>& planted, const std::optional<ChartStop>& down);

/*!
\brief Whether this many stops are enough to found a span.

THE ONE AUTHORITY for the founding law's arithmetic (shape rule 1), shared by the span derivation
and the importer's let-ring fragment donation: stops stated at one instant are a grip at two, and
sound alone founds a span only once three members ring together.

It counts and nothing else. Which stops count is each caller's question — the derivation counts
what a slot states and what still rings from inside the frontier; the importer, which runs before
any ring is decided, counts a figure's largest stroke and its whole membership, and so asks only
whether the figure could EVER found a span.

\param stated_together Stops stated at one instant.
\param sounding_together Every member sounding at that instant, \p stated_together included.
\return True when either count reaches its threshold.
*/
[[nodiscard]] bool foundsSpan(std::size_t stated_together, std::size_t sounding_together);

/*!
\brief Whether a span is an arpeggio rather than a strummed chord box — shape rule 5
(\ref design_rulesets_shapes).

The second half of the same derivation, and here beside the first for that reason: \ref
deriveChartShapes says where the hand goes and how long it stays, this says which of the two marks
the notation draws.

A span's partial soundings are read off the span (\ref ChartShape::sounds_in_parts), because a
fact about WHICH SLOTS the statement covers has to come from the walk that grouped them. Only the
right-hand trigger is derived here, because whether a right-hand onset lands inside the span is a
question about the extent this rule is handed. What it reads off the note stream is positions and
attacks alone, which presentation never touches, so the rule takes the stored stream and \ref
chartResolutions can answer the class before the bracket re-read that consumes it runs.

\param connections The resolved connections: the stored stream, sorted by (position, string), whose
       positions and attacks are all that is read, and its places.
\param shape A span derived from those connections.
\param posture The posture `shape` holds: a span of fewer than three members is never an arpeggio,
       a right-hand onset inside it included.
\return True where the span renders arpeggio-style.
*/
[[nodiscard]] bool chartShapeIsArpeggio(
    const ChartConnections& connections, const ChartShape& shape, const ChartPosture& posture);

/*!
\brief Classifies every shape span as an arpeggio or a strummed chord box
(\ref chartShapeIsArpeggio).

\param connections The resolved connections the spans were derived from.
\param shapes Derived hand-posture spans, sorted by place (\ref ChartResolutions::shapes).
\param postures The posture table `shapes` index (\ref ChartResolutions::postures).
\return One flag per shape, in `shapes` order: true where the span renders arpeggio-style.
*/
[[nodiscard]] std::vector<bool> chartShapeArrivals(
    const ChartConnections& connections, const std::vector<ChartShape>& shapes,
    const std::vector<ChartPosture>& postures);

/*!
\brief Where a shape hold's published extent ends (shape rule 9): the close of the span the walk
publishes for it, fronted at its start. The walk is the one authority on a hold, so this only reads.

A walk that published no span for the hold leaves it nothing, and so answers the start. A none hold
states no span and nothing inside it closes, so its extent is its own.

\param shapes The spans derived with the hold among the chart's holds (\ref deriveChartShapes),
       sorted by place.
\param tempo_map Tempo map the spans were derived against, placing the hold's edges.
\param hold The hold read.
\return The end on the whole-note axis; the start, or before it, where the extent is empty.
*/
[[nodiscard]] Fraction shapeHoldPublishedEnd(
    const std::vector<ChartShape>& shapes, const TempoMap& tempo_map, const ShapeHold& hold);

/*!
\brief Whether a shape hold fits its notes: its published extent (\ref shapeHoldPublishedEnd) is
exactly its stated extent (shape rule 9). The hold stream's structure is \ref validateShapeHolds's.

A none hold always fits.

\param shapes The spans derived with the hold among the chart's holds (\ref deriveChartShapes),
       sorted by place.
\param tempo_map Tempo map the spans were derived against, placing the hold's edges.
\param hold The hold judged.
\return True when the span the hold states runs exactly from the hold's start to its end.
*/
[[nodiscard]] bool shapeHoldFitsNotes(
    const std::vector<ChartShape>& shapes, const TempoMap& tempo_map, const ShapeHold& hold);

} // namespace rock_hero::common::core
