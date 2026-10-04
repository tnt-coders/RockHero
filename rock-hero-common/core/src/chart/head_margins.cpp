#include <algorithm>
#include <cassert>
#include <cstddef>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/chart/head_margins.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

// The places ascend, so a chord's members are contiguous and a head repeats only as the last one
// filed.
HeadMargins::HeadMargins(const std::vector<Fraction>& places, const TempoMap& tempo_map)
{
    for (const Fraction head : places)
    {
        if (!m_entries.empty() && m_entries.back().head == head)
        {
            continue;
        }
        m_entries.push_back(Entry{.head = head, .length = minimumSustainDistance(tempo_map, head)});
    }
}

Fraction HeadMargins::length(const Fraction head) const
{
    return entryAt(head).length;
}

const HeadMargins::Entry& HeadMargins::entryAt(const Fraction head) const
{
    const auto found = std::ranges::lower_bound(m_entries, head, std::ranges::less{}, &Entry::head);
    assert(found != m_entries.end() && found->head == head && "a margin is asked only at a head");
    return *found;
}

} // namespace rock_hero::common::core
