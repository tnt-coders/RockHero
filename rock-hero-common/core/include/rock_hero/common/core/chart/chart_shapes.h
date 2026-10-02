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
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
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
fretting hand is holding under it (\ref deriveChartShapes). A ring struck before the span never
joins its grip and prints in no later bracket: a bracket states the onsets inside its span, and
the tail already says the ring is still sounding. A stop carries no provenance here on purpose:
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
    statement earlier (the tie doctrine) — though no earlier than the coverage frontier at the
    restrike, since a beginning an emitted span already fronted is spent up to that span's close,
    so a restrike of a stop the closed span held dates the next span from where the last one
    ended — and a slid finger's began at the landing. A WHOLE-GRIP STROKE
    standing alone reads none of that — it is a span boundary (the absorption rule), and the box it
    founds dates from the stroke itself, never from a lone note before it that happened to hold one
    of its stops.

    The second half is what keeps spans from overlapping: a ring whose onset lies inside a span
    already emitted is CARRIED, and a carry never backdates. That is one comparison with two
    consequences, and \ref deriveChartShapes spends it once — a carry that dates a span is a
    founding member and BOUNDS it, while one crossing in from covered ground is a tail that joins
    nothing. Landings and death survivors are covered by construction, which is why a successor
    starts exactly where its predecessor ended.
    */
    GridPosition position;

    /*!
    \brief THE MUSICAL CLOSE, in beats from \ref position; always strictly positive.

    **The instant the span's statement actually ended, and nothing about how it is drawn.** Two
    arms, and the distinction is the whole field: where an EVENT closed the span — a contradiction,
    a growth split, any close at a slot — the close is that CLOSING EVENT'S OWN ONSET, because the
    hand demonstrably moved there; where the statement simply RAN OUT, the close is the shape's own
    reach (the continuity law's minimum). Storing the reach unconditionally would claim grip past a
    proven hand move, and storing the closing onset unconditionally would claim grip through proven
    silence, so the close is the EARLIER of the two.

    RULE 12A'S MARGIN IS NOT IN HERE. It is a DISPLAY rule, applied once where the view state is
    built (\ref makeChartViewState) from \ref closing_onset and \ref stated_extent beside this.
    Storing it instead would put display spacing inside every seam: a growth split would close the
    predecessor one display margin before the successor's own start, leaving spans that abut
    musically a margin apart in the data, which every reader of a figure's seams would then have to
    merge back across.

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

    THE INVARIANT ([D2]): **every span is strictly positive**, since every member sounds. A span
    runs as long as every sounding member goes on stating its stop (THE CONTINUITY LAW, in \ref
    deriveChartShapes), and every member's own chain reaches past the span start, so the extent can
    only reach the start itself where no member sounds at all. It is an invariant of the arithmetic
    rather than a case: the close is the earlier of two instants that are both at or after the
    start, so it can only answer the start itself where the reach does.

    TRAVEL does not shorten a span to its own start; the split happens at the LANDING: a chord slide
    keeps the fingers planted, so the rings run continuously and the continuity law itself covers
    the transit — the span states the departing grip, COVERS the glide, and ends where the new grip
    is established, which is exactly where the successor span opens. The two tile with no gap
    between them, and because the close carries no display margin they tile in the stored data too
    and not only in the walk's own reasoning.
    */
    Fraction sustain{};

    /*!
    \brief How far the span's own STATEMENTS reach, in beats from \ref position.

    The last instant an EVENT stated this span — its final strum, or the slot whose holds opened or
    grew it — capped at the musical close, and zero on a span no event ever stated. Rule 12a's
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
    \brief The SOUNDING onset that closed this span, where one did.

    The head rule 12a's display trim keeps its distance from, and the whole of what the trim needs
    beyond the close itself: it sits AT the close where the closing event is what ended the span,
    and AFTER it where the statement had already run out before the event arrived — a span whose
    rings died a full margin early keeps its own length and is not pulled back from a head it never
    reached.

    Empty on the two closes that have no head to clear. A span that simply RAN OUT has no closing
    event at all, and a slot of HELD FINGERS sounds nothing to keep a distance from — there the
    shape being replaced ends exactly where the new one starts, which is also what keeps a landing
    successor tiled onto its predecessor.
    */
    std::optional<GridPosition> closing_onset{};

    /*! \brief Index into the posture table derived alongside (\ref ChartShapes::postures). */
    std::size_t posture{0};

    /*!
    \brief True when some SOUNDING of this span was not the shape WHOLE.

    LAW III's class rule in ONE comparison: a slot striking fewer strings than the shape SOUNDS is
    the shape's members arriving SEPARATELY, so the span is an arpeggio for its whole length — the
    span is one statement, and its class is HOW that statement's members arrive.

    Asked of every slot inside the span that sounds anything, ITS OWN START INCLUDED. The start is
    not a second case: a span whose opening slot strikes fewer strings than its shape sounds is a
    span whose posture CARRIES a string into that start without an onset at it — the arrival rule's
    trigger (a), answered here rather than re-derived one slot earlier and off a different stream. A
    partial restrike, a lone re-pick and a carried start are one fact at three widths, and the walk
    answers all three with the one count.

    A LANDING SUCCESSOR (\ref landing_opened) has no fourth width, and deliberately gets no constant
    `true` saying that nothing struck at a landing means its members arrive separately. A LANDING IS
    NOT A SOUNDING, and neither is a DEATH — nothing is struck at either because the surviving rings
    simply carry on — so there is no sounding of the shape to be partial, and the walk's own guard
    already says it: a slot that sounds nothing is no sounding of the shape, and a boundary has no
    slot at all. Such a constant would be honest only if a successor could never be strummed, which
    rule 11's corollary 2 allows, so it would make a chord sliding into chords arrive an arpeggio at
    every landing. A successor classifies by the ordinary triggers like any other span.

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

    Rings struck before the span never enter this count, however they sound under it: they are no
    members, print in no bracket, and so class nothing.
    */
    bool sounds_in_parts{false};

    /*!
    \brief True when a LANDING opened this span — the one onset-less open the law admits (rule 6).

    The one span nothing states at its own start. Every other span is opened by an EVENT — a strum —
    that puts the statement at an instant; a landing successor's members are rings struck under the
    statement BEFORE it, whose travels arrived at the boundary the hand slid to. Nothing happens at
    its start except the previous statement ending and the landed grip standing: mere ring-out opens
    NOTHING, so this field has exactly one cause and is named for it.

    Display keys the opening mark's deferral on the same fact through \ref bracket_position (a
    landing states no ink; the first interior sounding fills it), and the chord name changes here
    once names exist. Published rather than inferred because no reader may substitute a test of its
    own: `last_stated_beat` stops answering the moment an interior re-pick states the successor, so
    that proxy is wrong in both directions. Its consumers are the census (the sanctioned instrument)
    and the tests; production display keys on \ref bracket_position. Declared here so that is read
    as deliberate rather than discovered.
    */
    bool landing_opened{false};

    /*!
    \brief Where this span's one OPENING MARK draws; absent where it draws none ([D2]).

    Every span an EVENT states — a strum, an authored hold — carries its own FRONT here (\ref
    position), because that is the statement's own extent and the rails run from it. An ACCUMULATION
    is no exception and needs no clause: its front is where its earliest uncovered member's
    statement began — that member's onset, or the landing a glide brought it to — so the bracket
    starts there and the later members' heads arrive under it. A LANDING SUCCESSOR carries its
    first INTERIOR sounding instead: the whole grip slid in, so its landing states nothing the
    predecessor's mark did not, and THE INK FOLLOWS THE SOUND. One that never sounds interiorly
    carries nothing and draws no mark at all — the rails and the chord name changing there are its
    whole statement.

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
    std::optional<GridPosition> bracket_position{};

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
\brief Derives the hand-posture spans a note stream implies — THE GRIP-TENURE LAW
(docs/plans/in-progress/span-derivation-ground-up.md).

One idea: a span is the statement "the hand holds this grip, from here to here", and the machine
keeps the EVIDENCE — what each string is doing — in one per-string table that outlives every span.

WHEN A SPAN EXISTS. A span OPENS at an onset stating a grip: two or more stops struck at one slot
(the statement threshold). A right-hand onset states no grip: it renews its string's sound and
answers only THE TAP'S FLOOR. Sound alone may ACCUMULATE one at three or more overlapping
members — the minimum gates founding by sound alone and nothing else. A LANDED TRAVEL is the one
onset-less open (\ref ChartShape::landing_opened): the grip held through the slide, at least one
finger arrived, two members ringing strictly past the boundary. NOTHING ELSE opens a span —
strings that merely ring on past a break are tails.

WHEN A SPAN RUNS AND ENDS. A span runs until its grip BREAKS: a member quits (any posture member —
its sound out with nothing on its own string renewing it at that instant, either hand's onset
renewing), or a contradiction (a strike naming a different stop on a string the grip states, the
hand audibly holds, or that audibly sounded another stop since the span's front — Law A, read
end-inclusively at the junction instant, and read back to the front at the join). A restatement of
the same grip is the same span CONTINUING; a stop the grip lacks GROWS it in place — growth IS
accumulation, and the quit arm is what guarantees absorption only ever unions grips whose sounds
genuinely overlap. Fingers traveling together with the grip held CARRY the statement; the break
lands where the new grip establishes. The stored close is the breaking event's onset or where the
statement ran out, whichever is earlier — never a display value (rule 12a's margin lives wholly at
the projection).

THE FRONT. One floor — the coverage frontier of every emitted span, and the displacement junction
of every stated string — and the earliest member onset at or after it dates the span. Members
behind the floor state their stops into the posture and date nothing. The frontier survives ONLY
as this dating floor; the reach never reads it.

THE LANDED SPAN'S EMISSION. A landing span is emitted if an event ever stated it, or its tenure
STRICTLY EXCEEDS the notated-distinguishability quantum at the closing head's measure, so the chord
name never flickers for a sliver. A held-but-never-restruck landed span is emitted: it is what
states the chord-name change at the landing. Publication rides the push, which is what keeps that
drop safe. A SHIFT SLIDE reaches that test never: its arrival stands at the ring's own end
(\ref arrivesIntoNextHead) and a landing opens a successor only where the ring runs strictly past
it, so what the test governs is a charter's own crowded landing.

\param connections The resolved connections, whose `saved_notes` is the stored stream this reads
       (rings are facts and are never written) and whose `arrives_into` tells a SLIDE-OUT from a
       shift slide's ARRIVAL at every end statement (\ref arrivesIntoNextHead): a slide-out takes
       the finger off the board where an arrival lands it on a stop. Handed over whole rather than
       as two vectors, because they are index-parallel and passing them apart is a mismatch waiting
       to happen. The hold-under table it reads (\ref chartPlantedStops, never read bare: every
       site asks \ref gripStatement) is a derivation OF the connections, so it is asked here rather
       than handed in and no caller can pass a table built from another revision.
\param tempo_map The beat axis every instant above is measured on.

\return The spans and their posture table (\ref ChartShapes).
*/
[[nodiscard]] ChartShapes deriveChartShapes(
    const ChartConnections& connections, const TempoMap& tempo_map);

/*!
\brief The grip a pull-off source states beneath the fret it sounds, where it states one.

THE ONE AUTHORITY for the hold-under law, shared by the span derivation and the importer's let-ring
figure walk. A pull-off proves a finger on its landing stop AT THE RELEASE and nothing about any
earlier instant, so a DERIVED landing stop states nothing by itself: a source states the fret it
sounds. The one thing the derivation may say is that a finger ADDED ABOVE a stop the string is
demonstrably already at moves nothing — there the source states that stop, the fret it sounds is
the ornament riding above it, and the grip under it neither breaks nor is rewritten. The evidence
is the caller's, because proof is a relation between this landing stop and the stop under judgment
at one site, never a property of the note.

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

THE ONE AUTHORITY for the founding law's arithmetic, shared by the span derivation and the
importer's let-ring fragment donation: stops stated at one instant are a grip at two, and sound
alone founds a span only once three members ring together.

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
\brief Classifies every shape span as an arpeggio or a strummed chord box.

The second half of the same derivation, and here beside the first for that reason: \ref
deriveChartShapes says where the hand goes and how long it stays, this says which of the two marks
the notation draws. Neither is authored, so neither has a rule a document could break.

ONE law decides it — ARPEGGIO iff the shape's members sound SEPARATELY — and the span stays a chord
box only while every sounding of it is the shape whole. THREE triggers, every one of them that same
question asked where a sounding can be incomplete.

(a) A posture string CARRIED into the span's start: still ringing there, with no onset at it. The
strum picks around the held note, so its start was never one full strum. THE STRUM is what makes it
a trigger — a sounding that reached only part of the shape — so a LANDING SUCCESSOR (\ref
ChartShape::landing_opened) is not this trigger at all: a landing is not a sounding, nothing is
struck at one, and the rings carry on. A successor is classified by whatever the two triggers
below find inside it, and a chord sliding into chords is therefore a BOX at both ends, joined by
sliding tails.

(b) A slot INSIDE the span that sounds only PART of the shape — a partial restrike, or a lone
re-pick of one member — which is the members arriving one group at a time.

(c) A picking-hand onset, a tap or a pick slide, sounding anywhere within the span: the fretting
hand holds the shape while the other hand sounds above it, so the chord is sustained through the
tapping rather than strummed.

Any of the three renders the shape as brackets around individual notes instead of one strummed box.

(a) and (b) are ONE comparison, and \ref ChartShape::sounds_in_parts is where it is answered: a
slot striking fewer strings than the shape SOUNDS is its members arriving separately, and asking
exactly that at the span's own start IS (a). Two of the three are therefore read off the span and
only (c) is derived here, which is not an accident — a fact about WHICH SLOTS the statement covers
has to come from the walk that grouped them, while whether a right-hand onset lands inside the span
is a question about the extent this rule is handed.

CLASSIFICATION READS THE STORED STREAM, because the class is a fact about the HANDS: where the
fingers are, and which of them the pick reached. The carry in (a) is the walk's own fold-in, which
asks the stored ring, so a dead string's carry classifies at a span's START exactly as at an
interior slot. E25 does not apply: it is a DISPLAY rule, about what a surface draws of a ring nobody
hears. What (c) reads off
the note stream is positions and attacks alone, which presentation never touches — so the rule
takes the stored stream, and \ref chartResolutions can answer the class before the bracket
re-read that consumes it runs.

"Fewer than two sounds at the span start" is not a trigger but the PRECONDITION of (a): rule 10
needs two MEMBERS to open a span, so a thin start always means a carry, and stating it here would
make it a second answer to a question already answered.

One forward cursor over the sorted notes serves every shape. No backward look is needed — nothing
here reads a posture string's most recent earlier note, which would mean walking back to the first
note in the song whenever a posture string had none.

\param notes Note stream sorted by (position, string); only positions and attacks are read.
\param shapes Hand-posture spans, sorted by position (\ref ChartResolutions::shapes).
\param tempo_map Song tempo map, for the signature-exact span end.
\return One flag per shape, in `shapes` order: true where the span renders arpeggio-style.
*/
[[nodiscard]] std::vector<bool> chartShapeArrivals(
    const std::vector<ChartNote>& notes, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map);

} // namespace rock_hero::common::core
