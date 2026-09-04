/*!
\file chart_legato.h
\brief The connection resolver: what a chart's legato claims resolve to, for every consumer.
*/

#pragma once

#include <cstddef>
#include <limits>
#include <optional>
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
last keyframe — never against predecessor identity. Four things disqualify a predecessor outright:
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

    /*!
    \brief True where this note's ring HANDS ITS STRING OVER: the next strike on it claims a
    connection and reaches back to take the sound.

    A TRANSFER rather than a release — the finger stays down and the next strike takes the sound off
    it — which is why it lives beside the relation that answers it rather than inside the one
    display rule that reads it (the tail law, \ref presentedChartNotes: furniture states GRIP, and a
    handover is sound moving from one strike to the next, which no furniture on the lane states, so
    the ring keeps its ribbon).

    Read off the STORED claim, never the resolved direction: \ref legatoClaimed plus
    \ref predecessorHoldReaches, the resolver's own strict-adjacency test called rather than
    restated. An equal-fret tie claim resolves \ref LegatoMotion::Unjustified and still hands the
    string over, so reading \ref legato here would silently change behaviour the day the tie lands.

    Written from the SUCCESSOR onto its predecessor, because that is where the chart states it, and
    a note has at most one claiming successor on its string — a sounding one displaces every later
    note's predecessor, and a silent hold claims nothing.
    */
    std::vector<bool> hands_over;
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
\brief Per note, the stop a PULL-OFF states its predecessor was holding; absent where none does.

THE DERIVATION ALONE (user ruling 2026-08-31, DERIVED HELD), separated from the fold that lays it
over the stored field because two questions read it and only one of them is "what is the stop". The
other is **who states it**: where an entry here is present the NOTATION owns that stop, so the
stored \ref ChartNote::held beside it is residue the writer must not emit
(\ref sweepDerivedHeldStops) and an authoring verb must not write (the editor refuses the held
channel there). Answering both off one function is what keeps "the derivation owns this" from being
spelled once as a value comparison and once as a rule.

The derivation is exactly the connection this walk has already resolved: a note's same-string
successor claims legato, that claim resolves to \ref LegatoMotion::Pull against this very onset
(which carries the strict-adjacency test with it — a released string hands nothing over), and the
successor stops the string at a real fret. You cannot pull off onto a fret unless a finger was
already waiting on it, so the connection IS the statement that the hand was holding that stop. A
pull onto an OPEN string derives nothing, because fret zero asserts no finger at all; and only a
right-hand onset can carry a held stop, so no other note takes a derived one.

Bounded by the onset's own TRAVELED RANGE, through the same \ref travelsThroughFret an authored
`held` is refused by: the planted finger is on the string for the whole of the onset's path, so a
stop the picking hand starts on, ends on or sweeps through is not one anything could have been
waiting on. One predicate for the rule and the derivation alike, so no resolution here can state a
stop the document would refuse.

\param connections The resolved connections, whose `saved_notes`, `legato` and `predecessors` are
                   the whole of what the derivation reads.

\return Per note, the stop a pull-off states it holds, or nothing where none does; index-parallel
        to `connections.saved_notes`.
*/
[[nodiscard]] std::vector<std::optional<int>> chartDerivedStops(
    const ChartConnections& connections);

/*!
\brief Each note's RESOLVED claimed stop — the one read of what the fretting hand states at a slot.

DERIVED HELD (user ruling 2026-08-31): a right-hand onset's held stop is DERIVED wherever a
PULL-OFF states it, and the stored field is authoritative only where no such evidence exists. You
cannot pull off onto a fret unless a finger was already waiting on it, so the connection the chart
already records IS the statement that the hand was holding that stop under the tap — an authored
`held` beside it would be the same fact written a second time, free to disagree.

The derivation is exactly the connection this walk has already resolved: the note's same-string
successor claims legato, that claim resolves to \ref LegatoMotion::Pull against this very onset
(which carries the strict-adjacency test with it — a released string hands nothing over), and the
successor stops the string at a real fret. A pull onto an OPEN string derives nothing, because fret
zero asserts no finger at all; only a right-hand onset can carry a held stop, so no other note
takes a derived one; and a stop inside the onset's own traveled range is refused exactly as an
authored one is (\ref chartDerivedStops).

Every other entry is the note's own stored claim (\ref claimedStop), unchanged: a
\ref NoteAttack::None hold IS its stop, and a plain onset claims nothing beyond the fret it sounds.

THE SINGLE READER AUTHORITY. Every consumer — the span derivation (\ref deriveChartShapes), the
projection's satellite digit, the editor's verbs — reads this and never \ref ChartNote::held, which
is what keeps a derived stop and an authored one the same kind of statement everywhere. Answered
off \ref ChartConnections rather than off a stream, so the same-string relation is READ from the one
walk that establishes it instead of being spelled a second time here.

\param connections The resolved connections, whose `saved_notes`, `legato` and `predecessors` are
                   the whole of what the derivation reads.

\return Per note, the stop it claims, or nothing where it claims none; index-parallel to
        `connections.saved_notes`.
*/
[[nodiscard]] std::vector<std::optional<int>> chartClaimedStops(
    const ChartConnections& connections);

/*!
\brief THE COMPLETE HELD TABLE: the stop the fretting hand holds under every right-hand onset.

THE DEFAULT HELD FACT (user ruling 2026-09-02). A tap says nothing about the other hand, so the
question "what is under this tap" always has an answer — and where the chart states none, the
answer is a FACT of the tap rather than a blank: the hand is holding whatever grip it is holding,
and releasing the tap lands on it. Absent only where the question does not arise, which is every
note that is not a right-hand onset: a fretting-hand onset IS the hand, and a silently-held stop
IS its own fret (\ref claimedStop), so neither has a second stop under it.

THREE TIERS, in precedence order, and the third is what this table adds over \ref chartClaimedStops:

- an AUTHORED held stop, which the charter typed;
- the PULL-OFF DERIVATION, which supersedes it (user ruling 2026-08-31, DERIVED HELD) — both of
  these arrive together as the resolved claim, already folded in that order;
- THE DEFAULT: the fret the COVERING SPAN'S POSTURE holds on the tap's own string, or 0 — the open
  string, nothing held — where no span covers the tap or the posture states nothing there.

A POST-SHAPES FACT, which is why it is a table of its own rather than another arm of the claim
fold. The default READS the derived posture, and the postures are derived from the claims: folding
it into \ref chartClaimedStops would make the spans an input to the very default they produce, and
a tap's transparency to the grouping — taps are the tapping hand, so they neither found postures nor
close them — would be gone with it. So this runs AFTER \ref deriveChartShapes and feeds nothing
that runs before it.

LIVE-DERIVED, and that falls out of being derived at all: an edit that reflows the spans around a
tap re-derives its default from the span now covering it, because there is no stored value anywhere
to go stale.

WHO states the stop is a separate question this table deliberately does not answer:
\ref chartDerivedStops says where the NOTATION owns one, and a default is owned by nobody — which
is exactly what makes a default's satellite the one an authoring verb may type a real held stop
into, where a derived one refuses.

\param notes Note stream the resolutions were built from (\ref ChartConnections::saved_notes).
\param claimed_stops The resolved claims (\ref chartClaimedStops), index-parallel to `notes`.
\param shapes The spans and postures derived against those claims (\ref deriveChartShapes).
\param tempo_map Song tempo map supplying the beat axis each span's extent is advanced along.

\return Per note, the stop the fretting hand holds under it, or nothing where the note is not a
        right-hand onset; index-parallel to `notes`.
*/
[[nodiscard]] std::vector<std::optional<int>> chartHeldStops(
    const std::vector<ChartNote>& notes, const std::vector<std::optional<int>>& claimed_stops,
    const ChartShapes& shapes, const TempoMap& tempo_map);

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
    \brief Each note's RESOLVED claimed stop (\ref chartClaimedStops).

    Carried here for the reason the connections are: a right-hand onset's held stop is DERIVED
    where a pull-off states it, so the resolution is a fact about the note's NEIGHBOUR, and every
    surface reading \ref ChartNote::held for itself would be reading the raw field the derivation
    supersedes. The spans below were derived against exactly this vector.
    */
    std::vector<std::optional<int>> claimed_stops;

    /*!
    \brief Each note's stop the NOTATION states (\ref chartDerivedStops), or nothing where none is.

    The other half of what the derivation answers, and the half \ref claimed_stops cannot be asked
    for: WHO states the stop. Where an entry here is present the pull-off owns that stop, so the
    stored field beside it is residue and the value is READ-ONLY — the verbs refuse to retype it,
    and the projection shows its face only while the note's truth is revealed
    (\ref StopMarkFace::Revealed), because the notation already prints that fret. Carried rather
    than re-derived by each consumer for the reason every vector here is: two readers asking the
    same walk twice is how a chart comes to be described two ways.

    Index-parallel to \ref ChartConnections::saved_notes, like every vector here. A present entry
    always equals the resolved claim beside it, since the derivation SUPERSEDES the stored field.
    */
    std::vector<std::optional<int>> derived_stops;

    /*!
    \brief Each note as it is DRAWN and scored (\ref presentedChartNotes).

    The tail rules applied to the saved stream: what both painters, hit testing, and the future
    scorer read (\ref NoteViewState is this form resolved to seconds). Same order and size as
    \ref ChartConnections::saved_notes; only tails and the payload riding them differ.

    ALL the tail rules, the span-scoped one included: \ref presentedChartNotes is handed the spans
    and owns every decision about what a tail draws, so there is one pass and no ordering contract
    between two of them.
    */
    std::vector<ChartNote> presented_notes;

    /*!
    \brief True where a note's OWN SPAN covers its whole ring, so its ribbon is not drawn.

    THE TAIL LAW'S published verdict (\ref presentedChartNotes): span furniture may HIDE a tail,
    never shorten one. The bit says WHERE a ribbon rests — the span accounts for a ring that is
    really sounding, so the 3D board suppresses it at distance and reveals it near the hit line,
    while the 2D lane draws it always (the execution-form amendment) — and it is what the hold
    extension keys on, so a hidden ribbon's return never re-released the pins.

    False for every tail rules 3 and 4 emptied, by construction rather than by a test: the law runs
    LAST and skips a tail that is already empty, so a staccato member and a dead chug enter this
    set never.
    */
    std::vector<bool> hidden;

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
    \brief Per note, the \ref shapes entry a silently-held stop joined; absent for every other note.

    Index-parallel to `notes` like everything else here (\ref ChartShapes::claim_shapes). A
    silent hold has no head of its own, so its face IS that span's posture bracket, wherever the
    mark draws: this is what places it, and what makes an unresolved hold draw — and therefore
    hit-test — nowhere.
    */
    std::vector<std::optional<std::size_t>> claim_shapes;

    /*!
    \brief Each span's CLASS (\ref chartShapeArrivals): true where its members arrive SEPARATELY.

    Span-parallel to \ref shapes. Derived here rather than at each surface because both surfaces
    draw it and one chart revision should answer the class once. NO TAIL RULE READS IT: the tail
    law is class-blind by construction — its one comparison asks only whether the note's own span
    covers the ring, and coverage says nothing about how the span's members arrived.
    */
    std::vector<bool> arrivals;

    /*!
    \brief Each right-hand onset's COMPLETE held stop (\ref chartHeldStops); absent elsewhere.

    What \ref NoteViewState::held carries, copied straight across: the authored stop, the one a
    pull-off derives over it, or — where the chart states neither — THE DEFAULT FACT of the tap,
    the grip the covering span holds on its string (user ruling 2026-09-02).

    Beside \ref claimed_stops rather than replacing it, because the two answer different questions
    and only one of them may reach the spans. A CLAIM is what the fretting hand STATES at a slot,
    and the postures are built from those; the default is what the hand HAPPENS to be holding under
    a tap that states nothing, read back OUT of the postures the claims produced. Folding the
    default into the claims would make every bare tap a member of the shape above it and move spans
    corpus-wide — the circularity the tier separation exists to prevent.
    */
    std::vector<std::optional<int>> held_stops;

    /*!
    \brief Each note's held length in beats (\ref chartHolds): how long the hand stays down.

    A HIDDEN member holds its own stored ring, which is deliberately NEUTRAL to the undecided
    scoring question (\ref hidden says why).
    */
    std::vector<Fraction> holds;
};

/*!
\brief Resolves a whole note stream once: connections, presented form, spans, holds.

The connections come from \ref chartConnections, so the walk that answers them is stated once for
both the callers that want the whole picture and the callers that want a claim.

The spans come from the same stream rather than from a caller, which is what makes them impossible
to disagree with it: a caller holding a stale span list has nowhere to pass it. The silently-held
members ride in that one stream too (\ref NoteAttack::None), so there is no second posture input a
caller could forget to hand over.

\param notes Note stream sorted by (position, string).
\param tempo_map Song tempo map supplying the beat axis.

\return The resolutions; every per-note vector is index-parallel to `notes`.
*/
[[nodiscard]] ChartResolutions chartResolutions(
    const std::vector<ChartNote>& notes, const TempoMap& tempo_map);

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

/*!
\brief Clears every claimed stop the chart's shapes leave stating nothing.

The legato sweep's sibling, and here beside it for the same reason: it is relational and stateless,
judging only the stream it is handed. One law over both shapes a claim can take (\ref claimedStop):
a claim that reaches no span (\ref ChartShapes::claim_shapes absent) changes no posture, draws
nowhere and hit-tests nowhere, so keeping it saves a statement the charter can neither see nor find.

Three ways to state nothing, one test for all of them, because the derivation answers all three the
same way: joining no span at all (a lone member, or a shape the hand alone stated that nothing ever
justified), landing past the end of the span it joined, and restating a stop that span already
states — the last being the redundant restatement, which adds no fret and flips no bracket.

The one test stays one because the derivation publishes what a claim DID as reach, not only what it
added: a claim whose answering justified a span has reached that span
(\ref ChartShapes::claim_shapes), even where it printed no fret and landed past the instant the span
states its stops at, because taking it away would dissolve the span (user ruling 2026-08-27). So
this sweep never asks about justification — "states nothing" and "does nothing" are the same
question here, and the one place that can answer it is the pass that derived the shapes.

What is taken is the STATEMENT, never more than the statement, and the two shapes of claim differ
only in how much of the record that is. A \ref NoteAttack::None note IS its claim — it has no head,
no ring and no sound — so the record goes with it. A held stop rides a note that still states its
own onset, so only the FIELD is cleared and the tap, scrape or slide underneath stays exactly as
authored: sweeping the note would delete a sound the charter wrote, which no invariant here asks
for.

ONE PASS is enough, exactly as it is for the legato sweep, and for the same kind of reason (user
ruling 2026-08-31, review #15). What is taken is a claim that reached NO span — so it was a member
of nothing, and no span's membership changes when it goes. The cascade this used to iterate for —
taking a claim leaves a span one member short, which then states nothing itself — has no way to
happen: every stop a span counts is stated by a member that reached it, and a record that reached
something is never what this takes.

Runs where the invariant has to hold: \ref normalizeChart's last stage, after the legato settle
(which changes an attack — no longer a span, since rule 11's amendment of 2026-08-29 keys spans on
POSITION), so a loaded chart is already swept; and
the editor's plan gate, so an edit that leaves a claim stating nothing takes it in the same undo
entry rather than saving one nothing draws.

\param notes Note stream sorted by (position, string); swept in place.
\param tempo_map Song tempo map supplying the beat axis.

\return One conversion per claim taken, each naming its position and string —
        \ref ChartRepair::InertSilentHold where the whole note went,
        \ref ChartRepair::InertHeldStop where only the field was cleared; empty when every claim in
        the stream already stated something.
*/
[[nodiscard]] std::vector<ChartConversion> sweepInertClaimedStops(
    std::vector<ChartNote>& notes, const TempoMap& tempo_map);

/*!
\brief Clears every stored held stop a PULL-OFF already states — the derivation's residue.

The third sweep beside the other two, and relational for the same reason they are: what makes a
stored \ref ChartNote::held redundant is a fact about the note's NEIGHBOUR
(\ref chartDerivedStops). Where the notation states the stop, the field is a second copy of one
fact — free to disagree with the first, and read by nobody, since every consumer reads the
resolution. So it is taken UNCONDITIONALLY (user ruling 2026-08-31, DERIVED HELD): an agreeing
value is duplication and a contradicting one is a lie, and keeping either would leave a document
whose reader must decide between two spellings of the same statement.

Nothing else moves. The stop stays exactly as stated — the resolution does not read the field it
clears, so the spans, the postures and every digit are identical before and after — and the note
keeps its onset, its ring and every technique it was authored with. That is what makes this a
NORMALIZATION rather than an edit: it changes the record's spelling and not the chart's meaning.

Runs at both ends of the document's life, which is the whole of what "the writer never emits it"
takes: \ref normalizeChart, so a chart carrying residue is cleaned on load and saved without it,
and the editor's plan gate, so authoring the pull-off that states a stop clears the field it
supersedes IN THE SAME UNDO ENTRY. Before \ref sweepInertClaimedStops, so a residual field the
inert law would also have taken is reported once, under the rule that actually explains it.

\param notes Note stream sorted by (position, string); swept in place.
\param tempo_map Song tempo map supplying the beat axis.

\return One \ref ChartRepair::DerivedHeldStop conversion per field cleared, in note order, each
        naming its position and string; empty when no stored held stop was superseded.
*/
[[nodiscard]] std::vector<ChartConversion> sweepDerivedHeldStops(
    std::vector<ChartNote>& notes, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
