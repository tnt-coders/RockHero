#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// HOW MANY MEMBERS STATE A SHAPE — the whole of the opening law's arithmetic (user ruling
// 2026-08-31, THE ONE-COUNT OPENING LAW). Two is the threshold because a shape is a conjunction:
// one stop is a note, and two held at once is a grip.
//
// Named once because the law is asked at two moments and must be ONE law at both: a slot's own
// member count, and the survivors at a boundary (\ref carry_successor, which is the opening law
// asked there). A number spelled twice is a law free to fork.
constexpr std::size_t g_span_member_threshold = 2;

// THE ACCUMULATION MINIMUM, SIGNED AT THREE (user sighted and signed 2026-09-04, after the
// provisional period ruled 2026-09-01): the minimum an ACCUMULATION opening needs. Statement-
// founded slots keep g_span_member_threshold unconditionally — a strum states its whole shape at
// once and a two-note strum stays a chord box — so this raises only staggered accumulations and
// the boundary successors that are the opening law asked at a boundary.
//
// Two numbers rather than one because they answer two different questions: what STATES a shape
// (two, above — one stop is a note, two held at once is a grip), and how many members must
// accumulate before staggered sound is read as a grip rather than as passing notes. The second is
// a readability judgement and was sighted as one.
//
// It fixes only what SOUND may found implicitly. An AUTHORED two-note span is still owed — the
// span marker never passes through the opening law, it defines — and remains the span-marker
// plan's deliverable (docs/plans/todo/span-marker-redesign.md).
constexpr std::size_t g_accumulation_member_minimum = 3;

// What one string SOUNDS at a slot on the fretting-hand axis. Two onsets fill it and they fill it
// with the same fact: a fretting-hand onset sounds the stop it presses, and a right-hand onset
// sounds the stop the OTHER hand holds under it (\ref chartClaimedStops), because a tap's pitch
// derives from the STOPPED length — tapping above a stop sounds that stop and never the point the
// tapping finger is on.
struct SoundedStop
{
    int fret{0};

    // The record whose own CLAIM is what sounds this stop, where one does: a right-hand onset plays
    // the stop the other hand holds under it, and that hold is a statement the settle beside this
    // walk judges. Empty for a fretting-hand onset, which sounds the fret it presses and claims
    // nothing — so answering with it publishes no reach, because there is no claim to publish it
    // for.
    std::optional<std::size_t> claim_note{};
};

// The sounded stops of one slot, indexed by string. Empty where nothing sounds a fretting-hand stop
// at all: a silently-held member, and a tap the hand states nothing under, whose only pitch is its
// own.
//
// ONE array rather than one per hand, because the fret-match law asks one question of it — is the
// claimed stop sounding here — and two arms asked separately would be two laws free to drift.
using SoundedStops = std::vector<std::optional<SoundedStop>>;

// One CLAIMED stop (\ref chartClaimedStops) resolved against the span it fell inside — a
// silently-held member, or the fretting-hand stop riding a right-hand onset. The note itself names
// no span, so this is the whole of the relationship: which note it came from, which string it
// claims, the beat it claims it at (which the span's own end then judges), and the stop it states.
// The note index rides along so the close can publish which span each claim ended up in, which is
// what the editor's face and its hit box are placed from — and what the inert sweep reads to
// decide whether the record stated anything at all.
struct StopClaim
{
    std::size_t note_index{0};
    std::size_t string_index{0};
    Fraction beat{};
    int fret{0};
};

// Where a slot's sound reaches, indexed by string: the beat a stored ring ends at, empty where the
// string sounds nothing there. What CONTINUES a string's sound, whichever hand made the onset.
using RingEnds = std::vector<std::optional<Fraction>>;

// THE SOUNDING GRIP at one instant, indexed by string: the stop the fretting hand is holding there
// because a ring still sounding says so, empty where nothing sounds one (user ruling 2026-09-03,
// LAW A). Read at a slot from the walk's own ring table, which is independent of every span's
// lifetime — the whole point of the law, since a ring outliving the span that covered it is a
// finger that is demonstrably still down and no span records it.
//
// THE STOP ALONE, because the reach is a property of the ring and the walk already holds it: the
// grip exists exactly where the ring covers the instant, so a second copy of "how far" stored
// beside the stop would be the `OpenSpan::stops` defect again — one fact with two authorities,
// free to disagree.
using SoundingGrips = std::vector<std::optional<int>>;

// One note's fret channel read for [D2]'s two moments, measured from an offset inside the note's
// own ring. The channel is an ordered run of statements — the onset, then every fret-STATING
// keyframe — so both moments come off one walk of it:
//
//   `departure` is the LAST offset at which the channel still states the stop it holds there,
//   which is where the shape stops being RESTATED — what a later strum would have to merge back
//   into (user ruling 2026-08-27, [D2]: "travel splits"); and
//
//   `arrival` is the offset at which the channel next comes to REST, with `fret` the stop it
//   rests on — the landing where the new statement is established, and where the span SPLITS
//   ([D2] amended 2026-08-29).
//
// The two are one travel read for two different questions, which is exactly the shape the
// amendment gave the law: a chord slide keeps the fingers planted, so the rings run continuously
// and the span COVERS the transit, while the statement the hand travelled out of is not restated
// by anything sounding inside it.
struct FretTravel
{
    Fraction departure{};
    Fraction arrival{};
    int fret{0};
};

// The whole of what a fret channel says from one offset onward (\ref statedStopFrom): which stop
// the finger is on there, and the travel it makes away from it. ONE answer rather than two
// queries, because they are one question asked at one moment — and because a caller that had to
// name the stop itself was a SECOND statement of this very fact, free to disagree with the channel
// (user ruling 2026-08-29, F1).
struct StatedStop
{
    // The stop the channel states at the queried offset; empty where it is MID-TRAVEL there. A
    // finger between a departure and the grip it lands on is on no stop at all, so a string caught
    // there is a member of nothing — the same disposition the departure already gives it, reached
    // by the same reading rather than restated beside it.
    std::optional<int> fret{};

    // The travel the channel is on, or is about to make, absent where it never leaves this stop.
    // Its `departure` is the last offset stating `fret` — already behind the query where the
    // channel is mid-travel — and its `arrival` the grip it comes to rest on.
    std::optional<FretTravel> travel{};
};

// THE channel reader: what stop `note` states at `from`, and the travel it makes away from it.
//
// The travel is empty where the channel never leaves that stop: a note with no keyframes, one
// whose keyframes only bend or shake, one whose fret statements all RESTATE the stop (a hold), and
// one whose only fret payload is its slide-out — the release IS the ring's end by definition, so
// the ring already bounds the statement and there is no separate departure to find.
//
// A fret the channel LEAVES AGAIN is a point on the path and never a landing, which is the model's
// own reading of the channel rather than a rule added here: "equal frets are a HOLD, different
// frets are travel" (\ref Keyframe), so a stop the channel comes to rest on is one it restates, or
// the last one it states at all — past which the fret holds flat to the ring's end. That is what
// keeps a continuous multi-fret glide one travel to its end while a glide with a HELD grip between
// its legs states each grip exactly once, and it is also why an offset INSIDE such a glide states
// NOTHING rather than the transit fret the channel was passing through.
//
// `from` is what lets ONE reader serve every question the walk asks of a channel: a span's opening
// strike reads it at the note's own onset, a carry-opened successor's member at the BOUNDARY that
// founded it — a landing or a member's death alike, since both hand rings over — and the carry
// fold-in at whatever later slot the ring crosses. The stop is NOT
// a parameter (user ruling 2026-08-29, F1): a caller naming the stop it expected was this fact
// stated a second time, and where the two disagreed — a finger the channel had already moved — the
// walk went on holding the departed fret.
[[nodiscard]] StatedStop statedStopFrom(const ChartNote& note, const Fraction from)
{
    // The grip the channel opens on, which is the note's own fret at its onset, and the last
    // offset that has restated it.
    int stop = note.fret;
    Fraction held{};
    std::optional<FretTravel> travel;
    for (const Keyframe& keyframe : note.keyframes)
    {
        // Bound to a local so the optional check and the access are provably the same object.
        const std::optional<int>& fret = keyframe.fret;
        if (!fret.has_value())
        {
            continue;
        }
        if (!travel.has_value())
        {
            // Still on the stop: every restatement moves the departure later, and the first
            // differing statement is where the hand leaves.
            if (*fret == stop)
            {
                held = keyframe.offset;
                continue;
            }
            travel = FretTravel{.departure = held, .arrival = keyframe.offset, .fret = *fret};
            continue;
        }
        if (*fret != travel->fret)
        {
            // The channel leaves again, so what looked like a landing was transit; the same glide
            // carries on to the next candidate.
            travel->arrival = keyframe.offset;
            travel->fret = *fret;
            continue;
        }
        // The channel RESTATES the stop it arrived on: the grip is real. A query at or before that
        // landing is answered by the statement this travel leaves, so the walk is done; one past
        // it is asking about the landed grip, and the walk carries on from there.
        if (from < travel->arrival)
        {
            break;
        }
        stop = travel->fret;
        held = keyframe.offset;
        travel.reset();
    }
    // Bound once so the presence test and every read below are provably the same object.
    const std::optional<FretTravel>& leaves = travel;
    if (!leaves.has_value() || from <= leaves->departure)
    {
        return StatedStop{.fret = stop, .travel = travel};
    }
    if (from < leaves->arrival)
    {
        // MID-TRAVEL: the hand left `stop` at the departure and has not reached the landing, so
        // the channel states no stop here at all.
        return StatedStop{.fret = std::nullopt, .travel = travel};
    }
    // Past a landing the channel never restates — the last stop it states at all, which holds flat
    // to the ring's end and travels nowhere after it.
    return StatedStop{.fret = leaves->fret, .travel = std::nullopt};
}

// ONE PER-STRING RECORD: everything a span knows about one of its SOUNDED strings — the stop it
// states there, and how far that statement reaches (N5 (a), user ruling 2026-08-30).
//
// What stood beside this was a second array, `OpenSpan::stops`, holding the same strings' frets;
// the chains held the reach. Two records over one fact produced two defects in two days — review
// R1's carried-chain inference and N5's travel reading blind to a fold-in's glide — because a
// string could be in one and not the other, and every reader had to know which. The seam was the
// flaw, so it is deleted rather than patched: a string is a sounded member of this span exactly
// when it has a chain here.
//
// THE STOP IS WRITTEN AT JOIN, never derived per query. Three acts make a string a member and each
// states its stop once: a MEMBER STRIKE (the fret it presses), a ring-through FOLD-IN (what its
// channel states at the slot it crosses), and a LANDING (the grip it came to rest on). All three
// ask \ref statedStopFrom, which stays the one authority on where a finger is; asking it again per
// query would walk a keyframe channel per string per slot and make this a fourth statement of the
// same fact.
//
// CLAIMS are deliberately NOT here (\ref OpenSpan::claims). A claim is the AUTHORED record — it
// carries provenance the sound never has, and justification, supersession, the inert sweep and the
// published face are all keyed on it — so folding it in would erase the distinction between what
// the hand SOUNDED and what the charter STATED. The posture is built from both in turn, sounded
// stops first and claimed stops after (\ref emit_span).
//
// The law asks two questions of a string and a
// right-hand onset drives them apart, so the two answers are stored apart rather than derived from
// one number that can only be right about one of them:
//
//   `coverage_end` — how far the SHAPE COVERS this string: the fretting hand's last strike on it,
//   bounded by the ring's end and by the LANDING its fret channel comes to rest at, whichever
//   comes first (\ref member_chain, [D2] amended). It is the only thing that bounds the span
//   (\ref spanReach), because how far the shape reaches is a statement about the hand that makes
//   shapes — and a chord slide keeps the fingers planted, so the span covers the transit and
//   splits where the new grip is established.
//
//   `sound_end` — where the string goes on sounding that stop, whichever hand made the onset. It
//   is what CONTINUITY reads (\ref statementInForce): a tap on a member string ends that member's
//   tail UNDERNEATH, with no hand lifting anywhere, so the sound was REPLACED, not silenced, and
//   the statement chains straight through it (user ruling 2026-08-28).
//
// BOTH are bounded by the LANDING and NEITHER by the departure (user ruling 2026-08-29). A
// travelling finger has not let go of anything, so it is no detachment: the statement stays in
// force across the whole glide, and what a mid-travel sounding may do is judged PER MEMBER rather
// than per slot — an open member restruck mid-slide is an interior subset sounding like any other,
// while a stop the shape does not state is a statement the span cannot absorb. That judgment lives
// in \ref slotJoinsShape, over what the channel states at the slot, which is one authority for
// "where is this finger now" instead of a second bound stored here.
//
// The two are equal until a right-hand onset covers the string, and every member strike brings
// them back together. Collapsing them into one field is the mistake to know about: read as sound
// it lets a tapped sixteenth decide a chord's extent, and read as member ring it fractures the
// two-hand run over a held shape at the first tap.
struct RingChain
{
    // The note whose sound wrote this chain — the record both beats below were read from, and what
    // the landing rule then asks for its fret channel, its ring and its drawn identity ([D2]).
    // Carried rather than re-found, for the reason every other answer in this walk is: the walk
    // that wrote the chain is the only thing that knows which sound it belongs to, and a second
    // search for "the last note on this string" would be free to name a different one.
    std::size_t member{0};

    // THE stop this string states inside the span: the fret every posture, every merge comparison
    // and every re-pick test reads. Never optional, because a finger on no stop is a member of
    // nothing — a note caught MID-TRAVEL joins no chain at all rather than joining one with an
    // empty fret (\ref statedStopFrom).
    int stop{0};

    Fraction coverage_end{};
    Fraction sound_end{};

    // True where this chain states the shape's STOP and nothing about its REACH — LAW III's
    // "carried members are extent-inert", carried as a flag instead of as an absence.
    //
    // ONE IDEA, three sites: a chain is inert exactly where the span takes no EVIDENCE from it —
    // where the span did not open BECAUSE of this ring.
    //
    // A ring-through FOLD-IN whose onset a preceding span already covered is let-ring texture
    // crossing in: it tells the posture where a finger is and classifies the span, but letting it
    // bound the extent would let a pedal tone under a passage decide how long that passage's own
    // statements run. A fold-in whose onset is UNCOVERED is the opposite case and the same rule —
    // the span DATES from it (user ruling 2026-08-31, THE ACCUMULATION LAW), so the figure was
    // founded on that ring and it bounds the span like the member it is. \ref open_span_here
    // spends the one comparison that answers both.
    //
    // A growth split INHERITS a chain whose coverage has already run out by the split's instant:
    // what the shape covered there is behind the split, so it is evidence for nothing the new
    // statement says, while the stop it left is still part of the grip the new span holds.
    //
    // A CARRY-OPENED SUCCESSOR writes none: its members' rings are the whole of what states it, so
    // they bound it exactly as a strike's ring bounds the span it opens.
    //
    // A member STRIKE on the string replaces the whole chain, inert one included: the fretting
    // hand has stated that stop itself, so the string now bounds the span like any other member.
    //
    // A FLAG rather than an inference. "A chain written by a note that began before this span"
    // was tried and is dead: review R1 found it reading a growth split's inherited chain as a
    // carry. Only the act that joins a string knows how it joined, so that act says so.
    bool extent_inert{false};
};

// Every string's chain in one span, indexed by string; empty where the span has no sounded member
// on that string.
using RingChains = std::vector<std::optional<RingChain>>;

// The chain a string BOUNDS a span by, or null where it bounds nothing: an empty slot, or one the
// span states a stop through but takes no reach from (\ref RingChain::extent_inert). ONE reader for
// every extent question — the reach, the continuity law, the landing arm — so the inert rule cannot
// be spelled three ways and drift.
//
// Returned as a pointer rather than tested in place so the guard and every read behind it are one
// expression the optional's guarantee provably covers, which is the shape this file uses wherever
// a presence test has to survive later reads.
[[nodiscard]] const RingChain* extentChain(const std::optional<RingChain>& chain)
{
    return chain.has_value() && !chain->extent_inert ? &*chain : nullptr;
}

// WHAT CLOSED A SPAN at a slot, as the walk knows it there. Built once per slot rather than at each
// branch, because every branch that closes a span closes it at the same instant for the same reason
// — six spellings of one fact were what let the callers differ.
struct SpanClose
{
    // The closing event's own onset. It is the span's MUSICAL CLOSE wherever it lands before the
    // statement's own reach (\ref ChartShape::sustain).
    Fraction beat{};

    // The closing slot, where something SOUNDS there: the head rule 12a's display trim keeps its
    // distance from, published to \ref ChartShape::closing_onset. Empty at a slot of held fingers,
    // which sounds nothing to keep a distance from — the shape being replaced then ends exactly
    // where the new one starts, which is also what keeps a landing successor tiled onto its
    // predecessor.
    std::optional<GridPosition> sounding_onset{};
};

// The span being held open: the STOPS a following onset must restate to join it, the silent
// claims inside it so far, how far each member string's coverage and sound now reach (`ring_chain`
// — THE CONTINUITY LAW's whole state), and where an EVENT last stated it (`last_stated_beat`),
// which the display trim floors on. The posture is NOT here: the vector is
// only complete once the span is, which is what lets one span key one posture instead of every
// strum re-keying it.
struct OpenSpan
{
    std::vector<StopClaim> claims;
    GridPosition position;
    Fraction start_beat{};

    // THE ONE RECORD of this span's SOUNDED strings (\ref RingChain): the stop each states, how
    // far the shape covers it, and where its sound reaches at all. A string joins the moment a
    // sound states it — a member strike, a ring-through fold-in, or a landing whose arrived rings
    // ARE the successor's statement ([D2]) — and leaves only where a growth split supersedes the
    // stop it stated. What it never does is leave because its reach ran out: the stop outlives the
    // reach, and the chain goes INERT rather than empty (\ref RingChain::extent_inert).
    //
    // A NON-EMPTY chain array is exactly "some member of this span SOUNDS", and every chain that
    // BOUNDS the span reaches strictly past its own start — which is what makes THE INVARIANT
    // below provable rather than checked.
    RingChains ring_chain;

    // WHERE this span's opening mark draws ([D2] amendment 2, refined by review F7), published to
    // \ref ChartShape::bracket_position for the surfaces.
    //
    // ONE field with one write rule, which is what makes the two cases one law instead of two.
    // Every span an EVENT states seeds it with its own FRONT, because that is where the statement
    // was made and where its rails run from. A CARRY-OPENED SUCCESSOR seeds nothing — nothing at
    // all is stated at a boundary, whether a landing or a member's death opened it — and the first
    // sounding INSIDE it fills the slot, because THE INK FOLLOWS THE SOUND. A successor that never
    // sounds interiorly leaves it empty and draws no opening mark, which is the whole of
    // amendment 2's seamless picture.
    //
    // The walk publishes it because the walk is the only thing that knows which slots this
    // statement covers; re-scanning the note stream for the span's first sounding was the same
    // grouping question asked a second time, off a window that cannot tell a slot the statement
    // RODE from the slot that CLOSED it.
    std::optional<GridPosition> bracket_position{};

    // The last instant at which an EVENT stated this span's shape: its final strum, or the slot
    // whose claims opened or grew it. Empty on a CARRY-OPENED SUCCESSOR and nowhere else — the one
    // span no event states, whichever boundary opened it, since its members are rings that were
    // struck under the statement before it and simply went on ringing.
    //
    // Two things read it, and both are the same fact. Its VALUE is published as
    // \ref ChartShape::stated_extent, because the display trim may not cut a span back behind its
    // own last statement; its PRESENCE is edge (b), because a span stated at no instant, left no
    // room by a close, states nothing either neighbour does not.
    //
    // It is no longer a floor on the derived extent, and that is the whole of what moving the trim
    // out changed here: the close is now the closing EVENT's own onset, which is at or after every
    // instant that stated the span, so a max against this could never bind.
    std::optional<Fraction> last_stated_beat{};

    // True when NOTHING sounds in this span — its stops are empty, so every member is a
    // silently-held stop. Fixed at open: a later onset can join such a span but never fills those
    // stops, so this stays the question "was this shape stated by the hand alone". Asked of the
    // stops rather than of the opening slot's strike count, because a span that GREW out of a
    // sounding one inherits that shape's strings without striking any of them here.
    bool silent_only{false};

    // True once the content a silent-only span was authored in front of has ARRIVED, which is one
    // thing and not two (user ruling 2026-08-27): one of this span's claimed stops SOUNDED inside
    // the span — the standing fret-match law, \ref answersClaim. A silent-only span that closes
    // without one is unjustified, because nothing it could be fronting exists, and it dissolves
    // exactly as a lone member states nothing. Meaningless on a span that opened with sound, which
    // is always emitted.
    //
    //     "a tap should not be able to justify a span on its own, the span should require one of
    //      its HELD frets to be played at some point during the span for it to be justified"
    //     — user, 2026-08-27
    //
    // What that overturned is the picking-hand COVERAGE half: taps on the shape's posture strings
    // used to justify it and carry its extent, and a tap sounds where the TAPPING finger lands, so
    // by itself it is evidence about the other hand and says nothing about whether the stated stops
    // are still down. What answers the quoted rule is not the tap but the STOP UNDER it: a tap
    // harmonic's pitch derives from the stopped length, so a right-hand onset whose held fret is a
    // claimed stop plays that stop as surely as a finger fretting it does (user ruling 2026-08-27,
    // the tap-harmonic arm). That is one law with two ways to sound a stop, not two justifications
    // — \ref SoundedStops carries both and \ref answersClaim compares once — and a tap holding
    // nothing, or holding a stop the shape never claimed, still justifies nothing at all.
    //
    // Arrival is not the same question as EXTENT, and the two are deliberately separate fields: a
    // chord can answer a claim without ever joining the span, which justifies the statement (it is
    // emitted at its own instant) while leaving the hand no longer demonstrably down.
    bool justified{false};

    // The records whose JUSTIFICATION reached this span — the answered claim and, where its own
    // claim is what sounded the stop, the answering record (\ref justify) — held here until the
    // span is EMITTED instead of published as they arrive.
    //
    // That deferral is the whole of what makes \ref emit_span the ONE writer of
    // \ref ChartShapes::claim_shapes: the index a reach names is `derived.shapes.size()`, which is
    // only the span's own index while the span is certain to be pushed, and a span is not — a
    // carry-opened successor a close leaves no room to be drawn in is dropped whole. Publishing as
    // the
    // justification happened made "a justified span is always emitted" a cross-function invariant
    // two arms had to keep in step; publishing at the push makes it a property of the code, since
    // the drop returns before this ledger is read.
    //
    // Never inherited: a successor is a new span, and what reached the span before it was published
    // when that one was emitted.
    std::vector<std::size_t> justified_by;

    // True once some SOUNDING of this span has been less than the shape WHOLE — LAW III's class
    // rule in one comparison (user ruling 2026-08-27, widened to the span's own START by the ruling
    // of 2026-08-28). Written at the one site that knows which span a slot left open, which is what
    // makes the start and the interior one question: the opening slot of a span whose posture
    // CARRIES a string states that carry by striking fewer strings than the shape sounds, and that
    // is the arrival rule's trigger (a) — a partial restrike, a lone re-pick and a carried start
    // are one fact at three widths.
    //
    // A slot that sounds nothing of the fretting hand is outside it either way: it sounds no
    // member, so it is no sounding of the shape. A span OPENING at such a slot needs no answer
    // from here, because every one of them claims a stop its own sound does not state and arrives
    // an arpeggio through \ref ChartShape::silent_member.
    //
    // Recorded HERE rather than re-scanned beside the arrival rule for the reason \ref
    // ChartShape::silent_member is: the answer needs to know WHICH SLOTS this statement covers, and
    // this walk is the only thing that does. What a reader can see is the span's WINDOW, and the
    // closing onset sits exactly ON its end whenever an event closed the span — so a re-derived
    // window cannot tell a slot the statement RODE from the slot that CLOSED it.
    bool sounds_in_parts{false};

    // True on the span \ref carry_successor opens and nowhere else — the one span no EVENT states
    // at its own start ([D2] amendment 2, generalized to the death cause 2026-08-31). Published to
    // \ref ChartShape::carry_opened, where display keys the bracket deferral on it.
    //
    // Not derivable from \ref last_stated_beat, which is empty at a successor's birth and stops
    // being so the moment an interior re-pick states it. The arm that opens one is the only thing
    // that knows, so it is the only thing that says.
    bool carry_opened{false};

    // How this span was founded, and therefore what an arriving new stop does to it
    // (\ref SpanFounding). Fixed at birth and INHERITED by every span that continues this
    // statement — a growth split and a carry-opened successor alike — because a grip sliding to
    // another fret, or a member of it falling silent, changes nothing about how the figure was
    // stated. Published to \ref ChartShape::founding.
    SpanFounding founding{SpanFounding::Statement};
};

// Whether a slot that CONTINUES a span sounded only PART of the shape: fewer of the strings the
// shape SOUNDS than the shape has. Two counts, and the denominator is well defined because a span's
// STOPS are fixed at its open — every split rule in this walk exists to keep the posture constant
// inside one span, which is what makes "the shape" a denominator at all. The amendment does not
// touch that: articulation was never part of the posture, so letting it vary changes nothing about
// which strings the shape holds.
//
// A partial restrike and a lone re-pick need no arm each: they are one fact at two widths, so this
// is asked identically of both. The SOUNDING strings are the whole denominator because a shape
// carrying a claimed member already classifies through \ref ChartShape::silent_member — a claim
// never sounds, so a shape holding one is members-sounding-separately by inspection.
//
// The denominator counts EVERY chain, inert ones included: a carried ring-through member is one of
// the strings the shape sounds, however little it says about the shape's reach. That is what makes
// a strum picking around a still-ringing member a partial sounding at all.
[[nodiscard]] bool partOfShapeStruck(const OpenSpan& span, const std::size_t struck)
{
    const auto sounded = static_cast<std::size_t>(std::ranges::count_if(
        span.ring_chain, [](const std::optional<RingChain>& chain) { return chain.has_value(); }));
    return struck < sounded;
}

// Whether a slot JOINS the standing shape — rule 11's merge, asked of the whole picture, and THE
// ACCUMULATION LAW's absorption asked in the same breath (user ruling 2026-08-31).
//
// ONE comparison, keyed on the FOUNDING (\ref SpanFounding), because the two modes differ about
// exactly one of the three things a slot can say. A stop the shape does not state on a string it
// never held is GROWTH: a simultaneous strike claimed a whole grip, so growth breaks its
// wholeness and the span splits — while an accumulation claimed only that its members arrive
// separately, so growth is that statement continuing and the arrival is ABSORBED, the posture
// gaining a string in place. The other two answers are the same in both modes: a DIFFERENT stop
// on a string the shape states is a finger that moved and splits either kind, and silence about a
// string the shape no longer covers is the statement being over.
//
// Writing the absorbed string into the span needs nothing here: \ref extendRingChain already
// writes every member strike into the chain array, whether or not the span held that string, so
// what used to keep growth out of an open span was this test refusing it and nothing else.
//
// POSITION IS THE WHOLE COMPARISON (RULE 11 AMENDED 2026-08-29): the same strings at the same
// stops restate the shape however they are articulated, so a plain chord, the dead chugs on it and
// the plain chord again are one span. The STOPS the slot's own chains state are what this reads,
// and nothing else about its notes reaches here at all — a span is a FRETTING-HAND statement, and
// palm-mute is the picking hand, dead is pressure, accent and ghost are dynamics.
//
// The ordinary form is EQUALITY, and that is what this mostly is: the ring-through fold-in has
// already filled in every member the slot did not strike, so a partial restrike over still-ringing
// members presents the shape whole and compares equal. Equality falls short in exactly one place,
// and [D2] is what put it there — a member MID-TRAVEL states no stop at all
// (\ref statedStopFrom), so nothing can fold it in, while the span goes on COVERING it all the way
// to its landing.
//
// So a mid-travel sounding is judged PER MEMBER, not per slot (user ruling 2026-08-29). A slot
// that says nothing about a string the shape still covers says nothing to disagree with: an open
// member restruck mid-slide — an open channel never departs — is an interior subset sounding like
// any other, riding the span, flipping the class through \ref OpenSpan::sounds_in_parts and
// leaving the split where it belongs, at the landing. What the span cannot absorb is a STATEMENT
// it does not make: a different stop on a string it states, or a string it does not state at all.
// Those truncate the travelling span here, exactly as they always have.
//
// EVERY chain speaks here, inert ones included, and both arms want that. The STOP a carried
// ring-through member states is part of the grip a strum has to restate; and the reach an inert
// chain records is exactly how long silence about that string still agrees — a fold-in caught
// MID-GLIDE says nothing at its slot, and the shape goes on covering it to its landing, which is
// the per-member mid-travel law reaching the carried members it could not see while their reach
// lived in a second record.
//
// AND THE SPAN IS NOT THE ONLY WITNESS (user ruling 2026-09-03, LAW A). A span knows only its own
// members' rings, and a ring can outlive the span that covered it — a growth split supersedes the
// string its claim moved a finger off, and the ring goes on sounding with no span recording it. To
// that span's successor the string is silent ground, so a strike stating ANOTHER stop on it used to
// read as ordinary growth and be absorbed. It is not growth: a finger was demonstrably still down
// at another fret, so the hand MOVED, which is the same contradiction a member's own stop makes and
// splits either founding for the same reason. \ref SoundingGrips is the witness, read from the
// walk's ring table rather than from any span.
[[nodiscard]] bool slotJoinsShape(
    const OpenSpan& span, const RingChains& slot_chains, const SoundingGrips& sounding_grip,
    const Fraction now)
{
    for (std::size_t string_index = 0; string_index < span.ring_chain.size(); ++string_index)
    {
        // Bound to locals so each presence test and its reads are provably the same object.
        const std::optional<RingChain>& stated = span.ring_chain[string_index];
        const std::optional<RingChain>& here = slot_chains[string_index];
        if (here.has_value())
        {
            if (!stated.has_value())
            {
                // A CONTRADICTION over a FOREIGN ring, and it outranks the founding because it is
                // the same fact as the member contradiction below: a stop still SOUNDING here that
                // this slot restates differently is a finger that moved. Equal stops are the tie
                // doctrine and never split — a string restated where it already is, is the hand
                // holding still. A slot's own fold-in answers with its own stop and so can never
                // contradict itself, which is what makes this arm about foreign rings alone
                // without a second test saying so.
                const std::optional<int>& sounding = sounding_grip[string_index];
                if (sounding.has_value() && *sounding != here->stop)
                {
                    return false;
                }
                // GROWTH: a string the shape does not hold at all. The founding decides.
                if (span.founding == SpanFounding::Statement)
                {
                    return false;
                }
                continue;
            }
            // A CONTRADICTION: a different stop on a string the shape states. The finger moved,
            // and no founding absorbs that.
            if (stated->stop != here->stop)
            {
                return false;
            }
            continue;
        }
        if (!stated.has_value())
        {
            continue;
        }
        // The shape states this string and the slot says nothing about it, which is agreement only
        // while the shape still covers it — past a member's own landing or its ring, silence
        // about it is the statement being over rather than being restated.
        if (!(now < stated->coverage_end))
        {
            return false;
        }
    }
    return true;
}

// Whether a span has any SOUND in it at all — the question \ref OpenSpan::silent_only stores,
// asked at each open so the two opens cannot answer it differently.
[[nodiscard]] bool nothingSounds(const RingChains& chains)
{
    return std::ranges::none_of(
        chains, [](const std::optional<RingChain>& chain) { return chain.has_value(); });
}

// The stop a shape states on one string: the one it SOUNDS there, else the one it CLAIMS there,
// else nothing at all. One reader over both ways a shape can state where a finger is, so a claim
// landing on that string is judged against the shape by one comparison — equal is the same hand
// restating itself, and anything else, an absent stop included, is a stop the shape does not state.
[[nodiscard]] std::optional<int> statedStop(const OpenSpan& span, const std::size_t string_index)
{
    // Bound to a local so the optional check and the access are provably the same object. EVERY
    // chain answers, inert ones included: a carried member's stop is part of the grip whatever it
    // says about the span's reach.
    const std::optional<RingChain>& sounded = span.ring_chain[string_index];
    if (sounded.has_value())
    {
        return sounded->stop;
    }
    const auto stated = std::ranges::find(span.claims, string_index, &StopClaim::string_index);
    return stated == span.claims.end() ? std::nullopt : std::optional<int>{stated->fret};
}

// Whether a slot ANSWERS one claim: it sounds that claim's stop, on that claim's string. THE
// fret-match law, spelled once for the three questions that ask it — the lone re-pick that keeps a
// sounding span (side ruling (ii)), the arrival that justifies a span the hand alone stated, and
// the tap harmonic that answers the very claim its own record makes — so they can never drift into
// different tests of the same thing. A different stop is a different hand: it answers nothing.
//
// SOUNDING is the whole of what it reads, and \ref SoundedStops is where the two ways a stop can
// sound are folded into one fact, so there is one comparison here and no arm to drift.
//
// Every claim's string index is in range by construction (the walk bounds it before pushing one),
// and the sounded array is always the model's full width, so the lookup needs no second bound.
[[nodiscard]] bool answersClaim(const StopClaim& claim, const SoundedStops& sounded)
{
    // Bound to a local so the optional check and the access are provably the same object.
    const std::optional<SoundedStop>& stop = sounded[claim.string_index];
    return stop.has_value() && stop->fret == claim.fret;
}

// The record whose own claim SOUNDS a string's stop here, where one does. Total, so a caller that
// has just matched a claim through \ref answersClaim never re-opens the optional it matched
// through — which is also what keeps the guard and the access one expression.
[[nodiscard]] std::optional<std::size_t> soundingClaimNote(
    const SoundedStops& sounded, const std::size_t string_index)
{
    const std::optional<SoundedStop>& stop = sounded[string_index];
    return stop.has_value() ? stop->claim_note : std::nullopt;
}

// Publishes what a claim-bearing record REACHED: the span being EMITTED, at the index the count is
// about to give it. Called from \ref emit_span alone, past every path that drops a span without
// pushing one, so the index it writes is the span's own by construction rather than by an
// invariant some other arm has to uphold — an unemitted span is reached by nothing.
//
// THE FIRST span a claim reaches keeps its face: a stop the hand goes on holding across a growth
// split is a member of every span it reaches, but it was authored once, at one slot, and its face
// must stay where the charter put it. ONE writer for that rule, because "does this record state
// anything" is a single question for \ref sweepInertClaimedStops and a second spelling of the
// first-reach test would be free to answer it differently.
void publishReach(ChartShapes& derived, const std::size_t note_index)
{
    // Bound to a local so the presence test and the write are provably the same object.
    std::optional<std::size_t>& reach = derived.claim_shapes[note_index];
    if (!reach.has_value())
    {
        reach = derived.shapes.size();
    }
}

// Whether a span is still being ASSEMBLED: stated by the hand alone, and still waiting for the
// content it was authored in front of. Nothing dates such a statement yet, so fingers landing
// inside it are part of the one statement being made rather than a shape change — which is why the
// growth split exempts it. Asked of \ref OpenSpan::justified rather than of \ref
// OpenSpan::silent_only, which is fixed at the open and never clears: once the content has arrived
// the statement is dated exactly as a sounding shape is, and a stop it does not state means the
// same thing there as anywhere else.
[[nodiscard]] bool stillAssembling(const OpenSpan& span)
{
    return span.silent_only && !span.justified;
}

// How far the span's statement reaches: THE CONTINUITY LAW's extent (user ruling 2026-08-27,
// [D3]). The statement is in force while EVERY sounding member goes on stating its stop, so the
// first member to stop stating one bounds the whole span — the extent is the MINIMUM of the
// chains, not the maximum of the rings. Min-extent is not a second rule beside this one; it is
// this rule's box case, which is why the two frets of an unevenly-rung chord end their box
// together, and a member whose finger goes TRAVELLING ([D2]) ends it at the grip it LANDS in
// exactly as one whose ring stops ends it where the ring stops.
//
// The minimum is also the whole of edge (c): where the travels land at DIFFERENT instants, the
// earliest landing is the first member to stop covering, so it ends the span — the successor is
// refused separately, and the truth stays in the members' sliding tails.
//
// A span with nothing sounding in it yet reaches its own start: it was authored in FRONT of the
// content it describes, and there is no ring to measure until that content arrives. That is the
// ONLY way this can answer the start itself — every chain that rides here reaches strictly past
// it — which is what makes "a span with a sounding member is strictly positive" an invariant of
// the walk rather than a check.
[[nodiscard]] Fraction spanReach(const OpenSpan& span)
{
    std::optional<Fraction> reach;
    for (const std::optional<RingChain>& slot : span.ring_chain)
    {
        // Only the chains that BOUND the span, which is LAW III's "carried members are
        // extent-inert" asked in the one place the extent is settled: let-ring texture folded into
        // a posture classifies the span and must never decide how long the passage's own
        // statements run (\ref extentChain).
        const RingChain* const bounding = extentChain(slot);
        if (bounding == nullptr)
        {
            continue;
        }
        // The COVERAGE end and nothing else: this is how far the SHAPE reached, and the other
        // hand's sound covering a string says nothing about that (\ref RingChain).
        const Fraction coverage_end = bounding->coverage_end;
        reach = reach.has_value() ? std::min(*reach, coverage_end) : coverage_end;
    }
    return reach.value_or(span.start_beat);
}

// Whether the span's STATEMENT is still in force at `now` — THE CONTINUITY LAW asked of one
// instant (user ruling 2026-08-27, [D3]), and the one authority for it: the slot that would
// replace the shape, the strum that would merge into it and the lone re-pick that would ride it
// all ask this, so none of them can answer differently.
//
// In force means every member string is CONTINUOUSLY sounding the stop the shape states there
// (\ref RingChain::sound_end): still going through `now`, or ending exactly at `now` with this very
// slot SOUNDING that string again — the strike-into-strike shape repeated strums store, which is
// why the slot's own rings are part of the question rather than a caller's separate test. The
// first member string that simply STOPS, with nothing sounding it at that end, is an authored
// statement of detachment, and the span ends there — at the shape's own reach, which \ref spanReach
// answers separately. A member whose finger is TRAVELLING ([D2] amended) is no detachment: it has
// let go of nothing, so the statement stays in force across the whole glide and fails this test
// only where its ring stops or its landing arrives. What such a member may not do is be RESTATED,
// and that is \ref slotJoinsShape's question over the picture rather than a second bound here.
//
// `sounding_rings` is every onset the slot SOUNDS, whichever hand made it (user ruling 2026-08-28,
// F2): a right-hand onset on a member string ends that member's tail underneath it without the
// hand lifting anywhere, so what happened to the sound is REPLACEMENT, not silence, and a
// statement does not end where nothing went quiet. That is why the two-hand run over a held shape
// is one statement through its own re-picks. A silent hold, by contrast, sounds nothing and stops
// nothing, so it is absent from both readings and can bridge no gap.
//
// Where nothing sounds the question is vacuous and the answer is yes, which is exactly right for
// the two cases that reach it: a shape the hand alone stated waits, unended, for the content it
// was authored in front of, and a successor span whose every sounding stop was superseded states
// the new grip and waits the same way. Both are then governed by justification, not by extent.
//
// A slot that sounds nothing at all (a hand coming down) makes this the strict comparison it has
// always been, because no chain can be continued by a sound that is not there.
[[nodiscard]] bool statementInForce(
    const OpenSpan& span, const Fraction now, const RingEnds& sounding_rings)
{
    for (std::size_t string_index = 0; string_index < span.ring_chain.size(); ++string_index)
    {
        // Only the chains that BOUND the span (\ref extentChain). An inert chain states a stop and
        // no reach, so it can end no statement either: a let-ring carry going quiet under a
        // passage is not the passage detaching, and a growth split's spent inheritance is already
        // behind the statement it rode into.
        const RingChain* const bounding = extentChain(span.ring_chain[string_index]);
        if (bounding == nullptr || now < bounding->sound_end)
        {
            continue;
        }
        if (now != bounding->sound_end || !sounding_rings[string_index].has_value())
        {
            return false;
        }
    }
    return true;
}

// Carries the span's chains over this slot's sound. The whole of this function is the law's split
// (\ref RingChain), written once so no branch can write half of it:
//
//   a MEMBER strike states both facts at once — the shape reaches this statement and so does the
//   string — which is what starts a chain and what every later member strike on that string
//   replaces;
//
//   any OTHER sounding onset on a string already chained carries the SOUND forward and leaves the
//   shape's own reach exactly where the fretting hand left it. That is the tap: it joins no
//   posture, so it cannot say how far the posture reaches, but it does sound the string, so the
//   statement chains through it.
//
// A right-hand onset on a string the shape never held joins nothing at all, exactly as it joins no
// posture. Only ever called where the law has just found the statement in force, so every chain it
// touches was continuous into this slot.
void extendRingChain(
    OpenSpan& span, const RingEnds& sounding_rings, const RingChains& member_strikes)
{
    for (std::size_t string_index = 0; string_index < span.ring_chain.size(); ++string_index)
    {
        // Bound to locals so each optional check and its access are provably the same object.
        std::optional<RingChain>& chain = span.ring_chain[string_index];
        const std::optional<RingChain>& struck = member_strikes[string_index];
        if (struck.has_value())
        {
            chain = *struck;
            continue;
        }
        const std::optional<Fraction>& sound = sounding_rings[string_index];
        // An INERT chain takes nothing from here, deliberately: it says where a finger is and
        // nothing about reach, so carrying a sound forward on it would write a bound the span does
        // not read and cannot mean. Only a member strike above makes such a string extent-bearing
        // again, and it does so by replacing the whole chain.
        if (chain.has_value() && !chain->extent_inert && sound.has_value())
        {
            chain->sound_end = *sound;
        }
    }
}

} // namespace

// The slot walk. Groups are contiguous runs of one grid position, which is the same partition
// presentation and the hold engine use — the stream is sorted by (position, string), so a group is
// an adjacency question and never a search. One stream carries every member, sounding or silent,
// so a slot whose members are all held fingers is simply a step of the walk like any other.
ChartShapes deriveChartShapes(
    const std::vector<ChartNote>& saved_notes, const std::vector<std::optional<int>>& claimed_stops,
    const TempoMap& tempo_map)
{
    ChartShapes derived;
    derived.claim_shapes.assign(saved_notes.size(), std::nullopt);

    // A posture array is indexed by string number, and the model bounds those at
    // g_max_chart_strings — so that constant IS the width, never a quantity read off the input.
    // The derivation runs inside `normalizeChart`, BEFORE `validateChartRules` has refused an
    // out-of-range string, so sizing from the stream (or from the tuning, equally unvalidated
    // there) would let a corrupt document's `"string"` decide an allocation. Every posture from
    // one stream is the same length either way, which is all the dedup below needs to compare
    // held frets alone; a string the tuning does not have simply never fills its slot.
    constexpr auto string_count = static_cast<std::size_t>(g_max_chart_strings);

    // Every onset's exact global beat, once. The whole rule is beat arithmetic — how far members
    // ring, the closing margin, the span's own length — and it stays rational end to end because a
    // span's sustain is a stored-shaped fraction, not a rounded one.
    std::vector<Fraction> onset_beat;
    onset_beat.reserve(saved_notes.size());
    for (const ChartNote& note : saved_notes)
    {
        onset_beat.push_back(beatDistance(tempo_map, GridPosition{}, note.position));
    }
    const auto ring_end_of = [&onset_beat, &saved_notes](const std::size_t index) {
        return onset_beat[index] + saved_notes[index].sustain;
    };

    // ONE STRING'S WHOLE RECORD, read from the stop it holds at `from`: the stop itself, how far
    // the shape COVERS the string, and how far the string goes on SOUNDING that stop
    // (\ref RingChain). ONE reader for every way a string joins a span — the slot where a strike
    // states it, the slot a ring-through CROSSES, and the landing a successor's carried members
    // arrive at — so the three can never measure one channel differently.
    //
    // EMPTY where the channel is MID-TRAVEL at `from`: a finger between the stop it left and the
    // grip it is moving to is on no stop at all, so it is a member of nothing and joins no chain.
    // That is one disposition of one fact rather than a chain carrying an absent fret, which is
    // what makes \ref RingChain::stop a plain int.
    //
    // The travel supplies ONE bound and it is the ARRIVAL ([D2] amended 2026-08-29): a chord slide
    // keeps the fingers planted, so the span covers the transit up to the grip the hand
    // establishes, and a finger still travelling has let go of nothing. A ring that ends first
    // bounds it instead, since a string that has stopped sounding covers nothing and states
    // nothing. The two beats start equal and part only where the OTHER hand sounds the string.
    const auto member_chain = [&saved_notes, &onset_beat, &ring_end_of](
                                  const std::size_t note,
                                  const Fraction from) -> std::optional<RingChain> {
        const Fraction ring = ring_end_of(note);
        const StatedStop stated = statedStopFrom(saved_notes[note], from);
        // Bound once so each presence test and its reads are provably the same object.
        const std::optional<int>& stop = stated.fret;
        const std::optional<FretTravel>& travel = stated.travel;
        if (!stop.has_value())
        {
            return std::nullopt;
        }
        const Fraction reaches =
            travel.has_value() ? std::min(ring, onset_beat[note] + travel->arrival) : ring;
        return RingChain{
            .member = note,
            .stop = *stop,
            .coverage_end = reaches,
            .sound_end = reaches,
            .extent_inert = false,
        };
    };

    std::map<std::vector<std::optional<int>>, std::size_t> posture_indices;
    std::optional<OpenSpan> open;

    // How far the spans emitted so far COVER the beat axis — THE DATING RULE's whole state (user
    // ruling 2026-08-31). A span dates from its earliest member onset not covered by a preceding
    // span, and one comparison against this answers both halves of that: a ring whose onset is
    // uncovered DATES the span it founds and bounds it like the member it is, while one crossing
    // in from covered ground states its stop and no reach. Zero is the honest start — the axis
    // begins at the first measure and nothing precedes it.
    Fraction covered{};

    // HOW FAR A CLOSED SPAN COULD BE DRAWN: rule 12a's margin taken off the closing onset, at that
    // onset's own measure. The trim itself is a DISPLAY rule and lives at the projection now (user
    // ruling 2026-09-04) — this is the ONE derivation question that still turns on drawable room,
    // and it is a question about EXISTENCE rather than extent: whether the landed grip of [D2]'s
    // edge (b) gets a moment of its own is exactly whether a reader could see one.
    //
    // A close that SOUNDS nothing keeps no distance, so a slot of held fingers reaches its own
    // instant.
    const auto drawable_until = [&tempo_map](const SpanClose& close) {
        // Bound to a local so the presence test and the read are provably the same object.
        const std::optional<GridPosition>& head = close.sounding_onset;
        if (!head.has_value())
        {
            return close.beat;
        }
        return close.beat -
               minimumSustainDistanceBeats(tempo_map.timeSignatureAt(head->measure).denominator);
    };

    // THE SUCCESSOR, and it is THE OPENING LAW asked at a boundary (user ruling 2026-08-31, which
    // generalized [D2]'s landing arm rather than putting a second arm beside it): at the instant a
    // span's statement ends, every string still stating a stop and still RINGING past that instant
    // is a member, and two or more of them open a span there.
    //
    // TWO CAUSES, ONE ANSWER. A LANDING was always just one way for carried rings to cross a
    // boundary — the grip a span's travels come to rest in — and a member's DEATH is the other:
    // the continuity law ends the span at the first ring that simply stops, and the survivors go
    // on holding a shape whether or not anything slid. Asking the same question of both is what
    // makes the posture truth criterion provable rather than checked, because the shape a bracket
    // prints is exactly the set that was still ringing for every instant it covers.
    //
    // Nothing here is a second span-opening rule, and it is not a growth split either: it is the
    // opening law itself, at a moment inside rings rather than at a slot. What used to stand here
    // was a scan for the travels' common arrival, with the staggered case refused by name; the
    // boundary the span already computes (\ref spanReach) IS that arrival whenever a landing is
    // what bounds the span, so the scan was the extent stated a second time.
    //
    // The ratified edges survive as CONSEQUENCES:
    //
    //   (a) ONE member travelling takes the same rule by symmetry. The members that stay put keep
    //       their stops and the traveller states its landed one, so the successor brackets and
    //       names the new voicing.
    //   (b) a travel landing straight into a restrike opens nothing. Under tiling this is not a
    //       test here at all: the successor opens at the landing and the restrike closes it a
    //       moment later with no room to be DRAWN in — and a span no EVENT states, with no room of
    //       its own, states nothing and is never emitted (\ref emit_span). The strike's own full
    //       box is the one statement of the new grip (LAW IV — ink has one owner). The same
    //       disposition covers a boundary the walk only reaches after some other statement has
    //       already replaced the one that was travelling.
    //   (c) a pure chord slide whose members land at DIFFERENT instants opens nothing at the
    //       earliest of them, and needs no clause to be refused: a finger mid-glide states no stop
    //       (\ref statedStopFrom), so fewer than two members are stating one. Where a ring that is
    //       NOT travelling survives beside a landed one, they hold a shape and the law opens it —
    //       refusing that would be this walk's own opening rule stated twice.
    //   (d) travels of UNEQUAL distance landing together are included, because nothing here asks
    //       how far a finger moved — only where it was last stated and where it states next.
    //
    // TERMINATION is a property of the boundary, not a guard: a successor starts strictly later
    // than its predecessor (every chain that bounds a span reaches strictly past its start), and
    // every member of it rings strictly past its own start, so a chain of successors climbs and
    // runs out of rings.
    const auto carry_successor = [&saved_notes, &onset_beat, &tempo_map, &member_chain](
                                     const OpenSpan& span) -> std::optional<OpenSpan> {
        const Fraction boundary = spanReach(span);
        if (!(span.start_beat < boundary))
        {
            // A statement that reached nowhere hands nothing on: a shape the hand alone stated,
            // still waiting for its content, has no rings to survive it.
            return std::nullopt;
        }
        // The whole of what each surviving string states at the boundary, read from the ONE
        // channel authority at that instant — its stop, and the reach that stop's own ring gives
        // it. Its bracket digits print these stops, and a slot restating them restates the shape.
        //
        // A FULL restrike of the surviving grip therefore MERGES into the successor (RULE 11
        // AMENDED 2026-08-29, corollary 2): under position-only continuation there is nothing left
        // to refuse it with, and the strike's own box comes from the display law.
        //
        // NOT inert: these rings ARE this span's statement, so they bound it exactly as a strike's
        // own ring bounds the span it opens ([D2]). EVERY chain of the predecessor is asked, inert
        // ones included — a carried ring that was texture under the statement that just ended is a
        // ring like any other once it is what the new statement is made of.
        RingChains chains(span.ring_chain.size());
        std::size_t members = 0;
        // The note the boundary's grid position is advanced FROM. Any surviving member serves: its
        // own onset is provably at or before the boundary, since its ring reaches past it.
        std::optional<std::size_t> anchor;
        for (std::size_t string_index = 0; string_index < span.ring_chain.size(); ++string_index)
        {
            // Bound to a local so the presence test and every read are provably the same object.
            const std::optional<RingChain>& slot = span.ring_chain[string_index];
            if (!slot.has_value())
            {
                continue;
            }
            const std::size_t note = slot->member;
            const std::optional<RingChain> survives =
                member_chain(note, std::max(Fraction{}, boundary - onset_beat[note]));
            // Nothing here asks WHY the string is still sounding — a finger that landed and a
            // finger that never moved answer alike — and a channel caught MID-TRAVEL states no
            // stop, which is how the staggered slide refuses itself.
            if (!survives.has_value() || !(boundary < survives->sound_end))
            {
                continue;
            }
            chains[string_index] = survives;
            anchor = note;
            ++members;
        }
        // Bound once so the presence test and the read below are provably the same object.
        const std::optional<std::size_t>& from = anchor;
        // The boundary successor is the opening law asked at a boundary, and its members arrive as
        // survivors rather than a strum — accumulation-natured — so it reads the accumulation
        // minimum.
        if (members < g_accumulation_member_minimum || !from.has_value())
        {
            return std::nullopt;
        }
        const GridPosition position = advanceGridPosition(
            tempo_map, saved_notes[*from].position, boundary - onset_beat[*from]);
        return OpenSpan{
            // REVIEW F5 (chart-ruleset.md, [D2] amended 2026-08-29): "a silently-held finger is
            // one of the fingers that slid or stayed, so the predecessor's un-superseded claims
            // RIDE into the successor exactly as the growth split they mirror carries them."
            //
            // No supersession filter is needed to say "un-superseded" here, and that is the growth
            // split's own rule rather than an omission: what supersedes a claim is a stop the new
            // statement makes on that string, and \ref emit_span already leaves a claim alone
            // wherever the sound states the string.
            .claims = span.claims,
            .position = position,
            .start_beat = boundary,
            .ring_chain = std::move(chains),
            // A boundary states nothing, so there is nothing to anchor an opening mark to yet: the
            // first sounding INSIDE the successor fills this, and one that never sounds draws no
            // mark at all ([D2] amendment 2, \ref OpenSpan::bracket_position).
            .bracket_position = std::nullopt,
            // No event states a successor (\ref OpenSpan::last_stated_beat): its members are rings
            // struck under the statement before it.
            .last_stated_beat = std::nullopt,
            .silent_only = false,
            .justified = false,
            // Nothing has reached this span yet: what reached the one it continues was published
            // when that span was emitted, a moment before this one opens.
            .justified_by = {},
            // A LANDING IS NOT A SOUNDING (user ruling 2026-08-30), and neither is a death: the
            // successor classifies by the ordinary triggers, like every other span. What stood
            // here was the constant `true` — trigger (a) "at its purest", honest only while a
            // successor could never be strummed. Corollary 2 ended that world (a full restrike of
            // the surviving grip MERGES), and the constant became a lie: a chord sliding into
            // chords was arriving an arpeggio at every landing, where the published picture is
            // boxes joined by sliding tails.
            //
            // Nothing sounds at a boundary — the rings simply continue — so there is no sounding
            // to be partial, and the walk's own guard already says exactly that: `struck > 0` is
            // what makes a slot a sounding of the shape at all, and a boundary has no slot. The
            // three triggers that CAN fire inside a successor go on firing from where they always
            // did: an interior partial sounding through the walk's one comparison below, an
            // inherited claim through \ref ChartShape::silent_member, and a right-hand onset
            // through \ref chartShapeArrivals.
            .sounds_in_parts = false,
            // The one site that sets it, because this is the one arm that opens a span on CARRIED
            // RINGS (\ref OpenSpan::carry_opened).
            .carry_opened = true,
            // A successor continues the statement it grew out of, so it goes on being the kind of
            // statement that was made (\ref SpanFounding).
            .founding = span.founding,
        };
    };

    // Emits the held span and keys its posture, consuming it. A span closed by a following event
    // ends AT that event's own onset — THE MUSICAL CLOSE (\ref ChartShape::sustain), which rule
    // 12a's display margin no longer touches. What the walk still owns of that margin is one
    // question, and it is about EXISTENCE: a carry-opened successor a close leaves no room to be
    // DRAWN in is not emitted at all ([D2] edge (b)).
    //
    // The posture is built HERE rather than at each onset because the span owns extent and a
    // silent claim is judged against it: the vector is only complete once the span is. Keying it
    // once per span rather than once per strum is what that buys back.
    //
    // Not called directly by the walk: \ref close_span below is the close, and this is the one act
    // it repeats when a span hands it the grip its travels landed in.
    const auto emit_span = [&derived,
                            &posture_indices,
                            &open,
                            &saved_notes,
                            &onset_beat,
                            &covered,
                            &drawable_until](const std::optional<SpanClose> close) {
        if (!open.has_value())
        {
            return;
        }
        // A shape the hand alone stated, with nothing arriving to justify it, is EVIDENCE of
        // nothing: no sounding note ever played one of the frets it claims, so the passage it was
        // authored in front of does not exist. It dissolves rather than printing a posture over
        // silence, which is the same nothing a lone member states. A run of taps over it dissolves
        // it too, unless one of them HOLDS a stop the shape claims — the shape's own held frets are
        // what have to be heard, and the stop under a tap is heard through it (the tap-harmonic
        // arm). What happens to the NOTES is not decided here: this walk only declines to emit
        // the span, and the settle beside it (\ref sweepInertClaimedStops) is what then removes
        // every hold this answer left reaching nothing.
        if (open->silent_only && !open->justified)
        {
            open.reset();
            return;
        }
        // THE MUSICAL CLOSE: the instant this statement actually ended. Two arms and one
        // comparison — where the statement simply RAN OUT it is the shape's own reach
        // (\ref spanReach), and where an EVENT closed the span it is that event's own onset,
        // because the hand demonstrably moved there. The EARLIER of the two, since storing the
        // reach at an event-closed seam would claim grip past a proven hand move and storing the
        // onset at a reach-closed one would claim grip through proven silence.
        //
        // THE INVARIANT falls out of that arithmetic rather than being enforced: both instants are
        // at or after the span's own start — every chain reaches strictly past it, and no slot
        // closes a span that opened after it — so the close answers the start only where the reach
        // does, which is a span nothing sounds in. That is the honest zero, and it is the one the
        // exact-adjacency fallback used to have to reconstruct after the trim had taken the length
        // away.
        const Fraction reach = spanReach(*open);
        const Fraction end = close.has_value() ? std::min(reach, close->beat) : reach;
        // [D2]'s EDGE (b), and the one derivation question that still reads the display margin. A
        // CARRY-OPENED SUCCESSOR is stated at no instant at all — a landing and a member's death
        // state nothing alike: it is the continuation of rings the span before it already covers,
        // and its whole content is that the surviving grip has A MOMENT OF ITS OWN. Whether it gets
        // one is therefore whether a reader could SEE one, so this is measured against drawable
        // room on purpose: a glide straight into a restrike lands exactly one margin before the
        // note it glides into, so the successor opens with nothing to show and what it would have
        // stated is already stated on both sides — the predecessor covers the transit and the next
        // event's own mark states the grip.
        //
        // The same answer covers a landing the walk reaches only after something else has replaced
        // the statement that was travelling: the successor opens behind the close, so no room at
        // all is left. Spans an EVENT states have no such test — a statement made at an instant is
        // drawn however crowded, which the trim's own floor at the projection is what keeps.
        if (!open->last_stated_beat.has_value() && close.has_value() &&
            !(open->start_beat < drawable_until(*close)))
        {
            open.reset();
            return;
        }
        // THE POSTURE IS BUILT IN TWO PHASES, because a shape states a stop in two ways and the
        // difference is real. The SOUNDED half is read straight off the one per-string record: the
        // stop each chain states, inert chains included, since a carried ring-through member's
        // finger is as much part of the grip as a struck one's. The CLAIMED half follows, from the
        // authored claims the sound cannot speak for.
        std::vector<std::optional<int>> frets(open->ring_chain.size());
        for (std::size_t string_index = 0; string_index < open->ring_chain.size(); ++string_index)
        {
            // Bound to a local so the presence test and the read are provably the same object.
            const std::optional<RingChain>& chain = open->ring_chain[string_index];
            if (chain.has_value())
            {
                frets[string_index] = chain->stop;
            }
        }
        // THE CLAIMED HALF: what the notes could not say. Every claim standing on a span STATES
        // something — the attach below is where that is judged, at the one moment it is askable —
        // so the only question left here is the span's own END.
        //
        // A claim authored past the span's own end is inert: that gap is after the shape stopped
        // sounding, and the mark the fret would print under never reaches it. "Past the end" is
        // asked as "not strictly inside, and not AT the start". The second half is not an
        // exception carved out for a degenerate case: a span opened by held fingers alone starts
        // and ends together until sound attaches to it, and its own opening members are exactly
        // the claims sitting at that instant. Excluding them would let a posture open and then
        // state nothing.
        //
        // The posture slot may ALREADY be filled by sound and the claim still state it, which the
        // one record is what makes visible: the arrival that answers a claim carries the span from
        // its start through its own ring, so it writes a chain on a string the hold is what put in
        // the posture. Both name the same fret — a re-pick at any other stop splits the span
        // rather than reaching here — so the claim adds its FACE and its silent-member flag over a
        // stop the sound now also states. That is the reported case the whole record exists for.
        bool silent_member = false;
        for (const StopClaim& claim : open->claims)
        {
            const bool inside = claim.beat < end || claim.beat == open->start_beat;
            if (!inside)
            {
                continue;
            }
            silent_member = true;
            // Bound to a local so the presence test and the write are provably the same object.
            std::optional<int>& fret = frets[claim.string_index];
            if (!fret.has_value())
            {
                fret = claim.fret;
            }
            // Which span this stop reached: the mark that prints it draws at this span's opening
            // mark, and a hold with no entry anywhere draws nowhere (\ref publishReach).
            publishReach(derived, claim.note_index);
        }
        // And what reached it by JUSTIFYING it (\ref OpenSpan::justified_by), published here for
        // the same reason at the same moment: a record whose answering is the only thing keeping
        // this span alive reaches it even where the extent above leaves it outside, and the drop
        // paths returned long before this line, so no reach can name a span that is never pushed.
        for (const std::size_t note_index : open->justified_by)
        {
            publishReach(derived, note_index);
        }
        const auto [entry, inserted] = posture_indices.try_emplace(frets, derived.postures.size());
        if (inserted)
        {
            derived.postures.push_back(ChartPosture{.frets = std::move(frets)});
        }
        derived.shapes.push_back(
            ChartShape{
                .position = open->position,
                .sustain = end - open->start_beat,
                // How far the span's own STATEMENTS reach, for the display trim to floor on
                // (\ref ChartShape::stated_extent). Capped at the close, because a statement
                // recorded past the span's own end is one the span does not cover — the two beats
                // part only where a right-hand onset carried a member's SOUND past where the
                // fretting hand's coverage stopped, and the coverage is what the span is.
                .stated_extent = std::min(open->last_stated_beat.value_or(open->start_beat), end) -
                                 open->start_beat,
                .closing_onset =
                    close.has_value() ? close->sounding_onset : std::optional<GridPosition>{},
                .posture = entry->second,
                .silent_member = silent_member,
                .sounds_in_parts = open->sounds_in_parts,
                .carry_opened = open->carry_opened,
                .founding = open->founding,
                .bracket_position = open->bracket_position,
            });
        // THE COVERAGE FRONTIER, and the whole of what THE DATING RULE needs (user ruling
        // 2026-08-31). A span dates from its earliest member onset NOT COVERED by a preceding
        // span, so the one thing the opening below has to know is how far the spans already
        // emitted reach — which is exactly this running maximum, written by the one act that
        // emits. Never a scan back over `derived.shapes`: a span that opens is asking about
        // ground, not about a neighbour, and a maximum is what ground means when a landing
        // successor tiles onto its predecessor.
        //
        // THE MUSICAL CLOSE is what covers, not a drawn extent (user ruling 2026-09-04). Ground a
        // statement held is ground it held; while this frontier carried rule 12a's trim, the last
        // margin of every closed span read as OPEN — so a ring struck at an interior slot inside
        // that margin was judged uncovered, and went on to DATE and BOUND the next span as if the
        // statement before it had never reached it.
        covered = std::max(covered, end);
        open.reset();
    };

    // The close itself: it emits the standing span and then runs straight on into the shape its
    // surviving rings hold, which the same close emits under the same bound. TILING is what makes
    // this one loop and no parked state at all (user amendment 2026-08-29): a successor opens
    // exactly where its predecessor closes, and the walk never has to remember a grip it has not
    // reached — where some other statement replaced the travelling one first, the successor opens
    // behind the close with no room and is not emitted at all (\ref emit_span). One law, every
    // disposition.
    //
    // `close` is the event this close happens at, absent only at the end of the stream.
    const auto close_span =
        [&open, &carry_successor, &emit_span](const std::optional<SpanClose> close) {
            while (open.has_value())
            {
                // Read before the close consumes the span it is read from.
                std::optional<OpenSpan> successor = carry_successor(*open);
                emit_span(close);
                if (!successor.has_value())
                {
                    return;
                }
                open = std::move(successor);
            }
        };

    // The hand-off, done where it actually happens: a statement whose coverage has run out by this
    // instant is over, and where two or more of its members ring on past that instant the shape
    // they hold is what STANDS from there. Run before the slot is judged, so every branch below
    // sees the statement that is really standing here — which is the whole of what lets a lone
    // re-pick of a surviving member ride the successor exactly as it rides any other span (review
    // F7). Without it the successor could only be born at a close, and the slot that caused the
    // close was the very one that wanted to ride it.
    //
    // The predecessor is emitted with NO closing event, because nothing at this slot is what ended
    // it: it ended at its own boundary, and the successor tiles onto that instant. The slot's own
    // event closes whatever span is standing when the branches below run, which from here on is the
    // successor.
    //
    // The reach test is the gate as well as the law — a span whose coverage still runs has handed
    // nothing on, so nothing else here is asked of a span that is simply still going. A successor
    // starts AT that reach by construction, so "has the hand-off already happened" needs no test
    // of its own: the loop's own condition is what it would have asked.
    //
    // ONE AUTHORITY DECIDES A DEATH (user ruling 2026-08-31, reviews #1 and #11). The reach says
    // where a boundary COULD fall; whether the statement actually ENDED there is THE CONTINUITY
    // LAW's own question, and \ref statementInForce is the one thing that answers it — a ring
    // ending exactly at its own same-string restrike is a REPLACEMENT and no death at all, which
    // is the strike-into-strike shape a stored chug chain has, and that law already says so. What
    // stood here was a second reading of the very same adjacency, spelled over the slot's rings
    // beside this one; one fact with two authorities is the defect, so the clause is DELETED
    // rather than corrected. It is also why the gate reads the slot's own rings, which is why this
    // runs after the group is scanned and not before it.
    //
    // A LANDING is judged by that same one reading, where the deleted clause insisted on refusing
    // it. Nothing observable turns on the difference, and the chart's own encoding is why: a
    // fret-stating keyframe never sits on a later onset of its own string, so a glide's arrival
    // lands one margin BEFORE the note it glides into and no slot ever sounds a member at its
    // landing instant. Edge (b) is left exactly where it was — a successor opened at the landing
    // and then left no room.
    const auto settle_successor =
        [&open, &carry_successor, &emit_span](const Fraction now, const RingEnds& sounding_rings) {
            while (open.has_value() && !(now < spanReach(*open)) &&
                   !statementInForce(*open, now, sounding_rings))
            {
                std::optional<OpenSpan> successor = carry_successor(*open);
                if (!successor.has_value())
                {
                    return;
                }
                emit_span(std::nullopt);
                open = std::move(successor);
            }
        };

    // Answers a waiting span with this slot's sounded stops, and publishes what the justification
    // thereby REACHED. The two are one act: a claim whose answer is the only thing keeping a span
    // alive states exactly as much as a member does — take that record away and the span dissolves
    // — so the settle beside this walk (\ref sweepInertClaimedStops) can go on asking its single
    // question, "does the derived face exist", instead of growing a second rule about justification
    // that would then have to be kept in step with this one (user ruling 2026-08-27).
    //
    // BOTH RECORDS OF THE ONE ACT reach the span, because taking EITHER away dissolves it (user
    // ruling 2026-08-31, blocker 3). The ANSWERED claim is the stop the shape states and the
    // evidence it was waiting for; it reaches even where the span's own extent leaves it outside —
    // a shape the hand alone stated reaches its own start, so a finger that joined it later and was
    // then played is inside nothing the close can see. The ANSWERING record reaches too where its
    // OWN claim is what sounded the stop (the tap-harmonic arm); a fretting-hand arrival presses
    // the fret it names, claims nothing, and has no face to publish.
    //
    // WHAT reached the span is recorded on the span (\ref OpenSpan::justified_by) and published
    // when it is EMITTED, rather than written into the ledger here. The index a reach names is the
    // one the count is about to give this span, which is its index only if the span is pushed at
    // all — and a carry-opened successor a close leaves no room to be drawn in is dropped whole. So
    // the publication rides the push, which makes \ref emit_span the one writer of
    // \ref ChartShapes::claim_shapes and leaves "a justified span is always emitted" nothing this
    // ledger has to rest on.
    const auto justify = [](OpenSpan& span, const SoundedStops& sounded) {
        for (const StopClaim& claim : span.claims)
        {
            if (!answersClaim(claim, sounded))
            {
                continue;
            }
            span.justified = true;
            span.justified_by.push_back(claim.note_index);
            if (const std::optional<std::size_t> answering =
                    soundingClaimNote(sounded, claim.string_index);
                answering.has_value())
            {
                span.justified_by.push_back(*answering);
            }
        }
    };

    // A string number the posture array can actually carry. Bounded here because this runs before
    // validation has refused an impossible one, so a corrupt document's `"string"` must never
    // decide an index — and a note naming one is not a member of anything either, which is why
    // every count and every attach below asks the same question.
    const auto note_string_index = [](const ChartNote& note) -> std::optional<std::size_t> {
        if (note.string < 1 || note.string > g_max_chart_strings)
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(note.string - 1);
    };

    // THE WALK'S ONE SOUNDING TABLE: the last note sounded per string, carried forward as the walk
    // advances and belonging to no span at all. Two laws read it, and they are two questions about
    // one fact — where is this string's sound now, and at what stop.
    //
    //   THE RING-THROUGH RULE: a note whose tail crosses a chord's onset on an un-struck string is
    //   still sounding, so its held fret joins the derived posture — and the arrival rule then
    //   renders the partly-struck span as an arpeggio.
    //
    //   THE SOUNDING GRIP (user ruling 2026-09-03, LAW A): a ring can outlive the span that
    //   covered it, so the span's own chains are not a complete record of what the hand is holding
    //   — and a slot restating such a string at another stop is the finger MOVING, whatever the
    //   standing span thinks (\ref SoundingGrips, \ref slotJoinsShape).
    //
    // Indexes rather than pointers, because both readings need the NOTE — its stop, its ring and
    // its fret channel, all read from the one record by \ref member_chain. Right-hand onsets and
    // silent holds stay out of it (see the update site at the loop's foot), and that is right for
    // BOTH readings: a tap sounds the stop the OTHER hand holds, so it asserts no grip of its own,
    // and a silent hold has no ring to assert one with.
    std::vector<std::optional<std::size_t>> ringing(string_count);

    // WHERE EACH STRING'S CURRENT GRIP WAS ESTABLISHED — the junction instants LAW A records for
    // the dating rule's second bound (user sighting 2026-09-03). A strike replacing a DIFFERENT
    // sounding stop is the finger observably moving, whether or not any span stands to refuse the
    // join at that slot — and a span that only MUSTERS its minimum later must not back-date its
    // front across that instant, because the fronted bracket asserts its whole grip from its
    // start and the string audibly held another stop before it. Zero is the honest floor: an
    // unestablished string bounds nothing, and a same-fret restatement is the tie doctrine and
    // records nothing.
    std::vector<Fraction> grip_established(string_count);

    // SIDE RULING (ii), NARROWED 2026-08-27 by THE CONTINUITY LAW ([D3]): a lone re-pick of a
    // string the open span already holds does not leave the shape. Any single-string onset used to
    // close the span, which killed a held chord at the exact moment a broken figure re-picked one
    // of its own members — and every fact needed to know better was already in the stream. Nothing
    // here is authored: it is the derivation reading what it had.
    //
    // TWO conditions now, where there were three. What the narrowing deleted is the third — the
    // PRESENTED-ring witness, a second reading of "the hand has not left the shape" that this rule
    // kept for itself. The continuity law states that fact once for every branch: an ADJACENT
    // re-pick IS continuity (the re-picked string's own stored ring ends exactly at this onset,
    // which \ref statementInForce reads off the slot's own rings), and a GAP re-pick arrives after
    // a stored statement of detachment that has already ended the span, so it can never be in
    // force. The witness answered the same question with worse evidence, and it is gone.
    const auto lone_repick_continues = [](const OpenSpan& span,
                                          const RingChains& member_strikes,
                                          const SoundedStops& sounded,
                                          const RingEnds& sounding_rings,
                                          const Fraction now) {
        // A `struck == 1` onset fills exactly one slot, which this finds without the walk having to
        // carry it out of the group loop.
        std::size_t struck_index = member_strikes.size();
        for (std::size_t string_index = 0; string_index < member_strikes.size(); ++string_index)
        {
            if (member_strikes[string_index].has_value())
            {
                struck_index = string_index;
            }
        }
        if (struck_index == member_strikes.size())
        {
            return false;
        }
        // 1. The string is a member of the open shape, at the STOP the shape states there — ONE
        //    fret comparison for every member, however the shape came to state it (RULE 11 AMENDED
        //    2026-08-29). A re-pick at a different stop is a different hand and splits; a re-pick
        //    at the same one rides whatever it is articulated with, because a span is a
        //    fretting-hand statement and palm-mute, dead, accent and ghost move no finger.
        //
        //    WHAT DIED HERE is the carried-chain inference — "a chain written by a note that began
        //    before this span is a carried ring", so compare its fret rather than its whole record.
        //    It existed only to give carried members (a landing successor's whole posture) the
        //    fret test while struck members took the articulation test, and review R1 found it
        //    reading a GROWTH SPLIT's inherited chain as a carry. With one comparison for all
        //    three shapes of member there is nothing left for it to distinguish, so it is deleted
        //    rather than corrected; the growth-split figure it misjudged — a silent hold splitting
        //    a ringing chord, a member re-picked at the same fret palm-muted — now continues the
        //    one span, which is what the amendment says it always was.
        //
        //    A member the chart HOLDS silently never sounded, so no stop of the shape's own sound
        //    states it; the authored claim is the whole test there, and this is where a silent
        //    hold and side ruling (ii) compose into the case the record exists for. The claim IS
        //    the whole test, which means all of it: a re-pick at a stop the claim does not state
        //    would let an authored fret outlive the note that contradicts it.
        //
        //    EVERY chain answers, inert ones included: a carried ring-through member is a member of
        //    the shape, so re-picking it at the stop it states rides the span like any other.
        const std::optional<RingChain>& held = span.ring_chain[struck_index];
        const std::optional<RingChain>& repick = member_strikes[struck_index];
        bool member = false;
        if (held.has_value())
        {
            member = repick.has_value() && held->stop == repick->stop;
        }
        else
        {
            // The claim law, asked of the RE-PICKED string alone. Scoped to it rather than run over
            // the whole slot, because the sounded stops now carry the other hand too: a
            // held-carrying tap elsewhere in this slot answers claims of its own (the tap-harmonic
            // arm), and this question is only ever about whether the string being re-picked is a
            // member. Same law, one string.
            member =
                std::ranges::any_of(span.claims, [&sounded, struck_index](const StopClaim& claim) {
                    return claim.string_index == struck_index && answersClaim(claim, sounded);
                });
        }
        if (!member)
        {
            return false;
        }
        // 2. The shape's statement is still in force — the continuity law, and the whole of what
        //    the deleted witness was reaching for. For a span with sound in it that means every
        //    sounding member is continuous here, the re-picked string included: its own ring
        //    reaching through this onset, or ending exactly at it and being struck again right
        //    now, which is this figure's own shape. For a span the hand alone stated there is no
        //    ring at all, so the question is vacuous and it waits, unended, for the content it was
        //    authored in front of — and this is that content ARRIVING.
        return statementInForce(span, now, sounding_rings);
    };

    std::size_t index = 0;
    while (index < saved_notes.size())
    {
        const GridPosition position = saved_notes[index].position;
        const Fraction position_beat = onset_beat[index];

        std::size_t onset_end = index;
        // The continuity law's two readings of this slot, and they differ by exactly one question.
        // `sounding_rings` is where every onset here reaches, whichever hand made it — what
        // CONTINUES a string's sound, read from the same onset set the ring's own bound reads.
        // `member_strikes` is the fretting hand's alone — what makes a string a MEMBER and what
        // WRITES its chain, first strike and every one after, since a right-hand onset joins no
        // posture and so bounds no span. Both are empty for a slot of silent holds, which sounds
        // nothing and continues nothing.
        RingEnds sounding_rings(string_count);
        RingChains member_strikes(string_count);
        // Rule 10 counts MEMBERS (user ruling 2026-08-27): a member is a sounding fretting-hand
        // onset here or a stop the hand CLAIMS here, so one sound plus one held finger states a
        // shape, two held fingers state one, and a lone member of either kind states none. A
        // silent hold was never a strike and still is not — what changed is that a shape is not
        // made of strikes, it is made of stops.
        //
        // A claimed stop is a claimed stop however it is written down: a tap carrying a held fret
        // is one record stating two facts at one slot, and it counts here exactly as the silent
        // hold it would otherwise have to be written as (user ruling 2026-08-27). That is also
        // what makes the same-instant justification below reachable at all — the tap and the claim
        // it articulates cannot collide, because they ARE one note.
        //
        // The slot's claimed stops are collected here as the claims they will become, rather than
        // counted here and re-found later: the branch below asks what they claim, and whatever span
        // this slot leaves open then takes them.
        std::size_t struck = 0;
        std::vector<StopClaim> slot_claims;
        SoundedStops sounded(string_count);
        while (onset_end < saved_notes.size() && saved_notes[onset_end].position == position)
        {
            const ChartNote& member = saved_notes[onset_end];
            const std::optional<std::size_t> string_index = note_string_index(member);
            // The fretting hand's stop, in whichever shape the record states it: a silently-held
            // member IS the statement, and a stop the hand holds under a right-hand onset rides
            // that note as its held fret. One query for both (\ref chartClaimedStops), so the
            // membership count, the justification and the fret-match can never read them
            // differently — and it is the RESOLVED claim, since a pull-off off a right-hand onset
            // STATES the stop the other hand was holding under it (user ruling 2026-08-31, DERIVED
            // HELD). The walk reads the resolution and never the stored field, which is what keeps
            // a derived stop and an authored one the same kind of member here.
            const std::optional<int> claim = claimed_stops[onset_end];
            // Where this string's sound reaches from here, read from the STORED ring: the
            // continuity law is about how long the string actually sounds, which is the sound
            // behind the picture rather than the picture.
            //
            // THE SET IS EVERY SOUNDING ONSET, whichever hand made it (user ruling 2026-08-28,
            // F2). The warrant is what physically happens to the member's tail at a right-hand
            // onset: the tap ends that tail UNDERNEATH, without the hand lifting anywhere, so the
            // sound was REPLACED and not silenced — and a statement of detachment is a statement
            // about sound STOPPING. It follows that a tap at a different fret on a member string
            // chains the statement straight through, which is the two-hand run over a held shape.
            // What a right-hand onset must never do is say how FAR the shape reaches (\ref
            // RingChain), and that is a separate field rather than a separate set.
            //
            // A silent hold sounds nothing and stops nothing, so it is in neither reading; a note
            // on a string the model cannot carry is a member of nothing and bounds nothing.
            if (string_index.has_value() && !silentHold(member.attack))
            {
                sounding_rings[*string_index] = ring_end_of(onset_end);
            }
            if (claim.has_value() && string_index.has_value())
            {
                slot_claims.push_back(
                    StopClaim{
                        .note_index = onset_end,
                        .string_index = *string_index,
                        .beat = position_beat,
                        .fret = *claim,
                    });
            }
            if (!silentHold(member.attack) && !rightHandOnset(member.attack))
            {
                // Right-hand onsets are invisible to span derivation: they join no posture and
                // extend no ring, so a mixed onset is judged by its fretting-hand members alone.
                if (string_index.has_value())
                {
                    // A MEMBER's whole record: the stop the fretting hand states on this string
                    // and how far that statement reaches. The strike states the stop from its own
                    // onset, so the channel is read there — the same reader, at its own moment,
                    // that a carried member is read at where its ring crosses a slot and a landing
                    // is read at where it comes to rest (\ref statedStopFrom). Naming the fret at
                    // this call site instead was the same fact stated twice, which is what the F1
                    // unification took out of every other site; the last copy of it was here,
                    // reading the PRESENTED note for a number the stored channel already gives.
                    //
                    // A channel is never mid-travel at offset zero, so this always states a stop;
                    // the guard is the type's own, not a case.
                    const std::optional<RingChain> struck_chain =
                        member_chain(onset_end, Fraction{});
                    // Bound to a local so the presence test and both reads are provably the same
                    // object.
                    if (struck_chain.has_value())
                    {
                        // What this string sounds, for the claim law's fret match. It carries no
                        // claim: this note's fret IS its stop.
                        sounded[*string_index] =
                            SoundedStop{.fret = struck_chain->stop, .claim_note = std::nullopt};
                        member_strikes[*string_index] = struck_chain;
                        ++struck;
                    }
                }
            }
            else if (rightHandOnset(member.attack) && string_index.has_value())
            {
                // Invisible to the posture, audible at the STOP: what a right-hand onset sounds on
                // the fret axis is the stop the other hand holds under it, which is why the claim
                // it makes is also the answer to one (the tap-harmonic arm). A tap holding nothing
                // sounds no fretting-hand stop at all, and the empty slot says exactly that. The
                // record rides along, because answering with a stop the record CLAIMS is that
                // claim doing something, which is what the settle beside this walk judges.
                if (claim.has_value())
                {
                    sounded[*string_index] = SoundedStop{.fret = *claim, .claim_note = onset_end};
                }
            }
            ++onset_end;
        }
        // THE HAND-OFF: a standing statement whose reach has run out by this instant is over, and
        // where two or more of its members ring on past that instant the shape they hold STANDS
        // from there ([D2]'s landing, generalized to the death cause 2026-08-31). Asked before any
        // branch reads the slot, so every one of them judges the statement really standing here —
        // which is what lets a lone re-pick of a surviving member ride the successor exactly as it
        // rides any other span (review F7).
        //
        // Asked AFTER the group is scanned, because the continuity law it gates on reads the slot's
        // own rings: a reach ending exactly where those rings are struck again is a REPLACEMENT and
        // no boundary at all.
        settle_successor(position_beat, sounding_rings);

        // THE justification, and now the only one (user ruling 2026-08-27): this slot sounding one
        // of the open shape's claimed stops is the content that shape was authored in front of,
        // whatever ELSE the slot holds. One of the span's own held frets has been played inside the
        // span, which is the whole of what the ruling asks.
        //
        // Asked here, before the branch, for a reason the branch itself cannot serve: the answer
        // must not depend on whether the slot goes on to continue the span or to replace it — a
        // chord carrying the claimed stop justifies the statement it fronts and then opens its own
        // shape, where routing the test through the lone re-pick alone would dissolve the statement
        // unseen, which is the vanishing the member rule was re-ruled to stop.
        //
        // What it cannot see is a claim this same slot MAKES, since the claims attach below; that
        // is the tap harmonic's own case and it is asked there.
        //
        // "Inside the span" needs no test of its own: an unjustified silent-only span sounds
        // nothing, so the continuity law has nothing to bound it with (\ref statementInForce) and
        // there is no instant this could fire at that is outside it. Once an arrival attaches, the
        // span's own extent governs like any other's.
        if (open.has_value() && open->silent_only)
        {
            justify(*open, sounded);
        }

        // Opening a span is one statement however the slot got here, sounding or silent: the shape
        // this slot states, and no claims yet (they attach below, where every branch's do).
        // Written once so the two branches that open a span cannot drift into two different spans.
        //
        // The chains ARE the shape (\ref OpenSpan::ring_chain), so the slot's own chains are what
        // opens the span — its strikes, and the ring-through members whose rings it overlapped
        // into a shape. The update site at the loop's foot then writes this slot's strikes again
        // for every span standing here, which for a span just opened is the same record a second
        // time: both come from one reader at one moment, so there is nothing for them to disagree
        // about.
        //
        // THE DATING RULE AND THE INERTNESS RULE ARE ONE COMPARISON (user ruling 2026-08-31), and
        // it is spent here because here is the one moment both are askable: a member's onset is
        // COVERED when a preceding span already reached past it. An uncovered ring is a member of
        // the figure being founded — it dates the span to its own onset, and it bounds it like the
        // struck member it is — while a covered one crossed in from someone else's ground, so it
        // states its stop into the posture and nothing about this statement's reach ([D3]'s
        // extent-inert rider, aimed at exactly that texture). Spending it twice, once for the
        // front and once for the flag, would be the recurring defect: one fact, two authorities,
        // free to disagree.
        //
        // The founding MODE is handed in rather than re-read here, because it is a fact about the
        // slot's COMPOSITION and one derivation of it serves every event open (\ref slot_founding,
        // user ruling 2026-08-31).
        const auto open_span_here = [&open,
                                     &position,
                                     position_beat,
                                     &saved_notes,
                                     &onset_beat,
                                     &covered,
                                     &grip_established](
                                        RingChains slot_chains, const SpanFounding founding) {
            GridPosition front = position;
            Fraction front_beat = position_beat;
            // THE DATING RULE'S SECOND BOUND (LAW A, user sighting 2026-09-03): the fronted
            // bracket asserts its WHOLE grip from its start, so the span may not date from before
            // the junction that ESTABLISHED any stop it states — before that instant the string
            // audibly held another stop, and a bracket claiming the new grip there lies. A member
            // behind this bound rides extent-inert exactly as one behind the coverage frontier
            // does; the establishing strike is itself the latest member on its own string, so the
            // surviving front never lands before the bound.
            Fraction grip_bound{};
            for (std::size_t string_index = 0; string_index < slot_chains.size(); ++string_index)
            {
                if (slot_chains[string_index].has_value())
                {
                    grip_bound = std::max(grip_bound, grip_established[string_index]);
                }
            }
            for (std::optional<RingChain>& slot : slot_chains)
            {
                // `slot` is the one name every test and read below goes through, so the optional's
                // guarantee provably covers all of them.
                if (!slot.has_value())
                {
                    continue;
                }
                const Fraction onset = onset_beat[slot->member];
                if (onset < covered || onset < grip_bound)
                {
                    slot->extent_inert = true;
                    continue;
                }
                if (onset < front_beat)
                {
                    front_beat = onset;
                    front = saved_notes[slot->member].position;
                }
            }
            // Asked before the aggregate, which leaves `slot_chains` moved-from.
            const bool silent = nothingSounds(slot_chains);
            open = OpenSpan{
                .claims = {},
                .position = front,
                .start_beat = front_beat,
                .ring_chain = std::move(slot_chains),
                // The rails run from the FRONT, so that is where the opening mark belongs — the
                // later members' heads arrive under it (\ref OpenSpan::bracket_position). For
                // every span nothing backdates, the front IS this slot.
                .bracket_position = front,
                // This slot is what states the shape, whether by striking it or by claiming it:
                // both are events, and both put the statement at this instant. It is the FLOOR the
                // display trim may not cut below (\ref ChartShape::stated_extent), which is why it
                // is the slot and not the front.
                .last_stated_beat = position_beat,
                .silent_only = silent,
                .justified = false,
                .justified_by = {},
                .sounds_in_parts = false,
                // This slot is an EVENT, which is the whole of what a carry-opened successor
                // lacks.
                .carry_opened = false,
                .founding = founding,
            };
        };

        // The shape this slot finds STANDING: one is open, and its statement is still in force
        // here (\ref statementInForce). Every branch below that continues, grows or replaces a
        // span asks exactly that, so it is asked once — and the branches that CONTINUE one then
        // write through this same handle, which is what keeps each of them a single expression the
        // optional's guarantee provably covers.
        //
        // Bound as a pointer so every read and write is provably behind the has_value check, the
        // shape this file uses wherever an optional's guarantee has to survive intervening calls.
        // It dangles the moment a close consumes the span, which is why the growth below builds its
        // successor first.
        OpenSpan* const standing =
            open.has_value() && statementInForce(*open, position_beat, sounding_rings) ? &*open
                                                                                       : nullptr;

        // GROWTH (user ruling 2026-08-27, which overturns rule 12b's join clause). A stop the hand
        // takes that the standing shape does not already state is the hand in a DIFFERENT shape
        // from here on, and that is the same answer the derivation already gives a strum that grows
        // by a string: growth splits. What the authored hold changes is WHERE the charter puts the
        // statement — one written at the shape's own onset states the shape whole from its start,
        // which is the case the whole record exists for, while one written later says the finger
        // came down later, because that is what it says.
        //
        // ONE comparison covers both ways a shape can fail to state what a claim says, because
        // they are one question asked of \ref statedStop: a string the shape states nothing on is
        // the hand growing into a new shape, and a string it states ANOTHER stop on is the finger
        // moved, which is a different shape however the old stop was stated — by sound or by claim
        // (user ruling 2026-08-27: same fret continues the span, a different one splits it). Only a
        // claim restating the shape's own stop leaves the shape alone, which is the redundancy the
        // settle then takes.
        //
        // A span still ASSEMBLING is deliberately exempt: nothing dates it yet, so later fingers
        // join the one statement being made rather than splitting a statement that is still waiting
        // for the content it fronts.
        //
        // The ruling is about what the FRETTING HAND did, and says nothing about whether the pick
        // moved beside it, so it is asked at EVERY slot that would otherwise continue the standing
        // shape: a hold on its own, a hold beside a lone re-pick, a hold beside a restrike of the
        // whole shape. Gating it on silence would date the finger from the shape's onset whenever a
        // strum happened to land under it, which is the one thing the ruling says the record must
        // never do. Where the slot already SPLITS the span there is nothing here to decide — the
        // claim founds the new statement and prints its face at its own slot either way.
        const auto takes_new_stop = [&slot_claims](const OpenSpan& shape) {
            return !stillAssembling(shape) &&
                   std::ranges::any_of(slot_claims, [&shape](const StopClaim& claim) {
                       return statedStop(shape, claim.string_index) != claim.fret;
                   });
        };

        // The split itself, in one place for the three continuations that can reach it. The shape
        // does not change identity where this slot says nothing about it — those strings are still
        // stated and still ringing — so the new span INHERITS them, the sounded stops and the
        // claimed ones alike, and the split divides the old span's extent at the instant the hand
        // moved.
        //
        // A string this slot states a DIFFERENT stop on is one the hand has just LEFT, so what the
        // shape said there is superseded: the successor drops it, and this slot's own claims
        // (attached below) state those strings instead. Without that the grown shape would print
        // the stop the hand moved off — the older statement wins the posture slot either way — and
        // the claim that split the span would state nothing anywhere, its own evidence swept away
        // by the settle. A claim RESTATING the shape's stop supersedes nothing, for the same reason
        // it splits nothing: it changes nothing.
        //
        // Every branch below closes at THIS slot, so what a close here IS gets built once
        // (\ref SpanClose) instead of being spelled at each of them — six spellings of one fact
        // were what let a growth split and a replacement close differ. Whether the slot SOUNDS is
        // the only thing a close carries besides its instant, and it is the display trim's question
        // rather than the walk's: a head is something furniture keeps clear of, and a slot of held
        // fingers draws none.
        const SpanClose slot_close{
            .beat = position_beat,
            .sounding_onset = struck > 0 ? std::optional<GridPosition>{position} : std::nullopt,
        };

        const auto grow_span_here = [&open,
                                     &slot_claims,
                                     &close_span,
                                     &position,
                                     position_beat,
                                     &slot_close](const OpenSpan& shape) {
            // Built before the close, which consumes the span it reads from.
            const auto superseded = [&slot_claims, &shape](const std::size_t string_index) {
                return std::ranges::any_of(
                    slot_claims, [&shape, string_index](const StopClaim& claim) {
                        return claim.string_index == string_index &&
                               statedStop(shape, string_index) != claim.fret;
                    });
            };
            std::vector<StopClaim> carried = shape.claims;
            std::erase_if(carried, [&superseded](const StopClaim& claim) {
                return superseded(claim.string_index);
            });
            // The chains ride with the strings that carry them: an inherited member goes on
            // ringing where it already was, so the two spans cover that ring with no gap and no
            // overlap, while a superseded string is no longer a member of anything here and bounds
            // nothing — the hand has left the stop its ring was evidence for.
            //
            // A chain whose own COVERAGE has already run out by this instant rides as INERT, and
            // that is the same distinction the other way round: what the shape covered on that
            // string is behind us, so it is evidence for nothing the new statement REACHES — while
            // the stop it left is still one of the frets the hand is holding, so the grown posture
            // keeps it. Reachable because the two ends of a chain answer different questions — a
            // right-hand onset carries the SOUND past where the fretting hand's coverage stopped
            // (\ref RingChain), which is what keeps a two-hand run one statement — and the split
            // can then land in that window. Without the inert flag the grown span would inherit a
            // reach BEHIND its own start and emit a negative sustain (review F2(iii)); with the
            // chain simply dropped it would lose the stop as well.
            RingChains chains = shape.ring_chain;
            for (std::size_t string_index = 0; string_index < chains.size(); ++string_index)
            {
                // Bound to a local so the presence test and the reads are provably the same object.
                std::optional<RingChain>& chain = chains[string_index];
                if (superseded(string_index))
                {
                    chain.reset();
                    continue;
                }
                if (chain.has_value() && !(position_beat < chain->coverage_end))
                {
                    chain->extent_inert = true;
                }
            }
            // Asked before the aggregate, which leaves `chains` moved-from.
            const bool silent = nothingSounds(chains);
            OpenSpan grown{
                .claims = std::move(carried),
                .position = position,
                .start_beat = position_beat,
                .ring_chain = std::move(chains),
                // A growth split is dated by the CLAIM that split it — an event at its own slot —
                // so its opening mark belongs there, however the span it grew out of was opened.
                .bracket_position = position,
                // The claim that split the span is what states the grown shape, at its own slot.
                .last_stated_beat = position_beat,
                .silent_only = silent,
                .justified = false,
                // A growth split is a new span, so nothing has reached it yet: what reached the
                // span it grew out of was published as that span closed, just below.
                .justified_by = {},
                // A statement of its own, classified by its OWN interior: what the shape it grew
                // out of sounded says nothing about how this one's members arrive, and this slot
                // is the new statement's own first sounding rather than something inside it.
                .sounds_in_parts = false,
                // A growth split is stated by an EVENT at its own slot, which is the whole of what
                // a carry-opened successor lacks.
                .carry_opened = false,
                // The hand went on making the statement it was making, one finger further into it,
                // so the grown span is founded the way its predecessor was (\ref SpanFounding).
                .founding = shape.founding,
            };
            close_span(slot_close);
            open = std::move(grown);
        };

        // THE SLOT'S WHOLE SHAPE: what it strikes, plus every string still RINGING through it at a
        // stated stop. Ring-through strings join the posture (they never count as struck), so span
        // merging compares the whole grip and not just what this slot struck — and under THE
        // ACCUMULATION LAW they are also what a lone strike OPENS a span with, which is why this
        // is read before the branch instead of inside the one that used to want it.
        //
        // THE ONE CARRY TEST, and now the only one anywhere (user ruling 2026-08-28, F1). It asks
        // the STORED ring — `ring_end_of` reads the saved sustain — because whether a finger is
        // still down is a fact about the HANDS, and a string the ear stops hearing is one the hand
        // has not necessarily left. The arrival rule used to re-derive this on the PRESENTED ring
        // for a span's opening slot alone, which made a dead string's carry classify inside a span
        // and not at its start; that reading is deleted and this one reaches both.
        //
        // AT THE STOP THE CHANNEL STATES HERE, never the one the note was struck at (user ruling
        // 2026-08-29, F1). A ring that has TRAVELLED carries the finger with it, so the posture
        // states the fret it landed on — the same reading [D2] already bounds a member's statement
        // by (\ref statedStopFrom), asked at this slot's own offset instead of at the span's.
        // Reading the onset fret here was this walk's second answer to that question, and it
        // printed a grip the hand had left: a chord slide's departed frets in every let-ring
        // posture after it. A carry caught MID-TRAVEL states no stop, and a finger on no stop is a
        // member of nothing, so it joins no posture here at all.
        //
        // Nothing is marked extent-inert here. Whether a carried ring BOUNDS the span it joins is
        // the dating rule's question, answerable only once the close has said how far the spans
        // before it reach, so \ref open_span_here spends that one comparison for both.
        //
        // Asked at EVERY slot, sounding or not (user ruling 2026-08-31, review #2): a ring crossing
        // a slot is a member of whatever that slot states, and a slot of held fingers is no
        // exception — the hand coming down over a still-ringing string states a shape with it. The
        // gate that stood here read the strike count, which made the same ring a member at a
        // struck slot and invisible at a claim-bearing one.
        //
        // AND THE SOUNDING GRIP, off the same read (user ruling 2026-09-03, LAW A). The grip and
        // the fold-in are one question — what does this string's still-sounding ring state here —
        // asked over two different windows, so they are one loop and one \ref member_chain call
        // rather than two walks of the same channel free to answer differently.
        //
        // THE GRIP IS END-INCLUSIVE, AND THAT IS THE WHOLE OF WHAT IT ADDS.
        // `normalizeSustainOverlaps` guarantees no stored same-string overlap, so a ring on a
        // string this slot STRIKES ends exactly at the strike and never past it — the junction
        // instant is the only one at which the old grip and the new statement coexist. Read
        // exclusively, that instant is invisible and the contradiction with it can never be seen;
        // read inclusively, it is exactly the moment the finger is observed moving.
        //
        // The FOLD-IN keeps the strict window it always had: a ring ending here crosses no slot
        // and joins no posture, and a struck string is stated by its strike.
        //
        // ITS TWIN AT IMPORT is `cutLetRingExtensionsAtGripContradictions` in the Guitar Pro
        // builder (rock-hero-editor/core/src/project/gp_chart_builder.cpp), which caps a let-ring
        // extension where a statement contradicts the sounding grip. Same concept, same
        // end-inclusive convention, deliberately in two layers and NOT shared: import cuts a RING
        // it invented, per transcription voice, against the source's own lines; this splits a
        // derived SPAN over the chart model, which has no voices at all. Neither is derivable from
        // the other, and a change to the concept belongs in both.
        RingChains slot_chains = member_strikes;
        SoundingGrips sounding_grip(string_count);
        std::size_t ring_members = struck;
        for (std::size_t string_index = 0; string_index < string_count; ++string_index)
        {
            const std::optional<std::size_t>& ring = ringing[string_index];
            if (!ring.has_value() || ring_end_of(*ring) < position_beat)
            {
                continue;
            }
            std::optional<RingChain> carried =
                member_chain(*ring, position_beat - onset_beat[*ring]);
            if (!carried.has_value())
            {
                // MID-TRAVEL: a finger between stops is on no stop, so it states no grip to
                // contradict and joins no posture — one disposition of one fact, for both readings.
                continue;
            }
            // A SLIDE-OUT ASSERTS NO GRIP — the import twin's own exemption (LAW I), mirrored.
            // The channel's last stated stop is the fret the glide DEPARTED, and by the ring's end
            // the finger is off the board, so reading it as a standing grip would split a span at
            // the ordinary slide-away-then-restrike figure. The fold-in below is untouched: a
            // slide-out ring crossing a slot still colours the posture exactly as it always has.
            if (!saved_notes[*ring].slide_out.has_value())
            {
                sounding_grip[string_index] = carried->stop;
            }
            if (slot_chains[string_index].has_value() || !(position_beat < ring_end_of(*ring)))
            {
                continue;
            }
            slot_chains[string_index] = std::move(carried);
            ++ring_members;
        }

        // THE JUNCTION RECORD (LAW A's dating half, user sighting 2026-09-03): a strike replacing
        // a DIFFERENT sounding stop establishes its string's grip HERE, recorded whether or not
        // any span stands at this slot — the join refusal below can only fire against a standing
        // span, and the sighted figure musters its opening minimum only AFTER the junction, so
        // without this record the dating rule back-dated the span across an instant the string
        // audibly held another stop. Same stop is the tie doctrine and records nothing.
        for (std::size_t string_index = 0; string_index < string_count; ++string_index)
        {
            // Bound to locals so each presence test and its reads are provably the same object.
            const std::optional<RingChain>& restated = member_strikes[string_index];
            const std::optional<int>& sounding = sounding_grip[string_index];
            if (restated.has_value() && sounding.has_value() && *sounding != restated->stop)
            {
                grip_established[string_index] = position_beat;
            }
        }

        // THE ONE MEMBER COUNT, and it is the WHOLE opening law (user ruling 2026-08-31, review
        // #2): a span opens where the threshold's worth of members meet at an instant, and a member
        // is a sounding fretting-hand onset, a ring still sounding at a stated stop, or a stop the
        // hand CLAIMS. One count over the three kinds, because they are three ways of stating one
        // thing — where a finger is — and a lone member of ANY kind opens nothing.
        //
        // Every ring in `slot_chains` is sounding at THIS INSTANT by construction (a strike starts
        // here and a carry was kept only for ringing past here), so the set mutually overlaps
        // without a second test — the STRONG form the ruling asked for rather than a pairwise
        // chain. Claims stay outside the overlap test and inside the count: a claim has no ring to
        // overlap with. What stood here was two counts in a disjunction, and a shape stated by one
        // claim beside one carried ring satisfied neither.
        const std::size_t slot_members = ring_members + slot_claims.size();

        // FOUNDING FOLLOWS COMPOSITION (user ruling 2026-08-31, review #10). A span is a STATEMENT
        // exactly where the event slot stated the WHOLE shape: its own members — struck stops and
        // claimed ones — reach the threshold by themselves, and nothing CARRIED was folded in. Any
        // other opening needed the rings to reach the threshold, which is members arriving
        // staggered, and that is an ACCUMULATION (\ref SpanFounding).
        //
        // Derived ONCE, here, for every EVENT open. The founding is a fact about the slot a span is
        // born at, so re-deriving it at each open would be one fact with two authorities; what is
        // INHERITED rather than re-derived is only the no-event continuation — a growth split and a
        // carry-opened successor, which continue a statement rather than making one.
        //
        // Derived BEFORE the opening test below because that test READS it: a Statement opens at
        // the ruled threshold, while an accumulation opens at the accumulation minimum.
        const SpanFounding slot_founding =
            struck + slot_claims.size() >= g_span_member_threshold && ring_members == struck
                ? SpanFounding::Statement
                : SpanFounding::Accumulation;
        const bool states_shape = slot_members >= (slot_founding == SpanFounding::Statement
                                                       ? g_span_member_threshold
                                                       : g_accumulation_member_minimum);

        if (struck == 0)
        {
            // Nothing sounds here, and a slot that sounds nothing never ENDS a held posture. Two
            // cases share that: tap-only onsets are transparent to the GROUPING (a chord ringing
            // under taps on other strings keeps its span, which the arrival rule then renders as a
            // held arpeggio — the corpus's held-shape-under-tapping case), and a slot of held
            // fingers is a hand coming down, which adds to a shape rather than replacing one.
            // Transparent to the grouping is not transparent to the ring: a tap is a real onset on
            // its own string, so the same-string clamp has already ended any ring there.
            //
            // Where no shape is STANDING, though, the members that meet here can state one — held
            // fingers alone, which is the whole of what a silent-only span is, or held fingers
            // beside a string still ringing through, which the ONE COUNT admits like any other
            // pair of members (user ruling 2026-08-31). A carried ring folded in that way is a
            // MEMBER of the span it opens: it states its stop into the posture, and whether it also
            // bounds the span is the dating rule's own question (\ref open_span_here).
            if (standing != nullptr && takes_new_stop(*standing))
            {
                grow_span_here(*standing);
            }
            else if (standing == nullptr && states_shape)
            {
                // Nothing SOUNDS here, so the close carries no head to keep clear of and the
                // shape being replaced ends exactly where the new one starts (\ref SpanClose).
                // That changes nothing for the span this slot replaces — it has already stopped
                // standing at or before here, so its reach is already behind this instant — and it
                // is what keeps a carry-opened successor this close emits inside the same bound
                // ([D2]).
                close_span(slot_close);
                open_span_here(std::move(slot_chains), slot_founding);
            }
        }
        else if (
            struck == 1 && standing != nullptr &&
            lone_repick_continues(*standing, member_strikes, sounded, sounding_rings, position_beat)
        )
        {
            // Side ruling (ii): the shape survives one of its own members being re-picked, and the
            // re-picked string rings on from here — the stated-instant floor included, so the
            // display trim can never cut the rails back past it. Asked BEFORE the posture test
            // below, since a
            // re-pick that happens to carry a held finger beside it is not a fresh shape stated by
            // the member count; letting it open one would break exactly the broken-chord figure
            // this ruling exists for.
            //
            // What the held finger CAN do is grow the shape, which is the ruling above and not the
            // member count: the re-pick continues the statement, and a stop the statement does not
            // already make dates a new one from here.
            //
            // For a shape the hand alone stated, the arrival that justified it was recorded above,
            // by the one claim law; the string's first CHAIN is taken below, where every branch's
            // is, so a continuation only has to record the restrike the closing floor is measured
            // from.
            if (takes_new_stop(*standing))
            {
                grow_span_here(*standing);
            }
            else
            {
                standing->last_stated_beat = position_beat;
            }
        }
        else if (states_shape)
        {
            // Rule 11's merge, under THE CONTINUITY LAW: a strum RESTATING the shape's stops
            // re-states the shape only while the shape's own statement is still in force.
            // Strike-into-strike is what that looks like in a stored chug chain — every member's
            // ring ends exactly here and every member is struck again here — while a genuine
            // stored gap before this strum is an authored detachment, so the standing statement
            // ended at its own rings and this strum states the shape afresh. The span no longer
            // outlives its sound waiting to be rejoined; a gap is a boundary, not a pause.
            //
            // What "joins" means is \ref slotJoinsShape: the same strings at the same STOPS,
            // however articulated (RULE 11 AMENDED 2026-08-29) — so the chord, the dead chugs on
            // it and the chord again are one span — with per-member agreement where the fold-in
            // cannot speak, a member mid-glide, and with GROWTH admitted where the standing span
            // is an ACCUMULATION, which is absorption (user ruling 2026-08-31). The absorbed
            // string is written into the span by the chain update at the loop's foot, which has
            // always written every member strike; refusing growth here was the whole of what kept
            // it out.
            //
            // Absorption is refused where the string was NOT silent ground: the sounding grip read
            // above says whether a ring is still holding that string at another stop, which is a
            // finger moving rather than a shape growing (LAW A). The refusal lands here, in the one
            // branch that continues a span, so the close and the fresh open are the walk's own
            // ordinary ones — this law adds no path of its own, and nothing about how the pieces
            // are then judged changes.
            if (standing != nullptr &&
                slotJoinsShape(*standing, slot_chains, sounding_grip, position_beat))
            {
                // The growth law again, and unchanged by the strum landing under it: a stop this
                // slot states that the shape does not already make dates the new grip from HERE,
                // whether or not the same slot restates the shape's own sound. It reads CLAIMS
                // alone — an authored hold says where the charter put the finger down, and that
                // dating is theirs in either founding.
                if (takes_new_stop(*standing))
                {
                    grow_span_here(*standing);
                }
                else
                {
                    standing->last_stated_beat = position_beat;
                }
            }
            else
            {
                close_span(slot_close);
                open_span_here(std::move(slot_chains), slot_founding);
            }
        }
        else
        {
            // Any other intervening non-chord onset ends the held posture.
            close_span(slot_close);
        }

        // This slot's MEMBER sound carries the chains of whatever span is open here — THE
        // CONTINUITY LAW's bookkeeping, in ONE place for the reason the claims below are: a span
        // the merge continued, the lone re-pick rode, or a growth split just opened must chain
        // identically, and four branches writing that themselves would be one law written four
        // times. The guard is the same law asked of this instant, so a span the walk left open but
        // the statement no longer covers takes nothing from here.
        if (open.has_value() && statementInForce(*open, position_beat, sounding_rings))
        {
            extendRingChain(*open, sounding_rings, member_strikes);
            // LAW III's class rule, asked of every slot that SOUNDS anything inside this statement
            // — the statement's OWN START included (user ruling 2026-08-28, F1). That last word is
            // the whole of the unification: a slot striking fewer strings than the shape sounds is
            // the shape's members arriving separately, and asking it at the start is exactly the
            // question "is a posture string carried into this span without being re-struck", which
            // the projection used to re-derive for itself. ONE comparison, one stream, one home —
            // \ref chartShapeArrivals no longer asks it at all.
            //
            // CLASSIFICATION READS THE STORED STREAM (same ruling). It is a fact about the HANDS:
            // where the fingers are and which of them the pick reached. The carry this reads is the
            // fold-in above, which has always asked the stored ring, so a dead string's stored ring
            // classifies at a span's start exactly as it does at an interior slot. What a surface
            // DRAWS of that ring is presentation's business and E25 stays what it always was, a
            // display rule.
            //
            // `struck > 0` is the whole guard, and it is what "a SOUNDING of the shape" means: a
            // slot of held fingers sounds no member, so it is no sounding of anything and no part
            // of one. A span that opens at such a slot needs no answer from here — every one of
            // them carries a claim the sound does not state, so it arrives an arpeggio through
            // \ref ChartShape::silent_member.
            if (struck > 0)
            {
                open->sounds_in_parts = open->sounds_in_parts || partOfShapeStruck(*open, struck);
                // THE INK FOLLOWS THE SOUND (review F7): the same "this slot SOUNDS the shape"
                // guard answers where a carry-opened successor's deferred mark anchors. Every
                // other span seeded this at its own front, so the write lands only where nothing
                // has —
                // one field, one rule, and no branch on how the span opened
                // (\ref OpenSpan::bracket_position).
                if (!open->bracket_position.has_value())
                {
                    open->bracket_position = position;
                }
            }
        }

        // This slot's held fingers join whatever span is open here. A hold outside every span is
        // refused nowhere — it simply attaches to nothing and states nothing, the same degrade an
        // unjustified connection claim takes. Asked after the branch so it reads the span this
        // slot actually left open, which for a growth split is the span the stop itself opened.
        if (open.has_value())
        {
            // A CLAIM STATES A STRING THE SHAPE DOES NOT, or it never joins at all — one
            // comparison over the one authority for "what does this shape say about this string"
            // (\ref statedStop), which is the same reader the growth law and the supersession use.
            // A string the shape already states is the finger's own default said aloud: either the
            // shape's sound states it ([D1]'s mid-chain "still held", ruled unstatable) or an
            // earlier claim of this same span does, and a restatement of either is
            // informationless. It publishes no reach, which is how the settle knows to take it.
            //
            // A DIFFERING stop needs no arm here. On a standing shape it has already split the
            // span above, so the grown statement says nothing about that string and this claim
            // founds it; on a shape still ASSEMBLING, which the split exempts, the first statement
            // owns the string and a second one could only be printed by unprinting the first.
            //
            // Asked HERE because here is the only moment it is answerable. At the close a string
            // re-picked later inside the span has a chain of its own, so a proxy over the finished
            // posture would call the hold that put that finger down at the START redundant, drop
            // its face and let the settle delete an authored record. At the claim's own slot the
            // shape is what it was when the charter wrote the hold.
            for (const StopClaim& claim : slot_claims)
            {
                if (!statedStop(*open, claim.string_index).has_value())
                {
                    open->claims.push_back(claim);
                }
            }
            // The tap harmonic's own case: a claim and the sound that answers it in ONE record, so
            // the ask before the branch ran while the claim did not yet exist. One note states the
            // stop and sounds it, which is the whole figure and needs nothing else to arrive.
            //
            // At the span's OWN instant and nowhere else, because that is where the claims a shape
            // STATES sit while it waits: a stop the record adds a beat later is not one the shape
            // was authored making, and a statement the shape does not make cannot be evidence for
            // it — otherwise every held-carrying tap would justify whatever span happened to be
            // open by claiming into it. A tap answering a claim the span already HELD is the ask
            // above's business, at any instant it lands on.
            if (open->silent_only && open->start_beat == position_beat)
            {
                justify(*open, sounded);
            }
        }

        // This onset's sounding fretting-hand notes become the ring candidates for later onsets
        // (updated after use: a note starting at an onset is struck there, not ringing through
        // it). Taps stay invisible here too — a ringing tap never folds into a later posture — and
        // so does a silent hold, which has no ring to fold in at all.
        for (std::size_t member = index; member < onset_end; ++member)
        {
            const ChartNote& note = saved_notes[member];
            if (const std::optional<std::size_t> string_index = note_string_index(note);
                string_index.has_value() && !rightHandOnset(note.attack) &&
                !silentHold(note.attack))
            {
                ringing[*string_index] = member;
            }
        }
        index = onset_end;
    }
    // The end of the stream: the close runs its own chain of landings out, which is why one pass
    // is the whole of it.
    close_span(std::nullopt);

    return derived;
}

std::vector<bool> chartShapeArrivals(
    const std::vector<ChartNote>& notes, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map)
{
    std::vector<bool> arpeggio;
    arpeggio.reserve(shapes.size());
    // Both streams ascend, so one cursor serves every shape: it walks forward to the first note at
    // or after each span's start and never goes back. What it used to carry besides — the most
    // recent note on every string — is gone with the ring reading it fed (see below).
    std::size_t next_note = 0;
    for (const ChartShape& shape : shapes)
    {
        while (next_note < notes.size() && notes[next_note].position < shape.position)
        {
            ++next_note;
        }
        // THE CLASS, and the whole of what this rule reads off the span (user ruling 2026-08-28,
        // F1). Both facts are the WALK's, because both are questions about which slots a statement
        // covers, and grouping is what the walk knows:
        //
        //   a silently-held member is a fret only the bracket can state — a chord box prints the
        //   notes' own heads and has nowhere to put one nothing struck — and nothing in the note
        //   stream tells a silently-held string from an absent one, which is the whole reason a
        //   NoteAttack::None note is authored at all;
        //
        //   a span that sounded IN PARTS had some sounding of it strike only some of the shape,
        //   at its own start or inside it, which is LAW III's class law in ONE comparison
        //   (\ref ChartShape::sounds_in_parts).
        //
        // What used to sit here beside them was a THIRD reading — a posture string still ringing at
        // the span start with no onset there, re-derived from the note stream — and it is deleted
        // rather than corrected. It was the same comparison as the second, asked one slot earlier
        // and off the PRESENTED ring, so a dead string's carry classified at an interior slot and
        // not at a start. Classification is a fact about the HANDS and reads STORED truth; the
        // walk's own fold-in has always asked the stored ring, so making that the sole authority
        // is a deletion and not a second rule. E25 is untouched by it, and stays what it always
        // was: a display rule about what a surface DRAWS of a ring nobody hears.
        //
        // The "fewer than two sounds at the span start" clause went with it, for the reason it was
        // never a peer of these: rule 10 needs two MEMBERS to open a span at all, so a start that
        // sounds fewer than two either carries a member (the comparison above) or claims one (the
        // silent member). It was the precondition of both, and stating it here made it a third
        // answer to a question already answered twice.
        if (shape.silent_member || shape.sounds_in_parts)
        {
            arpeggio.push_back(true);
            continue;
        }

        // A held chord played under a right-hand onset reads as a held arpeggio, not a strummed
        // box: the fretting hand holds the shape while the other hand sounds above it — taps and
        // pick slides alike. Any such note sounding within the span flips the box. The one trigger
        // still DERIVED here, because it is a question about the span's extent rather than about
        // which slots the statement covers.
        //
        // THE MUSICAL CLOSE bounds the window (\ref ChartShape::sustain), not a drawn extent: what
        // this asks is whether the other hand sounded while the fretting hand HELD the shape, and
        // rule 12a's display margin has nothing to say about that. While the extent carried the
        // trim, a tap inside a span's final margin read as outside it.
        const GridPosition span_end = advanceGridPosition(tempo_map, shape.position, shape.sustain);
        bool held_under_right_hand = false;
        for (std::size_t scan = next_note; scan < notes.size() && notes[scan].position < span_end;
             ++scan)
        {
            held_under_right_hand = held_under_right_hand || rightHandOnset(notes[scan].attack);
        }
        arpeggio.push_back(held_under_right_hand);
    }
    return arpeggio;
}

} // namespace rock_hero::common::core
