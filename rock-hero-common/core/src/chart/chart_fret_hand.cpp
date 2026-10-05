#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Where a behind-the-nut bend puts the hand (rule 13): the first fret, which the board clamp lifts
// above a capo like any other floor.
constexpr int g_behind_the_nut_fret{1};

// One demand on the hand at one instant, before the instants are merged into groups.
struct HandDemand
{
    Fraction place{};
    int fret{0};
    // True for a fretted note struck here; a keyframe or a plant only widens the hull.
    bool struck{false};
};

// One instant the hand must cover: the hull of every demand stated there, and how many fretted
// notes are struck there. Only a strike counts toward a chord, so a demand never makes one.
struct CoverageGroup
{
    Fraction place{};
    // The bar the instant lies in, read once from its place (rule 3's bar test).
    int measure{1};
    int lowest{0};
    int highest{0};
    int struck{0};
};

// A plant's reach (rule 12): from its source's onset through its pull-off's.
struct PlantSpan
{
    Fraction from{};
    Fraction to{};
    int fret{0};
};

// The plain rings' pins (rule 6), sorted by start: `reach[k]` is the latest end among the first
// k + 1, so the latest end of every ring started before an instant is one search.
struct RingPins
{
    std::vector<Fraction> starts;
    std::vector<Fraction> reach;

    [[nodiscard]] std::optional<Fraction> latestEndBefore(const Fraction instant) const
    {
        const auto past = std::ranges::lower_bound(starts, instant);
        if (past == starts.begin())
        {
            return std::nullopt;
        }
        return reach[static_cast<std::size_t>(std::distance(starts.begin(), past)) - 1];
    }
};

// Rule 2: every note's demands — its onset (\ref fretHandDemandAt), and every pitched keyframe fret
// statement but the slide-out, where pressure is already off. An arrival is a statement like any
// other and lands on the next head's instant, merging there.
void collectNoteDemands(
    const ChartConnections& connections, std::vector<HandDemand>& demands,
    FretHandDerivationTally& tally)
{
    for (std::size_t index = 0; index < connections.saved_notes.size(); ++index)
    {
        const ChartNote& note = connections.saved_notes[index];
        const Fraction place = connections.places[index];
        if (const std::optional<int> onset = fretHandDemandAt(note, Fraction{}); onset.has_value())
        {
            demands.push_back(HandDemand{.place = place, .fret = *onset, .struck = true});
        }
        const Keyframe* const slide_out = slideOutKeyframe(note, connections.arrives_into[index]);
        for (const Keyframe& keyframe : note.keyframes)
        {
            if (!keyframe.fret.has_value() || &keyframe == slide_out)
            {
                continue;
            }
            const std::optional<int> stop = fretHandDemandAt(note, keyframe.offset);
            if (!stop.has_value())
            {
                continue;
            }
            ++tally.keyframe_demands;
            demands.push_back(
                HandDemand{.place = place + keyframe.offset, .fret = *stop, .struck = false});
        }
    }
}

// Rule 12: a pressed stop a pull-off plants beneath its source is a demand at the source's onset —
// a tapped source included, which demands nothing of its own — that also widens every instant up
// to the pull-off (the spans returned).
[[nodiscard]] std::vector<PlantSpan> collectPlantDemands(
    const ChartConnections& connections, std::vector<HandDemand>& demands,
    FretHandDerivationTally& tally)
{
    const std::vector<std::optional<int>> planted = chartPlantedStops(connections);
    std::vector<PlantSpan> spans;
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
        demands.push_back(
            HandDemand{.place = connections.places[source], .fret = *stop, .struck = false});
        spans.push_back(
            PlantSpan{
                .from = connections.places[source],
                .to = connections.places[index],
                .fret = *stop,
            });
    }
    return spans;
}

// The instants the hand covers, ascending: every demand merged by instant, then every instant a
// plant reaches widened to it.
[[nodiscard]] std::vector<CoverageGroup> coverageGroupsOf(
    const ChartConnections& connections, const TempoMap& tempo_map, FretHandDerivationTally& tally)
{
    std::vector<HandDemand> demands;
    collectNoteDemands(connections, demands, tally);
    const std::vector<PlantSpan> spans = collectPlantDemands(connections, demands, tally);
    std::ranges::stable_sort(demands, std::ranges::less{}, &HandDemand::place);

    std::vector<CoverageGroup> groups;
    for (const HandDemand& demand : demands)
    {
        if (groups.empty() || groups.back().place != demand.place)
        {
            groups.push_back(
                CoverageGroup{
                    .place = demand.place,
                    .measure = tempo_map.gridPositionAtWholeNote(demand.place).measure,
                    .lowest = demand.fret,
                    .highest = demand.fret,
                    .struck = 0,
                });
        }
        CoverageGroup& group = groups.back();
        group.lowest = std::min(group.lowest, demand.fret);
        group.highest = std::max(group.highest, demand.fret);
        if (demand.struck)
        {
            ++group.struck;
        }
    }
    for (const PlantSpan& span : spans)
    {
        for (auto group = std::ranges::lower_bound(groups, span.from, {}, &CoverageGroup::place);
             group != groups.end() && group->place <= span.to;
             ++group)
        {
            group->lowest = std::min(group->lowest, span.fret);
            group->highest = std::max(group->highest, span.fret);
        }
    }
    tally.groups += static_cast<long long>(groups.size());
    tally.struck_groups +=
        std::ranges::count_if(groups, [](const CoverageGroup& group) { return group.struck > 0; });
    return groups;
}

// Rule 3's bar test: the measure changes between group `index` and the group before it.
[[nodiscard]] bool breaksAtBar(const std::vector<CoverageGroup>& groups, const std::size_t index)
{
    return index > 0 && groups[index].measure != groups[index - 1].measure;
}

// Rule 8: a run's width, its frets but never fewer than four — also the capacity rule 3 tests
// a run against.
[[nodiscard]] int runWidth(const int lowest, const int highest)
{
    return std::max(g_min_fret_hand_width, FretWindow::through(lowest, highest).width);
}

// One run of groups, `[head, next)`, and the hull of what it demands.
struct HandRun
{
    std::size_t next{0};
    int lowest{0};
    int highest{0};
};

// Rule 3: the run from `head` closes at a bar line, at a chord, or where the grip would outgrow the
// hand's capacity, max(4, hull) — the reach test keeps the four-fret floor. It also closes at
// `limit`, the first group an authored position takes, since that position states its own window
// from there. `head` must lie before `limit`.
[[nodiscard]] HandRun runFrom(
    const std::vector<CoverageGroup>& groups, const std::size_t head, const std::size_t limit)
{
    HandRun run{
        .next = head + 1,
        .lowest = groups[head].lowest,
        .highest = groups[head].highest,
    };
    for (; run.next < limit; ++run.next)
    {
        const CoverageGroup& group = groups[run.next];
        if (breaksAtBar(groups, run.next) || group.struck > 1)
        {
            break;
        }
        const int joined_lowest = std::min(run.lowest, group.lowest);
        const int joined_highest = std::max(run.highest, group.highest);
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

// Rule 6's plain rings: a note the fretting hand stops at a fret, not dead. Each PINS from its last
// pitched stop — where its fret channel last arrived somewhere new — to the end of its pitched
// ring, where a slide-out's departure ends it. So a slide-then-hold pins at its destination, an
// arrival into the next head pins nothing, and bends and vibrato, which state no fret, keep
// pinning.
[[nodiscard]] RingPins plainRingPinsOf(const ChartConnections& connections)
{
    std::vector<std::pair<Fraction, Fraction>> pins;
    for (std::size_t index = 0; index < connections.saved_notes.size(); ++index)
    {
        const ChartNote& note = connections.saved_notes[index];
        if (note.fret <= 0 || rightHandOnset(note.attack) || note.dead)
        {
            continue;
        }
        const Keyframe* const slide_out = slideOutKeyframe(note, connections.arrives_into[index]);
        int stop = note.fret;
        Fraction stop_from{};
        // The current stop's last statement, where a following slide-out departs from it.
        Fraction stated_last{};
        for (const Keyframe& keyframe : note.keyframes)
        {
            // Bound to a local so the optional check and the access are provably the same object.
            const std::optional<int>& fret = keyframe.fret;
            if (!fret.has_value() || &keyframe == slide_out)
            {
                continue;
            }
            if (*fret != stop)
            {
                stop = *fret;
                stop_from = keyframe.offset;
            }
            stated_last = keyframe.offset;
        }
        const Fraction end = slide_out != nullptr ? stated_last : note.sustain;
        if (stop_from < end)
        {
            const Fraction place = connections.places[index];
            pins.emplace_back(place + stop_from, place + end);
        }
    }
    std::ranges::sort(pins);
    RingPins ring_pins;
    ring_pins.starts.reserve(pins.size());
    ring_pins.reach.reserve(pins.size());
    for (const auto& [from, to] : pins)
    {
        ring_pins.starts.push_back(from);
        ring_pins.reach.push_back(
            ring_pins.reach.empty() ? to : std::max(ring_pins.reach.back(), to));
    }
    return ring_pins;
}

// One slide-out the window may ride: its segment on the whole-note axis and the fret it exits to.
struct SlideOutRide
{
    Fraction start{};
    Fraction end{};
    int exit_fret{0};
};

// The slide-outs of stops the fretting hand holds, each under a window that has stood since the
// slide began, ascending by end.
[[nodiscard]] std::vector<SlideOutRide> slideOutRidesOf(
    const ChartConnections& connections, const std::vector<FretHandEvent>& events)
{
    std::vector<SlideOutRide> rides;
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
        // The slide departs from the last stop stated before it, or the onset's.
        Fraction departure{};
        for (const Keyframe& keyframe : note.keyframes)
        {
            if (keyframe.fret.has_value() && &keyframe != slide_out)
            {
                departure = keyframe.offset;
            }
        }
        // The fretting hand travels only with a stop it holds: a tap's slide-out is the picking
        // hand's.
        if (!fretHandDemandAt(note, departure).has_value())
        {
            continue;
        }
        const Fraction place = connections.places[index];
        const SlideOutRide ride{
            .start = place + departure,
            .end = place + slide_out->offset,
            .exit_fret = *exit_fret,
        };
        // A hand event fronting while the slide travels is the hand moving on: no ride.
        const auto past = firstFretHandEventAfter(events, ride.end);
        if (past != events.begin() && std::prev(past)->front <= ride.start)
        {
            rides.push_back(ride);
        }
    }
    std::ranges::sort(rides, std::ranges::less{}, &SlideOutRide::end);
    return rides;
}

} // namespace

std::optional<int> fretHandDemandAt(const ChartNote& note, const Fraction offset)
{
    if (rightHandOnset(note.attack))
    {
        return std::nullopt;
    }
    const std::optional<int> held = heldFretAt(note, std::nullopt, offset);
    if (held.has_value() || !noteIsBent(note))
    {
        return held;
    }
    return g_behind_the_nut_fret;
}

std::vector<FretHandEvent> deriveFretHandEvents(
    const ChartConnections& connections, const TempoMap& tempo_map, const int capo,
    const std::vector<FretHandPosition>& authored, FretHandDerivationTally* const tally)
{
    // Counted into the caller's tally, or into one nobody reads.
    FretHandDerivationTally unread;
    FretHandDerivationTally& counts = tally != nullptr ? *tally : unread;
    const std::vector<CoverageGroup> groups = coverageGroupsOf(connections, tempo_map, counts);
    const RingPins ring_pins = plainRingPinsOf(connections);
    counts.ring_pins += static_cast<long long>(ring_pins.starts.size());
    const std::vector<Fraction>& onsets = connections.places;

    // Where each authored position stands on the whole-note axis; ascending, as the list is.
    std::vector<Fraction> authored_places;
    authored_places.reserve(authored.size());
    for (const FretHandPosition& position : authored)
    {
        authored_places.push_back(tempo_map.wholeNotePositionAt(position.position));
    }
    // The first group at or after authored position `index`, which that position takes; every
    // group where no such position remains.
    const auto first_group_at = [&](const std::size_t index) {
        if (index == authored_places.size())
        {
            return groups.size();
        }
        return static_cast<std::size_t>(std::distance(
            groups.begin(),
            std::ranges::lower_bound(groups, authored_places[index], {}, &CoverageGroup::place)));
    };

    std::vector<FretHandEvent> events;
    std::optional<FretWindow> standing;
    std::size_t head = 0;
    std::size_t stated = 0;
    while (head < groups.size() || stated < authored.size())
    {
        const std::size_t limit = first_group_at(stated);
        if (head == limit)
        {
            // AN AUTHORED POSITION, standing where it is stated and always an event of its own.
            // Its window is `[fret, end fret]` where the end is stated; else the floor is pinned
            // and rule 8 gives the top: the top of the run it heads, never under four frets. It
            // then stands until the chart contradicts it: it takes its own instant whatever
            // sounds there, and every later group that fits inside the window, up to the next
            // authored position. The derivation resumes at the first group that does not fit.
            const FretHandPosition& position = authored[stated];
            const Fraction place = authored_places[stated];
            ++stated;
            const std::size_t reach = first_group_at(stated);
            int top = position.fret + g_min_fret_hand_width - 1;
            // Bound once so the presence test and the read are provably one object.
            const std::optional<int>& end_fret = position.end_fret;
            if (end_fret.has_value())
            {
                top = *end_fret;
            }
            else if (head < reach)
            {
                top = std::max(top, runFrom(groups, head, reach).highest);
            }
            const FretWindow window = FretWindow::through(position.fret, top);
            events.push_back(FretHandEvent{.front = place, .window = window});
            standing = window;
            while (head < reach &&
                   (groups[head].place == place ||
                    (window.covers(groups[head].lowest) && window.covers(groups[head].highest))))
            {
                ++head;
            }
            continue;
        }

        const HandRun run = runFrom(groups, head, limit);
        ++counts.runs;
        const Fraction run_head = groups[head].place;
        const std::optional<Fraction> ring_end = ring_pins.latestEndBefore(run_head);

        // Rules 5 and 6 only ever remove motion, and only from an UNFORCED run, one the standing
        // window already covers: a one-fret run restates nothing, and nothing moves while a plain
        // ring struck earlier still sounds.
        const bool unforced =
            standing.has_value() && standing->covers(run.lowest) && standing->covers(run.highest);
        if (unforced && ring_end.has_value() && run_head < *ring_end)
        {
            ++counts.held_by_ring;
            head = run.next;
            continue;
        }
        if (unforced && run.lowest == run.highest)
        {
            ++counts.held_on_one_fret;
            head = run.next;
            continue;
        }

        // Rules 7, 8 and 9: the window sits on the run's floor, spans max(4, hull), and fits on the
        // board. Rule 11: it is emitted only where it changes.
        const int width = runWidth(run.lowest, run.highest);
        const FretWindow window{.fret = fittedWindowFret(run.lowest, width, capo), .width = width};
        if (standing == window)
        {
            ++counts.restated;
            head = run.next;
            continue;
        }

        // Rule 10: the front is the first onset after the latest of the previous run's last
        // instant, the latest end among the plain rings struck before this run, and the bar line
        // that broke the run — never later than the run's own head. The first run therefore
        // reaches back to the chart's first note. It never reaches back to the event before it:
        // an authored position over silence keeps every onset up to its own instant.
        Fraction bound{};
        if (ring_end.has_value())
        {
            bound = std::max(bound, *ring_end);
        }
        if (breaksAtBar(groups, head))
        {
            bound = std::max(
                bound, tempo_map.wholeNotePositionAt(measureDownbeat(groups[head].measure)));
        }
        auto after_previous =
            head == 0 ? onsets.begin() : std::ranges::upper_bound(onsets, groups[head - 1].place);
        if (!events.empty())
        {
            after_previous =
                std::max(after_previous, std::ranges::upper_bound(onsets, events.back().front));
        }
        const auto first_onset = std::ranges::lower_bound(after_previous, onsets.end(), bound);
        const Fraction front =
            first_onset != onsets.end() && *first_onset < run_head ? *first_onset : run_head;
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

// Keeps the slide-outs with room before the next demand, shifts the standing window over each
// instant's exits, and adds the way back where no hand event fronts first.
std::vector<FretHandEvent> deriveFretHandRides(
    const ChartConnections& connections, const TempoMap& tempo_map,
    const std::vector<FretHandEvent>& events)
{
    std::vector<SlideOutRide> slide_outs = slideOutRidesOf(connections, events);
    if (slide_outs.empty())
    {
        return {};
    }
    // The instants the hand derivation covers; what it met on the way is no concern of a ride.
    FretHandDerivationTally unread;
    const std::vector<CoverageGroup> demands = coverageGroupsOf(connections, tempo_map, unread);
    const auto next_demand_after = [&demands](const Fraction place) {
        return std::ranges::upper_bound(demands, place, std::ranges::less{}, &CoverageGroup::place);
    };

    // No room: the next demand after the slide begins lands before its end, or exactly on it.
    std::erase_if(slide_outs, [&next_demand_after](const SlideOutRide& slide_out) {
        return next_demand_after(slide_out.start) != next_demand_after(slide_out.end);
    });

    std::vector<FretHandEvent> rides;
    for (auto member = slide_outs.begin(); member != slide_outs.end();)
    {
        const Fraction end = member->end;
        int lowest = member->exit_fret;
        int highest = member->exit_fret;
        for (; member != slide_outs.end() && member->end == end; ++member)
        {
            lowest = std::min(lowest, member->exit_fret);
            highest = std::max(highest, member->exit_fret);
        }
        const auto past = firstFretHandEventAfter(events, end);
        const FretWindow standing = std::prev(past)->window;
        // The least shift that covers every exit, keeping the width; a spread wider than the
        // window keeps its lowest exit covered.
        const FretWindow ridden{
            .fret = std::min(std::max(standing.fret, highest - standing.width + 1), lowest),
            .width = standing.width,
        };
        if (ridden == standing)
        {
            continue;
        }
        rides.push_back(FretHandEvent{.front = end, .window = ridden});
        const auto back = next_demand_after(end);
        if (back != demands.end() && (past == events.end() || back->place < past->front))
        {
            rides.push_back(FretHandEvent{.front = back->place, .window = standing});
        }
    }
    // Slide-outs ending at different instants before one demand share its way back.
    std::ranges::sort(rides, std::ranges::less{}, &FretHandEvent::front);
    const auto repeated = std::ranges::unique(rides);
    rides.erase(repeated.begin(), repeated.end());
    return rides;
}

} // namespace rock_hero::common::core
