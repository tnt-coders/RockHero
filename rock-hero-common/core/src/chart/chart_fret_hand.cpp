#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

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

// THE width rule over the held ranges: a stretch holding nothing gets the narrowest window, the
// same answer a highest stop below the authored fret gives.
[[nodiscard]] std::vector<int> widthsOf(
    const std::vector<std::optional<HeldFretRange>>& ranges,
    const std::vector<FretHandPosition>& placements)
{
    std::vector<int> widths;
    widths.reserve(placements.size());
    for (std::size_t index = 0; index < placements.size(); ++index)
    {
        // Bound once so the presence test and the read are provably one object.
        const std::optional<HeldFretRange>& range = ranges[index];
        const int highest = range.has_value() ? range->highest : 0;
        widths.push_back(std::max(g_min_fret_hand_width, highest - placements[index].fret + 1));
    }
    return widths;
}

} // namespace

// One fold over every note's held fingers. Each interval widens the range of every placement whose
// stretch it overlaps: the stretch standing at its start (the last placement at or before it, or
// the first placement when it starts earlier) through the last placement starting before its end.
std::vector<std::optional<HeldFretRange>> deriveHeldFretRanges(
    const std::vector<ChartNote>& notes, const std::vector<std::optional<int>>& held_frets,
    const std::vector<FretHandPosition>& placements, const TempoMap& tempo_map)
{
    std::vector<std::optional<HeldFretRange>> ranges(placements.size());
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const ChartNote& note = notes[index];
        forEachHeldFret(
            note,
            held_frets[index],
            [&](const Fraction from, const Fraction to, const std::optional<int>& held) {
                if (!held.has_value() || !(from < to))
                {
                    return;
                }
                const int fret = *held;
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
                    std::optional<HeldFretRange>& range = ranges[static_cast<std::size_t>(
                        std::distance(placements.begin(), placement))];
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

// The saved notes and the held stops resolved beside them are index-parallel by construction; this
// is the one place that pairs them for the fold. A bare tap's default — the grip the covering span
// holds — is a finger the window covers like any other.
std::vector<std::optional<HeldFretRange>> deriveHeldFretRanges(
    const ChartResolutions& resolutions, const std::vector<FretHandPosition>& placements,
    const TempoMap& tempo_map)
{
    return deriveHeldFretRanges(
        resolutions.connections.saved_notes, resolutions.held_stops, placements, tempo_map);
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

} // namespace rock_hero::common::core
