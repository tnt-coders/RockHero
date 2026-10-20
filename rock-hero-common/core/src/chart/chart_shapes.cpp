// The chart's shape spans — chord boxes and arpeggio brackets — derived from the notes by one walk
// over the fretting hand's statements. The law this file implements is the shape rules,
// docs/design/rulesets/shape-rules.md; every "shape rule N" below cites that document.

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/chart/head_margins.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::common::core
{
namespace
{

// =================================================================================================
// THE GRIP-TENURE MACHINE.
//
// One idea: a span is the statement "the hand holds this grip, from here to here." The machine
// keeps the EVIDENCE — what each string is doing — in one per-string table that outlives every
// span, and derives spans as the maximal stretches over which one grip is continuously evidenced.
// Keeping that evidence inside each span's own records instead would kill it with the span, so
// every question that outlives a span (the foreign ring, the join break, the landing hand-off)
// would grow a mechanism of its own; this table is the one object that answers them all. A span
// opens at a statement and runs forward from it: nothing is ever dated back, so the walk never
// asks what the hand was doing before the instant it stands at.
//
// Shape rules 1 to 4 are the grip-tenure law.
// =================================================================================================

// The member statement threshold: two stops stated at one slot are a grip (shape rule 1). Chord
// boxes legitimately form at two, and the landing successor reads this same number (shape rule 4)
// because a landing's members were already established members of the span that just closed.
constexpr std::size_t g_span_member_threshold = 2;

// The accumulation minimum: sound alone founds a span only at three of its own members sounding at
// one instant (shape rule 1). It gates FOUNDING by sound alone and nothing else — a span opens and
// grows below it and is simply never published — and a landing opens at the statement threshold
// above.
constexpr std::size_t g_accumulation_member_minimum = 3;

// The fewest members a derived span sounds in parts with (shape rule 5): a bracket over two
// strings states nothing its boxes do not. Asked at PUBLICATION, since a grip picked apart may yet
// grow to three: one that never does is published as the box it was struck as, closing where the
// picking began.
constexpr std::size_t g_parts_member_minimum = 3;

// Whether a grip is too small to sound in parts (\ref g_parts_member_minimum).
[[nodiscard]] bool tooFewForParts(const std::vector<std::optional<ChartStop>>& stops)
{
    return static_cast<std::size_t>(std::ranges::count_if(
               stops, [](const auto& stop) { return stop.has_value(); })) < g_parts_member_minimum;
}

} // namespace

// One walk of the channel's statements: each travel goes on while its frets lie further in its
// direction, and the first travel landing after `from` ends the walk.
StatedStop statedStopFrom(
    const ChartNote& note, const Fraction from, const bool arrives_into_next_head)
{
    int stop = note.fret;
    Fraction held{};
    Fraction stop_from{};
    std::optional<FretTravel> travel;
    // The SLIDE-OUT is no statement about where the finger IS: the fret the pitch slides out
    // toward is a grip the hand never takes, and the finger leaves the string at the ring's end.
    // So the channel goes on stating the stop it slides from until then, exactly as the ring hold
    // reads it (\ref fretHandHolds). An ARRIVAL is the opposite and is READ: the finger glides onto
    // that stop and the next strike takes it, so the channel's last landing is the end
    // (\ref arrivesIntoNextHead).
    const Keyframe* const slide_out = slideOutKeyframe(note, arrives_into_next_head);
    for (const Keyframe& keyframe : note.keyframes)
    {
        // Bound to a local so the optional check and the access are provably the same object.
        const std::optional<int>& fret = keyframe.fret;
        if (!fret.has_value() || &keyframe == slide_out)
        {
            continue;
        }
        if (travel.has_value())
        {
            const bool onward = travel->fret > stop ? *fret > travel->fret : *fret < travel->fret;
            if (onward)
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
            held = travel->arrival;
            travel.reset();
        }
        if (*fret == stop)
        {
            held = keyframe.offset;
            continue;
        }
        travel = FretTravel{.departure = held, .arrival = keyframe.offset, .fret = *fret};
    }
    // THE NODE ARM, and the whole of the harmonic law's reach into the channel: a fretting-hand
    // harmonic touches a NODE and presses nothing, so the stop it states IS that node — never the
    // fret 0 beneath it, which reads as the open string a span already holds (node 5 is not fret 5,
    // and not the open string either). Such a note carries no fret channel at all (the normalizer
    // strips keyframes from one), so the wrap is constant over its ring and the travel machinery
    // never sees a node. The split a harmonic makes falls out of the ordinary contradiction law
    // reading a stop that cannot say a node is fret 0; what a node grip does NOT do — found, grow
    // or join a span — is the grip statement's one clause (\ref nodeGrip).
    const auto states = [&note](const int channel_fret) -> ChartStop {
        return frettingStopAt(note, channel_fret);
    };
    // Bound once so the presence test and every read below are provably the same object.
    const std::optional<FretTravel>& leaves = travel;
    if (!leaves.has_value() || from <= leaves->departure)
    {
        return StatedStop{.stop = states(stop), .travel = travel, .stated_from = stop_from};
    }
    if (from < leaves->arrival)
    {
        return StatedStop{.stop = std::nullopt, .travel = travel, .stated_from = leaves->arrival};
    }
    return StatedStop{
        .stop = states(leaves->fret), .travel = std::nullopt, .stated_from = leaves->arrival
    };
}

namespace
{

// What one string of the fretting hand is demonstrably doing, as the strings testify — owned by
// the walk and outliving every span. ONE table rather than several partial copies of the same
// object (per-span chains, a per-string ringing flag, a per-slot rebuild), each of which would be
// this rule stated again and free to drift from it.
//
// TWO REACH COLUMNS, deliberately, because the law asks two different physical questions of a
// string and a right-hand onset drives them apart — collapsing them into one field answers one of
// the two questions wrongly:
//
//   `covers` — how far the FRETTING HAND's own statement reaches: the last member strike's ring,
//   capped at the next landing of its fret channel. THE ONLY input to a span's reach and
//   close — how far the shape reaches is a statement about the hand that makes shapes.
//
//   `sounds` — how far the string goes on sounding over the fretting hand's stop, WHICHEVER hand
//   made the onset. THE ONLY input to renewal and continuity: a tap over a member's stop ends that
//   member's ring underneath with no hand lifting anywhere, so the sound was replaced, not
//   silenced, and the statement chains straight through it — without the tap ever moving a close.
//   A tap a pull-off plants on another stop sounds over that stop, so it extends nothing here.
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
    // asked of. Empty where the hand has never sounded the string. A right-hand onset never writes
    // it: a tap sounds the stop the OTHER hand holds, so it asserts no grip of its own.
    std::optional<std::size_t> finger;

    Fraction covers{};
    Fraction sounds{};

    // The end of the last FOREIGN sound on this string — the latest instant it audibly sounded a
    // stop other than the one its current grip states, as the string's strikes record it. The
    // join break's state (shape rule 3): a span opened at an instant asserts its whole grip from
    // that instant, so a string whose foreign sound ended after the span's front cannot join it and
    // breaks it instead. A displacement is the special case whose foreign end IS the displacing
    // strike (the same-string clamp puts them on one instant); a foreign ring that died into
    // silence bounds just as hard at its own end, so nothing resets this — the bound records what
    // the string audibly did, not how the grip was established. Zero where the string never
    // sounded a foreign stop. A glide's departure ends at its landing, which the channel already
    // knows, so it is no record here: read through \ref foreign_until_at, never bare — a bare read
    // misses the landing.
    Fraction foreign_until{};

    // The stop `finger`'s strike STATED beneath the fret it sounds (\ref gripStatement), empty
    // where it stated the fret it sounds. Decided once, at the strike, against what was down then —
    // so the slide-out riding back onto it and a later carry read the verdict rather than re-ask a
    // question whose evidence the strike itself replaced.
    std::optional<ChartStop> stated_beneath;
};

// A NODE GRIP: a natural harmonic's stop, a node touched with nothing pressed. It states no grip:
// the finger rests over the node for the strike and lifts, holding no stop a posture could keep,
// so a natural harmonic founds, grows and joins no span. Its STRIKE still sounds — displacement and
// the grip contradiction read the sound, so a node struck on a string a span holds at a fret still
// breaks that span. An artificial harmonic presses a fret under its node and is a grip like any
// other (\ref harmonicOverPressedStop).
[[nodiscard]] bool nodeGrip(const ChartStop& stop)
{
    return stop.node.has_value() && stop.fret == 0;
}

// The tenure an unnamed shape hold gives the span it states (shape rule 9): before the hold's end
// only a hand conflict breaks the span; from the end on it closes wherever the walk would close a
// span standing there, never short of that end.
struct HoldTenure
{
    // The hold's stated end, on the walk's whole-note axis.
    Fraction end{};

    // The earliest hand conflict before `end` that no slot states: a member's glide landing, or
    // hand-position rule 6's release by the note sounding a stated string. The span closes there.
    std::optional<Fraction> conflict{};
};

// The span being held open. Slim on purpose: the EVIDENCE lives in the hand table, so what a span
// carries is only its statement — which strings at which stops, and the handful of facts published
// at emit that only the walk's own passage through the slots can know.
// The slot a span's own strokes first sounded its grip in parts at, with the last instant an event
// had stated the span before it (empty where the opening stroke was the only statement).
struct SpanTurn
{
    Fraction place{};
    std::optional<Fraction> stated_before{};
};

struct OpenSpan
{
    Fraction front_place{};

    // The sounded grip: the stop this span states per string, empty where it states none. Growth
    // fills a string the grip lacks (shape rule 2: adding a stop breaks nothing — growth IS
    // accumulation), and nothing ever rewrites or removes an entry: a stop's silence is what BREAKS
    // the grip, never what shrinks it, and a higher fret restated inside a shape hold leaves it.
    std::vector<std::optional<ChartStop>> stops;

    // Where this span's opening mark draws, published to \ref ChartShape::bracket_place. Every
    // span an event states seeds it with its front — the statement it opened at, or a shape hold's
    // start; a landing successor seeds nothing and the first sounding inside it fills the slot
    // (shape rule 4).
    std::optional<Fraction> bracket_place{};

    // The last instant an EVENT stated this span's shape; empty only on a landing successor
    // nothing has stated yet. Published as \ref ChartShape::stated_extent (the display trim's
    // floor); its presence is the landing emit rule's stated arm (shape rule 4).
    std::optional<Fraction> last_stated_place{};

    // The hand's OWN strokes have sounded this grip in parts. Read through \ref inParts beside
    // the carried members, which make the figure sound in parts too.
    bool struck_in_parts{false};

    // Where the hand's own strokes first sounded this grip in parts; empty while they never have.
    // Publication's too-few rule closes a grip that never grew to three members at the turn
    // (shape rule 5), stated through the instant before it.
    std::optional<SpanTurn> turned{};

    // Whether this span is a span at all (shape rule 1): its own members met a threshold — two
    // stops stated at one slot, or three own members sounding at one instant — or a shape hold or
    // a landing states it. A span opens at its first statement and runs forward from there, so the
    // thresholds are asked of it as its own members accumulate; one that never meets them is
    // dropped at emit, unseen, its ground and rings left to whatever opens next. Carried members
    // count toward nothing here: a span exists on its own onsets.
    bool founded{false};

    // True on the span a landing opened and nowhere else (shape rule 4) — the one span no event
    // states at its own start. Published: the census keys landing-born spans on it, and the display
    // keys the bracket deferral on it. Not derivable from `last_stated_place`, which stops being
    // empty the moment an interior re-pick states the successor.
    bool landing_opened{false};

    // The unnamed shape hold stating this span (shape rule 9); empty on a span its notes alone
    // state. Published as \ref ChartShape::hold_stated.
    std::optional<HoldTenure> hold{};

    // Per string, whether the member was CARRIED into this span: a ring struck before the span's
    // front that still sounded there (shape rule 1). A carried member bounds the span like any
    // other, but its death is a SPLIT, not a close: the span it joined exists on its own onsets,
    // so it runs on past the death with the members still standing. A fretting-hand restrike
    // inside the span makes the member the span's own.
    std::vector<bool> carried;
};

// Whether the span sounds in parts (shape rule 5): its own strokes did, or a member was carried in
// rather than struck, which is a member the hand did not strike into this span.
[[nodiscard]] bool inParts(const OpenSpan& span)
{
    return span.struck_in_parts || std::ranges::any_of(span.carried, std::identity{});
}

// How many stops the span holds of its own — struck or landed rather than carried in (shape rule
// 1): the grip the hand stated, which is what renews and what founds.
[[nodiscard]] std::size_t ownStopCount(const OpenSpan& span)
{
    std::size_t count = 0;
    for (std::size_t string_index = 0; string_index < span.stops.size(); ++string_index)
    {
        if (span.stops[string_index].has_value() && !span.carried[string_index])
        {
            ++count;
        }
    }
    return count;
}

// Whether the span holds a stop of its own. False only on a shape hold's span before the first
// fretting-hand strike inside the hold, which opens at the hold's start over the rings carried
// into it alone (shape rule 9).
[[nodiscard]] bool holdsOwnStop(const OpenSpan& span)
{
    return ownStopCount(span) > 0;
}

// A shape hold on the walk's whole-note axis.
struct HoldExtent
{
    Fraction start{};
    Fraction end{};
    HoldGrip grip{};
};

// Whether a shape hold states a shape, rather than none (shape rule 9).
[[nodiscard]] bool statesShape(const HoldExtent& hold)
{
    return !std::holds_alternative<NoneGrip>(hold.grip);
}

// Whether the walk at `now` is still inside the shape hold stating `span`.
[[nodiscard]] bool insideHold(const OpenSpan& span, const Fraction now)
{
    // Bound once so the presence test and the read are provably the same object.
    const std::optional<HoldTenure>& tenure = span.hold;
    return tenure.has_value() && now < tenure->end;
}

// Whether a shape hold states `span` and ends at `place`.
[[nodiscard]] bool holdEndsAt(const OpenSpan& span, const Fraction place)
{
    // Bound once so the presence test and the read are provably the same object.
    const std::optional<HoldTenure>& tenure = span.hold;
    return tenure.has_value() && tenure->end == place;
}

// Everything one slot states, read before any span is touched. The reading is pure; the walk's
// verdicts are functions of it and the pre-instant hand table.
struct SlotReading
{
    Fraction place{};

    // Fretting-hand strikes: the stop each states at its own onset (a channel is never mid-travel
    // at offset zero) — what writes the hand table and what makes a string a member here.
    std::vector<std::optional<ChartStop>> strikes;
    std::vector<std::optional<std::size_t>> strike_notes;

    // Where every SOUNDING onset here reaches, whichever hand — what renews a string (shape rule
    // 3's per-string renewal) and what the sounds column takes.
    std::vector<std::optional<Fraction>> sounding;

    // The lowest fret each picking-hand stop here reaches (its hull's floor) — what the tap's floor
    // (shape rule 3) tests against a standing grip, since the tap cannot sound at or below a finger
    // holding the string.
    std::vector<std::optional<int>> tap_floors;

    std::size_t struck{0};
};

} // namespace

std::optional<ChartStop> gripStatement(
    const ChartNote& note, const std::optional<int>& planted, const std::optional<ChartStop>& down)
{
    // A source RIDES a held stop only where its whole path stays above it: a fret stated at or
    // below that stop would have sounded the stop instead, so a source whose travel sweeps the
    // plant — a slid source's two-finger landing — states what it sounds.
    if (!planted.has_value() || harmonicOverPressedStop(note) || down != fretStop(*planted) ||
        travelsThroughFret(note, *planted))
    {
        return std::nullopt;
    }
    return down;
}

bool foundsSpan(const std::size_t stated_together, const std::size_t sounding_together)
{
    return stated_together >= g_span_member_threshold ||
           sounding_together >= g_accumulation_member_minimum;
}

ChartShapes deriveChartShapes(
    const ChartConnections& connections, const HeadMargins& margins,
    const std::span<const ShapeHold> shape_holds, const TempoMap& tempo_map)
{
    const std::vector<ChartNote>& saved_notes = connections.saved_notes;
    // Both stop tables are derivations of the connections themselves, so they are asked here rather
    // than handed in: a caller could otherwise pass tables built from another revision.
    const std::vector<std::optional<int>> planted_stops = chartPlantedStops(connections);
    // A SLIDE-OUT or an ARRIVAL at every end statement, resolved once for the revision by the one
    // walk that establishes the pair (\ref ChartConnections::arrives_into). Read by the channel
    // reader and by the two sound tests below: every place this walk asks what a fret at a ring's
    // end means.
    const std::vector<bool>& arrives_into = connections.arrives_into;
    ChartShapes derived;

    constexpr auto string_count = static_cast<std::size_t>(g_max_chart_strings);

    // Every onset on the whole-note axis, computed once by the connections — the walk reasons on
    // that one rational axis end to end.
    const std::vector<Fraction>& onset_place = connections.places;
    const auto ring_end_of = [&saved_notes, &onset_place](const std::size_t note) {
        return onset_place[note] + saved_notes[note].sustain;
    };
    // THE CHANNEL READER (\ref statedStopFrom) asked of one note at an instant on that axis: the
    // one spelling every question below puts to a note's fret channel.
    const auto channel_at =
        [&saved_notes, &onset_place, &arrives_into](const std::size_t note, const Fraction at) {
            return statedStopFrom(saved_notes[note], at - onset_place[note], arrives_into[note]);
        };
    // THE HAND — the one evidence table (\ref StringHand).
    std::vector<StringHand> hand(string_count);

    // THE HOLDS (shape rule 9), on this axis. Every edge is a wall but an unnamed hold's end to its
    // own span: a span standing across one breaks there, and an unnamed hold's start opens its
    // span.
    std::vector<HoldExtent> holds;
    holds.reserve(shape_holds.size());
    for (const ShapeHold& hold : shape_holds)
    {
        holds.push_back(
            HoldExtent{
                .start = tempo_map.wholeNotePositionAt(hold.start),
                .end = tempo_map.wholeNotePositionAt(hold.end),
                .grip = hold.grip,
            });
    }
    // Nothing sounds past the song's end, so a hold's span reaches no further.
    const Fraction song_end = tempo_map.wholeNotePositionAt(terminalGridPosition(tempo_map));
    // Whether a span fronted at `front` lies inside a none hold, which publishes no arpeggio span.
    const auto inside_none_hold = [&holds](const Fraction front) {
        const auto after =
            std::ranges::upper_bound(holds, front, std::ranges::less{}, &HoldExtent::start);
        return after != holds.begin() && front < std::prev(after)->end &&
               !statesShape(*std::prev(after));
    };
    // The stated end of the unnamed hold whose span closed short of it. A hold's wall is its
    // published end, so the stated end walls nothing once the walk has closed the span before it.
    std::optional<Fraction> closed_hold_end;

    std::optional<OpenSpan> open;
    // Keyed by the posture itself, so the table deduplicates by exactly the identity the type
    // states and a field added to the posture can never fall out of the key.
    std::map<ChartPosture, std::size_t> posture_indices;

    // What the fretting hand covers on one string as of `now` — the channel re-asked at the
    // hand's own finger, so a travel's landing caps the coverage without a second record.
    const auto covers_at =
        [&channel_at,
         &hand](const std::size_t string_index, const Fraction now) -> std::optional<ChartStop> {
        const std::optional<std::size_t>& finger = hand[string_index].finger;
        if (!finger.has_value())
        {
            return std::nullopt;
        }
        return channel_at(*finger, now).stop;
    };

    // Whether one string still AUDIBLY sounds the fretting hand's finger as of `now`, read
    // end-INCLUSIVELY (the displacement window): its ring reaches `now` and does not SLIDE-OUT
    // away, whose finger is off the board by its end. A ring whose end ARRIVES is still the hand on
    // a stop, the next strike taking it, so it sounds like any other. What it holds there is
    // \ref covers_at's to say.
    const auto sounds_at =
        [&saved_notes, &arrives_into, &hand](const std::size_t string_index, const Fraction now) {
            const std::optional<std::size_t>& finger = hand[string_index].finger;
            return finger.has_value() && now <= hand[string_index].sounds &&
                   slideOutFretOrNull(saved_notes[*finger], arrives_into[*finger]) == nullptr;
        };
    // The stop that still-sounding finger holds, empty where nothing sounds or it is mid-travel.
    const auto held_at = [&sounds_at, &covers_at](
                             const std::size_t string_index,
                             const Fraction now) -> std::optional<ChartStop> {
        return sounds_at(string_index, now) ? covers_at(string_index, now) : std::nullopt;
    };

    // The end of this string's last FOREIGN sound as of `now` — the hand's own record
    // (\ref StringHand::foreign_until) re-asked at the channel, the second half the landing hands
    // on beside \ref coverage_at. A slid string sounded its departure stop until the landing, so a
    // finger that has LANDED somewhere new ended its foreign sound there: re-ask the channel, so
    // the landing needs no record.
    const auto foreign_until_at =
        [&channel_at, &onset_place, &hand](const std::size_t string_index, const Fraction now) {
            const Fraction recorded = hand[string_index].foreign_until;
            const std::optional<std::size_t>& finger = hand[string_index].finger;
            if (!finger.has_value())
            {
                return recorded;
            }
            const StatedStop stated = channel_at(*finger, now);
            return stated.stated_from == Fraction{}
                       ? recorded
                       : std::max(recorded, onset_place[*finger] + stated.stated_from);
        };

    // How far the fretting hand's own statement on this string reaches, measured from `now`: the
    // finger's ring, capped at the next landing of its channel (\ref StringHand::covers). ONE
    // authority for the three moments a statement (re)starts and the cap has to be re-read — a
    // strike stating it, a landing handing it on, and a carry folding into a new span. The third
    // moment is why this is a function at all: a landing NO SPAN WAS STANDING TO WITNESS restarts
    // coverage exactly as a witnessed one does, and without the re-read a lone glide's coverage
    // would stay frozen at its first arrival forever, so the next span to fold that string in
    // would reach only as far as a landing long past.
    const auto coverage_at = [&channel_at, &onset_place, &ring_end_of, &hand](
                                 const std::size_t string_index, const Fraction now) {
        const std::optional<std::size_t>& finger = hand[string_index].finger;
        if (!finger.has_value())
        {
            return Fraction{};
        }
        const Fraction onset = onset_place[*finger];
        const StatedStop stated = channel_at(*finger, now);
        const Fraction ring = ring_end_of(*finger);
        // Bound once so the presence test and the read are provably the same object.
        const std::optional<FretTravel>& leaves = stated.travel;
        return leaves.has_value() ? std::min(ring, onset + leaves->arrival) : ring;
    };

    // The stop a string's ring carries into `now`, where a span standing there would take it as
    // a member. MEMBERSHIP READS THE REACH COLUMN, never the sound (\ref StringHand): a ring the
    // FRETTING hand is no longer making crosses no instant, whatever the other hand is sounding
    // over it. A tap ringing on past the fretted note beneath it keeps `sounds` alive while
    // `covers` is spent, and a member folded in on that evidence would close the span at or before
    // its own front. Read through \ref coverage_at, since a landing the walk passed caps the
    // reach. Mid-travel states no grip and joins no posture, and a natural's ring carries no grip
    // either: the finger lifted at the strike, so the chime ringing on is no member of anything
    // opened under it (\ref nodeGrip, the same clause the struck path reads).
    const auto carried_stop = [&coverage_at, &covers_at](
                                  const std::size_t string_index,
                                  const Fraction now) -> std::optional<ChartStop> {
        if (!(now < coverage_at(string_index, now)))
        {
            return std::nullopt;
        }
        const std::optional<ChartStop> carried = covers_at(string_index, now);
        if (!carried.has_value() || nodeGrip(*carried))
        {
            return std::nullopt;
        }
        return carried;
    };

    // What a carried ring states as a member, like every grip statement: over a stop its string
    // still held, the ring sounds the ornament and the grip beneath it is that stop; anywhere else
    // it is the stop it is still ringing from.
    const auto carried_statement =
        [&hand](const std::size_t string_index, const ChartStop& carried) {
            return hand[string_index].stated_beneath.value_or(carried);
        };

    // HAND-POSITION RULE 6's RELEASE (\ref stringReleasesStopAt) of a stop a span states, by the
    // note sounding its string from `from`: the instant that note proves the finger on the stop
    // lifted, or nothing while it sounds. A release inside a glide lies at the glide's landing
    // (shape rule 4). An open string or a node holds no finger to release.
    const auto released_at = [&connections, &channel_at, &onset_place, &ring_end_of](
                                 const std::size_t string_index,
                                 const ChartStop& stop,
                                 const Fraction from,
                                 const std::size_t note) -> std::optional<Fraction> {
        if (!pressedFret(stop.fret).has_value())
        {
            return std::nullopt;
        }
        const Fraction ring = ring_end_of(note);
        const Fraction released =
            stringReleasesStopAt(connections, string_index, stop.fret, from, ring);
        if (!(released < ring))
        {
            return std::nullopt;
        }
        const StatedStop stated = channel_at(note, released);
        // Bound once so the presence test and the read are provably the same object.
        const std::optional<FretTravel>& travel = stated.travel;
        return !stated.stop.has_value() && travel.has_value() ? onset_place[note] + travel->arrival
                                                              : released;
    };

    // Whether the span's statement is still standing at `now` — the continuity law over SOUNDS
    // (shape rule 3's quit arm), with its per-string renewal: a member string whose sound ends
    // exactly here and is re-sounded by this slot was REPLACED, not silenced. Renewal is a grip's:
    // a span stating one stop of its own has no grip to renew — the rings carried into it are no
    // statement of the hand's — so it ends at that member's ring end however the slot re-sounds
    // it, and the restrike opens the figure anew (shape rule 1). Judged per
    // stop: a string still sounding keeps the span's stop in force only while its finger states
    // that stop, rides above it, or is gliding (the span covers the glide, shape rule 4). Inside
    // its shape hold a span stands whatever its members do: a quit is notation there (shape rule
    // 9).
    const auto in_force = [&hand, &covers_at](
                              const OpenSpan& span,
                              const Fraction now,
                              const std::vector<std::optional<Fraction>>& sounding) {
        if (insideHold(span, now))
        {
            return true;
        }
        const bool renews = ownStopCount(span) >= g_span_member_threshold;
        for (std::size_t string_index = 0; string_index < span.stops.size(); ++string_index)
        {
            // Bound once so the presence test and every read are provably the same object.
            const std::optional<ChartStop>& stop = span.stops[string_index];
            if (!stop.has_value() || !hand[string_index].finger.has_value())
            {
                continue;
            }
            const Fraction sounds = hand[string_index].sounds;
            if (now < sounds)
            {
                const std::optional<ChartStop> finger_stop = covers_at(string_index, now);
                if (!finger_stop.has_value() || *finger_stop == *stop ||
                    hand[string_index].stated_beneath == stop)
                {
                    continue;
                }
                return false;
            }
            if (now != sounds || !sounding[string_index].has_value())
            {
                return false;
            }
            // A carried member re-sounded at its ring end is the hand striking INTO the span —
            // growth that makes the member the span's own — not a stop of the grip renewing, so
            // the grip test is asked of the span's own stops only.
            if (!renews && !span.carried[string_index])
            {
                return false;
            }
        }
        return true;
    };

    // How far the span's own statement reaches: the minimum of its sounded members' coverage —
    // the fretting hand's rings, landing-capped. A span a shape hold states reaches to its first
    // hand conflict no slot states before the hold's end, and otherwise as its members do, never
    // short of that end nor past the song's, past which nothing sounds (shape rule 9).
    const auto span_reach = [&hand, song_end](const OpenSpan& span) {
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
        const Fraction members_reach = reach.value_or(span.front_place);
        // Bound once so the presence test and the reads are provably the same object.
        if (const std::optional<HoldTenure>& tenure = span.hold; tenure.has_value())
        {
            return std::min(
                {tenure->conflict.value_or(song_end),
                 std::max(members_reach, tenure->end),
                 song_end});
        }
        return members_reach;
    };

    // One slot reading, reused by every slot so each buffer is allocated once; the walk's one
    // record of what the instant it stands at sounds.
    SlotReading slot{
        .place = Fraction{},
        .strikes = std::vector<std::optional<ChartStop>>(string_count),
        .strike_notes = std::vector<std::optional<std::size_t>>(string_count),
        .sounding = std::vector<std::optional<Fraction>>(string_count),
        .tap_floors = std::vector<std::optional<int>>(string_count),
        .struck = 0,
    };

    // The one act that publishes a span (shape rule 3: the close is the closing event's instant or
    // where the statement ran out, whichever is earlier), given the instant that closed it, or
    // nothing at the stream's end. Shape rule 4's tenure drop lives here, before anything is
    // pushed, which is what lets publication ride the push.
    const auto emit = [&](const std::optional<Fraction> close_place) {
        if (!open.has_value())
        {
            return;
        }
        const Fraction reach = span_reach(*open);
        Fraction end = close_place.has_value() ? std::min(reach, *close_place) : reach;
        // The closing head is the fretting-hand strike standing at the close, whichever path closed
        // the span there — a break, a wall, a landing or a carried member's death at that slot —
        // and only where the statement was still live: one that had already run out, its reach
        // strictly before the close, owes its rails no distance to a head that arrived after it
        // ended. A slot of held fingers or bare taps states no new grip and is no head.
        std::optional<Fraction> head = close_place.has_value() && !(reach < *close_place) &&
                                               *close_place == slot.place && slot.struck > 0
                                           ? close_place
                                           : std::nullopt;
        // THE TOO-FEW RULE (shape rule 5), asked here because a grip picked apart may yet have
        // grown to three: a span that never did is no arpeggio, so it is published as the box its
        // stroke stated — closing at the strike that began picking it apart (its head), the parts
        // after that unpublished, where such a strike fell inside it; whole to its close where the
        // parts that absorbed its stroke never fell inside it. Inside its shape hold a span is an
        // arpeggio at any member count (shape rule 9), so a grip sounding in parts from an instant
        // inside the hold is exempt; past the hold's end the span runs on as it derives unheld,
        // and a turn there is judged like any other.
        // Bound once so the presence tests and the reads are provably the same objects.
        const std::optional<SpanTurn>& turned = open->turned;
        const std::optional<HoldTenure>& hold = open->hold;
        const Fraction parts_from = turned.has_value() ? turned->place : open->front_place;
        const bool held_in_parts =
            hold.has_value() && open->struck_in_parts && parts_from < hold->end;
        const bool too_few_in_parts =
            open->struck_in_parts && !held_in_parts && tooFewForParts(open->stops);
        std::optional<Fraction> last_stated = open->last_stated_place;
        if (too_few_in_parts && turned.has_value() && turned->place < end)
        {
            end = turned->place;
            head = end;
            last_stated = turned->stated_before;
        }
        // SHAPE RULE 4's emit test for the one onset-less span: a landing span is emitted if an
        // event ever stated it, or its tenure STRICTLY EXCEEDS the distinguishability quantum kept
        // before the closing head — the chord name never flickers for a sliver. A close with no
        // sounding head has no flicker to prevent, so only the degenerate zero-tenure span drops
        // there; a reach-closed landing span emits on its own rings.
        //
        // A SHIFT SLIDE REACHES THIS TEST NEVER, by construction: its ARRIVAL stands at the ring's
        // own END (\ref arrivesIntoNextHead), and a landing opens a successor only where the ring
        // runs STRICTLY PAST it (`settle_landings`). What the strictness governs is a charter's own
        // landing crowded against the head after it. A span a shape hold states is stated by the
        // hold, so this test never drops it.
        if (open->landing_opened && !open->last_stated_place.has_value() &&
            close_place.has_value() && !open->hold.has_value())
        {
            const Fraction tenure = end - open->front_place;
            const Fraction quantum = head.has_value() ? margins.length(*head) : Fraction{};
            if (!(quantum < tenure))
            {
                open.reset();
                return;
            }
        }
        // A span whose own members never met a threshold was never a span (shape rule 1): it
        // opened at a statement, ran, and is dropped unseen, its rings left for whatever opens
        // next.
        if (!open->founded)
        {
            open.reset();
            return;
        }
        // Bound once so the presence test and the reads are provably the same object.
        if (const std::optional<HoldTenure>& tenure = open->hold;
            tenure.has_value() && end < tenure->end)
        {
            closed_hold_end = tenure->end;
        }
        // A span closing at its front holds nothing, and publishes no span (shape rule 3: every
        // span is strictly positive): a hold's span closing at its start, or a split's remainder
        // the strike at its own front breaks at once.
        if (!(open->front_place < end))
        {
            open.reset();
            return;
        }
        ChartShape shape{
            .place = open->front_place,
            .sustain = end - open->front_place,
            .stated_extent =
                std::min(last_stated.value_or(open->front_place), end) - open->front_place,
            .closing_onset_place = head,
            // Indexed below, once the span is known to be published.
            .posture = 0,
            .sounds_in_parts = inParts(*open) && !too_few_in_parts,
            .landing_opened = open->landing_opened,
            .bracket_place = open->bracket_place,
            .hold_stated = open->hold.has_value(),
        };
        // Built once and handed to the table; the row copies it only on a first sighting.
        ChartPosture posture{.stops = std::move(open->stops)};
        // A none hold publishes no arpeggio span inside it (shape rule 9). The walk runs on
        // unchanged, so the span still covers its ground and its rings join no later span.
        if (inside_none_hold(shape.place) && chartShapeIsArpeggio(connections, shape, posture))
        {
            open.reset();
            return;
        }
        const auto [entry, inserted] =
            posture_indices.try_emplace(std::move(posture), derived.postures.size());
        if (inserted)
        {
            derived.postures.push_back(entry->first);
        }
        shape.posture = entry->second;
        derived.shapes.push_back(shape);
        open.reset();
    };

    // SHAPE RULE 4 — the landing, the one onset-less open, resolved BEFORE any slot at its instant
    // is judged (the within-instant law's first sentence): the predecessor closes at the landing
    // and the landed grip stands before the slot's own verdicts run against it. "Held through the
    // slide" is end-inclusive at the landing instant — a member's ring dying exactly AT the
    // landing belonged to the predecessor (the seam ownership) — and the survivors are the
    // members whose channels state a stop at the boundary and whose sound runs STRICTLY past it,
    // at the statement threshold: they were already established members of the span that just
    // closed, so nothing here is an accumulation. Each boundary is judged once, at the first
    // instant the walk reaches it, against the hand as it stood there: one a slot's verdicts then
    // let stand is never judged again with that slot's strikes counted among its survivors.
    //
    // Inside a shape hold the boundary is the span's first hand conflict no slot states (shape rule
    // 9), which closes the span whether or not a landing hands it over.

    // Whether the ring on a string the grip does not state joins a span fronted at `front` as a
    // CARRIED member, and with what stop (shape rule 1): struck before the front and still sounding
    // there, outliving it by the minimum sustain distance there — a shorter tail draws nothing into
    // the span and reads as released before it — and folding in on no string this slot strikes with
    // the fretting hand (a strike there is the span's own statement of that string, or a node
    // proving the finger left) nor, while still sounding, on one any onset sounds (the supersession
    // rule).
    const auto joins_at = [&](const std::size_t string_index,
                              const Fraction front) -> std::optional<ChartStop> {
        if (slot.strike_notes[string_index].has_value())
        {
            return std::nullopt;
        }
        const std::optional<std::size_t>& finger = hand[string_index].finger;
        if (!finger.has_value() || !(onset_place[*finger] < front))
        {
            return std::nullopt;
        }
        const std::optional<ChartStop> at_front = carried_stop(string_index, front);
        if (!at_front.has_value())
        {
            return std::nullopt;
        }
        const Fraction reach = coverage_at(string_index, front);
        if (!(minimumSustainDistance(tempo_map, front) < reach - front) ||
            (slot.place < reach && slot.sounding[string_index].has_value()))
        {
            return std::nullopt;
        }
        return at_front;
    };

    // THE CARRIED MEMBERS (shape rule 1): what sounds under a bracket's opening is what it prints.
    // Every ring that joins at `front` (\c joins_at) fills a string of `stops` the grip does not
    // state, whenever it was struck, stating what its strike stated, and bounds the span like any
    // member — its death inside the span is the split. Its coverage is read from the front, the
    // instant it joins, so a landing it passed later still caps it (shape rule 4). Returns which
    // strings were carried, for \ref OpenSpan::carried. One routine for the two spans that open
    // over rings — a statement's and a shape hold's (shape rule 9).
    const auto carry_into = [&](std::vector<std::optional<ChartStop>>& stops,
                                const Fraction front) {
        std::vector<bool> carried_in(string_count, false);
        for (std::size_t string_index = 0; string_index < string_count; ++string_index)
        {
            if (stops[string_index].has_value())
            {
                continue;
            }
            const std::optional<ChartStop> at_front = joins_at(string_index, front);
            if (!at_front.has_value())
            {
                continue;
            }
            stops[string_index] = carried_statement(string_index, *at_front);
            carried_in[string_index] = true;
            hand[string_index].covers = coverage_at(string_index, front);
        }
        return carried_in;
    };

    // THE SPLIT (shape rule 1): where the member whose coverage ends at `boundary` was CARRIED into
    // the span, the span does not close there — it exists on its own onsets — but splits: the
    // piece up to the death is published with the carried member in its grip, and the span runs
    // on from the death with the members still standing, fronted there, keeping the hand's own
    // in-parts verdict and nothing a carried member gave it. A member of the span's own ending
    // there is a quit (the slot path's business); a carried member whose coverage is capped at a
    // LANDING its ring runs past has not died, and the landing hands it on (shape rule 4); one the
    // slot at `now` restates is renewed there (the slot path's business again), while a strike at
    // another stop there finds the member already dead and is judged against the remainder; and
    // inside its shape hold a span's quits are suppressed, a carried member's included (shape rule
    // 9). None of those is a split. At the hold's own end a carried member's death is one: the
    // hold's span closes there, as the hold states, and the remainder runs on as it derives
    // unheld, so a hold placed exactly over a piece changes nothing else — only where the remainder
    // holds a stop of its own, since a span exists on its own onsets alone and a hold's span, which
    // exists on the hold, may hold nothing but the rings carried into it. Then the death is the
    // close, and the rings still sounding are tails.
    const auto split_at_carried_death = [&](const Fraction boundary, const Fraction now) {
        // Bound once so the presence test and the read are provably the same object.
        if (const std::optional<HoldTenure>& tenure = open->hold;
            tenure.has_value() && boundary < tenure->end)
        {
            return false;
        }
        bool dying = false;
        for (std::size_t string_index = 0; string_index < string_count; ++string_index)
        {
            // Bound once so the presence test and the reads are provably the same object.
            const std::optional<ChartStop>& stop = open->stops[string_index];
            if (!stop.has_value() || hand[string_index].covers != boundary)
            {
                continue;
            }
            const std::optional<std::size_t>& finger = hand[string_index].finger;
            if (!open->carried[string_index] ||
                (finger.has_value() && boundary < ring_end_of(*finger)))
            {
                return false;
            }
            // A restatement is judged on GRIP STATEMENTS like every identity question: a source
            // riding above the carried stop states that stop (the hold-under law), and any other
            // strike states what it sounds; either way the same stop renews the member.
            // Bound once so the presence tests and the reads are provably the same objects.
            const std::optional<std::size_t>& striking = slot.strike_notes[string_index];
            const std::optional<ChartStop>& struck = slot.strikes[string_index];
            if (boundary == now && slot.place == now && striking.has_value() &&
                struck.has_value() &&
                gripStatement(saved_notes[*striking], planted_stops[*striking], *stop)
                        .value_or(*struck) == *stop)
            {
                return false;
            }
            dying = true;
        }
        if (!dying)
        {
            return false;
        }
        // Bound once so the presence test and the read are provably the same object.
        const std::optional<Fraction>& stated = open->last_stated_place;
        OpenSpan remainder{
            .front_place = boundary,
            .stops = open->stops,
            .bracket_place = boundary,
            .last_stated_place =
                stated.has_value() && boundary < *stated ? stated : std::optional<Fraction>{},
            .struck_in_parts = open->struck_in_parts,
            // The remainder keeps the hand's in-parts verdict, so it is in parts from its front.
            .turned =
                open->struck_in_parts
                    ? std::optional<SpanTurn>{{.place = boundary, .stated_before = std::nullopt}}
                    : std::nullopt,
            .founded = open->founded,
            .landing_opened = false,
            .hold = std::nullopt,
            .carried = open->carried,
        };
        for (std::size_t string_index = 0; string_index < string_count; ++string_index)
        {
            if (remainder.carried[string_index] && hand[string_index].covers == boundary)
            {
                remainder.stops[string_index].reset();
                remainder.carried[string_index] = false;
            }
        }
        emit(boundary);
        if (holdsOwnStop(remainder))
        {
            open = std::move(remainder);
        }
        return true;
    };

    std::optional<Fraction> settled_through;
    const auto settle_landings = [&](const Fraction now) {
        const std::optional<Fraction> judged_through = std::exchange(settled_through, now);
        while (open.has_value())
        {
            const Fraction boundary = span_reach(*open);
            if (now < boundary)
            {
                return;
            }
            if (split_at_carried_death(boundary, now))
            {
                continue;
            }
            // A boundary inside the span's shape hold is a hand conflict, judged once like a
            // death would be only where a slot stands at it.
            const bool held = insideHold(*open, boundary);
            if (!held && judged_through.has_value() && boundary <= *judged_through)
            {
                return;
            }
            // Is this boundary a LANDING — does some member's travel arrive exactly here? A
            // boundary no travel arrives at is a death, and shape rule 1 says a death hands
            // nothing on: the survivors ring out as plain tails.
            bool arrived = false;
            std::vector<std::optional<ChartStop>> landed(string_count);
            std::size_t survivors = 0;
            // Every string the hand table knows is asked once here whether it survives into the
            // landed grip. The slot open asks the same of its carried rings, and both refuse a
            // ring by the one law (A RING BELONGS TO THE SPAN IT WAS STRUCK IN); they differ
            // only in who crosses: here the closing span's members, there nobody.
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                const std::optional<std::size_t>& finger = hand[string_index].finger;
                if (!finger.has_value())
                {
                    continue;
                }
                const StatedStop stated = channel_at(*finger, boundary);
                // Bound to a local so the presence test and the read are provably one object.
                const std::optional<ChartStop>& stop = stated.stop;
                if (!stop.has_value())
                {
                    // Mid-travel states nothing — the staggered slide refusing itself (shape rule
                    // 4, by scope). A harmonic has no travel at all, so it can never make a
                    // boundary a landing; it can still be a SURVIVOR carried into a successor
                    // while its neighbours slide, which is right — that finger stayed put.
                    continue;
                }
                if (!(boundary < ring_end_of(*finger)))
                {
                    // "Held through" is end-inclusive at the landing: a ring dying exactly here
                    // belonged to the closing span (the seam ownership), and only members whose
                    // own ring runs STRICTLY past the boundary survive into the landed grip.
                    // Membership reads the finger's ring, never the sound a tap keeps alive over
                    // it.
                    continue;
                }
                if (!open->stops[string_index].has_value())
                {
                    // A RING BELONGS TO THE SPAN IT WAS STRUCK IN, and the landing is the one
                    // way a member crosses a seam: the grip itself moved under the fingers that
                    // slid. So the survivors are the closing span's own members, its open strings
                    // included — they are part of the chord landed on. A ring that span never held
                    // was never its member, and is a plain tail.
                    continue;
                }
                landed[string_index] = stop;
                ++survivors;
                // A travel arriving exactly at the boundary, its ring running past it, is what
                // makes it a landing at all. Asked of the channel: inside a shape hold a member may
                // have landed before, after its coverage was last read.
                arrived = arrived || (stated.stated_from != Fraction{} &&
                                      onset_place[*finger] + stated.stated_from == boundary &&
                                      boundary < ring_end_of(*finger));
            }
            // A landing begins a statement exactly as a strike does, so one survivor opens a span
            // at the landing that growth may yet found; the successor is a span at once only where
            // its predecessor was and the statement threshold's two survivors carried the grip
            // over (shape rule 4).
            if (!arrived || survivors == 0)
            {
                // A boundary no landing hands over at is the SLOT PATH's business, not this
                // pass's: a death may still be renewed by the slot standing at it (the per-string
                // renewal in the quit arm), and where it is not, the ordinary break emits with
                // the close capped at the reach. Closing here instead would end a chug at every
                // restrike. A held span's conflict before this instant closes it here all the
                // same; one AT this instant is the slot path's hand conflict, judged against the
                // grip still standing, as the derivation judges it.
                if (held && boundary < now)
                {
                    emit(boundary);
                }
                return;
            }
            const bool founded = open->founded && survivors >= g_span_member_threshold;
            emit(boundary);
            OpenSpan successor{
                .front_place = boundary,
                .stops = std::move(landed),
                .bracket_place = std::nullopt,
                .last_stated_place = std::nullopt,
                .struck_in_parts = false,
                .turned = std::nullopt,
                .founded = founded,
                .landing_opened = true,
                .hold = std::nullopt,
                .carried = std::vector<bool>(string_count),
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

    // A span inside its shape hold watches for the hand conflicts no slot states (shape rule 9): a
    // member's glide leaving the stop the span states, at its landing (shape rule 4), and
    // hand-position rule 6's release by the note sounding a stated string from `now`. Asked after
    // every instant, of the notes then sounding; the earliest stands.
    const auto watch_conflicts = [&](const Fraction now) {
        if (!open.has_value())
        {
            return;
        }
        // Bound once so the presence test and the reads are provably the same object.
        std::optional<HoldTenure>& tenure = open->hold;
        if (!tenure.has_value() || !(now < tenure->end))
        {
            return;
        }
        HoldTenure& inside = *tenure;
        const auto conflict_at = [&inside](const Fraction at) {
            if (at < inside.end)
            {
                inside.conflict = std::min(inside.conflict.value_or(at), at);
            }
        };
        for (std::size_t string_index = 0; string_index < string_count; ++string_index)
        {
            // Bound once so each presence test and its reads are provably the same object.
            const std::optional<ChartStop>& stop = open->stops[string_index];
            const std::optional<std::size_t>& finger = hand[string_index].finger;
            if (!stop.has_value() || !finger.has_value())
            {
                continue;
            }
            const StatedStop stated = channel_at(*finger, now);
            const std::optional<FretTravel>& travel = stated.travel;
            if (stated.stop == stop && travel.has_value())
            {
                conflict_at(onset_place[*finger] + travel->arrival);
            }
            if (const std::optional<Fraction> released =
                    released_at(string_index, *stop, now, *finger);
                released.has_value())
            {
                conflict_at(*released);
            }
        }
    };

    // Each hold's edges in order — its start, then its end.
    const std::size_t edge_count = 2 * holds.size();
    const auto edge_place = [&holds](const std::size_t at) {
        const HoldExtent& extent = holds[at / 2];
        return at % 2 == 0 ? extent.start : extent.end;
    };

    // One grown grip, reused by every slot so the buffer is allocated once.
    std::vector<std::optional<ChartStop>> grown(string_count);

    std::size_t index = 0;
    std::size_t edge = 0;
    while (index < saved_notes.size() || edge < edge_count)
    {
        // The next instant anything happens: a slot, a hold's edge, or both.
        Fraction place = index < saved_notes.size() ? onset_place[index] : edge_place(edge);
        if (edge < edge_count)
        {
            place = std::min(place, edge_place(edge));
        }

        // ---- 1. READ THE SLOT (pure; nothing span-shaped is touched yet) --------------------
        slot.place = place;
        std::ranges::fill(slot.strikes, std::nullopt);
        std::ranges::fill(slot.strike_notes, std::nullopt);
        std::ranges::fill(slot.sounding, std::nullopt);
        std::ranges::fill(slot.tap_floors, std::nullopt);
        slot.struck = 0;
        std::size_t onset_end = index;
        while (onset_end < saved_notes.size() && onset_place[onset_end] == slot.place)
        {
            const ChartNote& member = saved_notes[onset_end];
            const std::optional<std::size_t> string_index = chartStringIndex(member);
            if (string_index.has_value())
            {
                slot.sounding[*string_index] = ring_end_of(onset_end);
                if (pickingHandStopsString(member.attack, member.harmonic_node))
                {
                    slot.tap_floors[*string_index] = fretHull(member).lowest;
                }
            }
            // THE FRETTING HAND'S OWN STRIKES alone: a right-hand onset is the other hand's and
            // asserts no grip of its own, so it strikes nothing here — its only span-facing
            // evidence is its sound (above) and THE TAP'S FLOOR.
            if (string_index.has_value() && !rightHandOnset(member.attack))
            {
                // A channel is never mid-travel at offset zero, so this always states a stop.
                const StatedStop struck = channel_at(onset_end, slot.place);
                // Bound once so the presence test and the read are provably the same object.
                const std::optional<ChartStop>& struck_stop = struck.stop;
                if (struck_stop.has_value())
                {
                    slot.strikes[*string_index] = struck_stop;
                    slot.strike_notes[*string_index] = onset_end;
                    ++slot.struck;
                }
            }
            ++onset_end;
        }
        // ---- 2. LANDINGS RESOLVE FIRST -------------------------------------------------------
        // A held span's conflict before this instant is one of them.
        settle_landings(slot.place);

        // ---- 2b. THE HOLDS' EDGES (shape rule 9) ----------------------------------------------
        // An unnamed hold's end is judged over the hold alone: its span runs on past it only while
        // every member's own statement reaches strictly past it, as the strikes before the end
        // wrote them, so an onset AT the end renews nothing inside and the span closes here like
        // any walled span — while the slot is still read against the grip that stood to the end
        // (the hold-under law), as it would be unheld. An end whose span already closed short of it
        // is no edge at all. Every other edge is a WALL: a span standing across it breaks here, on
        // the fretting hand's head here where it strikes, and the slot is then judged with no span
        // standing, like any slot after a break. A span a landing opened exactly here stands, and
        // an unnamed hold starting here states it (shape rule 4's hand-off).
        bool walled = false;
        std::optional<std::size_t> opened_hold;
        for (; edge < edge_count && edge_place(edge) == slot.place; ++edge)
        {
            const std::size_t hold_index = edge / 2;
            const bool ends = edge % 2 == 1;
            const bool states_shape = statesShape(holds[hold_index]);
            if (ends && states_shape && open.has_value() && holdEndsAt(*open, slot.place) &&
                slot.place < span_reach(*open))
            {
                continue;
            }
            if (ends && states_shape && closed_hold_end == slot.place)
            {
                closed_hold_end.reset();
                continue;
            }
            walled = true;
            if (!ends && states_shape)
            {
                opened_hold = hold_index;
            }
        }
        // The grip a span walled here was still holding, kept as this slot's hand-under evidence
        // (step 3): a wall is notation and lifts no finger, so a stroke at a hold's edge reads
        // exactly as the break it would make unheld — against the grip standing into the slot, and
        // against nothing where the walled span's statement had already run out.
        std::vector<std::optional<ChartStop>> walled_grip(string_count);
        bool walled_off = false;
        if (walled && open.has_value() && open->front_place < slot.place)
        {
            if (in_force(*open, slot.place, slot.sounding))
            {
                walled_grip = open->stops;
            }
            walled_off = true;
            emit(slot.place);
        }
        // THE HOLD'S SPAN opens at the hold's start (shape rule 9): the hold is the charter's
        // statement that one grip is held there, so the span is published whatever it comes to
        // hold, an empty grip over silence included. It opens over the rings carried into the
        // start exactly as a statement's span does (shape rule 1), every fretting-hand strike
        // inside grows it, and it closes by the end rule above.
        // Bound once so the presence test and the read are provably the same object.
        if (const std::optional<std::size_t>& opened = opened_hold; opened.has_value())
        {
            const HoldTenure tenure{.end = holds[*opened].end, .conflict = std::nullopt};
            if (open.has_value())
            {
                open->hold = tenure;
                open->founded = true;
            }
            else
            {
                std::vector<std::optional<ChartStop>> stops(string_count);
                std::vector<bool> carried_in = carry_into(stops, slot.place);
                open = OpenSpan{
                    .front_place = slot.place,
                    .stops = std::move(stops),
                    .bracket_place = slot.place,
                    .last_stated_place = std::nullopt,
                    .struck_in_parts = false,
                    .turned = std::nullopt,
                    .founded = true,
                    .landing_opened = false,
                    .hold = tenure,
                    .carried = std::move(carried_in),
                };
            }
        }
        // An instant no note sounds at is a hold's edge alone: a wall, handled above, or an instant
        // a span's statement no longer stands at.
        if (index == onset_end)
        {
            if (open.has_value() && !in_force(*open, slot.place, slot.sounding))
            {
                emit(slot.place);
            }
            watch_conflicts(slot.place);
            continue;
        }

        // ---- 3. THE VERDICT, against the PRE-instant hand table ------------------------------
        // Standing = the span's statement is still in force here, with per-string renewal (a
        // member whose sound ends exactly here and is re-sounded was replaced, not silenced).
        const bool standing = open.has_value() && in_force(*open, slot.place, slot.sounding);

        // THE GRIP HELD GOING IN, per string: the standing span's own entry — or the entry of the
        // span a hold's edge walled at this very instant, which the hand still held as this slot's
        // strikes came — and nothing where no span stood. It is the only evidence
        // \ref gripStatement is given for what this slot's notes state, because the law's premise
        // is a finger ADDED ABOVE A GRIP THAT IS HELD — a stroke that OPENS a span states the
        // frets it strikes, however its strings were ringing a moment before, so a chord box is
        // never labelled with a fret nobody struck. The tap's floor (below) reads the standing
        // span's own grip instead: it asks what the span being judged holds, not what was held.
        std::vector<std::optional<ChartStop>> gripped_before = walled_grip;
        if (standing && !walled_off)
        {
            gripped_before = open->stops;
        }

        // THE DISPLACEMENT WITNESS, against the PRE-instant hand and read end-INCLUSIVELY:
        // a statement replacing a DIFFERENT sounding stop is the finger observably moving, whether
        // or not any span stands — the same-string clamp puts the displaced ring's end exactly on
        // the displacing strike, so this instant is the only one at which the two coexist. A
        // slide-out ring asserts no grip: by its end the finger is off the board (the GP import
        // figure law's exemption, mirrored). A same-stop restrike is a restatement and witnesses
        // nothing. The witness feeds shape rule 3's foreign-ring break below; the FOREIGN-SOUND
        // record beside it is the join break's state, written here — from the pre-instant table,
        // before any verdict — so a span judged at this very slot reads a current bound.
        std::vector<bool> displaced_here(string_count, false);
        std::vector<bool> sounding_before(string_count, false);
        // THE HOLD-UNDER LAW. Whether a stop this slot states and a stop already down on the string
        // are ONE HAND rather than two. A pull-off proves a finger on its landing stop at the
        // RELEASE and at no earlier instant, so the derivation asserts nothing by itself; what it
        // may say is that a finger ADDED above a stop a standing grip ALREADY HOLDS contradicts
        // nothing, and that the stop RE-EMERGING under it as that finger lifts lifts nothing. ONE
        // authority (\ref gripStatement), asked at the figure's two ends: the note stating
        // here, against the stop under judgment, and the string's own finger, whose
        // verdict was decided at its strike (\ref StringHand::stated_beneath). The second arm rides
        // `sounding_before`, the very witness the displacement reads, so the ring bound and the
        // slide-out exemption are not spelled a second time — a dead source's held finger is as
        // gone as its sound.
        //
        // A source over a stop the string was NOT at states the fret it sounds, and its release is
        // an ordinary new statement: the figure derives exactly as the same notes plainly picked.
        // So does a harmonic over a pressed stop, which states that PRESSED STOP whatever waits
        // beneath it.
        //
        // WHAT THE LAW REACHES (the one list; the deriveChartShapes \param is a pointer here). The
        // pair feeds every seam verdict — displacement and the grip contradiction — and, under
        // THE FOLD, the foreign-sound record, so a planted spell never breaks the span a later
        // strike on its string would join. \c grip_statement_of carries the same verdict into the
        // grip column, so an ornament never rewrites the entry it rides above.
        //
        // An OPEN ring is held by no finger, so a source can ride it only as the standing grip's
        // own open entry. Outside the grip the open ring is foreign sound like any other (shape
        // rule 3): a source struck over it displaces it and ends a foreign spell, so no span prints
        // the source's fret back over the open ring.
        const auto plants_under = [&saved_notes, &planted_stops, &slot, &gripped_before](
                                      const std::size_t string_index, const ChartStop& stop) {
            if (!pressedFret(stop.fret).has_value() && gripped_before[string_index] != stop)
            {
                return false;
            }
            // Bound once so the presence test and the read are provably the same object.
            const std::optional<std::size_t>& striking = slot.strike_notes[string_index];
            return striking.has_value() &&
                   gripStatement(saved_notes[*striking], planted_stops[*striking], stop)
                       .has_value();
        };
        const auto planted_under =
            [&hand, &sounding_before](const std::size_t string_index, const ChartStop& stop) {
                return sounding_before[string_index] && hand[string_index].stated_beneath == stop;
            };
        // The one spelling of "these two stops are two hands": the stop the string held DOWN
        // and the stop stated HERE disagree, and neither plant arm bridges them. Displacement,
        // the foreign-sound record and the grip contradiction all judge exactly this, so they
        // call it rather than restate it — the arms' direction (down->plants_under,
        // here->planted_under) is decided once. A node against a fret, or against the open
        // string, disagrees here like any moved grip, and no plant arm can bridge it.
        const auto differs_by_hand = [&plants_under, &planted_under](
                                         const std::size_t string_index,
                                         const ChartStop& down_stop,
                                         const ChartStop& here_stop) {
            return down_stop != here_stop && !plants_under(string_index, down_stop) &&
                   !planted_under(string_index, here_stop);
        };
        // A strike's GRIP STATEMENT: the fret it sounds — except a pull-off source struck above the
        // stop the STANDING grip holds on its string (\c gripped_before), which states THAT stop,
        // the fret it sounds being the ornament riding above it. The grip column records grip
        // statements, so a source over its own held stop is a plain restatement (the held figure
        // rides, and the ornament never rewrites its string's entry), while one over any other
        // ground states what it sounds and founds, grows or contradicts as the same fret plainly
        // picked would.
        //
        // The harmonic clause rides the one authority (\ref gripStatement), so this site and every
        // other statement site read one rule: a harmonic over a PRESSED stop states that stop, and
        // the slide-out-through below hands back exactly it. A NODE-grip harmonic — a natural —
        // states NO grip at all (\ref nodeGrip): it is never a pull-off source, the resolver
        // refusing a fretHandHarmonic as one, and it founds, grows and joins nothing, its strike
        // reaching only the sound-reading verdicts.
        const auto grip_statement_of =
            [&saved_notes, &planted_stops, &slot, &gripped_before](
                const std::size_t string_index) -> std::optional<ChartStop> {
            // Bound once so the presence test and the read are provably the same object.
            const std::optional<std::size_t>& striking = slot.strike_notes[string_index];
            if (!striking.has_value())
            {
                return std::nullopt;
            }
            const std::optional<ChartStop> beneath = gripStatement(
                saved_notes[*striking], planted_stops[*striking], gripped_before[string_index]);
            if (beneath.has_value())
            {
                return beneath;
            }
            const std::optional<ChartStop>& struck = slot.strikes[string_index];
            return struck.has_value() && nodeGrip(*struck) ? std::nullopt : struck;
        };
        for (std::size_t string_index = 0; string_index < string_count; ++string_index)
        {
            const std::optional<ChartStop>& stated_stop = slot.strikes[string_index];
            if (!stated_stop.has_value())
            {
                continue;
            }
            const std::optional<std::size_t>& finger = hand[string_index].finger;
            const bool sounded = sounds_at(string_index, slot.place);
            sounding_before[string_index] = sounded;
            const std::optional<ChartStop> held = held_at(string_index, slot.place);
            // A DISPLACEMENT is a strike naming a different stop on a string still AUDIBLY holding
            // another — shape rule 3's foreign-ring break, read off the sound, not the grip. That
            // includes a hand-free ring: a fret or a node struck on a string still ringing OPEN
            // changes what that string sounds, and the bracket the standing span would otherwise
            // print states the new stop from a front before which the string audibly rang open.
            // Exempting hand-free rings here ("the hand arriving, not moving") would let a harmonic
            // chord struck over ringing opens grow the standing span, so its bracket would print
            // the nodes over strings still ringing open under it. A hand-free ring is no member,
            // but its SOUND is still evidence, and a strike that changes it breaks.
            displaced_here[string_index] =
                held.has_value() && differs_by_hand(string_index, *held, *stated_stop);

            // THE FOREIGN-SOUND RECORD (the join break's state): a strike taking this string marks
            // the end of whatever foreign stop it last sounded — the displacing strike's own
            // instant under a displacement, the dead ring's own end after a gap of silence. The
            // stop and its end are both the finger's own: a tap sounding on over it is the other
            // hand's, and never keeps an old fretted stop sounding.
            const ChartStop& struck_stop = *stated_stop;
            if (finger.has_value() &&
                slideOutFretOrNull(saved_notes[*finger], arrives_into[*finger]) == nullptr)
            {
                const Fraction sounded_until = std::min(ring_end_of(*finger), slot.place);
                const std::optional<ChartStop> last_held = covers_at(string_index, sounded_until);
                // THE FOLD (extending the hold-under law): a spell the hand provably never left is
                // not foreign. A strike planting the last-held stop is the same hand adding a
                // finger, and a strike the sounding finger plants is the same hand releasing one —
                // neither marks a foreign spell, so neither can break the span a later strike on
                // this string would otherwise join.
                if (last_held.has_value() && differs_by_hand(string_index, *last_held, struck_stop))
                {
                    hand[string_index].foreign_until = sounded_until;
                }
            }
        }

        // THE BREAK VERDICT has two parts. A HAND CONFLICT is the hand provably letting go of the
        // span's grip: a strike releasing a stop it states (hand-position rule 6), or the tap's
        // floor. A NOTATION SPLIT is every other break: the notation divides the span where the
        // hand may still hold its shape. The derivation breaks on either.
        //
        // A CONTRADICTION breaks the grip (shape rule 3): a statement naming a different stop on
        // a string whose stop the span SOUNDED, displacing a stop the hand still audibly holds
        // anywhere (the foreign-ring break: the grip provably moved even where the span never
        // stated that string). On a stated string it is a release, a hand conflict, or a higher
        // fret restated, another finger leaving the stop held; every other contradiction is
        // notation.
        bool hand_conflict = false;
        bool notation_contradiction = false;
        if (standing)
        {
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                // Bound once so each presence test and its reads are provably the same object.
                const std::optional<ChartStop>& stated_stop = slot.strikes[string_index];
                const std::optional<std::size_t>& striking = slot.strike_notes[string_index];
                if (!stated_stop.has_value() || !striking.has_value())
                {
                    continue;
                }
                const std::optional<ChartStop>& stated = open->stops[string_index];
                if (stated.has_value() &&
                    released_at(string_index, *stated, slot.place, *striking) == slot.place)
                {
                    hand_conflict = true;
                    continue;
                }
                // Judged against the strike's GRIP STATEMENT where one strikes: a source over the
                // stop its string still held restates it, and every other strike is judged on
                // what it sounds — a node strike, which states no grip, on its node.
                const std::optional<ChartStop> statement = grip_statement_of(string_index);
                const ChartStop here_stop = statement.value_or(*stated_stop);
                // THE JOIN BREAK: a span asserts its whole grip from its front, and growth cannot
                // move the front, so a string whose foreign sound ended INSIDE the span — a
                // natural harmonic's chime, an open ring dying under the shape, a free glide
                // landing under it — breaks rather than joins; the statement then opens a span of
                // its own with the survivors carried. The displacement is this rule's special
                // case, the ring still sounding at the strike; where the ring ended is no
                // difference in what the hand did. Without it the fret would join and the bracket
                // would print it over the chime's tail.
                notation_contradiction =
                    notation_contradiction || displaced_here[string_index] ||
                    (stated.has_value() && differs_by_hand(string_index, *stated, here_stop)) ||
                    (!stated.has_value() && statement.has_value() &&
                     open->front_place < foreign_until_at(string_index, slot.place));
            }
            // A held span's own pending conflict — a glide landing, a release by a sounding note —
            // falling at this slot.
            if (const std::optional<HoldTenure>& tenure = open->hold;
                tenure.has_value() && tenure->conflict == slot.place)
            {
                hand_conflict = true;
            }
            // THE TAP'S FLOOR. A picking-hand stop sounds only ABOVE the finger holding its
            // string, so a tap whose path reaches the standing grip's fret or below it — struck
            // there, or slid there — says that finger is gone: the grip moved and the span splits.
            // Over an open-string entry the tap proves nothing, since no finger is down.
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                const std::optional<int>& tap_floor = slot.tap_floors[string_index];
                const std::optional<ChartStop>& gripped = open->stops[string_index];
                if (tap_floor.has_value() && gripped.has_value() && gripped->fret > 0 &&
                    *tap_floor <= gripped->fret)
                {
                    hand_conflict = true;
                }
            }
        }

        // THE STATEMENT-CHARACTER SPLITS: a span's statements keep ONE character — whole or in
        // parts — and the walk splits where the character turns, so the class is a fact of how the
        // span's members arrived rather than a verdict on everything it ever contained.
        //
        // Parts -> chord (the unison restatement): a stroke striking EVERY stop the span states is
        // the whole grip said in unison — a chord statement, WHERE IT STANDS ALONE (the absorption
        // rule above: a stroke same-hold parts sound under is absorbed and states nothing). It
        // closes a sounds-in-parts span (the arpeggio's grip strummed whole is a chord), and it
        // closes ANY span when it also strikes a string never stated — a strict superset states the
        // whole chord AND MORE, a new statement, never growth.
        //
        // Chord -> parts: a stroke sounding PART of what a never-in-parts span STATED — some of its
        // own stops, not all — is the statement coming apart, so the chord span closes here and the
        // parts open their own span through the ordinary open below, on what they strike, with the
        // chord's rings carried into it (shape rule 1). A stroke on strings the span
        // never stated is not this direction at all: it states nothing about the span's own stops
        // coming apart, so it is the statement still assembling — growth. This direction reads the
        // ARITHMETIC alone and never the absorption: a stroke absorbed by the parts that follow is
        // the span FLOWING, so splitting it here would only move the fragmentation one slot
        // earlier.
        //
        // What continues is exactly the chug chain: a never-in-parts span restruck at precisely
        // its own grip. The FOUNDING slot never splits (no span stands at its own open). A
        // differing fret on a stated string does not always break above — THE HOLD-UNDER LAW
        // exempts a pull-off source planting the grip's stop — so the direction's arithmetic
        // counts a string as touched only where the strike RESTATES the span's own stop; the
        // source's ornament above the grip is neither the statement coming apart nor a
        // restatement.
        //
        // Whether any stated member's finger is MID-TRAVEL at this slot — the shape rule 4 fact
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
                    !covers_at(string_index, slot.place).has_value())
                {
                    member_travelling = true;
                    break;
                }
            }
        }

        // A PARTIAL SLIDE — a chord split mid sustain: this slot's statement is divided by its own
        // notated rings — a held member's ring ends while a co-struck glide is IN FLIGHT, where the
        // channel states no stop, so that member's sound dies mid-statement and the figure
        // necessarily sounds in parts from this very slot. A voicing-shift slide whose held strings
        // ring the whole transit stays one statement (the chug that slides up is still a chug), a
        // chord released exactly as one finger leaves on a glide sounded whole for its whole
        // tenure, and a whole-grip travel is shape rule 4's chord slide, with no holder to divide
        // it. The first holder to die is the one that bounds the span, so it alone is asked.
        std::optional<Fraction> shortest_hold;
        for (const std::optional<std::size_t>& striking : slot.strike_notes)
        {
            if (!striking.has_value() || channel_at(*striking, slot.place).travel.has_value())
            {
                continue;
            }
            const Fraction ring = saved_notes[*striking].sustain;
            if (!shortest_hold.has_value() || ring < *shortest_hold)
            {
                shortest_hold = ring;
            }
        }
        bool partial_slide = false;
        if (shortest_hold.has_value())
        {
            const Fraction hold_end = *shortest_hold;
            partial_slide = std::ranges::any_of(
                slot.strike_notes, [&](const std::optional<std::size_t>& striking) {
                    return striking.has_value() &&
                           !channel_at(*striking, slot.place + hold_end).stop.has_value();
                });
        }

        // Whether a strike RESTATES a grip's own stop: it SOUNDS that very stop. A stroke says a
        // STOP, not a string: a strike at a DIFFERENT fret is not a restatement of this one.
        // Equality rather than presence is a no-op on any stream without the hold-under law (a
        // differing fret on a stated string breaks as a contradiction before reaching here) and
        // load-bearing under it — a source striking above the grip is the figure's ornament, never
        // the stop restated. The touched count below reads it.
        const auto restates_stop = [&slot](
                                       const std::size_t string_index,
                                       const std::vector<std::optional<ChartStop>>& stops) {
            return stops[string_index].has_value() &&
                   slot.strikes[string_index] == stops[string_index];
        };
        // Whether this slot's strike on a string is a PULL-OFF landing: at a stop the standing grip
        // states, the release of the ornament that rode above it.
        const auto releases_ornament = [&slot, &connections](const std::size_t string_index) {
            const std::optional<std::size_t>& striking = slot.strike_notes[string_index];
            return striking.has_value() && connections.legato[*striking] == LegatoMotion::Pull;
        };
        // Whether this slot's stroke says the WHOLE of a grip: every stop of it that SOUNDS here —
        // struck now, or under a finger the hand table already holds — is one this stroke STATES.
        // ONE authority for the two sites that ask whether a stroke is a chord statement (the
        // unison restatement and the turn to parts): written out at each, the copies drift, and a
        // divergent one makes a figure's class depend on whether its strings had ever sounded
        // earlier in the chart. What a strike says is its GRIP STATEMENT
        // (\c grip_statement_of), the identity every question here is judged on: a pull-off source
        // riding above the stop its grip holds says that stop, so its string is no member left
        // unsaid, while the touched count, which reads the SOUND, counts its ornament nowhere —
        // neither the statement coming apart nor a restatement (THE HOLD-UNDER LAW: the bracket
        // does not seam when the finger arrives, even with the rest of the grip restruck beside
        // it).
        //
        // Wholeness is the HAND's own verdict (shape rule 5), so it is measured against the span's
        // own stops alone: a carried member is a ring the hand never struck into this span, and a
        // chug restating the whole grip beside it is the chord struck whole, not the shape sounded
        // in parts — the carried ring makes the piece sound in parts on its own (\ref inParts), and
        // the remainder after its death is the box the hand played.
        const auto stroke_says_whole =
            [&hand, &grip_statement_of, &open](const std::vector<std::optional<ChartStop>>& stops) {
                for (std::size_t string_index = 0; string_index < string_count; ++string_index)
                {
                    if (!stops[string_index].has_value() || open->carried[string_index] ||
                        grip_statement_of(string_index) == stops[string_index])
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

        // THE ABSORPTION RULE, which DECOUPLES the two laws a whole-grip stroke would otherwise
        // state at once. THE BOX LAW is display and UNCONDITIONAL — simultaneously struck notes
        // wear a chord box wherever they fall, spans included — and nothing here touches it: the
        // display boxes every co-struck group on its own fretting-hand count and never reads a
        // span's class. THE SPAN LAW is structure and CONDITIONAL: a whole-grip stroke is a span
        // BOUNDARY, and a box-class statement, only where it STANDS ALONE.
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
            const Fraction next_place = onset_place[onset_end];
            // THE PARTS: what the next slot sounds by the FRETTING hand. Every one of them must
            // be a stop THIS stroke struck, at that same stop, and they must be strictly fewer —
            // anything else is a contradiction or growth, and a slot restating the whole set is
            // another unison (the chug chain).
            std::vector<bool> restated(string_count, false);
            std::size_t parts = 0;
            for (std::size_t ahead = onset_end;
                 ahead < saved_notes.size() && onset_place[ahead] == onset_place[onset_end];
                 ++ahead)
            {
                const ChartNote& member = saved_notes[ahead];
                const std::optional<std::size_t> string_index = chartStringIndex(member);
                if (!string_index.has_value())
                {
                    return false;
                }
                const std::optional<ChartStop> stated = grip_statement_of(*string_index);
                // A right-hand onset is the other hand's and sounds no stop of the grip, so it is
                // no part; a fretting-hand strike sounds the stop it presses (a channel is never
                // mid-travel at offset zero). Restatement is judged on GRIP STATEMENTS, like every
                // identity question: the part is asked against the stroke's own statement, so a
                // source riding above the very stop the stroke stated restates it, and any other
                // part is judged on the fret it sounds.
                if (rightHandOnset(member.attack))
                {
                    continue;
                }
                const std::optional<ChartStop> sounded_stop =
                    channel_at(ahead, onset_place[ahead]).stop;
                if (!stated.has_value() || !sounded_stop.has_value())
                {
                    return false;
                }
                const ChartStop part_statement =
                    gripStatement(member, planted_stops[ahead], stated).value_or(*sounded_stop);
                if (*stated != part_statement)
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
            // than after it: EVERY member the parts do NOT restate is still sounding ITS OWN STOP
            // strictly past them, so the span flows through the parts instead of closing where a
            // member quits — a stroke whose un-restated members end at the parts stood alone, and
            // the quit arm closes its span there as the chord it was. Both halves are read off the
            // stroke's own stored notes — the ring, and the channel at that instant — and a
            // MID-TRAVEL channel holds nothing, because a finger between stops is a member of
            // nothing. That is the same reading transit gets everywhere else in this file: a chord
            // slide with transit picks is chord frames joined by slide lines, never an arpeggio
            // bracket, so a stroke with an un-restated member in flight states its chord and is
            // never absorbed.
            bool holds_underneath = false;
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                // Bound once so the presence test and every read are provably one object.
                const std::optional<std::size_t>& striking = slot.strike_notes[string_index];
                if (restated[string_index] || !striking.has_value())
                {
                    continue;
                }
                if (!(next_place < ring_end_of(*striking)) ||
                    !channel_at(*striking, next_place).stop.has_value())
                {
                    return false;
                }
                holds_underneath = true;
            }
            return holds_underneath;
        };
        const bool chord_statement_stands = !absorbed_by_the_next_slot();

        // THE TURN TO PARTS (shape rule 5), asked once of the grip as this slot's growth leaves it:
        // growth by a string the span never stated, a stroke that does not state the whole grip
        // standing alone, or a partial slide, turns a never-in-parts span in place. A member
        // travelling turns nothing: a chord slide with transit picks is chord frames joined by
        // slide lines, never an arpeggio bracket. A grip too small for parts turns like any other:
        // whether it grew to three is publication's question (\ref OpenSpan::turned).
        //
        // Growth, the whole-grip comparison and the absorption are asked only of a span that
        // already holds a stop of its own. A shape hold's span opens at the hold's start holding at
        // most the rings carried into it (shape rule 9), so the first slot to strike inside it is
        // its first statement, whole unless a partial slide divides it: parts sounding under it
        // inside the hold turn the span themselves when they arrive, and parts at the hold's end
        // lie outside the span, which is then the stroke whole. A carried member is no sounding the
        // hand missed — it makes the figure sound in parts on its own (\ref inParts) without
        // changing the hand's own verdict.
        bool turns_to_parts = false;
        bool grows = false;
        if (standing)
        {
            grown = open->stops;
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                if (const std::optional<ChartStop> statement = grip_statement_of(string_index);
                    statement.has_value())
                {
                    grows = grows || !grown[string_index].has_value();
                    grown[string_index] = statement;
                }
            }
            turns_to_parts = slot.struck > 0 && !member_travelling &&
                             ((holdsOwnStop(*open) &&
                               (grows || !(stroke_says_whole(grown) && chord_statement_stands))) ||
                              partial_slide);
        }

        bool unison_restatement = false;
        bool partial_sounding = false;
        bool small_grip_restated = false;
        if (standing && !hand_conflict && !notation_contradiction)
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
                // The span's OWN statement: a carried member is no stop the hand stated here, so a
                // superset of a lone own stop beside a carried ring is growth, not a new chord.
                if (open->carried[string_index])
                {
                    continue;
                }
                ++stated_count;
                // Touched means RESTATED (\c restates_stop) by a stroke of the span's own: a strike
                // at a different fret on a stated string is the hold-under figure's ornament
                // ARRIVING, and a pull-off landing back on the stated stop is that ornament's
                // RELEASE — within a standing span, a source above the gripped stop can only have
                // ridden it. THE HOLD-UNDER LAW seams the bracket at neither, so neither is the
                // span's own statement coming apart.
                if (restates_stop(string_index, open->stops) && !releases_ornament(string_index))
                {
                    ++touched_stated;
                }
            }
            // "Sounding in parts" here is the hand's own strokes' verdict (\c struck_in_parts): a
            // whole stroke over a span the hand sounded in parts is a new chord statement, while
            // the same stroke over a span a carried ring alone makes sound in parts is the chug.
            const bool restates_whole = stroke_says_whole(open->stops);
            unison_restatement = restates_whole && chord_statement_stands &&
                                 stated_count >= g_span_member_threshold &&
                                 (open->struck_in_parts || strikes_beyond_grip);
            // The chord->parts direction measures the stroke against the span's OWN statement, so
            // it fires only where the stroke touches a stated stop without restating them all —
            // sounding PART of what the span stated is the statement coming apart, while a stroke
            // on strings it never stated is the statement still ASSEMBLING, which is growth. Three
            // more guards, each with its own ground. A landing successor arrives stated by no event
            // (last_stated_place empty, a landing is not a sounding), and its FIRST sounding
            // defines its character in place — a lone re-pick turns it into parts where it stands
            // (the interior-class rule), splitting nothing. A member MID-TRAVEL blocks the
            // direction whole: fingers travelling together carry the statement, the glide is not
            // the figure coming apart, and the close belongs to the landing (shape rule 4) — so a
            // restrike beside a travelling member rides, per member and not per slot (the mid-slide
            // rule). And a span already IN PARTS — by its own strokes or by a carried ring, the
            // class that prints (\ref inParts) — wears the bracket that covers partial sounding, so
            // partials ride it unchanged.
            partial_sounding = !restates_whole && touched_stated > 0 && !inParts(*open) &&
                               open->last_stated_place.has_value() && !member_travelling;
            // THE PARTIAL-SLIDE SPLIT (the chord split mid sustain): a partial-slide slot touching
            // a never-in-parts span is the statement coming apart AT this slot — even where it
            // restates the whole grip, since part of that statement immediately leaves while the
            // rest stays — so the box closes here and this slot opens the parts figure at its own
            // onset. It shares every guard the chord->parts direction carries, and the
            // whole-grip slide never fires it.
            partial_sounding =
                partial_sounding || (partial_slide && touched_stated > 0 && !inParts(*open) &&
                                     open->last_stated_place.has_value() && !member_travelling);
            // A WHOLE stroke restating a grip too small for parts is a new statement of the box,
            // never a turn of the one before it: where the parts under its rings absorb it, the
            // chain breaks here and the stroke opens its own span, born in parts (shape rule 5). A
            // partial stroke turns such a grip instead, since it may yet grow to three — which is
            // publication's question (\ref OpenSpan::turned).
            small_grip_restated =
                turns_to_parts && restates_whole && open->founded && tooFewForParts(grown);
        }

        // ---- 4. DISPOSE: continue / grow / break-and-maybe-open ------------------------------
        // The notation half of the break verdict: a contradiction no hand conflict makes, or the
        // statement changing character.
        // A string the span never held, struck while a member is mid-travel, truncates the
        // travelling span there (shape rule 4): fingers travelling together carry the statement,
        // and a glide is not the figure growing.
        const bool notation_split = notation_contradiction || unison_restatement ||
                                    partial_sounding || small_grip_restated ||
                                    (grows && member_travelling);
        // Inside its shape hold a span breaks on a hand conflict alone; every notation split is
        // suppressed there (shape rule 9).
        if (standing && !hand_conflict && !(notation_split && !insideHold(*open, slot.place)))
        {
            // GROWTH IS ACCUMULATION (shape rule 2): every struck stop the grip lacks joins in
            // place; the quit arm is what guarantees absorption only ever unions grips whose sounds
            // genuinely overlap. Same-grip restatements ride as continuation. The grip records GRIP
            // STATEMENTS (\c grip_statement_of), so a source riding above the stop its string
            // still held states that stop — never the ornament it sounds — and the entry stays put.
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                std::optional<ChartStop>& entry = open->stops[string_index];
                if (!entry.has_value())
                {
                    entry = grip_statement_of(string_index);
                }
                // A fretting-hand strike on a carried member's string restates it: the member is
                // the span's own from here, and its death is a quit, not a split.
                if (slot.strikes[string_index].has_value())
                {
                    open->carried[string_index] = false;
                }
            }
            // THE THRESHOLDS (shape rule 1), asked of the span's OWN members as they accumulate:
            // two stops stated at this slot, or three own members sounding at this instant — the
            // ones stated here and the ones whose fretting-hand coverage reaches strictly past it
            // (membership reads the reach column). A carried member counts toward neither: a span
            // exists on its own onsets. Once met, the span is a span for good.
            if (!open->founded)
            {
                std::size_t stated_here = 0;
                std::size_t own_sounding = 0;
                for (std::size_t string_index = 0; string_index < string_count; ++string_index)
                {
                    if (!open->stops[string_index].has_value() || open->carried[string_index])
                    {
                        continue;
                    }
                    if (grip_statement_of(string_index).has_value())
                    {
                        ++stated_here;
                        ++own_sounding;
                    }
                    else if (
                        hand[string_index].finger.has_value() &&
                        slot.place < hand[string_index].covers
                    )
                    {
                        ++own_sounding;
                    }
                }
                open->founded = foundsSpan(stated_here, own_sounding);
            }
            if (slot.struck > 0)
            {
                // Live only for the spans the character splits exclude — a landing successor's
                // FIRST sounding, growth by strings the span never stated, the ABSORBED whole-grip
                // stroke the span flows through, and every turn a shape hold keeps the span whole
                // across — whose class still turns in place, for every way a statement divides
                // (the turn, above). The mid-slide protection — the span RIDES the transit, per
                // member, and closes at the landing — is the span-shape half, and it stands above.
                // The first slot sounding the grip in parts is recorded with the statement before
                // it, which is where publication closes a grip that never grows to three.
                open->struck_in_parts = open->struck_in_parts || turns_to_parts;
                if (open->struck_in_parts && !open->turned.has_value())
                {
                    open->turned =
                        SpanTurn{.place = slot.place, .stated_before = open->last_stated_place};
                }
                open->last_stated_place = slot.place;
                if (!open->bracket_place.has_value())
                {
                    open->bracket_place = slot.place;
                }
            }
        }
        else
        {
            // The grip broke here (a hand conflict, a notation split, a wall, or the statement no
            // longer stands), closing on the fretting hand's head here where it strikes.
            emit(slot.place);

            // THE OPEN (shape rule 1): a fretting-hand statement with no span standing opens a span
            // HERE, at its own instant, and the span runs forward from it; nothing is ever dated
            // back. Its stops are this slot's GRIP STATEMENTS (\c grip_statement_of: an opening
            // stroke states the frets it sounds, a pull-off source among them included, so a box
            // prints what was struck; a node grip is no statement, so three co-struck naturals
            // open nothing, and neither does a right-hand onset, shape rule 7), and the rings
            // sounding here that outlive it join as CARRIED members (\c carry_into). It is a span
            // only once its own members meet a threshold (\ref OpenSpan::founded): two stated here
            // found it at once, while a lone statement opens a span that growth may yet found or
            // that is dropped unseen.
            std::vector<std::optional<ChartStop>> stops(string_count);
            std::size_t stated = 0;
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                const std::optional<ChartStop> statement = grip_statement_of(string_index);
                if (statement.has_value())
                {
                    stops[string_index] = statement;
                    ++stated;
                }
            }
            if (stated > 0)
            {
                // THE CLASS AT THE OPEN (shape rules 3 and 5): a stroke standing alone is the
                // shape struck whole. A PARTIAL-SLIDE stroke is born in parts — the statement
                // itself announces that its members sound separately, the glide leaving while the
                // held strings stay — and so is a stroke ABSORBED by the parts sounding under its
                // rings (the absorption rule): the parts are what the figure turns out to be, so
                // the bracket covers the stroke rather than a one-slot box standing in front of
                // it — a two-stop stroke included, since the grip may yet grow to three (the
                // too-few rule is publication's). The carried members make the figure sound in
                // parts beside the hand's own verdict (\ref inParts).
                const bool whole_stroke = !partial_slide && chord_statement_stands;
                std::vector<bool> carried_in = carry_into(stops, slot.place);
                open = OpenSpan{
                    .front_place = slot.place,
                    .stops = std::move(stops),
                    .bracket_place = slot.place,
                    .last_stated_place = slot.place,
                    .struck_in_parts = !whole_stroke,
                    .turned = std::nullopt,
                    .founded = foundsSpan(stated, stated),
                    .landing_opened = false,
                    .hold = std::nullopt,
                    .carried = std::move(carried_in),
                };
            }
        }

        // ---- 5. APPLY the slot to the hand table (after every verdict has read it) -----------
        for (std::size_t note_at = index; note_at < onset_end; ++note_at)
        {
            const ChartNote& member = saved_notes[note_at];
            const std::optional<std::size_t> string_index = chartStringIndex(member);
            if (!string_index.has_value())
            {
                continue;
            }
            StringHand& string_hand = hand[*string_index];
            const Fraction ring = ring_end_of(note_at);
            if (rightHandOnset(member.attack))
            {
                // Over the finger's own stop only: a tap a pull-off plants elsewhere proves that
                // finger moved, and one with no plant rides whatever the hand holds.
                const std::optional<int>& planted = planted_stops[note_at];
                if (!planted.has_value() ||
                    gripStatement(member, planted, covers_at(*string_index, slot.place))
                        .has_value())
                {
                    string_hand.sounds = std::max(string_hand.sounds, ring);
                }
                continue;
            }
            string_hand.sounds = std::max(string_hand.sounds, ring);
            string_hand.finger = note_at;
            // Decided here, against the grip that stood going in: the dispose above may have closed
            // that span, and everything downstream of the strike reads this verdict instead.
            string_hand.stated_beneath =
                gripStatement(member, planted_stops[note_at], gripped_before[*string_index]);
            string_hand.covers = coverage_at(*string_index, slot.place);
        }
        // Every member's coverage is now written, so a boundary this slot's strikes put at this
        // very instant is judged here rather than at the next.
        settle_landings(slot.place);
        watch_conflicts(slot.place);

        index = onset_end;
    }

    // The stream's end: run the whole landing chain out. The horizon is past every ring, because
    // a chained second landing can lie past the first span's own reach, and a single evaluation
    // would resolve only one link.
    Fraction horizon{};
    for (std::size_t note = 0; note < saved_notes.size(); ++note)
    {
        horizon = std::max(horizon, ring_end_of(note));
    }
    settle_landings(horizon);
    emit(std::nullopt);

    return derived;
}

// Its own parts first, then its size, then the one trigger derived here.
bool chartShapeIsArpeggio(
    const ChartConnections& connections, const ChartShape& shape, const ChartPosture& posture)
{
    if (shape.sounds_in_parts)
    {
        return true;
    }
    // A grip with no member has nothing to arrive separately: a hold's span over silence is a box
    // whatever the other hand sounds inside it. A derived grip too small for parts is a box under
    // the other hand too (shape rule 5), while a span an unnamed shape hold states is an arpeggio
    // at any member count: the hold itself says one shape is held there (shape rule 9).
    const bool no_member = std::ranges::none_of(
        posture.stops, [](const std::optional<ChartStop>& stop) { return stop.has_value(); });
    if (no_member || (!shape.hold_stated && tooFewForParts(posture.stops)))
    {
        return false;
    }
    // The one trigger derived here, because it is a question about the span's extent: the other
    // hand sounding while the fretting hand held the shape. The musical close bounds the window
    // (shape rule 3); display's margin has nothing to say about it.
    const std::vector<Fraction>& places = connections.places;
    const auto from = std::ranges::lower_bound(places, shape.place);
    const auto until = std::ranges::lower_bound(from, places.end(), shape.place + shape.sustain);
    const auto first = static_cast<std::size_t>(std::distance(places.begin(), from));
    const auto last = static_cast<std::size_t>(std::distance(places.begin(), until));
    return std::ranges::any_of(
        std::span{connections.saved_notes}.subspan(first, last - first),
        [](const ChartNote& note) { return rightHandOnset(note.attack); });
}

std::vector<bool> chartShapeArrivals(
    const ChartConnections& connections, const std::vector<ChartShape>& shapes,
    const std::vector<ChartPosture>& postures)
{
    std::vector<bool> arpeggio;
    arpeggio.reserve(shapes.size());
    for (const ChartShape& shape : shapes)
    {
        arpeggio.push_back(chartShapeIsArpeggio(connections, shape, postures[shape.posture]));
    }
    return arpeggio;
}

// The hold's span is the one it states fronted at its start, so one search finds it.
Fraction shapeHoldPublishedEnd(
    const std::vector<ChartShape>& shapes, const TempoMap& tempo_map, const ShapeHold& hold)
{
    if (std::holds_alternative<NoneGrip>(hold.grip))
    {
        return tempo_map.wholeNotePositionAt(hold.end);
    }
    const Fraction start = tempo_map.wholeNotePositionAt(hold.start);
    const auto published = std::ranges::lower_bound(shapes, start, {}, &ChartShape::place);
    if (published == shapes.end() || !published->hold_stated || published->place != start)
    {
        return start;
    }
    return published->place + published->sustain;
}

} // namespace rock_hero::common::core
