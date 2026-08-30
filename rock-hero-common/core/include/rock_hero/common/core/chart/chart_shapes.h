/*!
\file chart_shapes.h
\brief The hand-posture derivation: the spans a note stream implies, and the postures they hold.
*/

#pragma once

#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief One hand posture: the fret held on each string while a span runs.

Array index 0 is the lowest-pitched string; a null entry means the string is not part of the
posture. The array is \ref g_max_chart_strings long — the model's own bound on a string number,
not a statement about the tuning — so a chart with fewer strings simply leaves the top slots
empty, and two postures always compare by their held frets alone.

Derived, never authored — the frets an onset's struck members hold, plus the stop whatever was
still ringing across it has REACHED by then, plus the stops a \ref NoteAttack::None note says the
hand takes silently
(\ref deriveChartShapes). A fret carries no provenance here on purpose: the posture is what the hand
holds, and where a given stop came from is the SPAN's question (\ref ChartShape::silent_member), so
two spans holding identical frets stay one deduplicated posture however each was learned. Chord
names and fingerings carry no field here because nothing writes one; when they are authored they
become a dictionary keyed by a posture rather than members of it.
*/
struct ChartPosture
{
    /*! \brief Fret held per string; nullopt when the string is not part of the posture. */
    std::vector<std::optional<int>> frets;

    /*!
    \brief Compares two postures by their held frets.
    \param lhs Left-hand posture.
    \param rhs Right-hand posture.
    \return True when both hold the same fret on every string.
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
    /*! \brief Musical start of the span. */
    GridPosition position;

    /*!
    \brief Span duration in beats; zero only where every member is silent.

    THE INVARIANT ([D2] amended 2026-08-29): **every span with a SOUNDING member is strictly
    positive.** A span runs as long as every sounding member goes on stating its stop (THE
    CONTINUITY LAW, in \ref deriveChartShapes), and every member's own chain reaches past the span
    start, so the extent can only reach the start itself where no member sounds at all. Where a
    closing event's margin would trim it below that, one of two things is true and the derivation
    says which: a span some EVENT stated at an instant — a strum, or an authored hold — falls back
    to exact adjacency and keeps its length, mirroring the sustain rules' protected-adjacency
    precedent; a span no event states, which is only ever a LANDING SUCCESSOR, states nothing the
    statements on either side of it do not, and is not emitted at all.

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
    opens. The two tile with no gap between them.
    */
    Fraction sustain{};

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

    A LANDING SUCCESSOR ([D2]) has no fourth width, and used to be given one: a constant `true`
    stating that nothing struck at a landing means its members arrive separately. A LANDING IS NOT
    A SOUNDING (user ruling 2026-08-30) — nothing is struck there because the rings simply carry
    on — so there is no sounding of the shape to be partial, and the walk's own guard already says
    it: a slot that sounds nothing is no sounding of the shape, and a landing has no slot at all.
    The constant was honest only while a successor could never be strummed; rule 11's corollary 2
    ended that, and a chord sliding into chords then arrived an arpeggio at every landing. A
    successor classifies by the ordinary triggers like any other span.

    Carried here rather than re-derived beside the arrival rule, for the same reason
    \ref silent_member is: answering it needs to know WHICH SLOTS this statement covers, and this
    walk is the only thing that does. What a reader can see is the TRIMMED extent — rule 12a's
    display margin, floored at the last instant an event stated the span — and a window re-derived
    from that disagrees with the walk at its own END, because the last strum sits ON the end when
    the closing onset crowds inside the margin, which a sixteenth-note passage does by
    construction. Measured against the corpus: of the 296 spans a lone re-pick appears in, 48 hold
    that re-pick only at the span's own end, and 39 of those print as boxes — the onset there
    CLOSED the span rather than
    continuing it. A window re-derived from the extent has no way to tell the two apart; the walk
    never has to ask, because riding the slot is what it did.

    Only the strings the shape SOUNDS are the denominator, because a shape that also CLAIMS a member
    already arrives an arpeggio through \ref silent_member: a claim never sounds, so a shape holding
    one has members sounding separately by inspection. That is also what leaves the silent openings
    covered: a span opening on held fingers alone strikes nothing, so this count says nothing there
    — and every such span states a stop no sound of its own states.
    */
    bool sounds_in_parts{false};

    /*!
    \brief True when this span opened at a LANDING rather than at an event ([D2] amendment 2).

    The one span nothing states at its own start. Every other span is opened by an EVENT — a strum,
    or an authored hold — that puts the statement at an instant; a landing successor's members are
    rings struck under the statement BEFORE it, which simply went on ringing through the glide and
    came to rest together. Nothing happens at its start except the fingers arriving.

    Display keys the opening mark on it, which is why the fact is published rather than inferred: a
    landing-opened span draws NO bracket at its start — the continued tails and the chord name
    changing at the landing are the whole statement — and defers the bracket to its first INTERIOR
    sounding, where the ink follows the sound. A claim-founded span keeps its start bracket, because
    there the start IS the statement rather than a continuation.

    No reader may substitute a test of its own for this. "Nothing sounds at the start" is a
    different question that agrees only by accident (a claim-founded span sounds nothing there
    either), and the walk's own `last_stated_beat` stops answering it the moment an interior
    re-pick states the successor. Only the arm that OPENS a successor knows, so that arm says so.

    Production display keys entirely on \ref bracket_position; this field's consumers are the
    census (the sanctioned instrument, which this field freed from a forbidden structural proxy)
    and the tests. That is deliberate, not an orphan — stated here so it is declared rather than
    discovered.
    */
    bool landing_opened{false};

    /*!
    \brief True when a member's fret TRAVEL runs inside this span's extent ([D2] amendment 1).

    The span COVERS its members' glide and splits at the landing, so over that stretch the
    furniture states the DEPARTING grip while the ribbons beneath it are moving to another one.
    That is the one thing C3's ink ownership cannot survive: suppressing a tail is honest exactly
    because the mark drawn over the span says what the member's own ribbon would say there
    (\ref chartSuppressedTails), and across a glide the two say different things. So a span covering
    travel owns NO member ink at all — the travelling members draw their sliding tails, and the
    ones that stay put draw straight through the figure instead of vanishing and then reappearing
    at the landing with no tail leading into it.

    Published rather than inferred, for \ref landing_opened's reason and one of its own: the spans
    that cover travel are NOT the spans that open a successor. A staggered landing, a landing with
    fewer than two rings past it, and a landing the close outruns each cover a glide and re-open
    nothing, so a reader keying off the successor beside it would see only some of them. Only the
    walk that read the channels knows, so it is the walk that says.
    */
    bool covers_travel{false};

    /*!
    \brief Where this span's one OPENING MARK draws; absent where it draws none ([D2] amendment 2).

    Every span an EVENT states — a strum, an authored hold, a growth split's own claim — carries its
    own start here, because that is where the statement was made. A LANDING SUCCESSOR carries its
    first INTERIOR sounding instead: a chord slide keeps the fingers planted, so all that happens at
    the landing is the fingers arriving, and THE INK FOLLOWS THE SOUND (review F7). One that never
    sounds interiorly carries nothing and draws no mark at all — the continued tails and the chord
    name changing at the landing are its whole statement.

    ONE field with one write rule, which is what makes those two cases one law rather than a branch
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
    Both shapes of claim resolve through it (\ref claimedStop): a silently-held member, whose whole
    existence is the fret it puts into a posture, and a held stop riding a right-hand onset, whose
    note has a head of its own but whose held fret does not.

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
\brief Derives the hand-posture spans and postures a note stream implies.

The chart stores no spans: a span is a statement about the notes under it, so deriving it is the
only way it can never disagree with them. This is that derivation, run once per chart revision
inside \ref chartResolutions and read from there by everything that draws a chord box, an arpeggio
bracket, or a span-implied hold.

A span opens at a slot holding two or more MEMBERS, where a member is a sounding fretting-hand
onset there or a stop the hand CLAIMS there (\ref claimedStop — a \ref NoteAttack::None hold, or a
held fret riding a right-hand onset) — one sound plus one held finger opens a span, two held
fingers with nothing sounding open one, and a LONE member of either kind opens nothing
(user ruling 2026-08-27). The posture is deduplicated by
its fret vector, and consecutive onsets restating the same stops merge into one span for as
long as its statement stays in force — the grouping the tab renders as a chord box over repeated
strums. Tap-only onsets are transparent to the whole derivation: taps are the tapping hand, so they
neither form postures nor close held spans, letting a ringing chord's span cover the taps above it.

A CHANGE IN ARTICULATION DOES NOT SPLIT THE SPAN (user ruling 2026-08-29, rule 11 amended). The
span is a FRETTING-HAND statement, so continuation and merging compare POSITION — the strings and
the stops — and nothing else: palm-mute is the picking hand, dead is pressure, accent and ghost are
dynamics, and none of them move the grip. A chord, the dead chugs played on it and the chord again
are ONE hand fact and derive as ONE span, with articulation varying freely inside it as per-onset
display data. WHAT STILL SPLITS is position and silence, the complete list: fret travel (the
landing split, [D2] below), growth (a new string changes the shape), a same-string different-fret
contradiction, and a GENUINE GAP. Strum durations never did. The posture table is deduplicated by
frets alone, as it always was — the hand posture is what a span states, and techniques render on
the notes. A note still ringing through
a chord's onset (tie-held from before, not re-struck) joins the posture on its string, AT THE STOP
ITS OWN FRET CHANNEL STATES THERE (user ruling 2026-08-29) — a ring that has travelled since its
strike carries the finger with it, so the posture states the grip it has reached and not the one it
was struck at, and a ring caught mid-glide states no stop at all and joins no posture, exactly as
its departure has already put it out of reach of a merge ([D2] below). One reader answers "where is
this finger now" for the carry, for a member's own reach and for a landing alike, so a chart
can never state two hand positions for the same finger at one instant. The shared
arrival rule (\ref chartShapeArrivals) then renders the partly-struck span as an arpeggio, while a
span every sounding of which is the shape WHOLE stays a chord box. A span closed by a
following event trims to the minimum-sustain-distance margin before it
(\ref minimumSustainDistanceBeats at the closing onset's measure), floored at the last instant an
EVENT stated the span, with an exact-adjacency fallback when even that would leave no length — the
same margin every other element keeps. That fallback protects a statement made at an instant, which
is why the one span nothing states at an instant has no fallback and simply ceases to exist there
(\ref ChartShape::sustain).

EXTENT is THE CONTINUITY LAW (user ruling 2026-08-27). A span's statement is in force while every
SOUNDING member's STORED ring is continuous — ringing through, or ending exactly at the next onset
that SOUNDS its string, which is the strike-into-strike shape a stored chug chain has. The onsets
that continue a string are EVERY sounding one, whichever hand made it (user ruling 2026-08-28): a
tap on a member string ends that member's tail underneath it with no hand lifting anywhere, so the
sound was REPLACED and not silenced, and detachment is a statement about sound STOPPING. A silent
hold sounds nothing and stops nothing, so it continues nothing. CONTINUING a chain and
WRITING one are different acts, and only the fretting hand does the second: a sounding onset of
either hand keeps the statement in force across it, while the chain's LENGTH is only ever written
by a member's own strike — a chain a tap wrote would let a tapped sixteenth decide how far the
shape reaches, or hold the shape open past the last sound the fretting hand made. The FIRST
genuine stored gap on any sounding member ends the span at that ring's end, because a ring that
simply stops with nothing sounding after it is the chart stating DETACHMENT; members still ringing
past that end are remainder context, and display draws them as ordinary tails — absorption is ink
ownership and never trims a presented sustain. So the extent is the MINIMUM of the members' chains,
not the maximum of their rings, and minimum-extent is this law's box case rather than a rule beside
it. Two members of one strum with unequal rings end their box together at the shorter; a run of
strums that ring into each other is one span through the last one's ring; and a run with a genuine
gap between two strums is two statements, because a span does not outlive its own sound waiting to
be rejoined. CLAIMS are exempt (a claim has no ring — it states where a finger is, never how long
anything sounds), so a zero-sound span's extent stays justification-driven: justification decides
whether that span EXISTS, never how far it runs, and it gains a length only once a member sounds
inside it. A held-carrying tap answering its claim is the case that makes the split visible — the
one right-hand onset whose ring is real evidence about the stop, since a tapped harmonic dies the
moment the held fret lifts — and it still writes no length, because that evidence arrives as a
CLAIM. CARRIED ring-through members are extent-inert, classifying the span without bounding it, or
let-ring texture under a passage would decide how long the passage's own statements are.

TRAVEL SPLITS AT THE LANDING, AND THE LANDED GRIP RE-OPENS THERE (user ruling 2026-08-27, [D2],
AMENDED 2026-08-29). A member's own fret channel bounds it exactly as its ring does, and the bound
is the LANDING: the span COVERS its members' travel and ends where the channel comes to rest on the
grip it was moving to. A chord slide keeps the fingers planted, so the rings run continuously and
the continuity law itself carries the transit; the extent is the minimum of the members' coverage,
so the EARLIEST landing ends the span exactly as the earliest stopped ring does. A travelling
finger has let go of nothing, so it is no detachment and nothing about it shortens the statement.

What a travelling member may NOT do is be RESTATED, and that is judged PER MEMBER rather than per
slot (user ruling 2026-08-29). A slot restates the shape while everything it sounds agrees with
what the shape states and it contradicts nothing the shape still covers: an OPEN member restruck
mid-slide — an open channel never departs — is an interior subset sounding like any other, riding
the span, flipping its class and leaving the split at the landing. A stop the shape does not state
— a different fret on a stated string, or a string it never held — is a statement the span cannot
absorb, and it truncates the travelling span there like any other replacement.

Where the first differing statement is the member's first fret-stating keyframe the hand departs at
the onset itself, which no longer shortens anything — the whole glide is the departing grip's span.

The channel is read by ONE authority, asked "what stop does this note state at this offset", and
every question about a finger's whereabouts is that one question at a different moment: a strike
reads it at the note's onset, a member's own reach at wherever the shape's own start falls
inside the ring, a landing at the arrival, and the ring-through fold-in above at the slot it
crosses. Naming the stop at a call site instead was the same fact stated twice and free to
disagree, which is how a carried finger came to be printed at a fret it had already left
(user ruling 2026-08-29).

The grip the travels land in re-opens THERE as a SUCCESSOR span whose members are the arrived rings
— the growth split's own act, made at a moment inside a ring rather than at a slot, which is why a
claim's departure and arrival are one instant and a travel's are two. Its members carry their
landed stops (a member that stayed put keeps the shape's, which is the one-finger case by
symmetry), it states that grip as the posture the dictionary names, and its extent is those rings'
own continuity. It draws NO opening mark at the landing itself ([D2] amendment 2) — the continued
tails and the chord name changing there are the whole statement — and where it classifies arpeggio
interior sounding instead (\ref ChartShape::bracket_position).

It opens where every travelling member comes to rest at the SAME instant and two or
more members go on RINGING past that instant — nothing more, because the landing is now where its
predecessor ends and the two TILE. It STANDS from that landing, taken up by the walk when it
reaches the instant rather than only when something closes the span before it, which is what lets
a lone re-pick of a landed member ride it exactly as a re-pick rides any other span.

Its members are stated by RINGS it never struck, which the amended rule 11 makes no special case at
all: the STOP is the whole test for every member, struck, carried or claimed alike. A lone re-pick
of a landed member rides it (review F7), and so does a FULL restatement of the landed grip — that
strike restates the successor's stops, so it MERGES (rule 11 amended, corollary 2). "The bracket
span never strums" dissolved with the articulation identity that used to refuse it; the strike's
own full box comes from the display law, not from a span of its own.

Whether the landed grip gets a moment of its own is then the CLOSE's question and not a second
condition here, which is what makes the four ratified edges fall out of rules that already exist. A
glide straight into a restrike states its arrival one margin before the note it lands on, so the
successor opens and is closed a moment later with no length — and a span no EVENT states, left with
no length, states nothing either neighbour does not, so it is never emitted and the strike's own
box states the new chord (\ref ChartShape::sustain). The same answer covers a landing the walk only
reaches after something else has replaced the travelling statement: the successor would open behind
the close. Landings that do not coincide open nothing and leave the truth in the sliding tails, and
the earliest of them ends the span. A fret the channel LEAVES again is a point on the path, never a
grip. And travels of unequal distance are included, because nothing here asks how far a finger
moved.

A lone onset does NOT close a span when it is a re-pick of a string that span already holds AT THE
STOP it holds there — stated by sound or by an authored hold, and re-picked however it is
articulated — and the span's statement is still in force. The hand demonstrably has not left the
shape, and every fact needed to know that is already
in the stream, so this is derived rather than authored: it is the one-note-at-a-time broken chord
over a held shape. An ADJACENT re-pick is continuity itself, and one arriving after a stored gap
is an ordinary onset the statement has already ended before. It cannot OPEN a span; it CONTINUES
one, and its own ring is that string's newest bound from there — so a re-pick ringing short ends
the span at its own gap exactly as any other member's gap does, and one ringing on carries the
statement with it. Where it does carry the span further, that widens the span's right-hand scan and
can turn a following box into an arpeggio.

A \ref NoteAttack::None hold lying inside a derived span joins that span's posture on its string —
the one thing here that is authored rather than read off the sound, because no function of a note
stream can distinguish a held finger from an absent one. One past the span's own end contributes
nothing and is inert, as is one on a string the shape already states. A hold is still not a
STRIKE — it never closes a span and never ends a held posture — but it is a MEMBER, so two at one
slot open a span where no shape is still STANDING, and one beside a single sounding note does too.
The posture is keyed at the span's CLOSE rather than at each onset, because a claim is judged
against the span's own extent — which is also why one span keys one posture instead of every strum
re-keying the same one.

GROWTH is where the hold lands (user ruling 2026-08-27, which overturned the earlier join clause).
A stop the standing shape does not already STATE puts the hand in a different shape from that
instant, and the derivation answers that exactly as it answers a strum growing by a string: the
span splits. One comparison decides it, because there is one question — what stop does the shape
state on this string, by sound or by claim: a string it states nothing on is the hand growing into
a new shape, and a string it states ANOTHER stop on is the finger MOVED, which is a shape change
however the old stop was written down (user ruling 2026-08-27: the same fret continues the span, a
different one splits it). Only a claim restating the shape's own stop leaves it alone, taking no
new stop and adding nothing.

The new span inherits the shape it grew out of — the stops it sounds and the stops already claimed
in it — and takes the extent the old one had left, so the two cover that ring with no gap and no
overlap, and a later strum whose POSITION equalizes with the grown one merges into it under the
ordinary rule. What the splitting slot states DIFFERENTLY is superseded rather than inherited: the
hand has left those stops, so the successor states this slot's claims there instead of the ones it
moved off. What the authored hold decides is therefore WHERE the statement sits: written at the
shape's own onset it states the shape whole from its start, which is the case the record exists
for; written later it says the finger came down later, because that is what it says. A shape still
ASSEMBLING is exempt — one the hand alone stated that is still waiting for its content has nothing
to date it by, so later fingers join the one statement being made; once that content arrives the
statement is dated like a sounding one and stops being assembled.

A stop that reaches a span more than once — carried across a growth split — is a member of each,
but its FACE is published for the FIRST: it was authored at one slot, and that is where the bracket
printing it belongs. A stop that reaches none states nothing anywhere, and
\ref sweepInertClaimedStops is what keeps such a record from being saved.

A span every one of whose members is a hold must be JUSTIFIED by the content it fronts, and it is
authored in front of that content by design. ONE thing justifies it (user ruling 2026-08-27): one
of its own HELD frets being PLAYED inside the span — the span's claimed stop SOUNDING on its own
string, which is the same fret-match test the lone re-pick above uses.

A stop sounds two ways, and the law has one arm for each. The fretting hand PRESSES it: a sounding
onset arrives on a claimed string at that claim's stop. Or a right-hand onset SOUNDS it from above:
a tap harmonic's pitch derives from the stopped length, so an onset whose held fret is the claimed
stop plays that stop as surely as a finger fretting it does (the tap-harmonic arm, user ruling
2026-08-27). Both are one law over one fact — what fretting-hand stop each string sounds here — so
neither can drift from the other. What justifies nothing is a right-hand onset holding NOTHING,
however many of them sound over the shape: such a tap sounds where the tapping finger lands, which
is evidence about the other hand and says nothing about whether the stated stops are still down.
Nor does one holding a stop the shape never claimed.

A claim whose ANSWERING justified a span has REACHED that span, and is published as reaching it
(user ruling 2026-08-27). The two are one act: without that record the span dissolves, so it states
exactly as much as a member does, however little of the posture it adds — which is what keeps
"states nothing" and "does nothing" one question for \ref sweepInertClaimedStops to ask once. A
claim that answers nothing is untouched by this: a right-hand onset restating a stop the shape
already states, on a shape that needed no justifying, changes nothing anywhere and is swept.

Until an arrival comes such a span has no ring to measure, so it stays open however long it waits
and states its posture at an instant; from the arrival the ordinary member-ring rule takes over,
the arrival itself being a member ring — an arrival that JOINS the span (the lone re-pick above)
carries it from its start through that ring, while one that merely answers and then opens its own
shape leaves the statement emitted at its own instant. A silent-only span that closes with nothing
having arrived dissolves: it is evidence of nothing, and it states nothing anywhere, exactly as a
lone member does. Its notes are then removed by \ref sweepInertClaimedStops rather than saved
stating nothing — this derivation only declines to emit the span; the settle is what takes the
records.

The two facts the CLASS rule cannot re-derive are recorded on the span as this walk resolves them
(\ref ChartShape::silent_member, \ref ChartShape::sounds_in_parts): which members the hand only
CLAIMED, and whether any sounding of the span — its own START included — was less than the shape
whole. Both are questions about slots this walk grouped, and grouping is what it knows, so it states
its own answer once rather than leaving a second scan to reconstruct the grouping from an extent the
closing trim has already shortened. Between them they carry three of the arrival rule's four
triggers, which is why \ref chartShapeArrivals now derives only the one that asks about the extent.

ONE STREAM, THE SAVED ONE. Every stop this walk reads now comes from the fret channel of the stored
note, through the one reader that answers "where is this finger at this moment" — the strike at its
own onset, the carry at the slot its ring crosses, the landing at its arrival. The PRESENTED stream
was a second source for exactly one of those (a strike's own fret), justified by presentation moving
no fret; a second source that can only ever agree is a second statement of one fact, and it went out
with the record unification (user ruling 2026-08-30, N5(a)).

The maintained plain-English spec is "Posture and shape derivation" in
`docs/developer/the-project-lifecycle.md`.

\param saved_notes Note stream in SAVED form, sorted by (position, string); read for its stops, its
                   rings and its fret channels.
\param tempo_map Tempo map supplying the exact beat axis and the meter at each closing onset.

\return The derived spans, the posture table they index, and each silent hold's resolution.
*/
[[nodiscard]] ChartShapes deriveChartShapes(
    const std::vector<ChartNote>& saved_notes, const TempoMap& tempo_map);

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
a trigger — a sounding that reached only part of the shape — so a LANDING SUCCESSOR ([D2]) is not
this trigger at all: a landing is not a sounding, nothing is struck there, and the rings carry on
(user ruling 2026-08-30). A successor is classified by whatever the three triggers below find
inside it, and a chord sliding into chords is therefore a BOX at both ends, joined by sliding
tails.

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
always was — a DISPLAY rule, about what a surface draws of a ring nobody hears. What this rule still
reads off the presented stream is (d)'s attacks, which presentation carries through unchanged.

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

\param presented_notes Notes as drawn, sorted by (position, string).
\param shapes Hand-posture spans, sorted by position (\ref ChartResolutions::shapes).
\param tempo_map Song tempo map, for the signature-exact span end.
\return One flag per shape, in `shapes` order: true where the span renders arpeggio-style.
*/
[[nodiscard]] std::vector<bool> chartShapeArrivals(
    const std::vector<ChartNote>& presented_notes, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map);

} // namespace rock_hero::common::core
