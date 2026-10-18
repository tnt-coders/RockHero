/*!
\file chart_legato.h
\brief The connection resolver: what a chart's legato claims resolve to, for every consumer.
*/

#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/head_margins.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <span>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Index sentinel for \ref ChartResolutions::predecessors: nothing earlier on that string.

Named once and shared, because the "nearest earlier note on the same string" relation has one
producer and more than one consumer.
*/
inline constexpr std::size_t g_no_chart_predecessor{std::numeric_limits<std::size_t>::max()};

/*!
\brief A note's same-string predecessor as the connection rules read it: the earlier note and the
whole notes from its onset to the note's.

The two travel together because every pairwise rule — the hold test, the adjacency clause, the
arrival — reads both, and a gap means nothing without the note it is measured from. Present or
absent as a whole (\ref ChartConnections::predecessorOf), so no caller invents a gap for a
predecessor that does not exist.
*/
struct LegatoPredecessor
{
    /*! \brief The nearest earlier note on the same string. */
    const ChartNote& note;

    /*! \brief Whole notes from that note's onset to the successor's. */
    Fraction onset_gap;
};

/*!
\brief The motion a note's connection claim justifies, or `Unjustified` when nothing does.

The ONE authority for which way a legato connection runs, and the only place the question is
answered — no direction is ever stored, so there is nothing else it could be read from. Validation
is intra-note only; the relational technique rules (\ref design_rulesets_techniques) that would
otherwise refuse a document live here as clauses instead, because a claim the chart cannot justify
is a claim that plays as a plain pick, not a broken file.

Judged against the FRET AT THE RING'S END — where the predecessor's finger ends, so a glide hands
over its last keyframe — never against predecessor identity. Four things disqualify a predecessor
outright: none exists, it is a scrape (its travel is the pick's position, so no finger waits at its
end), it is a fret-hand harmonic (a touch holds nothing to hand over), or its ring has already
stopped at this onset (\ref predecessorHoldReaches, strict adjacency). A string that stopped
sounding is a released string, which is why shrinking a tail drops the connection its neighbour
claimed and why a claim after a REST resolves to nothing.

A dead predecessor is an ordinary one: its finger is on the stop, and the muted cluck after it is a
hammer or pull like any other. It is bounded by the same one test, reading the same field — a dead
note stores the duration its damped stroke lasts (only the DRAWN tail goes, technique rule 25), so a
chug chained to its restrike connects and a cluck the hand left long before does not. The
alternative — disqualifying the dead note outright — turns every imported muted cluck into a picked
note.

Then the fret at the ring's end picks the direction: above the note is a pull-off, below it a
hammer-on. A pull-off carries no harmonic (it releases onto a plain stopped pitch); a hammer-on
needs somewhere to land, which the direction test already guarantees — that fret is never negative,
so a hammer's note is stopped at fret 1 or above by construction and needs no second test. Equal
frets justify nothing — there is no connection to record, and inventing one would be inventing data.

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
\param predecessor Nearest earlier note on the same string and the gap to it, or nothing when
       there is none.

\return The motion the claim resolves to, or `Unjustified` when nothing justifies one.
*/
[[nodiscard]] LegatoMotion resolveLegato(
    const ChartNote& note, const std::optional<LegatoPredecessor>& predecessor);

/*!
\brief Does \p predecessor's ring END exactly on the next head's onset — one instant carrying
both, whatever the end states?

Clause 2 of \ref arrivesIntoNextHead on its own, and here because two readers need it: that relation
and \ref ChartConnections::end_heads, which the surfaces read for the band two marks at one
x take. Strict equality is the whole of it, so the two cannot come to measure adjacency differently.

\param predecessor Note whose ring's end is in question, with the gap to the next note on its
       string.

\return True when the ring ends precisely where the next head starts.
*/
[[nodiscard]] bool endsOnNextHead(const LegatoPredecessor& predecessor);

/*!
\brief THE SHIFT SLIDE, as a fact the chart PROVES: does \p predecessor's end statement glide INTO
the stop \p successor is struck at?

A fret at a ring's end is one statement and two opposite gestures: the SLIDE-OUT, where pressure
comes off and the pitch slides away toward a fret the hand never sounds, and the ARRIVAL, where the
finger glides into position for a note that is then picked. Nothing stores which, because the
statement's own sentence already says it. FIVE clauses:

1. \p predecessor ends in a statement NAMING A FRET (\ref endStatedFretOrNull): nothing else is a
   gesture to classify.
2. Its ring ends EXACTLY at \p successor's onset (\ref endsOnNextHead): strict adjacency is the
   whole of "one instant".
3. NEITHER is a scrape: a scrape's travel is the PICK's, and its terminal is required at its end.
4. \p successor is not stopped by the PICKING hand (\ref pickingHandStopsString): a fretting hand
   sliding into a fret a different hand then stops is not one gesture. A TAPPED HARMONIC passes by
   construction, that hand holding the stop its node rides.
5. The fret named IS the stop \p successor is struck at, NODE-AWARE (\ref frettingStopAt): a
   same-string head at a DIFFERENT fret is a slide-out that merely abuts, so the test is never
   adjacency alone.

Whether \p successor is re-struck or claims legato is deliberately ABSENT: reading its claim would
give the pair two self-consistent readings, since the connection resolver reads this very answer to
find the predecessor's fret at its ring's end.

ONE PRODUCER, TWO WAYS TO ASK. The connections walk resolves it once per revision into
\ref ChartConnections::arrives_into, which every consumer reads; a caller already HOLDING the pair
(\ref resolveLegato, the `Shift+L` join) asks here rather than deriving the relation twice.

\param predecessor Note whose end statement is in question, with the gap to \p successor.
\param successor The next note on the same string.

\return True when the end statement is an arrival into \p successor.
*/
[[nodiscard]] bool arrivesIntoNextHead(
    const LegatoPredecessor& predecessor, const ChartNote& successor);

/*!
\brief Each note's nearest earlier note on its own string — THE same-string predecessor relation.

One forward walk carrying the most recent note per string, which IS each note's predecessor when it
is reached. The one producer of the relation: \ref chartConnections reads it, and so does the settle
sweep's question (\ref unjustifiedLegatoClaims), which needs nothing else the connections carry. A
note whose string lies outside the chart's lanes has no predecessor and is no one's.

\param notes Note stream sorted by (position, string).

\return Index-parallel to `notes`: each note's predecessor index, or \ref g_no_chart_predecessor.
*/
[[nodiscard]] std::vector<std::size_t> chartPredecessors(const std::vector<ChartNote>& notes);

/*!
\brief The legato claims the chart does not justify — the settle sweep's question, asked alone.

Each `Legato` note is resolved exactly as \ref chartConnections resolves it — \ref resolveLegato on
the saved forms of the note and its predecessor (\ref chartPredecessors) — so the answer is the
connections' `Unjustified` entry for that note by construction. Asked on its own because it runs at
every settle event and almost always finds nothing: it converts only the claims and their
predecessors to the saved form, where the connections would copy the whole stream.

`LeftTap` is never listed: its claim is local, so nothing can withdraw it.

\param notes Note stream sorted by (position, string).
\param tempo_map Song tempo map supplying the meter each claim's gap is measured in.

\return Indices into `notes` of every unjustified `Legato` claim, ascending; empty when the stream
        already satisfies the invariant.
*/
[[nodiscard]] std::vector<std::size_t> unjustifiedLegatoClaims(
    const std::vector<ChartNote>& notes, const TempoMap& tempo_map);

/*!
\brief The saved note stream and every connection claim it justifies.

What the connection rules need, and nothing more. \ref resolveLegato reads a predecessor's stored
position, ring, fret at the ring's end and attack class, so the saved stream plus one forward walk
answers every claim in the chart; nothing presentation derives — the ink ends, the hand-posture
spans, the holds — can change a verdict here.

That is why this is asked on its own rather than through \ref ChartResolutions. The editor's
legato verb wants only these vectors; deriving a whole song's ink ends, spans and holds to read one
flag would be a full pass over the chart thrown away on every keystroke. The settle sweep, which
runs at every caret move, selection change, seek and playback start, asks narrower still
(\ref unjustifiedLegatoClaims).

Every vector but \ref string_notes is index-parallel to the note stream it was built from.
*/
struct ChartConnections
{
    /*!
    \brief Each note in its saved form (\ref savedChartNote): in-memory latents stripped.

    What the RULES judge, what the presentation is derived from, and what every surface draws. Its
    `sustain` is the actual duration the string rings, which is what the connection resolver reads;
    a surface draws that ring only as far as its ink end (\ref ChartResolutions::ink_end).
    */
    std::vector<ChartNote> saved_notes;

    /*!
    \brief Each note's onset on the whole-note axis (\ref chartNotePlaces), computed once here.

    THE coordinate every pass over this revision reasons in: a same-string gap, a ring's end and a
    keyframe's instant are all exact sums and differences of these places, so no reader re-addresses
    a pair through the tempo map.
    */
    std::vector<Fraction> places;

    /*!
    \brief What each note's connection claim resolves to.

    `Unjustified` for every note that makes no claim, exactly as for a claim nothing justifies: both
    draw and score as plain picks, so display code can read this entry alone for notes in the
    \ref legatoClaimable family and needs no second test.
    */
    std::vector<LegatoMotion> legato;

    /*!
    \brief Each note's nearest earlier note on its own string, or \ref g_no_chart_predecessor.

    The relation \ref chartPredecessors produces and the walk reads to answer \ref legato, handed
    out rather than kept private: the `H` toggle asks the resolver its own hypothetical per
    selected note and needs the same predecessor, and re-deriving it there would be both a
    restatement of this rule and a backward scan per selected note.
    */
    std::vector<std::size_t> predecessors;

    /*!
    \brief Note \p index's same-string predecessor and the gap to it, or nothing when \p index is
    the first note on its string: the one place that pair is assembled from \ref predecessors and
    \ref places.

    \param index Index into the note stream.

    \return The predecessor, referring into \ref saved_notes, or nothing.
    */
    [[nodiscard]] std::optional<LegatoPredecessor> predecessorOf(std::size_t index) const;

    /*!
    \brief Every note on each string, of both hands, ascending by onset: `string_notes[s]` lists
    the indices of the notes on the zero-based string `s` (\ref chartStringIndex).

    The one per-string index of the revision, sized \ref g_max_chart_strings and not index-parallel
    to the notes. What releases a held stop is a later note on its string (\ref
    stringReleasesStopAt), so every reader of that question searches here rather than building
    its own.
    */
    std::vector<std::vector<std::size_t>> string_notes;

    /*!
    \brief True where this note's ring HANDS ITS STRING OVER: the next strike on it claims a
    connection and reaches back to take the sound.

    A TRANSFER rather than a slide-out — the finger stays down and the next strike takes the sound
    off it — which is why it lives beside the relation that answers it rather than inside the
    span-scoped display rules that read it (\ref chartPresentation and \ref chartNoteLengths:
    furniture states GRIP, and a handover is sound moving from one strike to the next, which no
    furniture on the lane states — so the ring keeps its whole ribbon, its statement finishing at
    the takeover, and its head pins only until then).

    Read off the STORED claim, never the resolved direction: \ref legatoClaimed plus
    \ref predecessorHoldReaches, the resolver's own strict-adjacency test called rather than
    restated. An equal-fret tie claim resolves \ref LegatoMotion::Unjustified and still hands the
    string over, so reading \ref legato here would silently change behaviour the day the tie lands.

    Written from the SUCCESSOR onto its predecessor, because that is where the chart states it, and
    a note has at most one claiming successor on its string — each note displaces every later note's
    predecessor.
    */
    std::vector<bool> hands_over;

    /*!
    \brief True where this note's end statement ARRIVES into the next head on its string rather
    than sliding out from it — the shift slide (\ref arrivesIntoNextHead states the clauses).

    THE ONE PRODUCER of the relation, filled in the same forward walk that answers \ref hands_over
    and for the same reason: the same-string pair this needs is exactly the one that walk
    establishes, and two producers of one relation is how a chart comes to be described two ways.
    Every reader of "is this end statement a slide-out" takes its answer from here
    (\ref slideOutKeyframe), so a slide-out and an arrival cannot be told apart two different ways.

    Written from the SUCCESSOR onto its predecessor, because the relation is about the pair and the
    walk reaches the successor second; a note has at most one same-string successor, so the entry is
    written at most once. False where nothing follows on the string: nothing is there to arrive
    into.
    */
    std::vector<bool> arrives_into;

    /*!
    \brief Where this note's ring ENDS exactly on the next head of its own string — one instant
    carrying both, whatever the end states — that head's index in the stream.

    \ref endsOnNextHead, which is what an ARRIVAL and an abutting SLIDE-OUT share: the surfaces need
    it because two marks then stand at one x, and the band each takes is decided by the pair rather
    than by either note (\ref NoteViewState::end_head). Filled in this walk beside \ref hands_over
    and for the same reason — the same-string pair it needs is the one the walk establishes.

    Written from the SUCCESSOR onto its predecessor, like the two relations above; empty where
    nothing follows on the string or the ring stops short of it.
    */
    std::vector<std::optional<std::size_t>> end_heads;
};

/*!
\brief Resolves a whole note stream's connections: the saved form, the motions, the predecessors.

One forward walk over the same-string predecessors (\ref chartPredecessors), so the connection
motions cost one pass over the stream rather than a backward search per note.

\param notes Note stream sorted by (position, string).
\param tempo_map Song tempo map supplying the meter every place is measured in.

\return The connections; every vector but the per-string index is index-parallel to `notes`.
*/
[[nodiscard]] ChartConnections chartConnections(
    const std::vector<ChartNote>& notes, const TempoMap& tempo_map);

/*!
\brief Per note, the stop its PULL-OFF lands on; absent where none is.

THE HOLD-UNDER DERIVATION: you cannot pull off onto a fret unless a finger is waiting on it AT THE
RELEASE, so a note that is pulled off FROM names a second stop beneath the one it sounds, WHICHEVER
hand made its onset. The chart writes that stop nowhere, because the notation already states it, in
the pull-off itself. WHEN the finger arrived there is a fact no chart carries, so this table is a
derivation and never an assertion about the source's whole ring: what it may state to a grip is
decided by \ref gripStatement, against evidence, and by nothing else.

The derivation is exactly the connection this walk has already resolved: a note's same-string
successor claims legato, that claim resolves to \ref LegatoMotion::Pull against this very onset
(which carries the strict-adjacency test with it — a released string hands nothing over), and the
successor states the stop the string falls to. EVERY fret derives alike, the open string included: a
pull onto the open string plants 0 — the stop beneath the source is the open string, always waiting,
no finger needed. Only a destination the chart never defines derives nothing.

BOUND BY THE RELEASE ALONE: the finger it proves is on the string at the
release, whatever path the source travelled first, so a slid source plants its stop exactly as an
unslid one does; the Pull resolution already puts that stop strictly below the fret released from.
The onset's TRAVELED RANGE (\ref travelsThroughFret) bounds THE RIDE instead (\ref gripStatement).

WHO READS IT: the seam machinery — the span machine's verdicts and dating (\ref deriveChartShapes)
and the let-ring cut law's figure seams (`letRingFigureEnds` in the importer) — and the complete
held table (\ref chartHeldStops).

\param connections The resolved connections, whose `saved_notes`, `legato` and `predecessors` are
                   the whole of what the derivation reads.

\return Per note, the stop its pull-off states is planted beneath it, or nothing; index-parallel
        to `connections.saved_notes`.
*/
[[nodiscard]] std::vector<std::optional<int>> chartPlantedStops(
    const ChartConnections& connections);

/*!
\brief THE HELD TABLE: the fret the fretting hand presses under every head that sounds elsewhere —
shape rule 8 (\ref design_rulesets_shapes).

A held stop is DERIVED, never typed. Every note a pull-off plants a stop beneath
(\ref chartPlantedStops) holds that stop, whichever hand made the onset. A note the PICKING HAND
STOPS THE STRING FOR (\ref pickingHandStopsString) — a plain tap or a pick slide — with no plant
holds THE DEFAULT: the fret the covering span's posture holds on its own string (coverage is
half-open, \ref SpanCover). An OPEN string is no finger, so a stop of 0 — a plant onto the open
string, or a default with no grip beneath it — holds nothing (\ref pressedFret), and every held stop
is a pressed fret. Every other note holds no second stop.

A POST-SHAPES FACT, which is why it is a table of its own: the default READS the derived postures,
so this runs AFTER \ref deriveChartShapes and feeds nothing that runs before it. LIVE-DERIVED: an
edit that reflows the spans re-derives every default.

\param connections The resolved connections the spans were derived from.
\param shapes The spans and postures derived from them (\ref deriveChartShapes).

\return Per note, the fret the fretting hand presses under it, or nothing where no finger is down
        beneath it; index-parallel to `connections.saved_notes`.
*/
[[nodiscard]] std::vector<std::optional<int>> chartHeldStops(
    const ChartConnections& connections, const ChartShapes& shapes);

/*!
\brief The notes layer of a chart revision: everything derived from the notes, resolved once for
every consumer.

A function of the notes, the shape holds, the capo and the tempo map alone — the holds being what
the spans obey (shape rule 9), and the authored positions the hand layer's input
(\ref FretHandResolutions), never this one's. The per-note facts each surface needs
and none may restate: the connections the saved stream justifies, where each note's ink stops — how
far every surface DRAWS the stored note and the scorer will judge it — and how long each note is
held — plus the hand-posture spans the notes imply and what they demand of the fretting hand, which
are not per-note but are derived from the same stream.

What is added here over \ref ChartConnections travels together because it is computed together —
the holds need the saved stream, the ink ends, the tail law's verdict AND the spans to be answered
at all — and because computing them separately would let the tab lane, the highway, the gameplay
build, the coverage gate and the reader disagree about the same chart. The connections are carried
rather than restated, so a consumer of the whole picture still reads them from one place.

Every per-note vector is index-parallel to the note stream it was built from. Consumed once per
chart revision, never per frame.
*/
struct ChartResolutions
{
    /*! \brief The saved stream and the connections it justifies (\ref chartConnections). */
    ChartConnections connections;

    /*!
    \brief The shape holds the spans obeyed (\ref deriveChartShapes), recorded beside the saved
    stream the connections record, so a derivation can be checked against the chart it claims.
    */
    std::vector<ShapeHold> shape_holds;

    /*!
    \brief Where the minimum sustain distance begins before each head of the saved stream.

    Derived first, because the spans, the ink ends and the drawn extents all keep that distance,
    and carried for the readers past this pass: the drawn span extent and each note's
    \ref NoteViewState::margin_start_seconds.
    */
    HeadMargins margins;

    /*!
    \brief What the notes demand of the fretting hand (\ref fretHandDemands): the stops it holds,
    the instants it covers, and the capo the layer was derived under. Not index-parallel to the
    notes.

    Derived after the spans — notes, then grips, then positions — as the one statement of what the
    notes demand of the hand, read by the hand layer (\ref fretHandResolutions), the coverage gate
    (\ref uncoveringFretHandPositions) and the editor's redundancy walk. The capo is carried here
    so every reader asks the layer instead of agreeing with it by hand.
    */
    FretHandDemands hand_demands;

    /*!
    \brief Where each note's ink stops, as a note-relative offset: \ref ChartPresentation::ink_end,
    carried here. Same order and size as \ref ChartConnections::saved_notes.
    */
    std::vector<Fraction> ink_end;

    /*!
    \brief Where each tail RESTS, or nothing where it never does:
    \ref ChartPresentation::rested_from, carried here beside \ref ink_end.
    */
    std::vector<std::optional<Fraction>> rested_from;

    /*!
    \brief The hand-posture spans the notes imply (\ref deriveChartShapes).

    Not stored anywhere: a span is a statement about the notes under it, so it is derived here from
    the two streams above and read from here by everything that draws a chord box or an arpeggio
    bracket. \ref shapes indexes \ref postures.
    */
    std::vector<ChartShape> shapes;

    /*! \brief The posture table \ref shapes indexes, in first-appearance order. */
    std::vector<ChartPosture> postures;

    /*!
    \brief Each span's CLASS (\ref chartShapeArrivals): true where its members arrive SEPARATELY.

    Span-parallel to \ref shapes. Derived here rather than at each surface because both surfaces
    draw it and one chart revision should answer the class once. NO TAIL RULE READS IT: the tail
    law is class-blind by construction — its one comparison asks only whether the note's own span
    covers the ring, and coverage says nothing about how the span's members arrived.
    */
    std::vector<bool> arrivals;

    /*!
    \brief Each note's held fret (\ref chartHeldStops); absent where the note holds no second stop.

    What \ref NoteViewState::held_fret carries, copied straight across. Read out of the postures
    above, never into them.
    */
    std::vector<std::optional<int>> held_stops;

    /*!
    \brief Each note's length in whole notes: how long the 3D board shows it as still in play.

    For a note inside a span the length follows from the span, which holds the shape. ONE RULE,
    and \ref chartNoteLengths states it — including which populations stand outside it. Nothing
    more is restated here, because a summary at the field is a second place to keep true.
    */
    std::vector<Fraction> note_lengths;
};

/*!
\brief Resolves a chart's notes layer once: connections, spans, hand demands, ink ends, note
lengths.

A function of the notes, the shape holds, the capo and the tempo map. The connections come from
\ref chartConnections, so the walk that answers them is stated once for both the callers that want
the whole picture and the callers that want a claim. The spans, the held stops and the hand demands
come from the same stream rather than from a caller, so there is no second posture input a caller
could forget to hand over.

The editor keeps one per revision in its controller memo, keyed by the notes it was derived from,
and a commit that seeds that memo asserts it was derived from the very notes and holds it landed
(\ref ChartResolutions::shape_holds): that memo
and that assertion are the coherence rule between a derivation and its chart.

\param notes Note stream to resolve, sorted by (position, string) and valid.
\param shape_holds The chart's shape holds (\ref Chart::shape_holds), structurally valid; the spans
       obey them (\ref deriveChartShapes).
\param capo The tuning's capo fret; 0 for none.
\param tempo_map Song tempo map supplying the meter every place is measured in.

\return The resolutions; every per-note vector is index-parallel to `notes`.
*/
[[nodiscard]] ChartResolutions chartResolutions(
    const std::vector<ChartNote>& notes, std::span<const ShapeHold> shape_holds, int capo,
    const TempoMap& tempo_map);

/*! \brief Shape holds in their normal form and what reaching it took (\ref normalizeShapeHolds). */
struct ShapeHoldNormalization
{
    /*! \brief The holds in their normal form, ascending and never overlapping. */
    std::vector<ShapeHold> shape_holds;

    /*! \brief One conversion per rewrite, each naming its hold as it now stands or stood. */
    std::vector<ChartConversion> conversions;
};

/*!
\brief Brings a chart's shape holds to their normal form (shape rule 9): THE one hold normalizer,
run after every edit that is not a hold verb — a note edit (the editor's plan gate), a tempo or
meter edit (the timeline funnel) and a load (\ref normalizeChart).

The walk is the one authority on a hold, so the normal form is what it publishes. A hold the walk
leaves nothing — one a re-address rounded onto no length, or whose span closes at its start — is
dropped, each judged on a derivation of the holds that remain, since a dropped hold's start was a
wall its neighbour's close may have stopped at. Then every end is written back where the walk
published it (\ref shapeHoldPublishedEnd): one rule that is both the crop, back to a hand conflict
an edit put inside, and the extension, on past an end where nothing closed. Neither can make two
holds overlap, since every span closes at the next hold's start. Each moved end is one the walk
closed a span at, so normalizing the result again changes nothing. A none hold always fits, so only
its length can drop it.

\param notes Note stream the holds are judged against, sorted by (position, string) and valid.
\param shape_holds The holds: ascending and never overlapping, possibly with a hold of no length.
\param tempo_map Song tempo map placing the notes and the holds.
\return The holds in their normal form and one conversion per rewrite; no conversions where the
        holds were already normal.
*/
[[nodiscard]] ShapeHoldNormalization normalizeShapeHolds(
    const std::vector<ChartNote>& notes, std::vector<ShapeHold> shape_holds,
    const TempoMap& tempo_map);

/*! \brief A notes layer derived under normalized shape holds (\ref normalizedChartResolutions). */
struct NormalizedChartResolutions
{
    /*! \brief The notes layer, which records the holds in their normal form. */
    ChartResolutions resolved;

    /*! \brief What normalizing the holds took (\ref ShapeHoldNormalization::conversions). */
    std::vector<ChartConversion> conversions;
};

/*!
\brief The notes layer under shape holds brought to their normal form first (\ref
normalizeShapeHolds), for the caller that judges the result whole: the derivation the normalizer
judged with is the one the layer is completed from, so nothing is derived twice.

\param notes Note stream to resolve, sorted by (position, string) and valid.
\param shape_holds The holds, as \ref normalizeShapeHolds takes them.
\param capo The tuning's capo fret; 0 for none.
\param tempo_map Song tempo map supplying the meter every place is measured in.
\return The layer, whose \ref ChartResolutions::shape_holds are the normalized holds, and the
        rewrites.
*/
[[nodiscard]] NormalizedChartResolutions normalizedChartResolutions(
    const std::vector<ChartNote>& notes, std::vector<ShapeHold> shape_holds, int capo,
    const TempoMap& tempo_map);

/*!
\brief Flattens the named legato claims to plain picks — THE statement of what a rejected claim
becomes.

The one place a withdrawn connection is written back: the sweep below applies it to the claims
\ref unjustifiedLegatoClaims names, and the editor's settle plan applies it to its own copy of the
stream, so the two can never come to flatten a claim differently. Only the attack changes — a pick
keeps the note's every other field — which is what lets one pass suffice (see
\ref sweepUnjustifiedLegato).

\param notes Note stream holding the claims; flattened in place.
\param claims Indices into `notes` of `Legato` claims, ascending.

\return One \ref ChartRepair::UnjustifiedLegato conversion per flattened claim, in the order given,
        each naming the claim's position and string.
*/
std::vector<ChartConversion> flattenLegatoClaims(
    std::vector<ChartNote>& notes, std::span<const std::size_t> claims);

/*!
\brief Flattens every legato claim the chart does not justify to a plain pick — the settle sweep.

The one relational mutation in the system, and stateless: it judges only the stream it is handed, so
there is no window state, no flagged notes, and no proofs to keep. The editor runs it at every
settle event, the chart normalizer (\ref normalizeChart) as its last stage on every load and
import, and the document writer before emitting — which is what makes the invariant `Unjustified`
cannot survive a settle or reach a file hold everywhere at once instead of per call site.

A `LeftTap` is never touched: its claim is local, so nothing can withdraw it. Which claims flatten
is \ref unjustifiedLegatoClaims's answer, and \ref flattenLegatoClaims applies it.

One pass is enough, and that is a property of the resolver rather than an assumption: resolution
reads a predecessor's fret at the ring's end, node, attack class, position and ring, and flattening
`Legato` to `Pick` changes none of them (a scrape is never a claim, so no flatten touches one), so
no flatten can create or destroy another note's justification.

\param notes Note stream sorted by (position, string); flattened in place.
\param tempo_map Song tempo map supplying the meter every place is measured in.

\return One \ref ChartRepair::UnjustifiedLegato conversion per flattened claim, in note order, each
        naming the claim's position and string; empty when the stream already satisfied the
        invariant, which is what callers test to know whether it changed.
*/
[[nodiscard]] std::vector<ChartConversion> sweepUnjustifiedLegato(
    std::vector<ChartNote>& notes, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
