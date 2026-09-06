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

    // The offset at which the stated stop's own statement BEGAN inside this channel: zero where
    // the channel never left the onset's stop, and the travel's arrival where a glide came to rest
    // on it — a landing is where a new statement is established (rule 10). Mid-travel it names
    // where the statement being travelled into will begin, the only honest answer while the finger
    // is on no stop at all. Read together with \ref StringHand::stated_since this is what dates a
    // slid string's grip with no second record of the landing kept anywhere, exactly as the same
    // re-ask caps that string's coverage there.
    Fraction stated_from{};
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
    Fraction stop_from{};
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
        stop_from = travel->arrival;
        held = keyframe.offset;
        travel.reset();
    }
    // Bound once so the presence test and every read below are provably the same object.
    const std::optional<FretTravel>& leaves = travel;
    if (!leaves.has_value() || from <= leaves->departure)
    {
        return StatedStop{.fret = stop, .travel = travel, .stated_from = stop_from};
    }
    if (from < leaves->arrival)
    {
        return StatedStop{.fret = std::nullopt, .travel = travel, .stated_from = leaves->arrival};
    }
    return StatedStop{.fret = leaves->fret, .travel = std::nullopt, .stated_from = leaves->arrival};
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

    // WHEN THE CURRENT STOP'S STATEMENT BEGAN — the dating rule's whole state, and the one thing
    // a span's front is measured from. THE TIE DOCTRINE (user ruling 2026-09-05): a same-stop
    // restrike whose predecessor's ring reaches it is one statement said twice, not a new one, so
    // it INHERITS the beginning rather than starting its own — transitively, since the value it
    // inherits may itself be inherited, and a chain of restrikes is still one statement with one
    // beginning. Written only by a fretting-hand strike, because only the fretting hand states a
    // stop; the LANDING half needs no record at all, since \ref StatedStop::stated_from re-asks
    // the channel for it exactly as \ref covers_at re-asks the stop. Zero where the hand has never
    // sounded the string. Read through \ref stated_since_at, never bare — a bare read misses the
    // landing.
    Fraction stated_since{};

    // The end of the last FOREIGN sound on this string — the latest instant it audibly sounded a
    // stop other than the one its current grip states. The dating clamp's whole state (Law A): a
    // span may not date its front across an instant at which one of its stated strings audibly
    // held another stop, because the fronted bracket asserts its whole grip from its start. A
    // displacement is the special case whose foreign end IS the displacing strike (the
    // same-string clamp puts them on one instant); a foreign ring that died into silence bounds
    // just as hard at its own end (the sighted gap figures), so nothing resets this — the bound
    // records what the string audibly did, not how the grip was established. Zero where the
    // string never sounded a foreign stop.
    Fraction foreign_until{};
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
    const std::vector<std::optional<int>>& planted_stops, const TempoMap& tempo_map)
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

    // When the statement of the stop this string holds as of `now` BEGAN — the hand's own column
    // (\ref StringHand::stated_since) re-asked at the channel, exactly as \ref covers_at re-asks
    // the stop. The two halves of one question: a strike's statement begins where the strike does
    // unless the tie doctrine hands it an earlier beginning, and a TRAVEL's statement begins at
    // the landing it comes to rest on, which the channel already knows — so the landing needs no
    // write anywhere and a lone glide under no span dates as honestly as one inside a shape. Zero
    // where the hand has never sounded the string.
    const auto stated_since_at = [&saved_notes, &onset_beat, &hand](
                                     const std::size_t string_index, const Fraction now) {
        const std::optional<std::size_t>& finger = hand[string_index].finger;
        if (!finger.has_value())
        {
            return Fraction{};
        }
        const StatedStop stated = statedStopFrom(saved_notes[*finger], now - onset_beat[*finger]);
        // A finger that has LANDED somewhere new began its statement there, and one still on the
        // stop it was struck at began it wherever the hand's column says — which may be long
        // before this note, since the column is what a chain of restrikes carries.
        return stated.stated_from == Fraction{} ? hand[string_index].stated_since
                                                : onset_beat[*finger] + stated.stated_from;
    };

    // How far the fretting hand's own statement on this string reaches, measured from `now`: the
    // finger's ring, capped at the next landing its channel comes to rest at (\ref
    // StringHand::covers). ONE authority for the three moments a statement (re)starts and the cap
    // has to be re-read — a strike stating it, a landing handing it on, and a carry folding into a
    // new span. The third moment is why this is a function at all: a landing NO SPAN WAS STANDING
    // TO WITNESS restarts coverage exactly as a witnessed one does, and without the re-read a lone
    // glide's coverage stayed frozen at its first arrival forever, so the next span to fold that
    // string in reached only as far as a landing long past.
    const auto coverage_at = [&saved_notes, &onset_beat, &ring_end_of, &hand](
                                 const std::size_t string_index, const Fraction now) {
        const std::optional<std::size_t>& finger = hand[string_index].finger;
        if (!finger.has_value())
        {
            return Fraction{};
        }
        const Fraction onset = onset_beat[*finger];
        const StatedStop stated = statedStopFrom(saved_notes[*finger], now - onset);
        const Fraction ring = ring_end_of(*finger);
        // Bound once so the presence test and the read are provably the same object.
        const std::optional<FretTravel>& leaves = stated.travel;
        return leaves.has_value() ? std::min(ring, onset + leaves->arrival) : ring;
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
                hand[string_index].covers = coverage_at(string_index, boundary);
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
        // doctrine and witnesses nothing. The witness feeds rule 8's foreign-ring break below;
        // the FOREIGN-SOUND record beside it is the dating floor's state, written here — from
        // the pre-instant table, before any verdict — so a span opening at this very slot reads
        // a current bound.
        //
        // THE STATEMENT-BEGAN COLUMN for this instant rides the same pass, for the same reason:
        // the dating below reads it before step 7 applies the slot to the hand, so it is a
        // pre-instant verdict like every other. A string this slot does not strike keeps the
        // beginning it already had (its statement did not change here); a strike begins its own
        // statement unless THE TIE DOCTRINE hands it one — the predecessor's ring reaches this
        // onset end-inclusively, the channel states EXACTLY this stop there (positive, so a
        // mid-glide finger inherits nothing), and the predecessor is not a slide-out asserting no
        // grip at all. Inheritance is transitive by construction: what it inherits was itself
        // read through \ref stated_since_at, so a chain of restrikes is one statement with one
        // beginning.
        std::vector<bool> displaced_here(string_count, false);
        std::vector<bool> sounding_before(string_count, false);
        std::vector<Fraction> stated_since_here(string_count);
        // THE HOLD-UNDER LAW (user ruling 2026-09-06, task #176). Whether a stop this slot
        // states and a stop already down on the string are ONE HAND rather than two. A
        // pull-off's SOURCE keeps its destination planted beneath the stop it sounds for the
        // whole of its ring — a finger has to be waiting on a fret to be pulled off onto — so a
        // finger ADDED above a standing grip contradicts nothing, and the grip RE-EMERGING under
        // it as that finger lifts lifts nothing. ONE derivation (\ref chartPlantedStops), read
        // at the figure's two ends: the note stating here, and the string's own finger. The
        // second arm rides `sounding_before`, the very witness the displacement reads, so the
        // ring bound and the slide-out exemption are not spelled a second time — a dead source's
        // planted finger is as gone as its sound.
        //
        // VERDICTS ONLY (THE NARROW FORM, user-ruled). Nothing here reaches the grip column, the
        // statement-began column or the foreign-sound floor: those read `covers_at` bare, which
        // is what keeps a source from backdating a front across ground the string audibly spent
        // on the foreign fret (the 17:3.5 restrike figure, 39c7b865).
        const auto plants_under = [&planted_stops,
                                   &slot](const std::size_t string_index, const int stop) {
            // Bound once so the presence test and the read are provably the same object.
            const std::optional<std::size_t>& striking = slot.strike_notes[string_index];
            return striking.has_value() && planted_stops[*striking] == stop;
        };
        const auto planted_under = [&planted_stops, &hand, &sounding_before](
                                       const std::size_t string_index, const int stop) {
            const std::optional<std::size_t>& finger = hand[string_index].finger;
            return sounding_before[string_index] && finger.has_value() &&
                   planted_stops[*finger] == stop;
        };
        for (std::size_t string_index = 0; string_index < string_count; ++string_index)
        {
            stated_since_here[string_index] = stated_since_at(string_index, slot.beat);
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
            displaced_here[string_index] = held.has_value() && *held != *stated_stop &&
                                           !plants_under(string_index, *held) &&
                                           !planted_under(string_index, *stated_stop);

            // THE FOREIGN-SOUND RECORD (Law A's state): a strike taking this string marks the
            // end of whatever foreign stop it last sounded — the displacing strike's own instant
            // under a displacement, the dead ring's own end after a gap of silence.
            const std::optional<int>& struck_stop = slot.strikes[string_index];
            if (struck_stop.has_value() && finger.has_value() &&
                !saved_notes[*finger].slide_out.has_value())
            {
                const Fraction sounded_until = std::min(hand[string_index].sounds, slot.beat);
                const std::optional<int> last_held = covers_at(string_index, sounded_until);
                // THE FOLD (user ruling 2026-09-06, extending the hold-under law): a spell the
                // hand provably never left is not foreign. A strike planting the last-held stop
                // is the same hand adding a finger, and a strike the sounding finger plants is
                // the same hand releasing one — neither marks a foreign spell, so the floor
                // cannot push a front past ground the plant accounts for.
                if (last_held.has_value() && *last_held != *struck_stop &&
                    !plants_under(string_index, *last_held) &&
                    !planted_under(string_index, *struck_stop))
                {
                    hand[string_index].foreign_until = sounded_until;
                }
            }
            // THE TIE DOCTRINE's one test, over the very witness above: a strike restating the
            // stop its predecessor still audibly holds inherits that statement's beginning, and
            // every other strike begins its own. THE FOLD (user ruling 2026-09-06) gives the
            // doctrine the hold-under law's two arms: a strike PLANTING the still-held stop
            // continues that statement through the ornament it sounds, and a strike the sounding
            // finger PLANTS inherits the statement the plant began — so one 5-7-5 figure is one
            // statement of 5 with one beginning, and a span folds back to where the planted
            // evidence started, which is what fronts the Torn intro at its first note.
            if (struck_stop.has_value())
            {
                const bool statement_continues =
                    (held.has_value() &&
                     (*held == *struck_stop || plants_under(string_index, *held))) ||
                    planted_under(string_index, *struck_stop);
                if (!statement_continues)
                {
                    stated_since_here[string_index] = slot.beat;
                }
            }
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
                if (stated.has_value() && *stated != *stated_stop &&
                    !plants_under(string_index, *stated) &&
                    !planted_under(string_index, *stated_stop))
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
                    // The hold-under exemption takes arm (a) ONLY: a source striking here that
                    // PLANTS the claimed stop is the claim's own grip with a finger added above
                    // it. `planted_under` is deliberately NOT composed — its premise is "the
                    // note sounding the first argument", and no finger sounds a carried claim,
                    // so composing it would exempt a figure whose claim genuinely IS
                    // contradicted (claim 8, finger sounding 10 planting 5, slot stating 5).
                    if (claim.string_index != string_index || claim.fret == *stated_stop ||
                        plants_under(string_index, claim.fret))
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

        // THE STATEMENT-CHARACTER SPLITS (user rulings 2026-09-03 through 09-05, each from a
        // corpus sighting): a span's statements keep ONE character — whole or in parts — and
        // the walk splits where the character turns, so the class is a fact of the span's
        // founding rather than a retroactive verdict on everything it ever contained.
        //
        // Parts -> chord (the unison restatement): a stroke striking EVERY stop the span
        // states is the whole grip said in unison — a chord statement, WHERE IT STANDS ALONE
        // (the absorption rule above, 2026-09-05: a stroke same-hold parts sound under is
        // absorbed and states nothing). It closes a sounds-in-parts span (the arpeggio's grip
        // strummed whole is a chord), and it closes ANY span when it also strikes a string
        // never stated — a strict superset states the whole chord AND MORE, a new statement,
        // never growth.
        //
        // Chord -> parts: a stroke sounding PART of what a never-in-parts span STATED — some
        // of its own stops, not all — is the statement coming apart, so the chord span closes
        // here and the partial founds the parts span through the ordinary slot open below —
        // which takes the still-ringing members in as carried texture, dates the span at this
        // slot (the carried onsets lie behind the coverage floor), and births it in parts. The
        // bracket therefore covers exactly the ground that sounds in parts. A stroke on
        // strings the span never stated is not this direction at all: it states nothing about
        // the span's own stops coming apart, so it is the statement still assembling — growth,
        // exactly as ruled 2026-09-04. This direction reads the ARITHMETIC alone and never the
        // absorption: a stroke absorbed by the parts that follow is the span FLOWING, so
        // splitting it here would only move the fragmentation one slot earlier.
        //
        // What continues is exactly the chug chain: a never-in-parts span restruck at
        // precisely its own grip. The FOUNDING slot never splits (no span stands at its own
        // open). Claim-carrying spans stand OUTSIDE both directions for now: a strike at a
        // claim-carrying span is evidence arriving against the claims (LAW II), not a
        // character turn, so those figures keep the riding behavior until sighted. A differing
        // fret on a stated string no longer always breaks above — THE HOLD-UNDER LAW exempts a
        // pull-off source planting the grip's stop — so the direction's arithmetic counts a
        // string as touched only where the strike RESTATES the span's own stop; the source's
        // ornament above the grip is neither the statement coming apart nor a restatement.
        // Whether any stated member's finger is MID-TRAVEL at this slot — the rule-8/10 fact
        // ("fingers travelling together carry the statement; the close belongs to the landing")
        // that the character split AND the class flip in the dispose arm both read: transit is
        // neither the figure coming apart nor its class turning.
        bool member_travelling = false;
        if (standing)
        {
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                if (open->stops[string_index].has_value() &&
                    hand[string_index].finger.has_value() &&
                    !covers_at(string_index, slot.beat).has_value())
                {
                    member_travelling = true;
                    break;
                }
            }
        }

        // A PARTIAL SLIDE (user ruling 2026-09-05, in the sighting's own words "that chord is
        // split mid sustain"): this slot's statement is divided by its own notated rings — a
        // held member's ring ends STRICTLY BEFORE a co-struck glide arrives, so that member's
        // sound dies while the statement is still in flight and the figure necessarily sounds
        // in parts from this very slot. A voicing-shift slide whose held strings ring the whole
        // transit stays one statement (the chug that slides up is still a chug), and a
        // whole-grip travel is the ruled chord slide ([D2]) with no holder to divide it.
        Fraction latest_arrival{};
        std::optional<Fraction> shortest_hold;
        for (std::size_t string_index = 0; string_index < string_count; ++string_index)
        {
            const std::optional<std::size_t>& striking = slot.strike_notes[string_index];
            if (!striking.has_value())
            {
                continue;
            }
            const ChartNote& member = saved_notes[*striking];
            const StatedStop stated = statedStopFrom(member, Fraction{});
            if (stated.travel.has_value())
            {
                latest_arrival = std::max(latest_arrival, stated.travel->arrival);
            }
            else if (!shortest_hold.has_value() || member.sustain < *shortest_hold)
            {
                shortest_hold = member.sustain;
            }
        }
        const bool partial_slide = shortest_hold.has_value() && *shortest_hold < latest_arrival;

        // Whether this slot's stroke says the WHOLE of a grip: every stop of it that SOUNDS here —
        // struck now, or under a finger the hand table already holds — is one this stroke struck.
        // ONE authority for the three sites that ask whether a stroke is a chord statement (the
        // break arm, the founding class, and the dispose arm's in-place turn): the review of
        // 2026-09-05 found two hand-written copies of this arithmetic had diverged, and the
        // divergent copy made a figure's class depend on whether its strings had ever sounded
        // earlier in the chart.
        const auto stroke_says_whole =
            [&slot, &hand, string_count](const std::vector<std::optional<int>>& stops) {
                for (std::size_t string_index = 0; string_index < string_count; ++string_index)
                {
                    // A stroke says a STOP, not a string: a strike at a DIFFERENT fret is not a
                    // restatement of this one. Equality rather than presence is a no-op on any
                    // stream without the hold-under law (a differing fret on a stated string
                    // broke as a contradiction before reaching here) and load-bearing under it —
                    // a source striking above the grip must not count as the grip restated.
                    if (!stops[string_index].has_value() ||
                        slot.strikes[string_index] == stops[string_index])
                    {
                        continue;
                    }
                    if (hand[string_index].finger.has_value())
                    {
                        return false;
                    }
                }
                return true;
            };

        // THE ABSORPTION RULE (user ruling 2026-09-05), which DECOUPLES the two laws a whole-grip
        // stroke used to state at once. THE BOX LAW is display and UNCONDITIONAL — simultaneously
        // struck notes wear a chord box wherever they fall, spans included — and nothing here
        // touches it: the display boxes every co-struck group on its own fretting-hand count and
        // never reads a span's class. THE SPAN LAW is structure and CONDITIONAL: a whole-grip
        // stroke is a span BOUNDARY, and a box-class statement, only where it STANDS ALONE.
        //
        // It does not stand alone when SAME-HOLD MATERIAL SOUNDS IN PARTS INSIDE ITS OWN RINGS,
        // and then it is ABSORBED — the standing span flows through it, its members fold in as
        // same-stop restatements, and it wears its box inside the span. Anything else (another
        // unison, a contradiction, growth) commits the judgment exactly as before. The evidence
        // is never the ring-divergence of the stroke's OWN members; it is what FOLLOWS within the
        // rings, so the whole of it is one predicate over the stored stream — this stroke and the
        // slot after it, asked before any verdict runs, which is what keeps every verdict below a
        // function of the pre-instant hand table alone.
        const auto absorbed_by_the_next_slot = [&] {
            if (onset_end >= saved_notes.size())
            {
                return false;
            }
            const Fraction next_beat = onset_beat[onset_end];
            // THE PARTS: what the next slot sounds by the FRETTING hand. Every one of them must
            // be a stop THIS stroke struck, at that same stop, and they must be strictly fewer —
            // anything else is a contradiction or growth, and a slot restating the whole set is
            // another unison (the chug chain).
            std::vector<bool> restated(string_count, false);
            std::size_t parts = 0;
            for (std::size_t ahead = onset_end;
                 ahead < saved_notes.size() &&
                 saved_notes[ahead].position == saved_notes[onset_end].position;
                 ++ahead)
            {
                const ChartNote& member = saved_notes[ahead];
                if (silentHold(member.attack) || rightHandOnset(member.attack))
                {
                    continue;
                }
                const std::optional<std::size_t> string_index = note_string_index(member);
                if (!string_index.has_value())
                {
                    return false;
                }
                // A channel is never mid-travel at offset zero, so this always states a stop.
                const StatedStop sounds = statedStopFrom(member, Fraction{});
                const std::optional<int>& stated = slot.strikes[*string_index];
                if (!stated.has_value() || !sounds.fret.has_value() || *stated != *sounds.fret)
                {
                    return false;
                }
                restated[*string_index] = true;
                ++parts;
            }
            if (parts == 0 || !(parts < slot.struck))
            {
                return false;
            }
            // THE HOLD UNDERNEATH, and it is what makes the parts sound UNDER the stroke rather
            // than after it: some member the parts do NOT restate is still sounding ITS OWN STOP
            // strictly past them. Both halves are read off the stroke's own stored notes — the
            // ring, and the channel at that instant — and a MID-TRAVEL channel holds nothing,
            // because a finger between stops is a member of nothing. That is the same reading
            // transit gets everywhere else in this file: a chord slide with transit picks is
            // chord frames joined by slide lines, never an arpeggio bracket, so a stroke whose
            // un-restated members are all in flight states its chord and is never absorbed.
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                if (restated[string_index])
                {
                    continue;
                }
                // Bound once so the presence test and every read are provably one object.
                const std::optional<std::size_t>& striking = slot.strike_notes[string_index];
                if (!striking.has_value() || !(next_beat < ring_end_of(*striking)))
                {
                    continue;
                }
                const StatedStop held =
                    statedStopFrom(saved_notes[*striking], next_beat - slot.beat);
                if (held.fret.has_value())
                {
                    return true;
                }
            }
            return false;
        };
        const bool chord_statement_stands = !absorbed_by_the_next_slot();

        bool unison_restatement = false;
        bool partial_sounding = false;
        if (standing && !contradiction && open->claims.empty())
        {
            std::size_t stated_count = 0;
            std::size_t touched_stated = 0;
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
                // Touched means RESTATED: a strike at a different fret on a stated string is
                // the hold-under figure's ornament, not the span's own statement coming apart.
                // Equality is a no-op without the law (a differing fret broke as a
                // contradiction before reaching here) and load-bearing under it.
                touched_stated += slot.strikes[string_index] == open->stops[string_index] ? 1 : 0;
            }
            const bool restates_whole = stroke_says_whole(open->stops);
            unison_restatement = restates_whole && chord_statement_stands &&
                                 stated_count >= g_span_member_threshold &&
                                 (open->sounds_in_parts || strikes_beyond_grip);
            // The chord->parts direction measures the stroke against the span's OWN statement,
            // so it fires only where the stroke touches a stated stop without restating them
            // all — sounding PART of what the span stated is the statement coming apart, while
            // a stroke on strings it never stated is the statement still ASSEMBLING, which is
            // growth exactly as ruled (2026-09-04). Three more guards, each a signed ruling's
            // own ground. A landing successor arrives stated by no event (last_stated_beat
            // empty, a landing is not a sounding), and its FIRST sounding defines its character
            // in place — a lone re-pick turns it into parts where it stands (the 2026-08-30
            // interior-class ruling), splitting nothing. A member MID-TRAVEL blocks the
            // direction whole: fingers travelling together carry the statement (rule 8), the
            // glide is not the figure coming apart, and the close belongs to the landing (rule
            // 10) — so a restrike beside a travelling member rides, per member and not per slot
            // (the 2026-08-29 mid-slide ruling). And a span already IN PARTS wears the bracket
            // that covers partial texture, so partials ride it unchanged.
            partial_sounding = !restates_whole && touched_stated > 0 && !open->sounds_in_parts &&
                               open->last_stated_beat.has_value() && !member_travelling;
            // THE PARTIAL-SLIDE SPLIT (user ruling 2026-09-05, the "split mid sustain"
            // sighting): a partial-slide slot touching a never-in-parts span is the statement
            // coming apart AT this slot — even where it restates the whole grip, since part of
            // that statement immediately leaves while the rest stays — so the box closes here
            // and this slot founds the parts figure, dated at its own onset. It shares every
            // guard the chord->parts direction carries, and the whole-grip slide never fires it.
            partial_sounding = partial_sounding ||
                               (partial_slide && touched_stated > 0 && !open->sounds_in_parts &&
                                open->last_stated_beat.has_value() && !member_travelling);
        }

        // ---- 5. DISPOSE: continue / grow / break-and-maybe-open ------------------------------
        if (standing && !contradiction && !replaces_unjustified && !unison_restatement &&
            !partial_sounding)
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
                // Live only for the spans the character splits exclude — a claim-carrying span,
                // a landing successor's FIRST sounding, growth by strings the span never
                // stated, and the ABSORBED whole-grip stroke the span now flows through — whose
                // class still turns in place, for every way a statement divides: sounding fewer
                // members than sound, the partial slide, and a stroke that stated the whole grip
                // but did not stand alone to state it. A partial beside a
                // TRAVELLING member turns nothing (user ruling 2026-09-05): a chord slide with
                // transit picks is chord frames joined by slide lines, never an arpeggio
                // bracket, so the box the chord earned survives its own slide out. The
                // 2026-08-29 mid-slide protection — the span RIDES the transit, per member, and
                // closes at the landing — is the span-shape half, and it stands above.
                open->sounds_in_parts =
                    open->sounds_in_parts ||
                    (!member_travelling &&
                     (!(stroke_says_whole(open->stops) && chord_statement_stands) ||
                      partial_slide));
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
            std::size_t own = 0;
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                const std::optional<int>& struck_stop = slot.strikes[string_index];
                if (struck_stop.has_value())
                {
                    stops[string_index] = *struck_stop;
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
                // A carry brings its coverage AS OF NOW into the span it joins: the third moment
                // \ref coverage_at names. Nothing was standing to restart this string at the
                // landing it may have passed — the emit above closed whatever was — so without
                // the re-read a lone glide would hand the span it joins a reach frozen at that
                // landing, which is the span the sighted slide figure emitted over its own
                // travel beats. A string a STANDING span states is never re-read: its cap is
                // live, and that cap is what closes the span at its member's landing (rule 10).
                hand[string_index].covers = coverage_at(string_index, slot.beat);
                ++total;
            }
            const bool opens =
                own >= g_span_member_threshold || total >= g_accumulation_member_minimum;
            if (opens)
            {
                // THE FRONT: one floor — the coverage frontier and the foreign-sound ends of
                // every stated string — and the earliest member STATEMENT at or after it dates
                // the span. Members behind the floor state their stops and date nothing.
                //
                // Every dating string reads the ONE statement-began column, struck and carried
                // alike (user ruling 2026-09-05): a member's own onset was never the question —
                // it was a substitute for the beginning of the statement that onset makes, and it
                // answered wrongly in both directions. A restrike of a stop already held began
                // its statement earlier (the tie doctrine), and a slid finger began its statement
                // LATER than the note it rides, at the landing.
                Fraction floor = covered;
                for (std::size_t string_index = 0; string_index < string_count; ++string_index)
                {
                    if (stops[string_index].has_value())
                    {
                        floor = std::max(floor, hand[string_index].foreign_until);
                    }
                }
                Fraction front_beat = slot.beat;
                for (std::size_t string_index = 0; string_index < string_count; ++string_index)
                {
                    if (!stops[string_index].has_value())
                    {
                        continue;
                    }
                    const Fraction stated_since = stated_since_here[string_index];
                    if (stated_since < floor)
                    {
                        continue;
                    }
                    front_beat = std::min(front_beat, stated_since);
                }
                // The front's own grid position, measured back along the beat axis from the slot
                // that opened the span — the one origin whose distance to the front the walk
                // knows exactly, and the same measurement the landing makes forward.
                const GridPosition front =
                    advanceGridPosition(tempo_map, slot.position, front_beat - slot.beat);
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
                    // A span a PARTIAL-SLIDE slot founds is born in parts: the founding
                    // statement itself announces that its members sound separately — the glide
                    // leaves while the held strings stay (user ruling 2026-09-05). A span an
                    // ABSORBED stroke founds is born in parts for the mirror reason: the parts
                    // that sound under its rings are what the figure turns out to be, so the
                    // bracket covers the stroke rather than a one-slot box standing in front of
                    // it (the absorption rule, same day).
                    open->sounds_in_parts =
                        !(stroke_says_whole(open->stops) && chord_statement_stands) ||
                        partial_slide;
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
            // The statement this strike makes began where the pre-instant witness above said it
            // began — its own onset, or the beginning it inherited under the tie doctrine.
            string_hand.stated_since = stated_since_here[*string_index];
            string_hand.covers = coverage_at(*string_index, slot.beat);
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
