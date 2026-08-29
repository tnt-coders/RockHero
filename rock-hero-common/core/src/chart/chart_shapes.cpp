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

// One struck string's contribution to a span's articulation identity: the whole note with its
// position and duration neutralized, so ChartNote equality decides "same chord" and a technique
// field added to the note later can never silently drop out of the comparison. Empty where the
// string takes no part in the posture.
using StringArticulation = std::optional<ChartNote>;

// What one string SOUNDS at a slot on the fretting-hand axis. Two onsets fill it and they fill it
// with the same fact: a fretting-hand onset sounds the stop it presses, and a right-hand onset
// sounds the stop the OTHER hand holds under it (\ref claimedStop), because a tap harmonic's pitch
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

// Reduces a presented note to the identity two strums are compared by, stopped at `stop`. Read
// from the PRESENTED note deliberately: two strums that DRAW identically are one box, so a gesture
// the presentation rules compressed is compared in its compressed form.
//
// The stop is a parameter because one note states more than one of them along its ring: a strike
// states the fret it presses, while a note the posture CARRIES — folded in at a later slot, or
// arriving at a landing — states whatever its fret channel has reached by then
// (\ref statedStopFrom). One function, so a carried member can never wear a fret its own channel
// has left; where the two came apart, a chart printed a departed grip.
//
// The identity itself, not a \ref StringArticulation: every caller filling a posture slot converts
// on assignment.
[[nodiscard]] ChartNote articulationOf(const ChartNote& presented, const int stop)
{
    ChartNote key = presented;
    key.position = GridPosition{};
    key.sustain = Fraction{};
    key.fret = stop;
    return key;
}

// One CLAIMED stop (\ref claimedStop) resolved against the span it fell inside — a silently-held
// member, or the fretting-hand stop riding a right-hand onset. The note itself names no span, so
// this is the whole of the relationship: which note it came from, which string it claims, the beat
// it claims it at (which the span's own end then judges), and the stop it states. The note index
// rides along so the close can publish which span each claim ended up in, which is what the
// editor's face and its hit box are placed from — and what the inert sweep reads to decide whether
// the record stated anything at all.
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

// One note's fret channel read for [D2]'s two moments, measured from an offset inside the note's
// own ring. The channel is an ordered run of statements — the onset, then every fret-STATING
// keyframe — so both moments come off one walk of it:
//
//   `departure` is the LAST offset at which the channel still states the stop it holds there,
//   which is where the shape stops being held (user ruling 2026-08-27, [D2]: "travel splits"); and
//
//   `arrival` is the offset at which the channel next comes to REST, with `fret` the stop it
//   rests on — the landing whose grip re-opens.
//
// When the first differing statement is the first fret-stating keyframe, the departure is the
// note's own onset, which is how "the span floors at the strike like every crowded close" falls
// out rather than being a case of its own.
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
// strike reads it at the note's own onset, a landing successor's carried member at the arrival
// that founded it, and the carry fold-in at whatever later slot the ring crosses. The stop is NOT
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

// One member string's standing in the continuity law. The law asks two questions of a string and a
// right-hand onset drives them apart, so the two answers are stored apart rather than derived from
// one number that can only be right about one of them:
//
//   `member_end` — where the SHAPE's own statement of this string reached: the fretting hand's
//   last strike on it, bounded by the ring's end and by the moment that strike's fret channel
//   LEAVES the stated stop, whichever comes first (\ref memberStatementEnd, [D2]). It is the only
//   thing that bounds the span (\ref spanReach), because how far the shape reaches is a statement
//   about the hand that makes shapes.
//
//   `sound_end` — where the string goes on sounding that stop, whichever hand made the onset. It
//   is what CONTINUITY reads (\ref statementInForce): a tap on a member string ends that member's
//   tail UNDERNEATH, with no hand lifting anywhere, so the sound was REPLACED, not silenced, and
//   the statement chains straight through it (user ruling 2026-08-28).
//
// The two are equal until a right-hand onset covers the string, and every member strike brings
// them back together. Collapsing them into one field is the mistake to know about: read as sound
// it lets a tapped sixteenth decide a chord's extent, and read as member ring it fractures the
// two-hand run over a held shape at the first tap.
struct RingChain
{
    // The note whose MEMBER strike wrote this chain — the record both beats below were read from,
    // and what the landing rule then asks for its fret channel, its ring and its drawn identity
    // ([D2]). Carried rather than re-found, for the reason every other answer in this walk is:
    // the walk that wrote the chain is the only thing that knows which strike it belongs to, and
    // a second search for "the last note on this string" would be free to name a different one.
    std::size_t member{0};

    Fraction member_end{};
    Fraction sound_end{};
};

// Every string's chain in one span, indexed by string; empty where the span has no member on that
// string.
using RingChains = std::vector<std::optional<RingChain>>;

// The span being held open: the articulation a following onset must repeat to join it, the silent
// claims inside it so far, how far each member's sound and each member string's sound now reach
// (`ring_chain` — THE CONTINUITY LAW's whole state), and where its final restrike sits
// (`last_strum_beat`), which is the floor the closing trim can never cut below. The posture is NOT
// here: the vector is only complete once the span is, which is what lets one span key one posture
// instead of every strum re-keying it.
struct OpenSpan
{
    std::vector<StringArticulation> articulation;
    std::vector<StopClaim> claims;
    GridPosition position;
    Fraction start_beat{};

    // Each member string's chain (\ref RingChain): where the shape's own statement of it reached,
    // and where its sound reaches at all. Only strings the fretting hand has SOUNDED inside this
    // span are here — a carried ring-through member and a claim are absent for their own reasons:
    // a carried ring is let-ring texture, which classifies but must not bound span structure, and
    // a claim has no ring at all. A string joins the moment its member first sounds inside the
    // span and leaves it only where a growth split supersedes the stop it stated. A landing
    // successor is the one span whose chains are taken from rings it never struck, because those
    // rings ARE its statement ([D2]).
    RingChains ring_chain;

    Fraction last_strum_beat{};

    // True when NOTHING sounds in this span — its articulation is empty, so every member is a
    // silently-held stop. Fixed at open: a later onset can join such a span but never fills its
    // articulation, so this stays the question "was this shape stated by the hand alone". Asked of
    // the articulation rather than of the opening slot's strike count, because a span that GREW out
    // of a sounding one inherits that shape's strings without striking any of them here.
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
    // this walk is the only thing that does. What a reader can see is the span's TRIMMED extent
    // (rule 12a's display margin, floored at the last strum), and a window re-derived from that
    // disagrees with the walk at its own END — the last strum sits exactly ON the end whenever the
    // closing onset crowds within the margin, which a sixteenth-note passage does by construction.
    bool sounds_in_parts{false};
};

// Whether a slot that CONTINUES a span sounded only PART of the shape: fewer of the strings the
// shape SOUNDS than the shape has. Two counts, and the denominator is well defined because a span's
// articulation is fixed at its open — every split rule in this walk exists to keep the posture
// constant inside one span, which is what makes "the shape" a denominator at all.
//
// A partial restrike and a lone re-pick need no arm each: they are one fact at two widths, so this
// is asked identically of both. The SOUNDING strings are the whole denominator because a shape
// carrying a claimed member already classifies through \ref ChartShape::silent_member — a claim
// never sounds, so a shape holding one is members-sounding-separately by inspection.
[[nodiscard]] bool partOfShapeStruck(const OpenSpan& span, const std::size_t struck)
{
    const auto sounded = static_cast<std::size_t>(std::ranges::count_if(
        span.articulation, [](const StringArticulation& slot) { return slot.has_value(); }));
    return struck < sounded;
}

// Whether a span has any SOUND in it at all — the question \ref OpenSpan::silent_only stores,
// asked at each open so the two opens cannot answer it differently.
[[nodiscard]] bool nothingSounds(const std::vector<StringArticulation>& articulation)
{
    return std::ranges::none_of(
        articulation, [](const StringArticulation& slot) { return slot.has_value(); });
}

// The stop a shape states on one string: the one it SOUNDS there, else the one it CLAIMS there,
// else nothing at all. One reader over both ways a shape can state where a finger is, so a claim
// landing on that string is judged against the shape by one comparison — equal is the same hand
// restating itself, and anything else, an absent stop included, is a stop the shape does not state.
[[nodiscard]] std::optional<int> statedStop(const OpenSpan& span, const std::size_t string_index)
{
    // Bound to a local so the optional check and the access are provably the same object.
    const StringArticulation& slot = span.articulation[string_index];
    if (slot.has_value())
    {
        return slot->fret;
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
// together, and a member whose finger goes TRAVELLING ([D2]) ends it exactly as one whose ring
// stops does.
//
// A span with nothing sounding in it yet reaches its own start: it was authored in FRONT of the
// content it describes, and there is no ring to measure until that content arrives.
[[nodiscard]] Fraction spanReach(const OpenSpan& span)
{
    std::optional<Fraction> reach;
    for (const std::optional<RingChain>& chain : span.ring_chain)
    {
        if (!chain.has_value())
        {
            continue;
        }
        // The MEMBER end and nothing else: this is how far the SHAPE reached, and the other hand's
        // sound covering a string says nothing about that (\ref RingChain).
        const Fraction member_end = chain->member_end;
        reach = reach.has_value() ? std::min(*reach, member_end) : member_end;
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
// answers separately. A member whose fret channel has DEPARTED ([D2]) is no longer sounding the
// shape's stop at all, so it fails this test for the same reason and by the same comparison: a
// later strum cannot merge back into a statement the hand has already left.
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
        // Bound to a local so the optional check and the access are provably the same object.
        const std::optional<RingChain>& chain = span.ring_chain[string_index];
        if (!chain.has_value() || now < chain->sound_end)
        {
            continue;
        }
        if (now != chain->sound_end || !sounding_rings[string_index].has_value())
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
        if (chain.has_value() && sound.has_value())
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
    const std::vector<ChartNote>& saved_notes, const std::vector<ChartNote>& presented_notes,
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

    // Where a MEMBER's statement of the stop it holds at `from` ends on its string: the first of
    // its ring's own end and the moment its fret channel LEAVES that stop ([D2]'s departure). ONE
    // reader for the two places that need it — the slot where a strike writes a chain, and the
    // landing where a successor's carried members take theirs — so the split can never be measured
    // two ways.
    //
    // The departure bounds the SHAPE's reach and the string's continuity alike, because both ask
    // the same thing of the string: is the shape's stop still being sounded here. A member whose
    // finger has gone travelling is sounding something else, so a later strum cannot merge back
    // into a statement the hand has already left.
    const auto member_statement_end =
        [&saved_notes, &onset_beat, &ring_end_of](const std::size_t note, const Fraction from) {
            const Fraction ring = ring_end_of(note);
            const StatedStop stated = statedStopFrom(saved_notes[note], from);
            // Bound once so the presence test and the read are provably the same object.
            const std::optional<FretTravel>& travel = stated.travel;
            return travel.has_value() ? std::min(ring, onset_beat[note] + travel->departure) : ring;
        };

    std::map<std::vector<std::optional<int>>, std::size_t> posture_indices;
    std::optional<OpenSpan> open;

    // The grip a closed span's travels are still on their way to ([D2]). The split and the re-open
    // are one act with the whole GLIDE between them, and slots fall inside that glide — the span
    // has already ended at its departure, so the walk closes it at the first of them and would
    // otherwise forget what the hand was reaching for. This is the walk remembering, and it is the
    // one thing here that outlives the span that produced it, because that is exactly what a glide
    // is.
    std::optional<OpenSpan> pending_landing;

    // One instant reduced by the minimum-sustain-distance margin at its own measure — where a span
    // that instant closes must end (rule 12a).
    const auto margin_before = [&tempo_map](const Fraction beat, const GridPosition& at) {
        return beat -
               minimumSustainDistanceBeats(tempo_map.timeSignatureAt(at.measure).denominator);
    };

    // The same margin taken before a closing ONSET, which is where all but one caller reads it.
    const auto margin_limit =
        [&onset_beat, &saved_notes, &margin_before](const std::size_t closing) {
            return margin_before(onset_beat[closing], saved_notes[closing].position);
        };

    // THE LANDING SUCCESSOR ([D2], user ruling 2026-08-27, final form): the grip a span's travels
    // land in re-opens as a span of its own, whose members are the arrived rings.
    //
    // Nothing here is a second span-opening rule. It is the GROWTH split asked of the one statement
    // a slot cannot carry: a claim states a new stop at an instant the walk stops at, while a fret
    // channel states one at a moment inside a ring, so a claim's departure and arrival are the same
    // slot and a travel's are two moments apart. Everything between them is the members sliding,
    // which the surfaces draw as tails and no span covers.
    //
    // Four edges, and only two of them are conditions here — the other two are what the shared
    // reader already answers:
    //
    //   (a) ONE member travelling takes the same rule by symmetry. The members that stay put keep
    //       their stops (the growth split's inheritance) and the traveller states its landed one,
    //       so the successor brackets and names the new voicing.
    //   (b) a travel landing straight into a restrike opens nothing. The chart's own encoding of
    //       "glides into that note" puts the arrival exactly one minimum sustain distance before
    //       the landing's onset, so such a landing has no room of its own and the strike's own box
    //       is the one statement of the new grip (LAW IV — ink has one owner). That is the same
    //       comparison that keeps a transit fret from printing a grip, and it is asked once.
    //   (c) landings that do not COINCIDE open nothing; the truth stays in the sliding tails
    //       (registered as a watch item, `docs/tracking/watch-items.md`).
    //   (d) travels of UNEQUAL distance landing together are included, because nothing here asks
    //       how far a finger moved — only where it was last stated and where it states next.
    const auto landing_successor =
        [&saved_notes,
         &presented_notes,
         &onset_beat,
         &tempo_map,
         &member_statement_end,
         &margin_before](const OpenSpan& span) -> std::optional<OpenSpan> {
        // One member string's standing at the landing: the note whose ring carries it and the stop
        // it holds from there on — its landed fret where it travelled, the shape's own where it
        // stayed put.
        struct ArrivedStop
        {
            std::size_t string_index{0};
            std::size_t note{0};
            int fret{0};
        };
        // Nothing here can travel at all, which is every span in a chart without a glide and the
        // overwhelming majority of spans in one with them. Answered before anything is built,
        // because this runs at every close.
        const bool any_keyframes = std::ranges::any_of(
            span.ring_chain, [&saved_notes](const std::optional<RingChain>& chain) {
                return chain.has_value() && !saved_notes[chain->member].keyframes.empty();
            });
        if (!any_keyframes)
        {
            return std::nullopt;
        }
        std::vector<ArrivedStop> arrived;
        std::optional<Fraction> arrival;
        // The TRAVELLING note the arrival was measured from, which is the one the landing's grid
        // position must be advanced from: it is the only member whose own onset is provably at or
        // before that instant.
        std::size_t arriving_note = 0;
        for (std::size_t string_index = 0; string_index < span.ring_chain.size(); ++string_index)
        {
            // Bound to a local so the optional check and the access are provably the same object.
            const std::optional<RingChain>& chain = span.ring_chain[string_index];
            if (!chain.has_value())
            {
                continue;
            }
            const std::size_t member = chain->member;
            // Where inside this note the span's own statement starts: its onset for a strike that
            // opened or restated the span, and the arrival that founded a successor for a member
            // carried into one. Derived from the two beats rather than stored, because the span's
            // start IS that arrival.
            const Fraction from = std::max(Fraction{}, span.start_beat - onset_beat[member]);
            const StatedStop stated = statedStopFrom(saved_notes[member], from);
            // Bound to locals so each optional check and its access are provably the same object.
            const std::optional<int>& stop = stated.fret;
            const std::optional<FretTravel>& travel = stated.travel;
            if (!stop.has_value())
            {
                // The channel is MID-TRAVEL at the span's own start, so this string is on no stop
                // to arrive FROM and states nothing here. Read off the channel rather than off the
                // shape's articulation, which is where the two used to be free to differ.
                continue;
            }
            if (!travel.has_value())
            {
                arrived.push_back(
                    ArrivedStop{.string_index = string_index, .note = member, .fret = *stop});
                continue;
            }
            const Fraction lands = onset_beat[member] + travel->arrival;
            if (arrival.has_value() && *arrival != lands)
            {
                // Edge (c): a staggered group states no single grip, so nothing re-opens.
                return std::nullopt;
            }
            arrival = lands;
            arriving_note = member;
            arrived.push_back(
                ArrivedStop{.string_index = string_index, .note = member, .fret = travel->fret});
        }
        // Bound once so the presence test and every read below are provably the same object.
        const std::optional<Fraction>& lands = arrival;
        if (!lands.has_value())
        {
            return std::nullopt;
        }
        const GridPosition landing = advanceGridPosition(
            tempo_map, saved_notes[arriving_note].position, *lands - onset_beat[arriving_note]);
        // The room every element keeps, taken from the one reader that owns the margin rather than
        // named a second time here.
        const Fraction margin = *lands - margin_before(*lands, landing);

        // "TWO OR MORE members ring ON at stated stops", asked as the model asks every other
        // question about room: a member goes on stating its stop past the landing by more than the
        // margin every element keeps. Both suppressions live in this one comparison — the glide
        // straight into a restrike, whose arrival sits exactly one margin before the ring's end
        // (edge b), and the transit fret of a continuous multi-fret glide, which departs at the
        // very moment it arrives.
        std::vector<StringArticulation> articulation(span.articulation.size());
        RingChains chains(span.ring_chain.size());
        std::size_t members = 0;
        for (const ArrivedStop& stop : arrived)
        {
            const Fraction from = *lands - onset_beat[stop.note];
            const Fraction statement_end = member_statement_end(stop.note, from);
            if (!(*lands + margin < statement_end))
            {
                continue;
            }
            // The identity the surfaces draw, wearing the stop the hand landed on: the presented
            // note is what makes two strums one box (rule 11), and the landed fret is what the
            // bracket digits state. Keeping the arrived note's own gesture in that identity is
            // also what stops a restrike of the same grip from merging into the bracket — the
            // strike states its chord itself.
            articulation[stop.string_index] = articulationOf(presented_notes[stop.note], stop.fret);
            chains[stop.string_index] = RingChain{
                .member = stop.note, .member_end = statement_end, .sound_end = statement_end
            };
            ++members;
        }
        if (members < 2)
        {
            return std::nullopt;
        }
        OpenSpan successor{
            .articulation = std::move(articulation),
            // The claims of the span it succeeds do not ride along: a claim states where a finger
            // is at the slot it was authored at, and this statement is dated at a landing the
            // charter never wrote. The successor is founded on sound alone.
            .claims = {},
            .position = landing,
            .start_beat = *lands,
            .ring_chain = std::move(chains),
            .last_strum_beat = *lands,
            .silent_only = false,
            .justified = false,
            .sounds_in_parts = false,
        };
        // LAW III's class rule, asked of the successor's own start with the count it has BY
        // CONSTRUCTION: nothing is struck there, and a slot striking fewer strings than the shape
        // sounds is the shape's members arriving separately. Answered through the one comparison
        // rather than asserted, so the class law stays stated once — the successor is trigger (a)
        // at its purest, every member carried and none struck.
        successor.sounds_in_parts = partOfShapeStruck(successor, 0);
        return successor;
    };

    // Emits the held span and keys its posture, consuming it. A span closed by a following event
    // trims to the margin before it (rule 12a — spans keep the same minimum sustain distance as
    // every other element), floored at the last strum so the box always reaches its final restrike.
    // A span that would lose all length (a single strum crowded closer than the margin) falls back
    // to exact adjacency, mirroring the sustain rules' protected-adjacency precedent.
    //
    // The posture is built HERE rather than at each onset because the span owns extent and a
    // silent claim is judged against it: the vector is only complete once the span is. Keying it
    // once per span rather than once per strum is what that buys back.
    //
    // Not called directly by the walk: \ref close_span below is the close, and this is the one act
    // it repeats when a span hands it the grip its travels landed in.
    const auto emit_span = [&derived, &posture_indices, &open](
                               const std::optional<Fraction> closing_limit,
                               const std::optional<Fraction>
                                   closing_beat) {
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
        // Where the statement itself reached (\ref spanReach), before any closing event has a say:
        // the continuity law owns the extent and the trim below only ever shortens it.
        const Fraction reach = spanReach(*open);
        Fraction end = reach;
        if (closing_limit.has_value() && *closing_limit < end)
        {
            end = std::max(*closing_limit, open->last_strum_beat);
        }
        if (!(open->start_beat < end) && closing_beat.has_value())
        {
            // Exact adjacency: the crowded span ends at the earlier of its own ring and the
            // closing onset — both sit strictly after the span start, so the span keeps positive
            // length even when the closer lands exactly on the ring's end (a dense run of short
            // strums).
            end = std::min(reach, *closing_beat);
        }
        std::vector<std::optional<int>> frets(open->articulation.size());
        for (std::size_t string_index = 0; string_index < open->articulation.size(); ++string_index)
        {
            // Bound to a local so the optional check and the access are provably the same
            // object (bugprone-unchecked-optional-access cannot track repeated indexing).
            const StringArticulation& slot = open->articulation[string_index];
            if (slot.has_value())
            {
                frets[string_index] = slot->fret;
            }
        }
        // What the notes could not say. A claim authored past the span's own end is inert — that
        // gap is after the shape stopped sounding, and the bracket the fret would print under
        // never reaches it. A string the sound already states is left alone: the hold adds nothing
        // there, so it makes no silent member either.
        //
        // "Past the end" is asked as "not strictly inside, and not AT the start". The second half
        // is not an exception carved out for a degenerate case: a span opened by held fingers
        // alone starts and ends together until sound attaches to it, and its own opening members
        // are exactly the claims sitting at that instant. Excluding them would let a posture open
        // and then state nothing.
        bool silent_member = false;
        for (const StopClaim& claim : open->claims)
        {
            std::optional<int>& fret = frets[claim.string_index];
            const bool inside = claim.beat < end || claim.beat == open->start_beat;
            if (!inside || fret.has_value())
            {
                continue;
            }
            fret = claim.fret;
            silent_member = true;
            // Which span this stop reached, published for the surfaces: the bracket that prints it
            // draws at this span's start, and a hold with no entry here draws nowhere. The FIRST
            // span a claim reaches keeps its face: a stop the hand goes on holding across a growth
            // split is a member of every span it reaches, but it was authored once, at one slot,
            // and its face must stay where the charter put it.
            if (!derived.claim_shapes[claim.note_index].has_value())
            {
                derived.claim_shapes[claim.note_index] = derived.shapes.size();
            }
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
                .posture = entry->second,
                .silent_member = silent_member,
                .sounds_in_parts = open->sounds_in_parts,
            });
        open.reset();
    };

    // The close itself: it emits the standing span and then, where that span's travels
    // have already landed by this instant, runs straight on into the grip they landed in
    // ([D2]) — nothing between the landing and here can have touched it, since the very
    // slots that could have are exactly the ones that would have closed the span it
    // succeeds. Where the landing is still ahead, it waits in `pending_landing` for the
    // slot that reaches it. `closing_beat` is the instant this close happens at, absent
    // only at the end of the stream, where everything left has landed.
    const auto close_span = [&open, &pending_landing, &landing_successor, &emit_span](
                                const std::optional<Fraction> closing_limit,
                                const std::optional<Fraction>
                                    closing_beat) {
        while (open.has_value())
        {
            // Read before the close consumes the span it is read from. Clearing whatever
            // was pending is the same rule stated once: the newest statement is the one
            // the walk goes on waiting for.
            std::optional<OpenSpan> successor = landing_successor(*open);
            pending_landing.reset();
            emit_span(closing_limit, closing_beat);
            if (!successor.has_value())
            {
                return;
            }
            if (closing_beat.has_value() && *closing_beat < successor->start_beat)
            {
                pending_landing = std::move(successor);
                return;
            }
            open = std::move(successor);
        }
    };

    // Materializes the grip a closed span's travels are arriving at, once the walk has passed its
    // landing. `not_after` is the instant the walk has reached, absent at the end of the stream
    // where everything left has landed.
    //
    // ONE span stands at a time, which is the model and not a limitation worked around here: a
    // landing the walk reaches with another statement already standing over it states nothing, and
    // the truth stays in the members' sliding tails — the same disposition edge (c) takes, reached
    // by the same road.
    const auto settle_landings = [&open,
                                  &pending_landing](const std::optional<Fraction> not_after) {
        if (!pending_landing.has_value())
        {
            return;
        }
        if (not_after.has_value() && *not_after < pending_landing->start_beat)
        {
            return;
        }
        if (open.has_value())
        {
            pending_landing.reset();
            return;
        }
        // Exchanged rather than moved-then-cleared, so the hand-off is one expression and the
        // emptied mailbox is the exchange's own result rather than a second statement about it.
        open = std::exchange(pending_landing, std::nullopt);
    };

    // Answers a waiting span with this slot's sounded stops, and publishes what the answering
    // record thereby REACHED. The two are one act: a claim whose answer is the only thing keeping a
    // span alive states exactly as much as a member does — take that record away and the span
    // dissolves — so the settle beside this walk (\ref sweepInertClaimedStops) can go on asking its
    // single question, "does the derived face exist", instead of growing a second rule about
    // justification that would then have to be kept in step with this one (user ruling
    // 2026-08-27).
    //
    // Only a record whose OWN claim sounded the stop has a face to publish: a fretting-hand arrival
    // presses the fret it names and claims nothing. The span is the one being justified, at the
    // index the count names right now — a justified span is always emitted, and nothing can push a
    // shape while it is open, because \ref close_span is the only push and it consumes the span.
    const auto justify = [&derived](OpenSpan& span, const SoundedStops& sounded) {
        for (const StopClaim& claim : span.claims)
        {
            if (!answersClaim(claim, sounded))
            {
                continue;
            }
            span.justified = true;
            if (const std::optional<std::size_t> answering =
                    soundingClaimNote(sounded, claim.string_index);
                answering.has_value())
            {
                // The FIRST span a claim reaches keeps its face, exactly as the close's members do.
                std::optional<std::size_t>& reach = derived.claim_shapes[*answering];
                if (!reach.has_value())
                {
                    reach = derived.shapes.size();
                }
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

    // The last note sounded per string, for the ring-through rule: a note whose tail crosses a
    // chord's onset on an un-struck string is still sounding, so its held fret joins the derived
    // posture — and the arrival rule then renders the partly-struck span as an arpeggio. Indexes
    // rather than pointers, because the posture it folds in comes from the presented stream while
    // the ring it tests comes from the stored one.
    std::vector<std::optional<std::size_t>> ringing(string_count);

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
                                          const std::vector<StringArticulation>& articulation,
                                          const SoundedStops& sounded,
                                          const RingEnds& sounding_rings,
                                          const Fraction now) {
        // A `struck == 1` onset fills exactly one slot, which this finds without the walk having to
        // carry it out of the group loop.
        std::size_t struck_index = articulation.size();
        for (std::size_t string_index = 0; string_index < articulation.size(); ++string_index)
        {
            if (articulation[string_index].has_value())
            {
                struck_index = string_index;
            }
        }
        if (struck_index == articulation.size())
        {
            return false;
        }
        // 1. The string is a member of the open shape. A member the SOUND states must be re-picked
        //    identically — rule 11 splits a chord on any articulation change, and a lone re-pick is
        //    that same question asked of one string. A member the chart HOLDS silently has no
        //    articulation to match, because nothing ever sounded it, so the authored claim is the
        //    whole test — and this is where a silent hold and this ruling compose into the case
        //    the whole record exists for.
        //
        //    The claim IS the whole test, which means all of it: a claim states where the finger
        //    is, so a re-pick at a DIFFERENT stop is a different hand and splits, exactly as the
        //    sound branch splits on a changed articulation. Passing it would let an authored stop
        //    outlive the note that contradicts it — the bracket printing the claim's fret at the
        //    span start while the note inside it sounds another.
        const StringArticulation& held = span.articulation[struck_index];
        const StringArticulation& repick = articulation[struck_index];
        bool member = false;
        if (held.has_value() && repick.has_value())
        {
            member = *held == *repick;
        }
        else if (!held.has_value())
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

        // [D2]: a span whose travels have landed by this instant ended at its first departure, and
        // the grip they landed in re-opens. Asked before anything reads the slot, so every branch
        // below judges the statement that is actually standing here.
        settle_landings(position_beat);

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
        std::vector<StringArticulation> articulation(string_count);
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
            // that note as its held fret. One query for both (\ref claimedStop), so the membership
            // count, the justification and the fret-match can never read them differently.
            const std::optional<int> claim = claimedStop(member);
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
                    // The stop a STRIKE states is the fret it presses, which is the note's own at
                    // its onset — the same quantity a carried member states at whatever its
                    // channel has reached (\ref articulationOf).
                    articulation[*string_index] =
                        articulationOf(presented_notes[onset_end], presented_notes[onset_end].fret);
                    // What this string sounds: the fretting hand's own stop, read from the
                    // PRESENTED note for the same reason the articulation is — what a strum sounds
                    // is what it draws. It carries no claim: this note's fret IS its stop.
                    sounded[*string_index] = SoundedStop{
                        .fret = presented_notes[onset_end].fret, .claim_note = std::nullopt
                    };
                    // A MEMBER's chain: the fretting hand's own statement of this stop on this
                    // string, which is what gives the string a chain to be continuous in at all.
                    // It ends where the ring does, or where this note's own fret channel leaves
                    // the stop it just struck ([D2]) — the strike states the stop from its onset,
                    // so the reading starts there.
                    const Fraction statement_end = member_statement_end(onset_end, Fraction{});
                    member_strikes[*string_index] = RingChain{
                        .member = onset_end,
                        .member_end = statement_end,
                        .sound_end = statement_end,
                    };
                    ++struck;
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
        const bool posture_slot = struck + slot_claims.size() >= 2;

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

        // Opening a span is one statement however the slot got here, sounding or silent: the
        // articulation it holds, no chains yet and no claims yet (both attach below, where every
        // branch's do). Written once so the two branches that open a span cannot drift into two
        // different spans.
        //
        // The chains start empty rather than being seeded here, because a span with no chain is
        // vacuously in force (\ref statementInForce) and the one update site below then states
        // this slot's own sound for it — the same law, in the one place, for a span the walk
        // opened and a span it continued alike.
        const auto open_span_here = [&open, &articulation, &position, position_beat] {
            // Asked before the aggregate, which leaves `articulation` moved-from.
            const bool silent = nothingSounds(articulation);
            open = OpenSpan{
                .articulation = std::move(articulation),
                .claims = {},
                .position = position,
                .start_beat = position_beat,
                .ring_chain = RingChains(string_count),
                .last_strum_beat = position_beat,
                .silent_only = silent,
                .justified = false,
                .sounds_in_parts = false,
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
        // stated and still ringing — so the new span INHERITS them, articulation and stated stops
        // alike, and the split divides the old span's extent at the instant the hand moved.
        //
        // A string this slot states a DIFFERENT stop on is one the hand has just LEFT, so what the
        // shape said there is superseded: the successor drops it, and this slot's own claims
        // (attached below) state those strings instead. Without that the grown shape would print
        // the stop the hand moved off — the older statement wins the posture slot either way — and
        // the claim that split the span would state nothing anywhere, its own evidence swept away
        // by the settle. A claim RESTATING the shape's stop supersedes nothing, for the same reason
        // it splits nothing: it changes nothing.
        //
        // `closing_limit` is the whole of what the callers differ by, and it is rule 12a asked as
        // usual: the margin before an onset that SOUNDS here, and this instant itself where a slot
        // of held fingers sounds nothing to keep a distance from — the shape being replaced then
        // ends exactly where the new one starts.
        const auto grow_span_here = [&open, &slot_claims, &close_span, &position, position_beat](
                                        const OpenSpan& shape, const Fraction closing_limit) {
            // Built before the close, which consumes the span it reads from.
            const auto superseded = [&slot_claims, &shape](const std::size_t string_index) {
                return std::ranges::any_of(
                    slot_claims, [&shape, string_index](const StopClaim& claim) {
                        return claim.string_index == string_index &&
                               statedStop(shape, string_index) != claim.fret;
                    });
            };
            std::vector<StringArticulation> inherited = shape.articulation;
            for (std::size_t string_index = 0; string_index < inherited.size(); ++string_index)
            {
                if (superseded(string_index))
                {
                    inherited[string_index].reset();
                }
            }
            std::vector<StopClaim> carried = shape.claims;
            std::erase_if(carried, [&superseded](const StopClaim& claim) {
                return superseded(claim.string_index);
            });
            // The chains ride with the strings that carry them: an inherited member goes on
            // ringing where it already was, so the two spans cover that ring with no gap and no
            // overlap, while a superseded string is no longer a member of anything here and bounds
            // nothing — the hand has left the stop its ring was evidence for. Every chain kept
            // reaches strictly past this instant, or is sounded again by it, because that is what
            // the law had to say for this slot to find a standing shape at all.
            RingChains chains = shape.ring_chain;
            for (std::size_t string_index = 0; string_index < chains.size(); ++string_index)
            {
                if (superseded(string_index))
                {
                    chains[string_index].reset();
                }
            }
            // Asked before the aggregate, which leaves `inherited` moved-from.
            const bool silent = nothingSounds(inherited);
            OpenSpan grown{
                .articulation = std::move(inherited),
                .claims = std::move(carried),
                .position = position,
                .start_beat = position_beat,
                .ring_chain = std::move(chains),
                .last_strum_beat = position_beat,
                .silent_only = silent,
                .justified = false,
                // A statement of its own, classified by its OWN interior: what the shape it grew
                // out of sounded says nothing about how this one's members arrive, and this slot
                // is the new statement's own first sounding rather than something inside it.
                .sounds_in_parts = false,
            };
            close_span(closing_limit, position_beat);
            open = std::move(grown);
        };

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
            // Where no shape is STANDING, though, held fingers alone can state one — that is the
            // whole of what a silent-only span is. Its articulation is empty because nothing
            // sounds in it, and its claims (attached below) are its entire posture.
            if (standing != nullptr && takes_new_stop(*standing))
            {
                grow_span_here(*standing, position_beat);
            }
            else if (standing == nullptr && posture_slot)
            {
                // No margin: nothing SOUNDS here to keep a distance from, so the shape being
                // replaced ends exactly where the new one starts. That limit changes nothing for
                // the span this slot replaces — it has already stopped standing at or before
                // here, so its reach is already behind this instant — and it is what keeps a
                // landing successor this close emits inside the same bound ([D2]).
                close_span(position_beat, position_beat);
                open_span_here();
            }
        }
        else if (
            struck == 1 && standing != nullptr &&
            lone_repick_continues(*standing, articulation, sounded, sounding_rings, position_beat)
        )
        {
            // Side ruling (ii): the shape survives one of its own members being re-picked, and the
            // re-picked string rings on from here — the last-strum floor included, so the closing
            // trim can never cut back past it. Asked BEFORE the posture test below, because a
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
                grow_span_here(*standing, margin_limit(index));
            }
            else
            {
                standing->last_strum_beat = position_beat;
            }
        }
        else if (posture_slot)
        {
            // Ring-through strings join the posture (they never count as struck): the held note's
            // articulation folds in so span merging still compares whole notes.
            //
            // THE ONE CARRY TEST, and now the only one anywhere (user ruling 2026-08-28, F1). It
            // asks the STORED ring — `ring_end_of` reads the saved sustain — because whether a
            // finger is still down is a fact about the HANDS, and a string the ear stops hearing is
            // one the hand has not necessarily left. The arrival rule used to re-derive this on the
            // PRESENTED ring for a span's opening slot alone, which made a dead string's carry
            // classify inside a span and not at its start; that reading is deleted and this one
            // reaches both, through the strike count taken below. The IDENTITY folded in is still
            // the presented note, because what two strums DRAW is what makes them one box — the
            // class question is stored, the sameness question is drawn.
            //
            // AT THE STOP THE CHANNEL STATES HERE, never the one the note was struck at (user
            // ruling 2026-08-29, F1). A ring that has TRAVELLED carries the finger with it, so the
            // posture states the fret it landed on — the same reading [D2] already bounds a
            // member's statement by (\ref statedStopFrom), asked at this slot's own offset instead
            // of at the span's. Reading the onset fret here was this walk's second answer to that
            // question, and it printed a grip the hand had left: a chord slide's departed frets in
            // every let-ring posture after it.
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                const std::optional<std::size_t>& ring = ringing[string_index];
                if (articulation[string_index].has_value() || !ring.has_value() ||
                    !(position_beat < ring_end_of(*ring)))
                {
                    continue;
                }
                const StatedStop carried =
                    statedStopFrom(saved_notes[*ring], position_beat - onset_beat[*ring]);
                // Bound to a local so the optional check and the access are provably the same
                // object. A carry caught MID-TRAVEL states no stop, and a finger on no stop is a
                // member of nothing — so it joins no posture here, exactly as its departure has
                // already ended its own span.
                const std::optional<int>& stop = carried.fret;
                if (stop.has_value())
                {
                    articulation[string_index] = articulationOf(presented_notes[*ring], *stop);
                }
            }
            // Rule 11's merge, under THE CONTINUITY LAW: an identical strum re-states the shape
            // only while the shape's own statement is still in force. Strike-into-strike is what
            // that looks like in a stored chug chain — every member's ring ends exactly here and
            // every member is struck again here — while a genuine stored gap before this strum is
            // an authored detachment, so the standing statement ended at its own rings and this
            // strum states the shape afresh. The span no longer outlives its sound waiting to be
            // rejoined; a gap is a boundary, not a pause.
            if (standing != nullptr && standing->articulation == articulation)
            {
                // The growth law again, and unchanged by the strum landing under it: a stop this
                // slot states that the shape does not already make dates the new grip from HERE,
                // whether or not the same slot restates the shape's own sound.
                if (takes_new_stop(*standing))
                {
                    grow_span_here(*standing, margin_limit(index));
                }
                else
                {
                    standing->last_strum_beat = position_beat;
                }
            }
            else
            {
                close_span(margin_limit(index), position_beat);
                open_span_here();
            }
        }
        else
        {
            // Any other intervening non-chord onset ends the held posture.
            close_span(margin_limit(index), position_beat);
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
            }
        }

        // This slot's held fingers join whatever span is open here. A hold outside every span is
        // refused nowhere — it simply attaches to nothing and states nothing, the same degrade an
        // unjustified connection claim takes. Asked after the branch so it reads the span this
        // slot actually left open, which for a growth split is the span the stop itself opened.
        if (open.has_value())
        {
            open->claims.insert(open->claims.end(), slot_claims.begin(), slot_claims.end());
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
    // The end of the stream: a grip still arriving has landed by now, and the close that follows
    // runs its own chain out — which is why one pass of each is the whole of it.
    settle_landings(std::nullopt);
    close_span(std::nullopt, std::nullopt);

    return derived;
}

std::vector<bool> chartShapeArrivals(
    const std::vector<ChartNote>& presented_notes, const std::vector<ChartShape>& shapes,
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
        while (next_note < presented_notes.size() &&
               presented_notes[next_note].position < shape.position)
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
        const GridPosition span_end = advanceGridPosition(tempo_map, shape.position, shape.sustain);
        bool held_under_right_hand = false;
        for (std::size_t scan = next_note;
             scan < presented_notes.size() && presented_notes[scan].position < span_end;
             ++scan)
        {
            held_under_right_hand =
                held_under_right_hand || rightHandOnset(presented_notes[scan].attack);
        }
        arpeggio.push_back(held_under_right_hand);
    }
    return arpeggio;
}

} // namespace rock_hero::common::core
