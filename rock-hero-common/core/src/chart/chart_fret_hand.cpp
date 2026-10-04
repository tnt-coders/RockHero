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

// Visits every finger a note holds down, as `visit(from, to, fret)` over the ring offsets
// [from, to), the fret empty where no finger is down. A note the picking hand stops the string for
// holds its held stop over the whole ring. Otherwise each fret statement — the onset, then every
// keyframe stating a fret — holds until the next one or the ring's end, so the statement AT the
// end yields an empty interval and holds nothing, with no case of its own.
template <typename Visit>
void forEachHeldFret(const ChartNote& note, const std::optional<int>& held, const Visit& visit)
{
    if (pickingHandStopsString(note.attack, note.harmonic_node))
    {
        visit(Fraction{}, note.sustain, heldFretAt(note, held, Fraction{}));
        return;
    }
    Fraction from{};
    for (const Keyframe& keyframe : note.keyframes)
    {
        if (!keyframe.fret.has_value())
        {
            continue;
        }
        visit(from, keyframe.offset, heldFretAt(note, held, from));
        from = keyframe.offset;
    }
    visit(from, note.sustain, heldFretAt(note, held, from));
}

// THE width rule over the held ranges: an authored end fret is the window's end; otherwise a
// stretch holding nothing gets the narrowest window, the same answer a highest stop below the
// authored fret gives.
[[nodiscard]] std::vector<int> widthsOf(
    const std::vector<std::optional<HeldFretRange>>& ranges,
    const std::vector<FretHandPosition>& placements)
{
    std::vector<int> widths;
    widths.reserve(placements.size());
    for (std::size_t index = 0; index < placements.size(); ++index)
    {
        const FretHandPosition& placement = placements[index];
        // Each bound once so the presence test and the read are provably one object.
        const std::optional<int>& end_fret = placement.end_fret;
        if (end_fret.has_value())
        {
            widths.push_back(FretWindow::through(placement.fret, *end_fret).width);
            continue;
        }
        const std::optional<HeldFretRange>& range = ranges[index];
        const int highest = range.has_value() ? range->highest : 0;
        widths.push_back(std::max(g_min_fret_hand_width, highest - placement.fret + 1));
    }
    return widths;
}

// One fold over every note's held fingers, on the whole-note axis: each interval is its note's
// place plus two ring offsets, and each placement's stretch starts at its own place. Each interval
// widens the range of every placement whose stretch it overlaps: the stretch standing at its start
// (the last placement at or before it, or the first placement when it starts earlier) through the
// last placement starting before its end.
[[nodiscard]] std::vector<std::optional<HeldFretRange>> heldFretRangesOf(
    const std::vector<ChartNote>& notes, const std::vector<Fraction>& places,
    const std::vector<std::optional<int>>& held_frets,
    const std::vector<FretHandPosition>& placements, const TempoMap& tempo_map)
{
    std::vector<Fraction> placement_places;
    placement_places.reserve(placements.size());
    for (const FretHandPosition& placement : placements)
    {
        placement_places.push_back(tempo_map.wholeNotePositionAt(placement.position));
    }
    std::vector<std::optional<HeldFretRange>> ranges(placements.size());
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const Fraction place = places[index];
        forEachHeldFret(
            notes[index],
            held_frets[index],
            [&](const Fraction from, const Fraction to, const std::optional<int>& held) {
                if (!held.has_value() || !(from < to))
                {
                    return;
                }
                const int fret = *held;
                auto placement = std::ranges::upper_bound(placement_places, place + from);
                if (placement != placement_places.begin())
                {
                    placement = std::prev(placement);
                }
                const auto past = std::ranges::lower_bound(placement_places, place + to);
                for (; placement < past; ++placement)
                {
                    std::optional<HeldFretRange>& range = ranges[static_cast<std::size_t>(
                        std::distance(placement_places.begin(), placement))];
                    if (range.has_value())
                    {
                        range->lowest = std::min(range->lowest, fret);
                        range->highest = std::max(range->highest, fret);
                    }
                    else
                    {
                        range = HeldFretRange{.lowest = fret, .highest = fret};
                    }
                }
            });
    }
    return ranges;
}

} // namespace

std::vector<std::optional<HeldFretRange>> deriveHeldFretRanges(
    const std::vector<ChartNote>& notes, const std::vector<std::optional<int>>& held_frets,
    const std::vector<FretHandPosition>& placements, const TempoMap& tempo_map)
{
    return heldFretRangesOf(
        notes, chartNotePlaces(notes, tempo_map), held_frets, placements, tempo_map);
}

// The saved notes, their places and the held stops resolved beside them are index-parallel by
// construction; this is the one place that pairs them for the fold. A bare tap's default — the
// grip the covering span holds — is a finger the window covers like any other.
std::vector<std::optional<HeldFretRange>> deriveHeldFretRanges(
    const ChartResolutions& resolutions, const std::vector<FretHandPosition>& placements,
    const TempoMap& tempo_map)
{
    return heldFretRangesOf(
        resolutions.connections.saved_notes,
        resolutions.connections.places,
        resolutions.held_stops,
        placements,
        tempo_map);
}

std::vector<int> deriveFretHandWidths(
    const std::vector<ChartNote>& notes, const std::vector<std::optional<int>>& held_frets,
    const std::vector<FretHandPosition>& placements, const TempoMap& tempo_map)
{
    return widthsOf(deriveHeldFretRanges(notes, held_frets, placements, tempo_map), placements);
}

std::vector<int> deriveFretHandWidths(
    const ChartResolutions& resolutions, const std::vector<FretHandPosition>& placements,
    const TempoMap& tempo_map)
{
    return widthsOf(deriveHeldFretRanges(resolutions, placements, tempo_map), placements);
}

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
    FretHandDerivationTally* const tally)
{
    // Counted into the caller's tally, or into one nobody reads.
    FretHandDerivationTally unread;
    FretHandDerivationTally& counts = tally != nullptr ? *tally : unread;
    const std::vector<CoverageGroup> groups = coverageGroupsOf(connections, tempo_map, counts);
    const RingPins ring_pins = plainRingPinsOf(connections);
    counts.ring_pins += static_cast<long long>(ring_pins.starts.size());
    const std::vector<Fraction>& onsets = connections.places;

    std::vector<FretHandEvent> events;
    std::optional<FretWindow> standing;
    std::size_t head = 0;
    while (head < groups.size())
    {
        // Rule 3: the run closes at a bar line, at a chord, or where the grip would outgrow the
        // hand's capacity, max(4, hull) — the reach test keeps the four-fret floor.
        int lowest = groups[head].lowest;
        int highest = groups[head].highest;
        std::size_t next = head + 1;
        for (; next < groups.size(); ++next)
        {
            const CoverageGroup& group = groups[next];
            if (breaksAtBar(groups, next) || group.struck > 1)
            {
                break;
            }
            const int joined_lowest = std::min(lowest, group.lowest);
            const int joined_highest = std::max(highest, group.highest);
            if (FretWindow::through(joined_lowest, joined_highest).width >
                runWidth(lowest, highest))
            {
                break;
            }
            lowest = joined_lowest;
            highest = joined_highest;
        }
        ++counts.runs;
        const Fraction run_head = groups[head].place;
        const std::optional<Fraction> ring_end = ring_pins.latestEndBefore(run_head);

        // Rules 5 and 6 only ever remove motion, and only from an UNFORCED run, one the standing
        // window already covers: a one-fret run restates nothing, and nothing moves while a plain
        // ring struck earlier still sounds.
        const bool unforced =
            standing.has_value() && standing->covers(lowest) && standing->covers(highest);
        if (unforced && ring_end.has_value() && run_head < *ring_end)
        {
            ++counts.held_by_ring;
            head = next;
            continue;
        }
        if (unforced && lowest == highest)
        {
            ++counts.held_on_one_fret;
            head = next;
            continue;
        }

        // Rules 7, 8 and 9: the window sits on the run's floor, spans max(4, hull), and fits on the
        // board. Rule 11: it is emitted only where it changes.
        const int width = runWidth(lowest, highest);
        const FretWindow window{.fret = fittedWindowFret(lowest, width, capo), .width = width};
        if (standing == window)
        {
            ++counts.restated;
            head = next;
            continue;
        }

        // Rule 10: the front is the first onset after the latest of the previous run's last
        // instant, the latest end among the plain rings struck before this run, and the bar line
        // that broke the run — never later than the run's own head. The first run therefore
        // reaches back to the chart's first note.
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
        const auto after_previous =
            head == 0 ? onsets.begin() : std::ranges::upper_bound(onsets, groups[head - 1].place);
        const auto first_onset = std::ranges::lower_bound(after_previous, onsets.end(), bound);
        const Fraction front =
            first_onset != onsets.end() && *first_onset < run_head ? *first_onset : run_head;
        if (front < run_head)
        {
            ++counts.fronts_reaching_back;
        }

        events.push_back(FretHandEvent{.front = front, .window = window});
        standing = window;
        head = next;
    }
    return events;
}

} // namespace rock_hero::common::core
