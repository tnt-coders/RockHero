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

// =================================================================================================
// THE GRIP-TENURE MACHINE (the ground-up rebuild, user-signed law 2026-09-04).
//
// One idea: a span is the statement "the hand holds this grip, from here to here." The machine
// keeps the EVIDENCE — what each string is doing — in one per-string table that outlives every
// span, and derives spans as the maximal stretches over which one grip is continuously evidenced.
// The old machine kept the evidence inside each span's own records and died with them, which is
// why every question that outlived a span (the foreign ring, the dating clamp, the landing
// hand-off) grew its own mechanism; this table is that one missing object, built once.
//
// The law this file implements is docs/plans/in-progress/span-derivation-ground-up.md — the
// eleven rules plus the siege repairs. Where a comment below cites a rule number, that document
// is the referent.
// =================================================================================================

// The member statement threshold: two stops stated at one slot are a grip (rule 4). Chord boxes
// legitimately form at two, and the landing successor reads this same number (rule 6) because a
// landing's members were already established members of the span that just closed.
constexpr std::size_t g_span_member_threshold = 2;

// The accumulation minimum: sound alone may found a span only at three overlapping members
// (rule 5, signed 2026-09-04 with its sighting rig deleted). It gates FOUNDING by sound alone and
// nothing else — growing a standing span has no minimum, and a landing opens at the statement
// threshold above.
constexpr std::size_t g_accumulation_member_minimum = 3;

// One note's fret channel read for the law's two moments, measured from an offset inside the
// note's own ring. The channel is an ordered run of statements — the onset, then every
// fret-stating keyframe — so both moments come off one walk of it: `departure` is the last offset
// at which the channel still states the stop it holds there, and `arrival` is the offset at which
// it next comes to REST, with `fret` the stop it rests on — the landing where the new statement
// is established and where the span hands over (rule 10).
struct FretTravel
{
    Fraction departure{};
    Fraction arrival{};
    int fret{0};
};

// The whole of what a fret channel says from one offset onward (\ref statedStopFrom): which stop
// the finger is on there, and the travel it makes away from it. One answer rather than two
// queries, because a caller that had to name the stop itself was a second statement of this very
// fact, free to disagree with the channel.
struct StatedStop
{
    // The stop the channel states at the queried offset; empty exactly where it is MID-TRAVEL — a
    // finger between stops is on no stop at all, so a string caught there is a member of nothing.
    std::optional<int> fret{};

    // The travel the channel is on, or is about to make; absent where it never leaves this stop.
    std::optional<FretTravel> travel{};
};

// THE channel reader, kept verbatim from the machine this file replaced: it was already the one
// authority on where a finger is, and every defect in the old walk traced to a copy of this fact
// kept elsewhere, never to this reader. A fret the channel LEAVES AGAIN is a point on the path
// and never a landing ("equal frets are a hold, different frets are travel"), which keeps a
// continuous multi-fret glide one travel to its end while a glide with a held grip between its
// legs states each grip exactly once.
[[nodiscard]] StatedStop statedStopFrom(const ChartNote& note, const Fraction from)
{
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
            travel->arrival = keyframe.offset;
            travel->fret = *fret;
            continue;
        }
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
        return StatedStop{.fret = std::nullopt, .travel = travel};
    }
    return StatedStop{.fret = leaves->fret, .travel = std::nullopt};
}

// What one string of the fretting hand is demonstrably doing, as the strings testify — owned by
// the walk and outliving every span. This one table replaces the old machine's four partial
// copies of the same object (the per-span chains, `ringing[]`, `grip_established[]`, and the
// per-slot `SoundingGrips` rebuild), which is where its recurring rule-stated-twice defects
// lived.
//
// TWO REACH COLUMNS, deliberately, because the law asks two different physical questions of a
// string and a right-hand onset drives them apart (the siege proved every one-field collapse
// against a pinned fixture):
//
//   `covers` — how far the FRETTING HAND's own statement reaches: the last member strike's ring,
//   capped at the landing its fret channel comes to rest at. THE ONLY input to a span's reach and
//   close — how far the shape reaches is a statement about the hand that makes shapes.
//
//   `sounds` — how far the string goes on sounding, WHICHEVER hand made the onset. THE ONLY input
//   to renewal and continuity: a tap on a member string ends that member's ring underneath with
//   no hand lifting anywhere, so the sound was replaced, not silenced, and the statement chains
//   straight through it — without the tap ever moving a close.
//
// TWO QUERY WINDOWS over the one stop, named so they cannot be collapsed: the CONTRADICTION and
// DISPLACEMENT witnesses read end-INCLUSIVELY (the same-string clamp puts a displaced ring's end
// exactly on the displacing strike, so the junction instant is the only one at which the two
// coexist — read strictly, the junction is invisible), while MEMBERSHIP and the fold-in read
// STRICTLY (a ring ending at a slot crosses no slot; the inclusive reading births zero-length
// spans).
struct StringHand
{
    // The last fretting-hand sounding note on this string — the record the channel reader is
    // asked of. Empty where the hand has never sounded the string. Right-hand onsets and silent
    // holds never write it: a tap sounds the stop the OTHER hand holds, so it asserts no grip of
    // its own, and a silent hold has no ring to assert one with.
    std::optional<std::size_t> finger;

    Fraction covers{};
    Fraction sounds{};

    // The instant this string's current grip was established by DISPLACING a different sounding
    // stop — the dating clamp's whole state (Law A): a span may not date its front across an
    // instant at which one of its stated strings audibly held another stop, because the fronted
    // bracket asserts its whole grip from its start. Zero where the string's grip displaced
    // nothing; RESET by a fresh strike over a silent string, because silence is not contradiction
    // and a stale junction from a long-dead figure bounds nothing.
    Fraction displaced_at{};
};

// One authored claim inside a span: the record the charter stated, carried apart from the sounded
// grip because it holds provenance the sound never has — justification, the published face, and
// the inert sweep are all keyed on it (LAW II machinery, kept whole from the old machine).
struct StopClaim
{
    std::size_t note_index{0};
    std::size_t string_index{0};
    Fraction beat{};
    int fret{0};
};

// What one slot SOUNDS on the fret-hand axis, for the claim law's fret match: a fretting-hand
// strike sounds the stop it presses, and a right-hand onset sounds the stop the OTHER hand holds
// under it, because a tap's pitch derives from the stopped length. One query for both, so the
// membership count, the justification and the fret-match can never read them differently.
struct SoundedStop
{
    int fret{0};
    std::optional<std::size_t> claim_note{};
};

using SoundedStops = std::vector<std::optional<SoundedStop>>;

// Whether a slot's sounded stops answer a claim: the standing fret-match law — one of the span's
// own held frets played inside the span (user ruling 2026-08-27), by either hand's way of
// sounding a stop.
[[nodiscard]] bool answersClaim(const StopClaim& claim, const SoundedStops& sounded)
{
    const std::optional<SoundedStop>& stop = sounded[claim.string_index];
    return stop.has_value() && stop->fret == claim.fret;
}

[[nodiscard]] std::optional<std::size_t> soundingClaimNote(
    const SoundedStops& sounded, const std::size_t string_index)
{
    const std::optional<SoundedStop>& stop = sounded[string_index];
    return stop.has_value() ? stop->claim_note : std::nullopt;
}

// The span being held open. Slim on purpose: the EVIDENCE lives in the hand table, so what a span
// carries is only its statement — which strings at which stops, the authored claims, and the
// handful of facts published at emit that only the walk's own passage through the slots can know.
struct OpenSpan
{
    GridPosition position;
    Fraction front_beat{};

    // The sounded grip: the stop this span states per string, empty where it states none. Growth
    // writes in place (rule 8: adding a stop breaks nothing — growth IS accumulation), a member
    // strike restates in place, and nothing ever removes one: a stop's silence is what BREAKS the
    // grip, never what shrinks it.
    std::vector<std::optional<int>> stops;

    // Which strings joined the grip as evidence of THIS span's own founding or statements —
    // dating members. A ring carried in from ground an earlier span covered, or from behind a
    // displacement junction, states its stop into the posture but never dates the front (the
    // dating rule's one comparison, spent at the open).
    std::vector<StopClaim> claims;

    // Where this span's opening mark draws, published to \ref ChartShape::bracket_position. Every
    // span an event states seeds it with its own front; a landing successor seeds nothing and the
    // first sounding inside it fills the slot — the ink follows the sound (rule 12 of the law's
    // display section).
    std::optional<GridPosition> bracket_position{};

    // The last instant an EVENT stated this span's shape; empty only on a landing successor
    // nothing has stated yet. Published as \ref ChartShape::stated_extent (the display trim's
    // floor); its presence is the landing emit rule's stated arm (rule 6).
    std::optional<Fraction> last_stated_beat{};

    bool silent_only{false};
    bool justified{false};
    bool sounds_in_parts{false};

    // True on the span a landing opened and nowhere else (rule 6) — the one span no event states
    // at its own start. Published: the census keys landing-born spans on it, and the display keys
    // the bracket deferral on it. Not derivable from `last_stated_beat`, which stops being empty
    // the moment an interior re-pick states the successor.
    bool landing_opened{false};

    // The records whose justification reached this span, published when it is emitted — the
    // deferral that makes emit the one writer of \ref ChartShapes::claim_shapes, since a landing
    // span can still be dropped by the tenure rule and publication must ride the push.
    std::vector<std::size_t> justified_by;
};

// Everything one slot states, read before any span is touched. The reading is pure; the walk's
// verdicts are functions of it and the pre-instant hand table.
struct SlotReading
{
    GridPosition position;
    Fraction beat{};

    // Fretting-hand strikes: the stop each states at its own onset (a channel is never mid-travel
    // at offset zero) — what writes the hand table and what makes a string a member here.
    std::vector<std::optional<int>> strikes;
    std::vector<std::optional<std::size_t>> strike_notes;

    // Where every SOUNDING onset here reaches, whichever hand — what renews a string (rule 8's
    // per-string renewal) and what the sounds column takes.
    std::vector<std::optional<Fraction>> sounding;

    std::vector<StopClaim> claims;
    SoundedStops sounded;

    std::size_t struck{0};
};

} // namespace

ChartShapes deriveChartShapes(
    const std::vector<ChartNote>& saved_notes, const std::vector<std::optional<int>>& claimed_stops,
    const TempoMap& tempo_map)
{
    ChartShapes derived;
    derived.claim_shapes.assign(saved_notes.size(), std::nullopt);

    constexpr auto string_count = static_cast<std::size_t>(g_max_chart_strings);

    // Every onset in beats, once — the walk reasons on one rational axis end to end.
    std::vector<Fraction> onset_beat;
    onset_beat.reserve(saved_notes.size());
    for (const ChartNote& note : saved_notes)
    {
        onset_beat.push_back(beatDistance(tempo_map, GridPosition{}, note.position));
    }
    const auto ring_end_of = [&saved_notes, &onset_beat](const std::size_t note) {
        return onset_beat[note] + saved_notes[note].sustain;
    };
    const auto note_string_index = [](const ChartNote& note) -> std::optional<std::size_t> {
        if (note.string < 1 || note.string > g_max_chart_strings)
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(note.string - 1);
    };

    // THE HAND — the one evidence table (\ref StringHand).
    std::vector<StringHand> hand(string_count);

    // The dating rule's frontier: how far every emitted span reaches. Survives ONLY as a dating
    // floor — the reach never reads it — and that asymmetry is the single thing separating the
    // front from the close, on purpose: ground a statement held is ground it held, but a new
    // figure may still be founded on rings that outlive it.
    Fraction covered{};

    std::optional<OpenSpan> open;
    std::map<std::vector<std::optional<int>>, std::size_t> posture_indices;

    // What the fretting hand covers on one string as of `now` — the channel re-asked at the
    // hand's own finger, so a travel's landing caps the coverage without a second record.
    const auto covers_at = [&saved_notes, &onset_beat, &ring_end_of, &hand](
                               const std::size_t string_index,
                               const Fraction now) -> std::optional<int> {
        const std::optional<std::size_t>& finger = hand[string_index].finger;
        if (!finger.has_value())
        {
            return std::nullopt;
        }
        const StatedStop stated = statedStopFrom(saved_notes[*finger], now - onset_beat[*finger]);
        return stated.fret;
    };

    // Whether the span's statement is still standing at `now` — the continuity law over SOUNDS
    // (rule 8's quit arm), with the per-string renewal the siege repaired in: a member string
    // whose sound ends exactly here and is re-sounded by this slot was REPLACED, not silenced.
    // Claims are outside it entirely — a claim has no evidence, and LAW II is what governs
    // hand-alone spans.
    const auto in_force = [&hand](
                              const OpenSpan& span,
                              const Fraction now,
                              const std::vector<std::optional<Fraction>>& sounding) {
        for (std::size_t string_index = 0; string_index < span.stops.size(); ++string_index)
        {
            if (!span.stops[string_index].has_value() || !hand[string_index].finger.has_value())
            {
                continue;
            }
            const Fraction sounds = hand[string_index].sounds;
            if (now < sounds)
            {
                continue;
            }
            if (now != sounds || !sounding[string_index].has_value())
            {
                return false;
            }
        }
        return true;
    };

    // How far the span's own statement reaches: the minimum of its sounded members' coverage —
    // the fretting hand's rings, landing-capped. Claims never bound (a claim has no evidence);
    // a span whose members are all claims reaches its own start, which is the honest zero.
    const auto span_reach = [&hand](const OpenSpan& span) {
        std::optional<Fraction> reach;
        for (std::size_t string_index = 0; string_index < span.stops.size(); ++string_index)
        {
            if (!span.stops[string_index].has_value() || !hand[string_index].finger.has_value())
            {
                continue;
            }
            const Fraction covers = hand[string_index].covers;
            reach = reach.has_value() ? std::min(*reach, covers) : covers;
        }
        return reach.value_or(span.front_beat);
    };

    // The one act that publishes a span (rule 9: the close is the breaking event's onset or where
    // the statement ran out, whichever is earlier). LAW II's dissolution and rule 6's tenure drop
    // both live here, before anything is pushed, which is what lets publication ride the push.
    const auto emit = [&](const std::optional<Fraction> close_beat,
                          const std::optional<GridPosition>
                              closing_onset) {
        if (!open.has_value())
        {
            return;
        }
        // An unjustified span the hand alone stated dissolves: nothing it could have been
        // fronting exists (LAW II, unchanged).
        if (open->silent_only && !open->justified)
        {
            open.reset();
            return;
        }
        const Fraction reach = span_reach(*open);
        const Fraction end = close_beat.has_value() ? std::min(reach, *close_beat) : reach;
        // The head belongs to the close only where the EVENT actually cut a live statement. A
        // statement that had already run out — the reach strictly before the event's beat — was
        // reach-closed, and its rails owe no distance to a head that arrived after it ended.
        const std::optional<GridPosition> head =
            close_beat.has_value() && !(reach < *close_beat) ? closing_onset : std::nullopt;
        // RULE 6's emit test for the one onset-less span: a landing span is emitted if an event
        // ever stated it, or its tenure STRICTLY EXCEEDS the distinguishability quantum at the
        // closing head's measure. The importer synthesizes every glide-into-restrike arrival
        // exactly one quantum before the replacing onset, so the equality case IS the ratified
        // suppressed population — strict is the whole ruling. A close with no sounding head has
        // no flicker to prevent, so only the degenerate zero-tenure span drops there; a
        // reach-closed landing span emits on its own rings.
        if (open->landing_opened && !open->last_stated_beat.has_value() && close_beat.has_value())
        {
            const Fraction tenure = end - open->front_beat;
            const Fraction quantum = head.has_value()
                                         ? minimumSustainDistanceBeats(
                                               tempo_map.timeSignatureAt(head->measure).denominator)
                                         : Fraction{};
            if (!(quantum < tenure))
            {
                open.reset();
                return;
            }
        }
        std::vector<std::optional<int>> frets = open->stops;
        bool silent_member = false;
        for (const StopClaim& claim : open->claims)
        {
            const bool inside = claim.beat < end || claim.beat == open->front_beat;
            if (!inside)
            {
                continue;
            }
            silent_member = true;
            std::optional<int>& fret = frets[claim.string_index];
            if (!fret.has_value())
            {
                fret = claim.fret;
            }
            std::optional<std::size_t>& reach_entry = derived.claim_shapes[claim.note_index];
            if (!reach_entry.has_value())
            {
                reach_entry = derived.shapes.size();
            }
        }
        for (const std::size_t note_index : open->justified_by)
        {
            std::optional<std::size_t>& reach_entry = derived.claim_shapes[note_index];
            if (!reach_entry.has_value())
            {
                reach_entry = derived.shapes.size();
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
                .sustain = end - open->front_beat,
                .stated_extent = std::min(open->last_stated_beat.value_or(open->front_beat), end) -
                                 open->front_beat,
                .closing_onset = head,
                .posture = entry->second,
                .silent_member = silent_member,
                .sounds_in_parts = open->sounds_in_parts,
                .landing_opened = open->landing_opened,
                .bracket_position = open->bracket_position,
            });
        covered = std::max(covered, end);
        open.reset();
    };

    // RULE 6 — the landing, the one onset-less open, resolved BEFORE any slot at its instant is
    // judged (the within-instant law's first sentence): the predecessor closes at the landing and
    // the landed grip stands before the slot's own verdicts run against it. "Held through the
    // slide" is end-inclusive at the landing instant — a member's ring dying exactly AT the
    // landing belonged to the predecessor (the seam ownership) — and the survivors are the
    // members whose channels state a stop at the boundary and whose sound runs STRICTLY past it,
    // at the statement threshold: they were already established members of the span that just
    // closed, so nothing here is an accumulation.
    const auto settle_landings = [&](const Fraction now) {
        while (open.has_value())
        {
            const Fraction boundary = span_reach(*open);
            if (now < boundary)
            {
                return;
            }
            // The boundary's own grid position, advanced from the span it closes — the one
            // origin whose distance to the boundary the walk knows exactly.
            const GridPosition boundary_position =
                advanceGridPosition(tempo_map, open->position, boundary - open->front_beat);
            // Is this boundary a LANDING — does some member's travel arrive exactly here? A
            // boundary no travel arrives at is a death, and rule 7 says a death hands nothing
            // on: the survivors ring out as plain tails.
            bool arrived = false;
            std::vector<std::optional<int>> landed(string_count);
            std::size_t survivors = 0;
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                if (!open->stops[string_index].has_value())
                {
                    continue;
                }
                const std::optional<std::size_t>& finger = hand[string_index].finger;
                if (!finger.has_value())
                {
                    continue;
                }
                const StatedStop stated =
                    statedStopFrom(saved_notes[*finger], boundary - onset_beat[*finger]);
                // Bound to a local so the presence test and the read are provably one object.
                const std::optional<int>& stop = stated.fret;
                if (!stop.has_value())
                {
                    // Mid-travel states nothing — the staggered slide refusing itself (rule 6's
                    // edge, by scope).
                    continue;
                }
                if (!(boundary < hand[string_index].sounds))
                {
                    // "Held through" is end-inclusive at the landing: a ring dying exactly here
                    // belonged to the closing span (the seam ownership), and only members ringing
                    // STRICTLY past the boundary survive into the landed grip.
                    continue;
                }
                landed[string_index] = stop;
                ++survivors;
                // A travel arriving exactly at the boundary is what makes it a landing at all:
                // this member's coverage was capped at its arrival while its ring runs past it.
                arrived = arrived || (hand[string_index].covers == boundary &&
                                      boundary < ring_end_of(*finger));
            }
            std::vector<StopClaim> carried_claims = open->claims;
            const bool opens_successor = arrived && survivors >= g_span_member_threshold;
            if (!opens_successor)
            {
                // A boundary no landing hands over at is the SLOT PATH's business, not this
                // pass's: a death may still be renewed by the slot standing at it (the per-string
                // renewal in the quit arm), and where it is not, the ordinary break emits with
                // the close capped at the reach. Closing here instead would end a chug at every
                // restrike — the migration's own first bug.
                return;
            }
            emit(boundary, std::nullopt);
            OpenSpan successor{
                .position = boundary_position,
                .front_beat = boundary,
                .stops = std::move(landed),
                // The fingers slid; they never lifted — the authored records ride the statement
                // they were authored against.
                .claims = std::move(carried_claims),
                .bracket_position = std::nullopt,
                .last_stated_beat = std::nullopt,
                .silent_only = false,
                .justified = false,
                .sounds_in_parts = false,
                .landing_opened = true,
                .justified_by = {},
            };
            // The landing restarts each survivor's fretting-hand coverage at the landed grip —
            // re-read from the channel, so a multi-leg glide's NEXT departure still caps it
            // rather than the restart silently extending coverage to the ring's end.
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                if (!successor.stops[string_index].has_value())
                {
                    continue;
                }
                const std::optional<std::size_t>& finger = hand[string_index].finger;
                if (!finger.has_value())
                {
                    continue;
                }
                const StatedStop landed_state =
                    statedStopFrom(saved_notes[*finger], boundary - onset_beat[*finger]);
                const std::optional<FretTravel>& next_travel = landed_state.travel;
                const Fraction ring = ring_end_of(*finger);
                hand[string_index].covers =
                    next_travel.has_value()
                        ? std::min(ring, onset_beat[*finger] + next_travel->arrival)
                        : ring;
            }
            open = std::move(successor);
        }
    };

    std::size_t index = 0;
    while (index < saved_notes.size())
    {
        // ---- 1. READ THE SLOT (pure; nothing span-shaped is touched yet) --------------------
        SlotReading slot{
            .position = saved_notes[index].position,
            .beat = onset_beat[index],
            .strikes = std::vector<std::optional<int>>(string_count),
            .strike_notes = std::vector<std::optional<std::size_t>>(string_count),
            .sounding = std::vector<std::optional<Fraction>>(string_count),
            .claims = {},
            .sounded = SoundedStops(string_count),
            .struck = 0,
        };
        std::size_t onset_end = index;
        while (onset_end < saved_notes.size() && saved_notes[onset_end].position == slot.position)
        {
            const ChartNote& member = saved_notes[onset_end];
            const std::optional<std::size_t> string_index = note_string_index(member);
            const std::optional<int> claim = claimed_stops[onset_end];
            if (string_index.has_value() && !silentHold(member.attack))
            {
                slot.sounding[*string_index] = ring_end_of(onset_end);
            }
            if (claim.has_value() && string_index.has_value())
            {
                slot.claims.push_back(
                    StopClaim{
                        .note_index = onset_end,
                        .string_index = *string_index,
                        .beat = slot.beat,
                        .fret = *claim,
                    });
            }
            if (string_index.has_value() && !silentHold(member.attack) &&
                !rightHandOnset(member.attack))
            {
                // A channel is never mid-travel at offset zero, so this always states a stop.
                const StatedStop struck = statedStopFrom(member, Fraction{});
                if (struck.fret.has_value())
                {
                    slot.sounded[*string_index] =
                        SoundedStop{.fret = *struck.fret, .claim_note = std::nullopt};
                    slot.strikes[*string_index] = *struck.fret;
                    slot.strike_notes[*string_index] = onset_end;
                    ++slot.struck;
                }
            }
            else if (string_index.has_value() && rightHandOnset(member.attack) && claim.has_value())
            {
                // What a right-hand onset sounds on the fret axis is the stop the other hand
                // holds under it — the tap-harmonic arm, and the whole of the tap's held-fret
                // participation on the statement path.
                slot.sounded[*string_index] = SoundedStop{.fret = *claim, .claim_note = onset_end};
            }
            ++onset_end;
        }

        // ---- 2. LANDINGS RESOLVE FIRST -------------------------------------------------------
        settle_landings(slot.beat);

        // ---- 3. JUSTIFY a hand-alone span with this slot's sounded stops (LAW II) -----------
        if (open.has_value() && open->silent_only && !open->justified)
        {
            for (const StopClaim& claim : open->claims)
            {
                if (!answersClaim(claim, slot.sounded))
                {
                    continue;
                }
                open->justified = true;
                open->justified_by.push_back(claim.note_index);
                const std::optional<std::size_t> answering =
                    soundingClaimNote(slot.sounded, claim.string_index);
                if (answering.has_value())
                {
                    open->justified_by.push_back(*answering);
                }
            }
        }

        // ---- 4. THE VERDICT, against the PRE-instant hand table ------------------------------
        // Standing = the span's statement is still in force here, with per-string renewal (a
        // member whose sound ends exactly here and is re-sounded was replaced, not silenced).
        const bool standing = open.has_value() && in_force(*open, slot.beat, slot.sounding);

        // WHAT THIS SLOT STATES per string, strikes and claims as one table: a silent hold and a
        // tap's held fret are STATEMENTS about where the fretting hand is, exactly as a strike is
        // (rule 2 — the held fret participates fully on the statement path), so the contradiction
        // and displacement witnesses read them identically. A slot never states one string twice:
        // two records at one (position, string) are a collision, not an overlap.
        std::vector<std::optional<int>> stated_here(string_count);
        for (std::size_t string_index = 0; string_index < string_count; ++string_index)
        {
            stated_here[string_index] = slot.strikes[string_index];
        }
        for (const StopClaim& claim : slot.claims)
        {
            if (!stated_here[claim.string_index].has_value())
            {
                stated_here[claim.string_index] = claim.fret;
            }
        }

        // THE DISPLACEMENT WITNESS (Law A), against the PRE-instant hand and read
        // end-INCLUSIVELY: a statement replacing a DIFFERENT sounding stop is the finger
        // observably moving, whether or not any span stands — the same-string clamp puts the
        // displaced ring's end exactly on the displacing strike, so this instant is the only one
        // at which the two coexist. A slide-out ring asserts no grip: by its end the finger is
        // off the board (LAW I's exemption, mirrored from the import twin). Same stop is the tie
        // doctrine and witnesses nothing. Computed once and spent twice — as rule 8's foreign-
        // ring break below, and as the junction record the dating floor reads at every open.
        std::vector<bool> displaced_here(string_count, false);
        std::vector<bool> sounding_before(string_count, false);
        for (std::size_t string_index = 0; string_index < string_count; ++string_index)
        {
            const std::optional<int>& stated_stop = stated_here[string_index];
            if (!stated_stop.has_value())
            {
                continue;
            }
            const std::optional<std::size_t>& finger = hand[string_index].finger;
            const bool sounded = finger.has_value() && slot.beat <= hand[string_index].sounds &&
                                 !saved_notes[*finger].slide_out.has_value();
            sounding_before[string_index] = sounded;
            const std::optional<int> held =
                sounded ? covers_at(string_index, slot.beat) : std::nullopt;
            displaced_here[string_index] = held.has_value() && *held != *stated_stop;
        }

        // A CONTRADICTION breaks the grip (rule 8): a statement naming a different stop on a
        // string whose stop the span SOUNDED, displacing a stop the hand still audibly holds
        // anywhere (Law A's foreign-ring break: the grip provably moved even where the span
        // never stated that string), or crossing a carried claim per the graded witness below.
        bool contradiction = false;
        if (standing)
        {
            for (std::size_t string_index = 0; string_index < string_count && !contradiction;
                 ++string_index)
            {
                const std::optional<int>& stated_stop = stated_here[string_index];
                if (!stated_stop.has_value())
                {
                    continue;
                }
                if (displaced_here[string_index])
                {
                    contradiction = true;
                    continue;
                }
                const std::optional<int>& stated = open->stops[string_index];
                if (stated.has_value() && *stated != *stated_stop)
                {
                    contradiction = true;
                    continue;
                }
                // THE CLAIM WITNESS, and it is graded because EVIDENCE OUTRANKS ASSERTION (the
                // sighted Law A semantics). A differing CLAIM against a carried claim always
                // breaks: assertion against assertion is the charter re-authoring the hand, and
                // there is no evidence for either side to outrank. A differing STRIKE breaks a
                // carried claim only where the grip is ESTABLISHED — the span has sounded
                // members — and the string is silent: an established grip's assertion says where
                // the finger IS, so playing elsewhere is a different hand (the re-pick split). A
                // strike over a string still SOUNDING its stop is the tie doctrine (displacement
                // above heard any disagreement), and a strike against a still-assembling SILENT
                // statement is new evidence arriving, not a contradiction — a lone one joins the
                // assembly, a full statement replaces it (LAW II's territory, below).
                for (const StopClaim& claim : open->claims)
                {
                    if (claim.string_index != string_index || claim.fret == *stated_stop)
                    {
                        continue;
                    }
                    const bool statement_is_claim = !slot.strikes[string_index].has_value();
                    if (statement_is_claim ||
                        (!open->silent_only && !sounding_before[string_index]))
                    {
                        contradiction = true;
                    }
                }
            }
        }

        // AN EVIDENCE-LESS STATEMENT IS REPLACED, NOT GROWN (migration finding, the honest
        // boundary of item 1's no-guard ruling): the sound-overlap proof — "a dead ring fires the
        // quit arm first" — needs SOUNDED evidence, and an unjustified hand-alone span has none
        // to quit. A slot stating a grip of its own that does not answer such a span's claims
        // (justification ran above, before this verdict) is rule 5's "a new grip replaces it":
        // the claim-only statement dissolves unheard (LAW II) and the real statement opens its
        // own span. A lone addition still joins the assembling statement.
        const bool replaces_unjustified =
            standing && open->silent_only && !open->justified &&
            slot.struck + slot.claims.size() >= g_span_member_threshold;

        // THE UNISON RESTATEMENT SPLIT (user ruling 2026-09-03, widened 2026-09-04 on a corpus
        // sighting): a stroke striking EVERY stop the span states is the whole grip said again
        // in unison — a CHORD statement, no longer texture — and it closes the span and founds
        // a chord span of its own through the ordinary slot open below, in exactly two cases.
        // A sounds-in-parts span splits under any whole restatement (the original ruling: the
        // arpeggio's grip strummed whole is a chord). And ANY span splits when the stroke also
        // strikes a string it never stated — a strict superset states the whole chord AND MORE,
        // which is a new statement, never growth (the widening: a rung dyad followed by the
        // full chord strummed is two statements, not a dyad quietly growing into a figure the
        // late texture then brackets whole). What continues is exactly the chug chain: a
        // never-in-parts span restruck at precisely its own grip. The FOUNDING strum never
        // splits (no span stands at its own open), and a PARTIAL restatement rides as texture.
        // Claim-carrying spans stand OUTSIDE this arm for now: a strike first-sounding a
        // claimed stop is that statement ARRIVING (LAW II's justification), not a restatement,
        // and the strum-plus-held-finger figure keeps continuing until sighted otherwise.
        // Equal frets need no check of their own — a differing fret on a stated string already
        // broke as a contradiction above.
        bool unison_restatement = false;
        if (standing && !contradiction && open->claims.empty())
        {
            std::size_t stated_count = 0;
            bool restates_whole = true;
            bool strikes_beyond_grip = false;
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                if (!open->stops[string_index].has_value())
                {
                    strikes_beyond_grip =
                        strikes_beyond_grip || slot.strikes[string_index].has_value();
                    continue;
                }
                ++stated_count;
                restates_whole = restates_whole && slot.strikes[string_index].has_value();
            }
            unison_restatement = restates_whole && stated_count >= g_span_member_threshold &&
                                 (open->sounds_in_parts || strikes_beyond_grip);
        }

        // ---- 5. DISPOSE: continue / grow / break-and-maybe-open ------------------------------
        if (standing && !contradiction && !replaces_unjustified && !unison_restatement)
        {
            // GROWTH IS ACCUMULATION (rule 8, user item 1): every struck or claimed stop the grip
            // lacks joins in place; the quit arm is what guarantees absorption only ever unions
            // grips whose sounds genuinely overlap. Same-grip restatements ride as continuation.
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                const std::optional<int>& struck_stop = slot.strikes[string_index];
                if (struck_stop.has_value())
                {
                    open->stops[string_index] = *struck_stop;
                }
            }
            if (slot.struck > 0)
            {
                open->last_stated_beat = slot.beat;
                std::size_t sounded_members = 0;
                for (std::size_t string_index = 0; string_index < string_count; ++string_index)
                {
                    if (open->stops[string_index].has_value() &&
                        hand[string_index].finger.has_value())
                    {
                        ++sounded_members;
                    }
                }
                open->sounds_in_parts = open->sounds_in_parts || slot.struck < sounded_members;
                if (!open->bracket_position.has_value())
                {
                    open->bracket_position = slot.position;
                }
            }
            else if (!slot.claims.empty())
            {
                open->last_stated_beat = slot.beat;
            }
        }
        else
        {
            // The grip broke here (a contradiction, or the statement no longer stands). The close
            // carries this slot's sounding head where one exists; a slot of held fingers sounds
            // nothing to keep a distance from.
            // The close carries a sounding head only where the FRETTING hand strikes here — the
            // display trim keeps its distance from a head that states the new grip, and a slot
            // of held fingers or bare taps states none (the shipped convention, kept).
            emit(
                slot.beat,
                slot.struck > 0 ? std::optional<GridPosition>{slot.position} : std::nullopt);

            // THE SLOT OPEN (rules 4 and 5, one disjunction): the slot's own stated members, plus
            // the rings still sounding strictly past this instant on strings it does not state —
            // read STRICTLY, the membership window.
            std::vector<std::optional<int>> stops(string_count);
            std::vector<bool> dates(string_count, false);
            std::size_t own = 0;
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                const std::optional<int>& struck_stop = slot.strikes[string_index];
                if (struck_stop.has_value())
                {
                    stops[string_index] = *struck_stop;
                    dates[string_index] = true;
                    ++own;
                }
            }
            own += slot.claims.size();
            std::size_t total = own;
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                if (stops[string_index].has_value())
                {
                    continue;
                }
                const std::optional<std::size_t>& finger = hand[string_index].finger;
                if (!finger.has_value() || !(slot.beat < hand[string_index].sounds))
                {
                    continue;
                }
                const std::optional<int> carried = covers_at(string_index, slot.beat);
                if (!carried.has_value())
                {
                    // Mid-travel states no grip and joins no posture.
                    continue;
                }
                // A carry never folds in on a string this slot STATES OTHERWISE: a claim at a
                // different stop is proof the finger left the ring, so the ring is a tail and the
                // claim's stop is the grip's (it joins through the claims path at emit). This one
                // scope is what replaced the old machine's whole supersession apparatus.
                const std::optional<int>& stated_stop = stated_here[string_index];
                if (stated_stop.has_value() && *stated_stop != *carried)
                {
                    continue;
                }
                stops[string_index] = *carried;
                dates[string_index] = true;
                ++total;
            }
            const bool opens =
                own >= g_span_member_threshold || total >= g_accumulation_member_minimum;
            if (opens)
            {
                // THE FRONT: one floor — the coverage frontier and the displacement junctions of
                // every stated string — and the earliest member onset at or after it dates the
                // span. Members behind the floor state their stops and date nothing.
                Fraction floor = covered;
                for (std::size_t string_index = 0; string_index < string_count; ++string_index)
                {
                    if (stops[string_index].has_value())
                    {
                        floor = std::max(floor, hand[string_index].displaced_at);
                    }
                }
                GridPosition front = slot.position;
                Fraction front_beat = slot.beat;
                for (std::size_t string_index = 0; string_index < string_count; ++string_index)
                {
                    if (!stops[string_index].has_value() || !dates[string_index])
                    {
                        continue;
                    }
                    const std::optional<std::size_t>& finger = hand[string_index].finger;
                    const std::optional<std::size_t>& striking = slot.strike_notes[string_index];
                    const std::size_t member =
                        striking.has_value() ? *striking : finger.value_or(saved_notes.size());
                    if (member >= saved_notes.size())
                    {
                        continue;
                    }
                    const Fraction onset = onset_beat[member];
                    if (onset < floor)
                    {
                        continue;
                    }
                    if (onset < front_beat)
                    {
                        front_beat = onset;
                        front = saved_notes[member].position;
                    }
                }
                const bool silent = slot.struck == 0 && total == slot.claims.size();
                open = OpenSpan{
                    .position = front,
                    .front_beat = front_beat,
                    .stops = std::move(stops),
                    .claims = {},
                    .bracket_position = front,
                    .last_stated_beat = slot.beat,
                    .silent_only = silent,
                    .justified = false,
                    .sounds_in_parts = false,
                    .landing_opened = false,
                    .justified_by = {},
                };
                if (slot.struck > 0)
                {
                    std::size_t sounded_members = 0;
                    for (std::size_t string_index = 0; string_index < string_count; ++string_index)
                    {
                        if (open->stops[string_index].has_value() &&
                            (slot.strikes[string_index].has_value() ||
                             hand[string_index].finger.has_value()))
                        {
                            ++sounded_members;
                        }
                    }
                    open->sounds_in_parts = slot.struck < sounded_members;
                }
            }
        }

        // ---- 6. ATTACH this slot's claims to whatever stands ---------------------------------
        if (open.has_value())
        {
            for (const StopClaim& claim : slot.claims)
            {
                const std::optional<int>& stated = open->stops[claim.string_index];
                const bool restates = stated.has_value() && *stated == claim.fret;
                if (!restates)
                {
                    open->claims.push_back(claim);
                }
            }
            // A claim this same slot makes can be answered by the slot's own sounded stops (the
            // tap-harmonic arm): one record stating two facts at one instant.
            if (open->silent_only && !open->justified)
            {
                for (const StopClaim& claim : open->claims)
                {
                    if (answersClaim(claim, slot.sounded))
                    {
                        open->justified = true;
                        open->justified_by.push_back(claim.note_index);
                    }
                }
            }
        }

        // ---- 7. APPLY the slot to the hand table (after every verdict has read it) -----------
        for (std::size_t note_at = index; note_at < onset_end; ++note_at)
        {
            const ChartNote& member = saved_notes[note_at];
            const std::optional<std::size_t> string_index = note_string_index(member);
            if (!string_index.has_value() || silentHold(member.attack))
            {
                continue;
            }
            StringHand& string_hand = hand[*string_index];
            const Fraction ring = ring_end_of(note_at);
            string_hand.sounds = std::max(string_hand.sounds, ring);
            if (rightHandOnset(member.attack))
            {
                continue;
            }
            string_hand.finger = note_at;
            const StatedStop struck = statedStopFrom(member, Fraction{});
            const std::optional<FretTravel>& travel = struck.travel;
            string_hand.covers =
                travel.has_value() ? std::min(ring, slot.beat + travel->arrival) : ring;
            if (displaced_here[*string_index])
            {
                string_hand.displaced_at = slot.beat;
            }
            else if (!sounding_before[*string_index])
            {
                // A fresh strike over SILENCE resets a stale junction: silence is not
                // contradiction, and a floor from a long-dead figure bounds nothing. A same-stop
                // restatement over a sounding string is the tie doctrine and changes nothing.
                string_hand.displaced_at = Fraction{};
            }
        }

        index = onset_end;
    }

    // The stream's end: run the whole landing chain out. The horizon is past every ring, because
    // a chained second landing can lie past the first span's own reach (the migration's second
    // bug — a single evaluation resolved only one link).
    Fraction horizon{};
    for (std::size_t note = 0; note < saved_notes.size(); ++note)
    {
        horizon = std::max(horizon, ring_end_of(note));
    }
    settle_landings(horizon);
    emit(std::nullopt, std::nullopt);

    return derived;
}

std::vector<bool> chartShapeArrivals(
    const std::vector<ChartNote>& notes, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map)
{
    std::vector<bool> arpeggio;
    arpeggio.reserve(shapes.size());
    std::size_t next_note = 0;
    for (const ChartShape& shape : shapes)
    {
        while (next_note < notes.size() && notes[next_note].position < shape.position)
        {
            ++next_note;
        }
        if (shape.silent_member || shape.sounds_in_parts)
        {
            arpeggio.push_back(true);
            continue;
        }
        // The one trigger still derived here, because it is a question about the span's extent:
        // the other hand sounding while the fretting hand held the shape. The musical close
        // bounds the window (rule 9); display's margin has nothing to say about it.
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
