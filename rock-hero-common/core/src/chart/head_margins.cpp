#include <algorithm>
#include <cassert>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/chart/head_margins.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

// The stream is sorted, so a chord's members are contiguous and a head repeats only as the last
// one filed. The beats are minimumSustainDistanceBeats' own expression applied to the start just
// asked, so the authority answers once per head instead of twice.
HeadMargins::HeadMargins(const std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    for (const ChartNote& note : notes)
    {
        if (!m_entries.empty() && m_entries.back().head == note.position)
        {
            continue;
        }
        const GridPosition start = marginBefore(tempo_map, note.position);
        m_entries.push_back(
            Entry{
                .head = note.position,
                .before = start,
                .beats = beatDistance(tempo_map, start, note.position),
            });
    }
}

GridPosition HeadMargins::before(const GridPosition& head) const
{
    return entryAt(head).before;
}

Fraction HeadMargins::beats(const GridPosition& head) const
{
    return entryAt(head).beats;
}

const HeadMargins::Entry& HeadMargins::entryAt(const GridPosition& head) const
{
    const auto found = std::ranges::lower_bound(m_entries, head, std::ranges::less{}, &Entry::head);
    assert(found != m_entries.end() && found->head == head && "a margin is asked only at a head");
    return *found;
}

} // namespace rock_hero::common::core
