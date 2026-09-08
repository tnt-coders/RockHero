/*!
\file chart_shapes.h
\brief The hand-posture derivation: the spans a note stream implies, and the postures they hold.
*/

#pragma once

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
\brief One hand posture: the stop held on each string while a span runs.

Array index 0 is the lowest-pitched string; a null entry means the string is not part of the
posture. The array is \ref g_max_chart_strings long — the model's own bound on a string number,
not a statement about the tuning — so a chart with fewer strings simply leaves the top slots
empty, and two postures always compare by their held stops alone.

Derived, never authored — the stops an onset's struck members hold (\ref ChartStop: a fret
pressed, the open string, or a harmonic node touched), plus the stop whatever was still ringing
across it has REACHED by then, plus the stops a \ref NoteAttack::None note says the hand takes
silently (\ref deriveChartShapes). A stop carries no provenance here on purpose: the posture is
what the hand holds, and where a given stop came from is the SPAN's question
(\ref ChartShape::silent_member), so two spans holding identical stops stay one deduplicated
posture however each was learned — while a node grip and a fret grip printing the same number are
two postures, because they are two grips. Chord names and fingerings carry no field here because
nothing writes one; when they are authored they become a dictionary keyed by a posture rather than
members of it.
*/
struct ChartPosture
{
    /*!
    \brief THE GRIP: the stop the hand holds per string; nullopt where it holds none.

    What every rule reads — founding, extent, class, contradiction, the tap's held default, the
    census's carry rows. Never a texture string: the two are disjoint by construction.
    */
    std::vector<std::optional<ChartStop>> stops;

    /*!
    \brief THE TEXTURE under the grip: hand-free rings sounding through the span that belong to an
    earlier span; nullopt where none does, and always nullopt where \ref stops holds the string.

    A ring no hand holds belongs only to the span it was struck in (user ruling 2026-09-07), so an
    open string or natural harmonic ringing on out of a closed span founds nothing, bounds nothing
    and classifies nothing — but it SOUNDS under whatever founds over it, and the bracket states
    what sounds under the shape ("included in that span's brackets display", same day). Published
    beside the grip rather than merged into it so that a display can union the two and a rule can
    read the grip alone, with neither having to guess which is which.
    */
    std::vector<std::optional<ChartStop>> texture;

    /*!
    \brief Compares two postures by their grip and their texture.
    \param lhs Left-hand posture.
    \param rhs Right-hand posture.
    \return True when both hold the same stop, and print the same texture, on every string.
    */
    friend bool operator==(const ChartPosture& lhs, const ChartPosture& rhs) = default;
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

    THE DATING RULE (user ruling 2026-08-31): a span dates from its EARLIEST MEMBER ONSET NOT
    COVERED by a preceding span. An accumulation's members arrive one at a time, and the statement
    began where the first of them was struck — so the rails run from there and the later members
    arrive inside it, rather than the mark starting at whichever arrival happened to reach the
    threshold.

    The second half is what keeps spans from overlapping: a ring whose onset lies inside a span
    already emitted is CARRIED, and a carry never backdates. That is one comparison with two
    consequences, and \ref deriveChartShapes spends it once — a carry that dates a span is a
    founding member and BOUNDS it, while one crossing in from covered ground is texture that states
    a stop and no reach. Landings and death survivors are covered by construction, which is why a
    successor starts exactly where its predecessor ended.
    */
    GridPosition position;

    /*!
    \brief THE MUSICAL CLOSE, in beats from \ref position; zero only where every member is silent.

    **The instant the span's statement actually ended, and nothing about how it is drawn** (user
    ruling 2026-09-04). Two arms, and the distinction is the whole field: where an EVENT closed the
    span — a contradiction, a growth split, any close at a slot — the close is that CLOSING EVENT'S
    OWN ONSET, because the hand demonstrably moved there; where the statement simply RAN OUT, the
    close is the shape's own reach (the continuity law's minimum). Storing the reach unconditionally
    would claim grip past a proven hand move, and storing the closing onset unconditionally would
    claim grip through proven silence, so the close is the EARLIER of the two.

    RULE 12A'S MARGIN IS NOT IN HERE. It is a DISPLAY rule now, applied once where the view state is
    built (\ref makeChartViewState) from \ref closing_onset and \ref stated_extent beside this. What
    made that a precondition rather than a tidy-up, recorded as history: the OLD machine's growth
    split closed the predecessor one display margin before the successor's own start, and the old
    figure-based tail law then had to merge across seams a margin wide. Both are deleted — growth
    happens in place and the tail law asks one span — and the display margin stayed out of storage.

    THE POSTURE TRUTH CRITERION (user ruling 2026-08-31), which this field is what enforces: **no
    span claims a stop the hand abandoned while it ran.** A span's posture is a per-span set that
    only ever GROWS (growth IS accumulation — user, 2026-09-04), so the one way it could come
    to lie is by outliving a member — and the extent law below is what forbids that. The first
    member whose statement stops bounds the whole span — the trailing edge — and the dating floor
    bounds the front by the end of each stated string's last FOREIGN sound (Law A's clamp, the
    leading edge), so every fret a bracket prints was held for every instant the bracket covers.
    A long accumulation bracket is therefore true BY CONSTRUCTION, not by measurement.

    THE INVARIANT ([D2] amended 2026-08-29): **every span with a SOUNDING member is strictly
    positive.** A span runs as long as every sounding member goes on stating its stop (THE
    CONTINUITY LAW, in \ref deriveChartShapes), and every member's own chain reaches past the span
    start, so the extent can only reach the start itself where no member sounds at all. It is now an
    invariant of the arithmetic rather than a case: the close is the earlier of two instants that
    are both at or after the start, so it can only answer the start itself where the reach does.

    Zero is therefore reserved for the one case that means it: a span whose members are ALL held
    fingers states its posture at an instant until a MEMBER's sound attaches to it, and then runs
    through the ring of the note that matched one of its claims. A silently-held stop rings for
    nothing, and taps articulating such a shape justify it without lengthening it — a right-hand
    onset says nothing about the fretting hand, so it never bounds a span. The bracket draws at the
    span start whatever the length, and the rails a positive span draws have nothing to cover.

    TRAVEL no longer shortens a span to its own start. The amendment moved the split to the
    LANDING: a chord slide keeps the fingers planted, so the rings run continuously and the
    continuity law itself covers the transit — the span states the departing grip, COVERS the
    glide, and ends where the new grip is established, which is exactly where the successor span
    opens. The two tile with no gap between them — and since the close stopped carrying a display
    margin, they tile in the stored data too and not only in the walk's own reasoning.
    */
    Fraction sustain{};

    /*!
    \brief How far the span's own STATEMENTS reach, in beats from \ref position.

    The last instant an EVENT stated this span — its final strum, or the slot whose holds opened or
    grew it — capped at the musical close, and zero on a span no event ever stated. Rule 12a's
    display trim floors on it, because a span's furniture may not retreat behind its own last
    statement: at anything faster than a sixteenth the closing onset crowds inside the margin, and a
    box trimmed blindly would stop before the strum it is drawn over.

    Published rather than re-derived beside the trim, for the reason every other span fact here is:
    answering it means knowing WHICH SLOTS this statement covers, and this walk is the only thing
    that does. A projection-side scan for "the last onset inside the extent" would count a tap and a
    redundant claim slot, neither of which states the shape, so it would be the same rule written
    twice and free to disagree.
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
    \brief True when a posture member of this span came from a silent hold rather than from sound.

    The one fact the arrival rule (\ref chartShapeArrivals) cannot re-derive from what SOUNDS, and
    the reason it is carried here instead of asked again: this walk is what resolved the holds, so
    stating the answer on the span it resolved them into is one authority publishing its result,
    where a second scan beside the arrival would be the same rule written twice and free to
    disagree.

    The per-span summary of \ref ChartShapes::claim_shapes, which names the resolution note by
    note. Both are written in the same loop of the same pass, so they cannot disagree; this one
    exists because the arrival rule asks the question once per SPAN and re-scanning the note stream
    for each span would make one classification quadratic.

    It is what flips the span to an arpeggio. The bracket is the only mark that states a posture
    fret at all — a chord box draws the notes' own heads — so a span carrying a silently-held member
    must arrive as an arpeggio or the authored fact is stored and never shown.
    */
    bool silent_member{false};

    /*!
    \brief True when some SOUNDING of this span was not the shape WHOLE.

    LAW III's class rule in ONE comparison: a slot striking fewer strings than the shape SOUNDS is
    the shape's members arriving SEPARATELY, so the span is an arpeggio for its whole length — the
    span is one statement, and its class is HOW that statement's members arrive.

    Asked of every slot inside the span that sounds anything, ITS OWN START INCLUDED (user ruling
    2026-08-28). The start is not a second case: a span whose opening slot strikes fewer strings
    than its shape sounds is a span whose posture CARRIES a string into that start without an onset
    at it — the arrival rule's trigger (a), which used to re-derive the same comparison one slot
    earlier and off a different stream. A partial restrike, a lone re-pick and a carried start are
    one fact at three widths, and the walk answers all three with the one count.

    A LANDING SUCCESSOR (\ref landing_opened) has no fourth width, and used to be given one: a
    constant `true` stating that nothing struck at a landing means its members arrive separately. A
    LANDING IS NOT A SOUNDING (user ruling 2026-08-30), and neither is a DEATH — nothing is struck
    at either because the surviving rings simply carry on — so there is no sounding of the shape to
    be partial, and the walk's own guard already says it: a slot that sounds nothing is no sounding
    of the shape, and a boundary has no slot at all. The constant was honest only while a successor
    could never be strummed; rule 11's corollary 2 ended that, and a chord sliding into chords then
    arrived an arpeggio at every landing. A successor classifies by the ordinary triggers like any
    other span.

    An ACCUMULATION needs no clause here either, and that is worth stating because it looks like it
    should: its opening slot strikes fewer strings than the shape sounds BY DEFINITION — the rings
    it overlapped into are the rest — so this one count answers it at the founding, and every
    accumulation is an arpeggio by construction rather than by a rule of its own.

    Carried here rather than re-derived beside the arrival rule, for the same reason
    \ref silent_member is: answering it needs to know WHICH SLOTS this statement covers, and this
    walk is the only thing that does. What a reader can see is the span's WINDOW, and the closing
    onset sits exactly ON its end whenever an event closed the span — so a window re-derived from
    the extent cannot tell a slot the statement RODE from the slot that CLOSED it. Measured against
    the corpus: of the 296 spans a lone re-pick appears in, 48 hold that re-pick only at the span's
    own end, and 39 of those print as boxes — the onset there CLOSED the span rather than
    continuing it. A re-derived window has no way to tell the two apart; the walk never has to ask,
    because riding the slot is what it did.

    Only the strings the shape SOUNDS are the denominator, because a shape that also CLAIMS a member
    already arrives an arpeggio through \ref silent_member: a claim never sounds, so a shape holding
    one has members sounding separately by inspection. That is also what leaves the silent openings
    covered: a span opening on held fingers alone strikes nothing, so this count says nothing there
    — and every such span states a stop no sound of its own states.
    */
    bool sounds_in_parts{false};

    /*!
    \brief True when a LANDING opened this span — the one onset-less open the law admits (rule 6).

    The one span nothing states at its own start. Every other span is opened by an EVENT — a strum,
    or an authored hold — that puts the statement at an instant; a landing successor's members are
    rings struck under the statement BEFORE it, whose travels arrived at the boundary the hand slid
    to. Nothing happens at its start except the previous statement ending and the landed grip
    standing (user rulings 2026-09-04: mere ring-out opens NOTHING — the old death cause is gone,
    so this field has exactly one cause and is named for it).

    Display keys the opening mark's deferral on the same fact through \ref bracket_position (a
    landing states no ink; the first interior sounding fills it), and the chord name changes here
    once names exist. Published rather than inferred because no reader may substitute a test of its
    own: "nothing sounds at the start" agrees only by accident (a claim-founded span sounds nothing
    there either), and `last_stated_beat` stops answering the moment an interior re-pick states the
    successor — the siege proved that proxy wrong in both directions against pinned fixtures. Its
    consumers are the census (the sanctioned instrument) and the tests; production display keys on
    \ref bracket_position. Declared here so that is read as deliberate rather than discovered.
    */
    bool landing_opened{false};

    /*!
    \brief Where this span's one OPENING MARK draws; absent where it draws none ([D2] amendment 2).

    Every span an EVENT states — a strum, an authored hold — carries its own FRONT here
    (\ref position), because that is the statement's own extent and the rails run
    from it. An ACCUMULATION is no exception and needs no clause: its front is its earliest
    uncovered member's onset, which is where the statement began, so the bracket starts there
    and the later members' heads arrive under it. A LANDING SUCCESSOR carries its first INTERIOR
    sounding instead: nothing at all is stated at a landing, so THE INK FOLLOWS THE SOUND
    (review F7). One that never sounds interiorly carries nothing and draws no mark at all — the
    rails and the chord name changing there are its whole statement.

    ONE field with one write rule, which is what makes those cases one law rather than a branch
    on \ref landing_opened: the seed happens where a span opens and the fill happens at the first
    sounding, so the second only ever lands where the first did not.

    Consulted only where a BRACKET actually draws — an ARPEGGIO-classified span
    (\ref chartShapeArrivals). A box-class span states itself with its strums' own boxes, so its
    anchor is never read, and after the 2026-08-30 successor ruling that is the ordinary disposition
    of a landing successor rather than a corner of one.

    The WALK publishes it because the walk is what knows which slots this statement covers. A
    re-scan of the note stream for "the first sounding at or after the span's start" was that
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

    /*!
    \brief Per input note, the span its CLAIMED stop joined; absent where it claims none or reaches
    none.

    Same order and size as the note streams, so a caller indexes it by the note it already holds.
    Both shapes of claim resolve through it (\ref chartClaimedStops): a silently-held member, whose
    whole existence is the fret it puts into a posture, and a held stop riding a right-hand onset,
    whose note has a head of its own but whose held fret does not.

    This is where a claim BECOMES visible. A silent hold draws no head, so the posture bracket
    printing its stop wherever that span's mark draws is its whole face, and the editor reads this
    to place that face and to hit test it; a held stop prints in the satellite slot beside that same
    bracket, which is its own independent target. An absent entry means the claim resolved to
    nothing and therefore draws nowhere — exactly the property "nothing undrawn is clickable"
    needs, published by the pass that knows rather than re-derived by the surface, and the same
    entry the inert sweep reads to decide what states nothing.
    */
    std::vector<std::optional<std::size_t>> claim_shapes;
};

/*!
\brief Derives the hand-posture spans a note stream implies — THE GRIP-TENURE LAW (user-signed
2026-09-04, docs/plans/in-progress/span-derivation-ground-up.md).

One idea: a span is the statement "the hand holds this grip, from here to here", and the machine
keeps the EVIDENCE — what each string is doing — in one per-string table that outlives every span.

WHEN A SPAN EXISTS. A span OPENS at an onset stating a grip: two or more stops struck or claimed
at one slot (the statement threshold). Sound alone may ACCUMULATE one at three or more overlapping
members — the minimum gates founding by sound alone and nothing else. A LANDED TRAVEL is the one
onset-less open (\ref ChartShape::landing_opened): the grip held through the slide, at least one
finger arrived, two members ringing strictly past the boundary. NOTHING ELSE opens a span —
strings that merely ring on past a break are tails.

WHEN A SPAN RUNS AND ENDS. A span runs until its grip BREAKS: a member quits (any posture member —
its sound out with nothing on its own string renewing it at that instant, either hand's onset
renewing), or a contradiction (a strike naming a different stop on a string the grip states or the
hand audibly holds — Law A, read end-inclusively at the junction instant). A restatement of the
same grip is the same span CONTINUING; a stop the grip lacks GROWS it in place — growth IS
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
STRICTLY EXCEEDS the notated-distinguishability quantum at the closing head's measure — the
importer synthesizes every glide-into-restrike arrival exactly one quantum before the replacing
onset, so the strictness IS the ratified suppressed population, and the chord name never flickers
for a sliver. A held-but-never-restruck landed span is emitted: it is what states the chord-name
change at the landing. LAW II is unchanged beside it: an unjustified span the hand alone stated
dissolves, and publication rides the push, which is what keeps both drops safe.

\param saved_notes The stored stream, sorted by position; rings are facts and are never written.
\param claimed_stops The resolved claim table: what the fretting hand HOLDS at each record, for a
       silent hold and a right-hand onset alike (a tap's pitch derives from the stopped length, so
       its held fret participates fully on the statement path).
\param planted_stops The hold-under table (\ref chartPlantedStops, user ruling 2026-09-06): per
       note, the stop its pull-off states is planted beneath it, whichever hand made the onset.
       Feeds the seam verdicts and — since THE FOLD — the statement dating, never the grip
       column or the claim column; the one column-by-column list lives at the predicate pair in
       the walk, so this contract and that list cannot drift apart.
\param tempo_map The beat axis every instant above is measured on.

\return The spans, their posture table, and per-note claim reaches (\ref ChartShapes).
*/
[[nodiscard]] ChartShapes deriveChartShapes(
    const std::vector<ChartNote>& saved_notes, const std::vector<std::optional<int>>& claimed_stops,
    const std::vector<std::optional<int>>& planted_stops, const TempoMap& tempo_map);

/*!
\brief Classifies every shape span as an arpeggio or a strummed chord box.

The second half of the same derivation, and here beside the first for that reason: \ref
deriveChartShapes says where the hand goes and how long it stays, this says which of the two marks
the notation draws. Neither is authored, so neither has a rule a document could break.

ONE law decides it — ARPEGGIO iff the shape's members sound SEPARATELY — and the span stays a chord
box only while every sounding of it is the shape whole. FOUR triggers, every one of them that same
question asked where a sounding can be incomplete.

(a) A posture string CARRIED into the span's start: still ringing there, with no onset at it. The
strum picks around the held note, so its start was never one full strum. THE STRUM is what makes it
a trigger — a sounding that reached only part of the shape — so a LANDING SUCCESSOR
(\ref ChartShape::landing_opened) is not this trigger at all: a landing is not a sounding,
nothing is struck at one, and the rings carry on (user ruling 2026-08-30). A
successor is classified by whatever the three triggers below find inside it, and a chord sliding
into chords is therefore a BOX at both ends, joined by sliding tails.

(b) A silently-held member (\ref ChartShape::silent_member): the hand states a stop it never sounds,
so the members demonstrably do not all arrive together.

(c) A slot INSIDE the span that sounds only PART of the shape — a partial restrike, or a lone
re-pick of one member — which is the members arriving one group at a time.

(d) A picking-hand onset, a tap or a pick slide, sounding anywhere within the span: the fretting
hand holds the shape while the other hand sounds above it, so the chord is sustained through the
tapping rather than strummed.

Any of the four renders the shape as brackets around individual notes instead of one strummed box.

(a) and (c) are ONE comparison, and \ref ChartShape::sounds_in_parts is where it is answered (user
ruling 2026-08-28): a slot striking fewer strings than the shape SOUNDS is its members arriving
separately, and asking exactly that at the span's own start IS (a). Three of the four are therefore
read off the span and only (d) is derived here, which is not an accident — a fact about WHICH SLOTS
the statement covers has to come from the walk that grouped them, while whether a right-hand onset
lands inside the span is a question about the extent this rule is handed.

CLASSIFICATION READS THE STORED STREAM (same ruling), because the class is a fact about the HANDS:
where the fingers are, and which of them the pick reached. The carry in (a) is the walk's own
fold-in, which has always asked the stored ring, so a dead string's carry now classifies at a span's
START exactly as it already did at an interior slot. E25 is untouched by this and stays what it
always was — a DISPLAY rule, about what a surface draws of a ring nobody hears. What (d) reads off
the note stream is positions and attacks alone, which presentation carries through unchanged — so
the rule takes the stored stream, and \ref chartResolutions can answer the class before the bracket
re-read that consumes it runs.

A posture string is either SOUNDED by the span or CLAIMED by it, which is why "merely silent at the
start" is no longer a case to decide: a string nothing sounds and nothing claims is in no posture at
all. A carried string is in the span's sounded stops and answers through (a)/(c); a claimed one
answers through (b). The distinction the old rule had to guess at is structural now.

"Fewer than two sounds at the span start" is likewise not a trigger but the PRECONDITION of (a) and
(b): rule 10 needs two MEMBERS to open a span, so a thin start always means a carry or a claim, and
stating it here made it a third answer to a question already answered twice.

One forward cursor over the sorted notes serves every shape. The backward look this rule used to
need — each posture string's most recent earlier note, reached by walking back to the first note in
the song whenever a posture string had none — went with the ring reading that wanted it.

\param notes Note stream sorted by (position, string); only positions and attacks are read, which
             presentation never moves, so the stored and the presented form answer identically.
\param shapes Hand-posture spans, sorted by position (\ref ChartResolutions::shapes).
\param tempo_map Song tempo map, for the signature-exact span end.
\return One flag per shape, in `shapes` order: true where the span renders arpeggio-style.
*/
[[nodiscard]] std::vector<bool> chartShapeArrivals(
    const std::vector<ChartNote>& notes, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map);

} // namespace rock_hero::common::core
