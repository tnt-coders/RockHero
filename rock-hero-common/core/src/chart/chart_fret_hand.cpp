#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Visits every stop a note holds, as `visit(from, to, stop)` over the ring offsets [from, to). A
// right-hand onset holds its claim over the whole ring. Otherwise each fret statement — the onset,
// then every keyframe stating a fret — holds until the next one or the ring's end, so the statement
// AT the end yields an empty interval and holds nothing, with no case of its own.
template <typename Visit>
void forEachHeldStop(const ChartNote& note, const std::optional<int>& claim, const Visit& visit)
{
    if (rightHandOnset(note.attack))
    {
        visit(Fraction{}, note.sustain, fretHandStopAt(note, claim, Fraction{}));
        return;
    }
    Fraction from{};
    for (const Keyframe& keyframe : note.keyframes)
    {
        if (!keyframe.fret.has_value())
        {
            continue;
        }
        visit(from, keyframe.offset, fretHandStopAt(note, claim, from));
        from = keyframe.offset;
    }
    visit(from, note.sustain, fretHandStopAt(note, claim, from));
}

} // namespace

// One fold over every note's held stops. Each interval raises every placement whose stretch it
// overlaps: the stretch standing at its start (the last placement at or before it, or the first
// placement when it starts earlier) through the last placement starting before its end.
std::vector<int> deriveFretHandWidths(
    const std::vector<ChartNote>& notes, const std::vector<std::optional<int>>& claimed_stops,
    const std::vector<FretHandPosition>& placements, const TempoMap& tempo_map)
{
    std::vector<int> highest(placements.size(), 0);
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const ChartNote& note = notes[index];
        forEachHeldStop(
            note,
            claimed_stops[index],
            [&](const Fraction from, const Fraction to, const std::optional<ChartStop>& stop) {
                if (!stop.has_value() || !(from < to))
                {
                    return;
                }
                const int fret = handFretOf(*stop);
                const GridPosition start = advanceGridPosition(tempo_map, note.position, from);
                const GridPosition end = advanceGridPosition(tempo_map, note.position, to);
                auto placement = std::ranges::upper_bound(
                    placements, start, std::ranges::less{}, &FretHandPosition::position);
                if (placement != placements.begin())
                {
                    placement = std::prev(placement);
                }
                const auto past = std::ranges::lower_bound(
                    placements, end, std::ranges::less{}, &FretHandPosition::position);
                for (; placement < past; ++placement)
                {
                    int& stretch_highest = highest[static_cast<std::size_t>(
                        std::distance(placements.begin(), placement))];
                    stretch_highest = std::max(stretch_highest, fret);
                }
            });
    }

    std::vector<int> widths;
    widths.reserve(placements.size());
    for (std::size_t index = 0; index < placements.size(); ++index)
    {
        widths.push_back(
            std::max(g_min_fret_hand_width, highest[index] - placements[index].fret + 1));
    }
    return widths;
}

} // namespace rock_hero::common::core
