#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <span>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// One demand on the hand at one place, before the demands are merged by instant.
struct HandDemand
{
    Fraction place{};
    int fret{0};
    // True for a fretted note struck here; a keyframe or a plant only widens the hull.
    bool struck{false};
    // The fret a fretting finger slid from to state this demand, where it slid here from a
    // different pressed stop (the hand-position slide rule).
    std::optional<int> slid_from{};
};

// One stretch of a note's ring over which its fret channel states one stop, as offsets from the
// onset: the onset's stop up to the first fret statement, each statement's up to the next, and the
// last to the ring's end. A slide-out's fret is no stop the hand takes, so the stop it slides from
// lasts to the end; an arrival into the next head is a stretch of no length on that head's instant.
struct ChannelStretch
{
    Fraction from{};
    Fraction to{};
    // The stop the string is stopped at, by whichever hand stops it: the fretting hand's, read
    // node-aware (\ref frettingStopAt), or the picking hand's own fret where that hand stops it.
    ChartStop stop{};
};

// The walk of a note's fret channel over time: visits, in order, each stop the string is stopped at
// and for how long. Hand-position rule 2's demands start the stretches, the ring holds are the
// pressed ones (\ref fretHandHolds), and hand-position rule 6's release reads them
// (\ref noteReleasesStopAt). A stretch's end is known only once the next statement is reached, so
// one stretch is held pending and visited then.
template <typename Visit>
void forEachChannelStretch(
    const ChartNote& note, const bool arrives_into_next_head, const Visit& visit)
{
    const bool picked = pickingHandStopsString(note.attack, note.harmonic_node);
    // A stretch's fret is the one its opening statement names: the note's own at the onset, then
    // each fret keyframe's.
    const auto stretch_from = [&note, picked](const Fraction from, const int fret) {
        return ChannelStretch{
            .from = from,
            .to = note.sustain,
            .stop = picked ? fretStop(fret) : frettingStopAt(note, fret),
        };
    };
    ChannelStretch pending = stretch_from(Fraction{}, note.fret);
    const Keyframe* const slide_out = slideOutKeyframe(note, arrives_into_next_head);
    for (const Keyframe& keyframe : note.keyframes)
    {
        // Bound once so the presence test and the read are provably the same object.
        const std::optional<int>& fret = keyframe.fret;
        if (fret.has_value() && &keyframe != slide_out)
        {
            pending.to = keyframe.offset;
            visit(pending);
            pending = stretch_from(keyframe.offset, *fret);
        }
    }
    visit(pending);
}

// Hand-position rule 2: every note's demands — the start of every stretch of its fret channel
// (\ref forEachChannelStretch), the onset's struck. An arrival lands on the next head's instant,
// merging there. A stretch reached from a different demanded fret is a slide, and carries the
// fret its finger slid from (the hand-position slide rule).
void collectNoteDemands(
    const ChartConnections& connections, const int capo, std::vector<HandDemand>& demands,
    FretHandDerivationTally& tally)
{
    for (std::size_t index = 0; index < connections.saved_notes.size(); ++index)
    {
        const ChartNote& note = connections.saved_notes[index];
        const Fraction place = connections.places[index];
        std::optional<int> previous;
        bool onset = true;
        forEachChannelStretch(
            note, connections.arrives_into[index], [&](const ChannelStretch& stretch) {
                const std::optional<int> demand = fretHandDemandAt(note, stretch.from, capo);
                // The fret this stretch is reached from: the one the channel demanded before it.
                const std::optional<int> from = std::exchange(previous, demand);
                const bool struck = std::exchange(onset, false);
                if (!demand.has_value())
                {
                    return;
                }
                if (!struck)
                {
                    ++tally.keyframe_demands;
                }
                demands.push_back(
                    HandDemand{
                        .place = place + stretch.from,
                        .fret = *demand,
                        .struck = struck,
                        .slid_from = from.has_value() && *from != *demand ? from : std::nullopt,
                    });
            });
    }
}

// A stop demanded over `[from, to)`: a demand where it begins, widening every instant up to its
// end. Every hold is one, and so is every plant, which is no hold.
struct Widening
{
    Fraction from{};
    Fraction to{};
    int fret{0};
};

// Hand-position rule 12: a pressed stop a pull-off plants beneath its source, demanded from where
// the source states the stop it is pulled from (\ref offsetBeforeEnd: its onset, or a slide's
// arrival) — a tapped source included, which demands nothing of its own — up to the pull-off,
// which itself demands that stop. A demand and no hold (\ref fretHandHolds): the pull-off proves
// the finger down at the release alone, so it pins nothing.
[[nodiscard]] std::vector<Widening> plantWideningsOf(
    const ChartConnections& connections, FretHandDerivationTally& tally)
{
    const std::vector<std::optional<int>> planted = chartPlantedStops(connections);
    std::vector<Widening> widenings;
    for (std::size_t index = 0; index < connections.saved_notes.size(); ++index)
    {
        // A note's one same-string successor is the only note that can plant beneath it, so a
        // plant at this note's predecessor is this note's pull-off.
        const std::size_t source = connections.predecessors[index];
        if (source == g_no_chart_predecessor || !planted[source].has_value())
        {
            continue;
        }
        ++tally.plants;
        const std::optional<int> stop = planted[source].and_then(pressedFret);
        if (!stop.has_value())
        {
            continue;
        }
        ++tally.pressed_plants;
        widenings.push_back(
            Widening{
                .from =
                    connections.places[source] + offsetBeforeEnd(connections.saved_notes[source]),
                .to = connections.places[index],
                .fret = *stop,
            });
    }
    return widenings;
}

// Hand-position rule 3's bar test: the measure changes between instant `index` and the instant
// before it.
[[nodiscard]] bool breaksAtBar(
    const std::vector<FretHandInstant>& instants, const std::size_t index)
{
    return index > 0 && instants[index].measure != instants[index - 1].measure;
}

// Hand-position rule 8: a run's width, its frets but never fewer than four — also the capacity
// hand-position rule 3 tests a run against. A slide's grip can prove a hand narrower, which still
// reaches this far (`reachOf`).
[[nodiscard]] int runWidth(const int lowest, const int highest)
{
    return std::max(g_min_fret_hand_width, FretWindow::through(lowest, highest).width);
}

// One run of instants, `[head, next)`, its floor and the highest fret it demands. The floor is the
// lowest fret demanded (hand-position rule 7), or the floor a slide proved (the slide rule).
struct HandRun
{
    std::size_t next{0};
    int lowest{0};
    int highest{0};
};

// Hand-position rules 8 and 9 over a given floor: the window from `floor` reaching `highest`, never
// under four frets, fitted on the board.
[[nodiscard]] FretWindow windowFrom(const int floor, const int highest, const int capo)
{
    const int width = runWidth(floor, highest);
    return FretWindow{.fret = fittedWindowFret(floor, width, capo), .width = width};
}

// Hand-position rule 8's reach: how far a hand standing in `hand` reaches from its own floor, never
// under four frets — so a hand a slide's grip proved narrower still reaches four — and never past
// the board's end. The floor stays where the hand stands: a hand near the top is cut by the board,
// never refitted below its floor.
[[nodiscard]] FretWindow reachOf(const FretWindow& hand)
{
    return FretWindow::through(
        hand.fret, std::min(g_max_fret, hand.fret + runWidth(hand.fret, hand.top()) - 1));
}

// What the fingers pressed across a slide's end prove of the hand (`slideProof`).
struct SlideProof
{
    FretWindow hand{};
    // Whether the slide proves `hand`'s top as well as its floor. A hand whose fingers all
    // travelled one distance moved as one: the slide proves the slot they kept, not the width the
    // hand stood at, so its top is proven only where a finger lands on it.
    bool top_proven{false};
};

// The fingers nearest the floor and the top before a slide, each the one that ended outermost
// where several were nearest: the fingers each edge of the hand keeps its distance from
// (`slideProof`).
[[nodiscard]] std::ranges::minmax_result<FretHandFinger> nearestFingers(
    const std::span<const FretHandFinger> fingers)
{
    assert(!fingers.empty() && "a slide's end has the finger that slid there");
    return std::ranges::minmax(fingers, std::ranges::less{}, [](const FretHandFinger& finger) {
        return std::pair{finger.from, finger.to};
    });
}

// Whether a finger slides past the one nearest an edge before the slide, ending beyond it: a
// crossing, the only slide whose edge the pin in `slideProof` can move. Arriving exactly at that
// finger's fret crosses nothing.
[[nodiscard]] bool crossesAtAnEdge(const std::span<const FretHandFinger> fingers)
{
    const auto nearest = nearestFingers(fingers);
    return std::ranges::any_of(fingers, [&nearest](const FretHandFinger& finger) {
        return finger.to < nearest.min.to || finger.to > nearest.max.to;
    });
}

// THE HAND-POSITION SLIDE RULE, stated once for a slide arriving at a stop and for a slide-out's
// end: where the fingers pressed across the end put the hand that stood in `standing`, given
// `lowest` to `highest`, what it must cover there. Each sliding finger keeps its slot and each stop
// held across the end stays put, so each edge of the standing window keeps its distance from the
// finger nearest it before the slide — the one that ended outermost, where several were nearest —
// and the hand spans every finger: fingers all travelling one distance move the whole hand by it, a
// slide beside a held finger moves only the edge on the slider's side, held fingers on both sides
// of it move neither edge, and a held finger pins an edge a crossing slide would leave it outside
// of. The hand is fitted on the board by its own width, which never outgrows the board, so a hand
// the grip proves narrower than four frets stands at either edge of the board as proven. Absent
// where a finger pressed before the end lies outside the standing window, or where the hand does
// not reach what it must cover (`reachOf`): the hand did not move with the slide, and rules 7 to 9
// place it (`placedWindow`).
[[nodiscard]] std::optional<SlideProof> slideProof(
    const FretWindow& standing, const std::span<const FretHandFinger> fingers, const int lowest,
    const int highest, const int capo)
{
    const auto nearest = nearestFingers(fingers);
    const auto after = std::ranges::minmax(fingers, std::ranges::less{}, &FretHandFinger::to);
    if (!standing.covers(nearest.min.from, nearest.max.from))
    {
        return std::nullopt;
    }
    const int floor_fret =
        std::min(nearest.min.to - (nearest.min.from - standing.fret), after.min.to);
    const int top_fret =
        std::max(nearest.max.to + (standing.top() - nearest.max.from), after.max.to);
    const int width = std::min(top_fret - floor_fret + 1, g_max_fret - firstPlayableFret(capo) + 1);
    const FretWindow hand{.fret = fittedWindowFret(floor_fret, width, capo), .width = width};
    if (!reachOf(hand).covers(lowest, highest))
    {
        return std::nullopt;
    }
    const int travel = fingers.front().to - fingers.front().from;
    const bool moved_as_one = std::ranges::all_of(fingers, [travel](const FretHandFinger& finger) {
        return finger.to - finger.from == travel;
    });
    return SlideProof{
        .hand = hand,
        .top_proven =
            !moved_as_one || std::ranges::any_of(fingers, [&hand](const FretHandFinger& finger) {
                return finger.to == hand.top();
            }),
    };
}

// Hand-position rules 7 to 9 and THE HAND-POSITION SLIDE RULE: the one placement of a window over
// `lowest` to `highest`, for every derived run and every slide-out's end. On `lowest` at
// max(4, hull), fitted on the board — unless a slide proved the hand (`slideProof`), whose floor it
// keeps: the proven hand itself where its top is proven and nothing reaches past it, else rule 8's
// width from that floor as far as the board allows (`reachOf`).
[[nodiscard]] FretWindow placedWindow(
    const std::optional<SlideProof>& proof, const int lowest, const int highest, const int capo)
{
    if (!proof.has_value())
    {
        return windowFrom(lowest, highest, capo);
    }
    if (proof->top_proven && highest <= proof->hand.top())
    {
        return proof->hand;
    }
    return reachOf(FretWindow::through(proof->hand.fret, highest));
}

// THE HAND-POSITION SLIDE RULE's landing: the run from the arrival at `head` is played in the hand
// the slide proved, on its floor, as far as that hand reaches (`reachOf`); the first instant it
// does not reach — a finger outside the hand — is the hand moving again, and heads the next run.
// The run never outruns `landing`, rule 3's run from the arrival: a slide proves the hand, never a
// figure wider than rule 3 forms.
[[nodiscard]] HandRun playedIn(
    const std::vector<FretHandInstant>& instants, const std::size_t head, const HandRun& landing,
    const SlideProof& proof)
{
    const FretWindow reach = reachOf(proof.hand);
    HandRun run{.next = head + 1, .lowest = proof.hand.fret, .highest = instants[head].highest};
    for (; run.next < landing.next &&
           reach.covers(instants[run.next].lowest, instants[run.next].highest);
         ++run.next)
    {
        run.highest = std::max(run.highest, instants[run.next].highest);
    }
    return run;
}

// Hand-position rule 3: the run from `head` closes at a bar line, at a chord, or where the grip
// would outgrow the hand's capacity, max(4, hull) — the reach test keeps the four-fret floor. It
// also closes at `limit`: the first instant an authored position takes, since that position states
// its own window from there, or the next a slide arrives into (the hand-position slide rule).
// `head` must lie before `limit`.
[[nodiscard]] HandRun runFrom(
    const std::vector<FretHandInstant>& instants, const std::size_t head, const std::size_t limit)
{
    HandRun run{
        .next = head + 1,
        .lowest = instants[head].lowest,
        .highest = instants[head].highest,
    };
    for (; run.next < limit; ++run.next)
    {
        const FretHandInstant& instant = instants[run.next];
        if (breaksAtBar(instants, run.next) || instant.struck > 1)
        {
            break;
        }
        const int joined_lowest = std::min(run.lowest, instant.lowest);
        const int joined_highest = std::max(run.highest, instant.highest);
        if (FretWindow::through(joined_lowest, joined_highest).width >
            runWidth(run.lowest, run.highest))
        {
            break;
        }
        run.lowest = joined_lowest;
        run.highest = joined_highest;
    }
    return run;
}

// One landing of a train of slides: the fingers pressed across the slide into it, and the run it
// heads (rule 3).
struct SlideLanding
{
    std::span<const FretHandFinger> fingers{};
    HandRun run{};
};

// The hand-position slide rule's train: the landings the slides link from the run ending at
// `arrival`, each heading a run of its own, for as long as each run ends at a slide's arrival,
// before `limit`. The slides of a train are linked by the stop the finger keeps pressed between
// them (rule 6's ring), so each proves the hand at its arrival from the hand at its departure, and
// nothing past its arrival — which other instants share a landing's window is rule 3's call, as for
// every figure.
[[nodiscard]] std::vector<SlideLanding> slideTrain(
    const std::vector<FretHandInstant>& instants, const std::size_t arrival,
    const std::size_t limit, const std::vector<std::size_t>& next_slide)
{
    std::vector<SlideLanding> train;
    for (std::size_t at = arrival; at < limit && !instants[at].slide_fingers.empty();
         at = train.back().run.next)
    {
        train.push_back(
            SlideLanding{
                .fingers = instants[at].slide_fingers,
                .run = runFrom(instants, at, std::min(limit, next_slide[at + 1])),
            });
    }
    return train;
}

// The hand-position slide rule read forward along a train: whether, from `window`, the slide into
// every landing proves a hand covering that landing's run — the hand standing where it is and
// travelling only with the slides.
[[nodiscard]] bool standsThrough(
    const FretWindow& window, const std::span<const SlideLanding> train, const int capo)
{
    FretWindow at = window;
    for (const SlideLanding& landing : train)
    {
        const std::optional<SlideProof> proof =
            slideProof(at, landing.fingers, landing.run.lowest, landing.run.highest, capo);
        if (!proof.has_value())
        {
            return false;
        }
        at = placedWindow(proof, landing.run.lowest, landing.run.highest, capo);
    }
    return true;
}

// The hand-position slide rule's carry: `proven`, the window the run a slide lands in provably
// stands in, is where the hand stood after the slide, so the slide read backward — every finger
// travelling back — places the run it leaves exactly as the forward rule places a landing, so the
// two sides of a slide agree wherever no finger crosses the one nearest an edge. Absent where the
// reversed slide proves no hand over the run: the hand moved apart from the slide.
[[nodiscard]] std::optional<FretWindow> carriedBack(
    const HandRun& run, const FretWindow& proven, const std::span<const FretHandFinger> fingers,
    const int capo)
{
    std::vector<FretHandFinger> reversed;
    reversed.reserve(fingers.size());
    std::ranges::transform(fingers, std::back_inserter(reversed), [](const FretHandFinger& finger) {
        return FretHandFinger{.from = finger.to, .to = finger.from};
    });
    const std::optional<SlideProof> proof =
        slideProof(proven, reversed, run.lowest, run.highest, capo);
    if (!proof.has_value())
    {
        return std::nullopt;
    }
    return placedWindow(proof, run.lowest, run.highest, capo);
}

// The hand-position slide rule read backward along a train, from the run that leaves it: the
// window the run provably stands in, where the train proves one. The hand stands where it is
// unless the train refutes it. Read from the last landing first, each landing stands in its own
// window (rules 7 to 9) where the hand stands through the rest of the train from it, else in the
// next landing's proven window with that slide reversed (`carriedBack`); where the reversed slide
// proves no hand over it, the hand moved apart from that slide, and the train is cut there for
// every run before. The leaving run is read the same way from `window`: absent where the hand
// stands through the train from it, or where the reversed slide proves no hand over it.
[[nodiscard]] std::optional<FretWindow> provenBefore(
    const HandRun& run, const FretWindow& window, std::vector<SlideLanding> train, const int capo)
{
    if (train.empty())
    {
        return std::nullopt;
    }
    // The proven window of the landing after the one in hand; the last landing's own to begin.
    FretWindow proven{};
    for (std::size_t index = train.size(); index > 0; --index)
    {
        const SlideLanding& landing = train[index - 1];
        const FretWindow own = windowFrom(landing.run.lowest, landing.run.highest, capo);
        const std::span<const SlideLanding> onward =
            std::span<const SlideLanding>{train}.subspan(index);
        if (onward.empty() || standsThrough(own, onward, capo))
        {
            proven = own;
            continue;
        }
        const std::optional<FretWindow> carried =
            carriedBack(landing.run, proven, onward.front().fingers, capo);
        if (!carried.has_value())
        {
            train.resize(index);
            proven = own;
            continue;
        }
        proven = *carried;
    }
    if (standsThrough(window, train, capo))
    {
        return std::nullopt;
    }
    return carriedBack(run, proven, train.front().fingers, capo);
}

// How many holds begin before `place`: only those can be held across it (hand-position rule 6).
[[nodiscard]] std::size_t holdsBegunBefore(const FretHandDemands& demands, const Fraction place)
{
    return static_cast<std::size_t>(std::distance(
        demands.holds.begin(),
        std::ranges::lower_bound(demands.holds, place, std::ranges::less{}, &FretHandHold::from)));
}

// Whether any of the first `count` holds still holds at `place`: their latest end passes it.
[[nodiscard]] bool anyHoldsPast(
    const FretHandDemands& demands, const std::size_t count, const Fraction place)
{
    return count > 0 && place < demands.hold_reach[count - 1];
}

// Hand-position rule 6: each hold PINS the hand for as long as it holds, so the hand is pinned at
// `place` where a hold begun before it still holds there; one search over the holds' reach.
[[nodiscard]] bool insideHold(const FretHandDemands& demands, const Fraction place)
{
    return anyHoldsPast(demands, holdsBegunBefore(demands, place), place);
}

// Every hold standing ACROSS `place` — begun before it and still holding there (`insideHold`) —
// walked back from the last one begun until the holds' reach falls to `place`: no earlier hold
// can still stand there.
template <typename Visit>
void forEachHoldAcross(const FretHandDemands& demands, const Fraction place, const Visit& visit)
{
    for (std::size_t begun = holdsBegunBefore(demands, place); anyHoldsPast(demands, begun, place);
         --begun)
    {
        const FretHandHold& hold = demands.holds[begun - 1];
        if (place < hold.to)
        {
            visit(hold);
        }
    }
}

// THE HAND-POSITION SLIDE RULE's held fingers at `place`: every stop held across it
// (hand-position rule 6), a finger that travels none.
[[nodiscard]] std::vector<FretHandFinger> fingersHeldAcross(
    const FretHandDemands& demands, const Fraction place)
{
    std::vector<FretHandFinger> held;
    forEachHoldAcross(demands, place, [&held](const FretHandHold& hold) {
        held.push_back(FretHandFinger{.from = hold.fret, .to = hold.fret});
    });
    return held;
}

// One slide-out of a stop the fretting hand holds, on the whole-note axis: where it leaves its last
// stop, where it ends, its finger from that stop to the exit fret, and its note.
struct HeldSlideOut
{
    Fraction departure{};
    Fraction end{};
    FretHandFinger finger{};
    std::size_t note{0};
};

// Every slide-out of a stop the fretting hand holds, ascending by end and, among those ending
// together, in note order.
[[nodiscard]] std::vector<HeldSlideOut> heldSlideOuts(
    const ChartConnections& connections, const int capo)
{
    std::vector<HeldSlideOut> slide_outs;
    for (std::size_t index = 0; index < connections.saved_notes.size(); ++index)
    {
        const ChartNote& note = connections.saved_notes[index];
        const bool arrives = connections.arrives_into[index];
        const Keyframe* const slide_out = slideOutKeyframe(note, arrives);
        const int* const exit_fret = slideOutFretOrNull(note, arrives);
        if (slide_out == nullptr || exit_fret == nullptr)
        {
            continue;
        }
        const Fraction departure = offsetBeforeEnd(note);
        // The fretting hand travels only with a stop it holds: a tap's slide-out is the picking
        // hand's.
        const std::optional<int> leaving = fretHandDemandAt(note, departure, capo);
        if (!leaving.has_value())
        {
            continue;
        }
        const Fraction place = connections.places[index];
        slide_outs.push_back(
            HeldSlideOut{
                .departure = place + departure,
                .end = place + slide_out->offset,
                .finger = FretHandFinger{.from = *leaving, .to = *exit_fret},
                .note = index,
            });
    }
    // Stable, so slide-outs ending together keep note order, and of those departing first the
    // earliest note leads their ride (`FretHandRide::note`).
    std::ranges::stable_sort(slide_outs, std::ranges::less{}, &HeldSlideOut::end);
    return slide_outs;
}

// Where one note releases the `held_fret` on its string, judged from `from`
// (\ref stringReleasesStopAt): each statement in force after `from` is judged, the first of them
// deciding whether the note grips.
[[nodiscard]] std::optional<Fraction> noteReleasesStopAt(
    const ChartConnections& connections, const std::size_t index, const Fraction from,
    const int held_fret)
{
    assert(pressedFret(held_fret).has_value() && "a hold states only a pressed fret");
    const ChartNote& note = connections.saved_notes[index];
    const Fraction place = connections.places[index];
    const bool fretting = !pickingHandStopsString(note.attack, note.harmonic_node);
    const ChartStop held = fretStop(held_fret);
    std::optional<bool> gripping;
    std::optional<Fraction> released;
    forEachChannelStretch(
        note, connections.arrives_into[index], [&](const ChannelStretch& stretch) {
            if (released.has_value() || !(from < place + stretch.to))
            {
                return;
            }
            const bool grips = gripping.has_value()
                                   ? *gripping
                                   : gripping.emplace(fretting && stretch.stop == held);
            // The gripping finger lets go where it slides away. Any other note proves it lifted
            // where it sounds the string at or below it, a node and the open string (both fret 0)
            // included; one above is another finger. One AT it does too: two fingers cannot share
            // one fret on one string.
            const bool releases = grips ? stretch.stop != held : stretch.stop.fret <= held_fret;
            if (releases)
            {
                released = std::max(place + stretch.from, from);
            }
        });
    return released;
}

} // namespace

// Reads the finger's fret, lifted onto the playable board; a bent open string reads as the open
// string, so the lift alone puts it at the first playable fret (hand-position rule 13).
std::optional<int> fretHandDemandAt(const ChartNote& note, const Fraction offset, const int capo)
{
    if (rightHandOnset(note.attack))
    {
        return std::nullopt;
    }
    // The fret slot the finger occupies, pressing a fret or touching a natural harmonic's node;
    // nothing for the open string.
    const std::optional<int> occupied =
        fretHandStopAt(note, std::nullopt, offset).transform(handFretOf).and_then(pressedFret);
    if (!occupied.has_value() && !noteIsBent(note))
    {
        return std::nullopt;
    }
    // A pressed fret already lies above the capo; what the lift moves is a bent open string and a
    // node nearest a fret at or below the capo.
    return std::max(occupied.value_or(0), firstPlayableFret(capo));
}

// The earliest release by any note on the string, scanning only the notes that can come first.
Fraction stringReleasesStopAt(
    const ChartConnections& connections, const std::size_t string_index, const int held_fret,
    const Fraction front, const Fraction close)
{
    const std::vector<std::size_t>& string_notes = connections.string_notes[string_index];
    const auto place_of = [&connections](const std::size_t index) {
        return connections.places[index];
    };
    auto note = std::ranges::lower_bound(string_notes, front, std::ranges::less{}, place_of);
    if (note != string_notes.begin())
    {
        --note;
    }
    Fraction released = close;
    for (; note != string_notes.end() && place_of(*note) < released; ++note)
    {
        const std::optional<Fraction> note_release =
            noteReleasesStopAt(connections, *note, front, held_fret);
        if (note_release.has_value())
        {
            released = std::min(released, *note_release);
        }
    }
    return released;
}

std::vector<FretHandHold> fretHandHolds(
    const ChartConnections& connections, const ChartShapes& shapes, const int capo)
{
    const std::vector<ChartNote>& notes = connections.saved_notes;
    const std::vector<Fraction>& places = connections.places;
    std::vector<FretHandHold> holds;
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const Fraction place = places[index];
        const ChartNote& note = notes[index];
        forEachChannelStretch(
            note,
            connections.arrives_into[index],
            [&holds, &note, place, capo](const ChannelStretch& stretch) {
                // A stretch is a hold where a fretting finger presses its stop: an open string, a
                // natural harmonic's node and a picking-hand onset press nothing.
                const std::optional<int> demand = fretHandDemandAt(note, stretch.from, capo);
                if (demand.has_value() && pressedFret(stretch.stop.fret).has_value() &&
                    stretch.from < stretch.to)
                {
                    holds.push_back(
                        FretHandHold{
                            .from = place + stretch.from,
                            .to = place + stretch.to,
                            .fret = *demand,
                            .string = note.string,
                        });
                }
            });
    }
    // A GRIP: the posture's pressed stop on each string is held from the span's front until the
    // string releases it, else to the close. A slid finger's new stops are the ring holds above.
    for (const ChartShape& shape : shapes.shapes)
    {
        const Fraction close = shape.place + shape.sustain;
        const std::vector<std::optional<ChartStop>>& stops = shapes.postures[shape.posture].stops;
        for (std::size_t string_index = 0; string_index < stops.size(); ++string_index)
        {
            // Bound once so the presence test and the read are provably the same object.
            const std::optional<ChartStop>& stop = stops[string_index];
            if (!stop.has_value() || !pressedFret(stop->fret).has_value())
            {
                continue;
            }
            const Fraction released =
                stringReleasesStopAt(connections, string_index, stop->fret, shape.place, close);
            if (shape.place < released)
            {
                // A posture is indexed by the zero-based string (chartStringIndex).
                holds.push_back(
                    FretHandHold{
                        .from = shape.place,
                        .to = released,
                        .fret = stop->fret,
                        .string = static_cast<int>(string_index) + 1,
                    });
            }
        }
    }
    return holds;
}

// The holds, then the instants: every demand merged by instant, then every instant a stop is held
// at — a plant or a hold — widened to it. A held stop is a demand where it begins, so a span
// fronting where nothing fretted is struck is still an instant the hand covers.
FretHandDemands fretHandDemands(
    const ChartConnections& connections, const ChartShapes& shapes, const int capo,
    const TempoMap& tempo_map, FretHandDerivationTally* const tally)
{
    // Counted into the caller's tally, or into one nobody reads.
    FretHandDerivationTally unread;
    FretHandDerivationTally& counts = tally != nullptr ? *tally : unread;
    FretHandDemands hand_demands;
    hand_demands.capo = capo;
    hand_demands.holds = fretHandHolds(connections, shapes, capo);
    // Stable, so holds beginning together keep the order they were found in.
    std::ranges::stable_sort(hand_demands.holds, std::ranges::less{}, &FretHandHold::from);
    hand_demands.hold_reach.reserve(hand_demands.holds.size());
    for (const FretHandHold& hold : hand_demands.holds)
    {
        hand_demands.hold_reach.push_back(
            hand_demands.hold_reach.empty() ? hold.to
                                            : std::max(hand_demands.hold_reach.back(), hold.to));
    }

    std::vector<HandDemand> demands;
    collectNoteDemands(connections, capo, demands, counts);
    std::vector<Widening> widenings = plantWideningsOf(connections, counts);
    for (const FretHandHold& hold : hand_demands.holds)
    {
        widenings.push_back(Widening{.from = hold.from, .to = hold.to, .fret = hold.fret});
    }
    // Sorted by start so one cursor below widens every instant they cover. Their order among equal
    // starts changes nothing: their demands carry no slide, and a hull is order-independent.
    std::ranges::sort(widenings, std::ranges::less{}, &Widening::from);
    for (const Widening& widening : widenings)
    {
        demands.push_back(
            HandDemand{
                .place = widening.from, .fret = widening.fret, .struck = false, .slid_from = {}
            });
    }
    std::ranges::stable_sort(demands, std::ranges::less{}, &HandDemand::place);

    std::vector<FretHandInstant>& instants = hand_demands.instants;
    for (const HandDemand& demand : demands)
    {
        if (instants.empty() || instants.back().place != demand.place)
        {
            instants.push_back(
                FretHandInstant{
                    .place = demand.place,
                    .measure = tempo_map.gridPositionAtWholeNote(demand.place).measure,
                    .lowest = demand.fret,
                    .highest = demand.fret,
                    .struck = 0,
                    .slide_fingers = {},
                });
        }
        FretHandInstant& instant = instants.back();
        instant.lowest = std::min(instant.lowest, demand.fret);
        instant.highest = std::max(instant.highest, demand.fret);
        if (demand.struck)
        {
            ++instant.struck;
        }
        // Bound once so the presence test and the read are provably the same object.
        const std::optional<int>& slid_from = demand.slid_from;
        if (slid_from.has_value())
        {
            instant.slide_fingers.push_back(FretHandFinger{.from = *slid_from, .to = demand.fret});
        }
    }
    // THE HAND-POSITION SLIDE RULE's fingers, finished: every stop held across an arrival
    // (hand-position rule 6) is a finger there too, one that travels none.
    for (FretHandInstant& instant : instants)
    {
        if (!instant.slide_fingers.empty())
        {
            const std::vector<FretHandFinger> held = fingersHeldAcross(hand_demands, instant.place);
            instant.slide_fingers.insert(instant.slide_fingers.end(), held.begin(), held.end());
        }
    }
    // The widenings ascend by start, so the first instant each covers is searched for from where
    // the previous one's search landed.
    auto first_covered = instants.begin();
    for (const Widening& widening : widenings)
    {
        first_covered = std::ranges::lower_bound(
            first_covered,
            instants.end(),
            widening.from,
            std::ranges::less{},
            &FretHandInstant::place);
        for (auto covered = first_covered;
             covered != instants.end() && covered->place < widening.to;
             ++covered)
        {
            covered->lowest = std::min(covered->lowest, widening.fret);
            covered->highest = std::max(covered->highest, widening.fret);
        }
    }
    counts.instants += static_cast<long long>(instants.size());
    counts.struck_instants += std::ranges::count_if(
        instants, [](const FretHandInstant& instant) { return instant.struck > 0; });
    counts.slide_arrivals += std::ranges::count_if(
        instants, [](const FretHandInstant& instant) { return !instant.slide_fingers.empty(); });
    counts.slide_crossings += std::ranges::count_if(instants, [](const FretHandInstant& instant) {
        return !instant.slide_fingers.empty() && crossesAtAnEdge(instant.slide_fingers);
    });
    return hand_demands;
}

// The onsets struck at the place, one search over the ascending places, then the holds standing
// there: those begun exactly at it, then every hold standing across it (`forEachHoldAcross`).
void forEachGrippedStop(
    const ChartConnections& connections, const FretHandDemands& demands, const Fraction place,
    const std::function<void(int fret, Fraction from, int string)>& visit)
{
    const auto [first, past] = std::ranges::equal_range(connections.places, place);
    for (auto onset = first; onset != past; ++onset)
    {
        const auto index =
            static_cast<std::size_t>(std::distance(connections.places.begin(), onset));
        const ChartNote& note = connections.saved_notes[index];
        const std::optional<int> demand = fretHandDemandAt(note, Fraction{}, demands.capo);
        if (demand.has_value())
        {
            visit(*demand, place, note.string);
        }
    }
    for (const FretHandHold& hold :
         std::ranges::equal_range(demands.holds, place, std::ranges::less{}, &FretHandHold::from))
    {
        if (place < hold.to)
        {
            visit(hold.fret, hold.from, hold.string);
        }
    }
    forEachHoldAcross(demands, place, [&visit](const FretHandHold& hold) {
        visit(hold.fret, hold.from, hold.string);
    });
}

std::vector<FretHandEvent> deriveFretHandEvents(
    const ChartConnections& connections, const FretHandDemands& demands, const TempoMap& tempo_map,
    const std::vector<FretHandPosition>& authored, FretHandDerivationTally* const tally)
{
    // Counted into the caller's tally, or into one nobody reads.
    FretHandDerivationTally unread;
    FretHandDerivationTally& counts = tally != nullptr ? *tally : unread;
    const std::vector<FretHandInstant>& instants = demands.instants;
    const int capo = demands.capo;
    counts.hold_pins += static_cast<long long>(demands.holds.size());
    const std::vector<Fraction>& onsets = connections.places;

    // Where each authored position stands on the whole-note axis; ascending, as the list is.
    std::vector<Fraction> authored_places;
    authored_places.reserve(authored.size());
    for (const FretHandPosition& position : authored)
    {
        authored_places.push_back(tempo_map.wholeNotePositionAt(position.position));
    }
    // The first instant at or after authored position `index`, which that position takes; every
    // instant where no such position remains.
    const auto first_instant_at = [&](const std::size_t index) {
        if (index == authored_places.size())
        {
            return instants.size();
        }
        return static_cast<std::size_t>(std::distance(
            instants.begin(),
            std::ranges::lower_bound(
                instants, authored_places[index], {}, &FretHandInstant::place)));
    };

    // THE HAND-POSITION SLIDE RULE's boundaries: `next_slide[k]` is the first instant at or after
    // `k` that a fretting finger slides into, or every instant where none remains. No figure and no
    // authored position's tenure runs across one.
    std::vector<std::size_t> next_slide(instants.size() + 1, instants.size());
    for (std::size_t index = instants.size(); index > 0; --index)
    {
        next_slide[index - 1] =
            !instants[index - 1].slide_fingers.empty() ? index - 1 : next_slide[index];
    }

    std::vector<FretHandEvent> events;
    std::optional<FretWindow> standing;
    std::size_t head = 0;
    std::size_t stated = 0;
    while (head < instants.size() || stated < authored.size())
    {
        const std::size_t authored_instant = first_instant_at(stated);
        if (head == authored_instant)
        {
            // AN AUTHORED POSITION, standing where it is stated and always an event of its own. Its
            // window is `[fret, end fret]` where the end is stated; else the floor is pinned and
            // hand-position rule 8 (`runWidth`) gives the top: the highest of every stop gripped at
            // its place and of the run it heads, cut at the first later instant reaching below its
            // fret — the position never holds that one, so it widens nothing — never under four
            // frets. A hold standing there but ending before the next instant enters no instant the
            // run reads, so only the gripped stops keep it. The position then stands until the
            // chart contradicts it: it takes its own instant whatever sounds there — a slide
            // arriving there included — and every later instant that fits inside the window, up to
            // the next authored position or slide arrival. The derivation resumes at the first
            // instant that does not fit.
            const FretHandPosition& position = authored[stated];
            const Fraction place = authored_places[stated];
            ++stated;
            const std::size_t own =
                head < instants.size() && instants[head].place == place ? head + 1 : head;
            const std::size_t reach = std::min(first_instant_at(stated), next_slide[own]);
            FretWindow window{};
            // Bound once so the presence test and the read are provably one object.
            const std::optional<int>& end_fret = position.end_fret;
            if (end_fret.has_value())
            {
                window = FretWindow::through(position.fret, *end_fret);
            }
            else
            {
                int top = position.fret;
                forEachGrippedStop(
                    connections,
                    demands,
                    place,
                    [&top](const int fret, const Fraction /*from*/, const int /*string*/) {
                        top = std::max(top, fret);
                    });
                std::size_t held = own;
                while (held < reach && instants[held].lowest >= position.fret)
                {
                    ++held;
                }
                if (head < held)
                {
                    top = std::max(top, runFrom(instants, head, held).highest);
                }
                window = FretWindow{.fret = position.fret, .width = runWidth(position.fret, top)};
            }
            events.push_back(FretHandEvent{.front = place, .window = window});
            standing = window;
            while (head < reach && (instants[head].place == place ||
                                    window.covers(instants[head].lowest, instants[head].highest)))
            {
                ++head;
            }
            continue;
        }

        // THE HAND-POSITION SLIDE RULE reads runs, never single instants: a slide arrival heads a
        // run of its own (`next_slide` closes the one before it), and the run before it is the one
        // the slide leaves.
        const FretHandInstant& arriving = instants[head];
        const bool slid = !arriving.slide_fingers.empty();
        const HandRun figure =
            runFrom(instants, head, std::min(authored_instant, next_slide[head + 1]));
        // THE HAND-POSITION SLIDE RULE, forward: the fingers pressed across the arrival prove
        // where the hand stands there (`slideProof`), and the figure the slide lands in is played
        // in that hand as far as it reaches (`playedIn`); the first instant it does not reach heads
        // the next run. Where they prove nothing, rules 7 to 9 place the figure. Either way the
        // arrival stays a forced boundary that rules 5, 6 and 10 cannot hold or reach back across.
        // A slide-out's end is placed by the same proof (`deriveFretHandRides`).
        const std::optional<SlideProof> proof =
            slid && standing.has_value()
                ? slideProof(
                      *standing, arriving.slide_fingers, arriving.lowest, arriving.highest, capo)
                : std::nullopt;
        const HandRun run = proof.has_value() ? playedIn(instants, head, figure, *proof) : figure;
        ++counts.runs;
        if (proof.has_value() && standing.has_value())
        {
            ++(proof->hand.width == standing->width ? counts.slide_shifts : counts.slide_reshapes);
        }
        const Fraction run_head = arriving.place;
        // The run's first instant the standing window does not cover, or `run.next` where it covers
        // them all. The instants before it are played where the hand already stands. A slide
        // arrival is forced at its head.
        const auto covered = [&](const FretHandInstant& instant) {
            return !slid && standing.has_value() &&
                   standing->covers(instant.lowest, instant.highest);
        };
        auto forced = static_cast<std::size_t>(std::distance(
            instants.begin(),
            std::find_if_not(
                std::next(instants.begin(), static_cast<std::ptrdiff_t>(head)),
                std::next(instants.begin(), static_cast<std::ptrdiff_t>(run.next)),
                covered)));

        // Hand-position rule 6 only ever removes motion, and only from an UNFORCED run, one the
        // standing window already covers: nothing moves while a stop held from earlier still holds,
        // a slide leaving the run included — the hold is the finger's own proof of where the hand
        // is.
        const bool unforced = forced == run.next;
        if (unforced && insideHold(demands, run_head))
        {
            ++counts.held_by_hold;
            head = run.next;
            continue;
        }
        // Hand-position rule 5: a covered one-fret run states nothing about the hand, so it is
        // played where the hand stands — and that is the window a slide leaving it moves (below).
        const bool one_fret = unforced && run.lowest == run.highest;

        // Hand-position rules 7, 8 and 9 place the run — on the hand a slide proved, where one
        // did (`placedWindow`); rule 11 emits only changes.
        FretWindow window = one_fret && standing.has_value()
                                ? *standing
                                : placedWindow(proof, run.lowest, run.highest, capo);

        // THE HAND-POSITION SLIDE RULE, backward: where this run ends at a slide, the train of
        // slides from there — a note's keyframes, a glide into the next head — is read back from
        // its last landing (`provenBefore`): the hand stands where it is unless the train refutes
        // it, and then the window carried back along the train is where the hand provably was, so
        // a slide landing in the pinky's slot left from the pinky however many stops the finger
        // passed. The slide compels the move, so the run is forced from its last instant before
        // the slide. A run the forward rule placed is already proven and is never carried: the
        // forward rule reads from the carried window and plays the train on.
        if (!proof.has_value())
        {
            const std::optional<FretWindow> proven = provenBefore(
                run, window, slideTrain(instants, run.next, authored_instant, next_slide), capo);
            if (proven.has_value())
            {
                ++counts.slide_carried_back;
                window = *proven;
                forced = std::min(forced, run.next - 1);
            }
        }
        if (standing == window)
        {
            if (one_fret)
            {
                ++counts.held_on_one_fret;
            }
            else
            {
                ++counts.restated;
            }
            head = run.next;
            continue;
        }

        // Hand-position rule 10: the front is the first onset after the previous run's last
        // instant, the previous event and the bar line that broke the run that lies inside no hold
        // — never later than the run's first instant the standing window does not cover. An
        // unforced run's move, which nothing compels, is never later than its head. The first run
        // therefore reaches back to the chart's first note. It never reaches back to the event
        // before it: an authored position over silence keeps every onset up to its own instant.
        Fraction bound{};
        if (breaksAtBar(instants, head))
        {
            bound = tempo_map.wholeNotePositionAt(measureDownbeat(instants[head].measure));
        }
        auto after_previous =
            head == 0 ? onsets.begin() : std::ranges::upper_bound(onsets, instants[head - 1].place);
        if (!events.empty())
        {
            after_previous =
                std::max(after_previous, std::ranges::upper_bound(onsets, events.back().front));
        }
        const Fraction latest = forced == run.next ? run_head : instants[forced].place;
        const auto first_free = std::find_if(
            std::ranges::lower_bound(after_previous, onsets.end(), bound),
            onsets.end(),
            [&](const Fraction onset) { return !(onset < latest) || !insideHold(demands, onset); });
        const Fraction front =
            !slid && first_free != onsets.end() && *first_free < latest ? *first_free : latest;
        if (front < run_head)
        {
            ++counts.fronts_reaching_back;
        }

        events.push_back(FretHandEvent{.front = front, .window = window});
        standing = window;
        head = run.next;
    }
    return events;
}

// Finds the first event fronting strictly after a place; the one before it stands there.
std::vector<FretHandEvent>::const_iterator firstFretHandEventAfter(
    const std::vector<FretHandEvent>& events, const Fraction place)
{
    return std::ranges::upper_bound(events, place, std::ranges::less{}, &FretHandEvent::front);
}

// Keeps the slide-outs with room, places the hand at each end they share exactly as at a slide's
// arrival, and finds the way back where no hand event or later ride is first.
std::vector<FretHandRide> deriveFretHandRides(
    const ChartConnections& connections, const FretHandDemands& demands,
    const std::vector<FretHandEvent>& events)
{
    const int capo = demands.capo;
    std::vector<HeldSlideOut> slide_outs = heldSlideOuts(connections, capo);
    // The instants the hand derivation covers.
    const std::vector<FretHandInstant>& instants = demands.instants;

    // ROOM: nothing the hand must do lies strictly inside the slide — no instant it covers, and no
    // hand event fronting, which would be the hand moving on — and a window stands where it
    // departs. Either one exactly at the end is where the ride lands.
    std::erase_if(slide_outs, [&instants, &events](const HeldSlideOut& slide_out) {
        const auto demand = std::ranges::upper_bound(
            instants, slide_out.departure, std::ranges::less{}, &FretHandInstant::place);
        const auto event = firstFretHandEventAfter(events, slide_out.departure);
        return event == events.begin() || (event != events.end() && event->front < slide_out.end) ||
               (demand != instants.end() && demand->place < slide_out.end);
    });

    std::vector<FretHandRide> rides;
    for (auto member = slide_outs.begin(); member != slide_outs.end();)
    {
        const Fraction end = member->end;
        const HeldSlideOut* leading = &*member;
        // THE HAND-POSITION SLIDE RULE at the end, as at a slide's arrival: the fingers pressed
        // across it are every stop held across it, which travels none, and every slide-out ending
        // there, from its last stop to its exit fret.
        std::vector<FretHandFinger> fingers = fingersHeldAcross(demands, end);
        for (; member != slide_outs.end() && member->end == end; ++member)
        {
            fingers.push_back(member->finger);
            if (member->departure < leading->departure)
            {
                leading = &*member;
            }
        }
        // What the hand must cover there: every finger's fret, and nothing more. An instant
        // standing exactly at the end is where the ride lands (the room test above), which the
        // hand events place rather than the ride.
        const auto stops = std::ranges::minmax(fingers, std::ranges::less{}, &FretHandFinger::to);
        const FretWindow standing =
            std::prev(firstFretHandEventAfter(events, leading->departure))->window;
        const FretWindow ridden = placedWindow(
            slideProof(standing, fingers, stops.min.to, stops.max.to, capo),
            stops.min.to,
            stops.max.to,
            capo);
        // Hand-position rule 11: a window the slide leaves where it stands rides nowhere.
        if (ridden == standing)
        {
            continue;
        }
        // The way back: the first instant the hand covers at the end or after it, unless a hand
        // event fronts there or first.
        const auto after = firstFretHandEventAfter(events, end);
        const auto back =
            std::ranges::lower_bound(instants, end, std::ranges::less{}, &FretHandInstant::place);
        std::optional<Fraction> back_place;
        if (std::prev(after)->front != end && back != instants.end() &&
            (after == events.end() || back->place < after->front))
        {
            back_place = back->place;
        }
        rides.push_back(
            FretHandRide{
                .departure = leading->departure,
                .end = end,
                .standing = standing,
                .ridden = ridden,
                .note = leading->note,
                .back = back_place,
            });
    }
    // A later ride arriving by an earlier one's way back takes the window on from there, so the
    // earlier one never returns.
    for (std::size_t index = 0; index + 1 < rides.size(); ++index)
    {
        const std::optional<Fraction>& back = rides[index].back;
        if (back.has_value() && rides[index + 1].end <= *back)
        {
            rides[index].back.reset();
        }
    }
    return rides;
}

// The pipeline's last stage over the notes layer's own demands: the events first, because the
// rides stand only where no event fronts.
FretHandResolutions fretHandResolutions(
    const ChartResolutions& notes_layer, const std::vector<FretHandPosition>& authored,
    const TempoMap& tempo_map)
{
    FretHandResolutions hand;
    hand.events = deriveFretHandEvents(
        notes_layer.connections, notes_layer.hand_demands, tempo_map, authored);
    hand.rides =
        deriveFretHandRides(notes_layer.connections, notes_layer.hand_demands, hand.events);
    return hand;
}

} // namespace rock_hero::common::core
