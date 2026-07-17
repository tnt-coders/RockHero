#include "chart/chart_selection.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <utility>

namespace rock_hero::editor::core
{

void ChartSelection::clear() noexcept
{
    m_notes.clear();
    m_focus.reset();
}

void ChartSelection::replaceWith(const ChartNoteKey& key)
{
    m_notes.assign(1, key);
    m_focus = key;
}

void ChartSelection::replaceWith(std::vector<ChartNoteKey> keys)
{
    std::ranges::sort(keys);
    const auto duplicates = std::ranges::unique(keys);
    keys.erase(duplicates.begin(), duplicates.end());
    m_notes = std::move(keys);
    // A batch replacement carries no single touched note; a surviving focus stays, an evicted
    // one falls back to the last key so digits always have a target while anything is selected.
    if (!m_focus.has_value() || !contains(*m_focus))
    {
        m_focus = m_notes.empty() ? std::nullopt : std::optional{m_notes.back()};
    }
}

void ChartSelection::add(const ChartNoteKey& key)
{
    const auto insert_at = std::ranges::lower_bound(m_notes, key);
    if (insert_at == m_notes.end() || *insert_at != key)
    {
        m_notes.insert(insert_at, key);
    }
    m_focus = key;
}

void ChartSelection::toggle(const ChartNoteKey& key)
{
    const auto found = std::ranges::lower_bound(m_notes, key);
    if (found != m_notes.end() && *found == key)
    {
        m_notes.erase(found);
        if (m_focus == key)
        {
            m_focus = m_notes.empty() ? std::nullopt : std::optional{m_notes.back()};
        }
        return;
    }
    m_notes.insert(found, key);
    m_focus = key;
}

void ChartSelection::applyBox(const std::vector<ChartNoteKey>& keys, bool extend)
{
    const std::optional<ChartNoteKey> previous_focus = m_focus;
    if (!extend)
    {
        m_notes.clear();
    }
    for (const ChartNoteKey& key : keys)
    {
        add(key);
    }
    // The box has no single touched note: a surviving previous focus wins (so retypes and
    // selection-follow keep the focus in place), else the last boxed key stands in.
    if (previous_focus.has_value() && contains(*previous_focus))
    {
        m_focus = previous_focus;
    }
    else if (m_focus.has_value() && !contains(*m_focus))
    {
        m_focus = m_notes.empty() ? std::nullopt : std::optional{m_notes.back()};
    }
}

bool ChartSelection::contains(const ChartNoteKey& key) const noexcept
{
    return std::ranges::binary_search(m_notes, key);
}

const std::vector<ChartNoteKey>& ChartSelection::notes() const noexcept
{
    return m_notes;
}

bool ChartSelection::empty() const noexcept
{
    return m_notes.empty();
}

std::optional<ChartNoteKey> ChartSelection::focused() const noexcept
{
    return m_focus;
}

void ChartSelection::focus(const ChartNoteKey& key) noexcept
{
    if (contains(key))
    {
        m_focus = key;
    }
}

// Both sequences share the (position, string) order, so one linear merge resolves every key.
std::vector<std::size_t> selectedNoteIndices(
    const std::vector<common::core::ChartNote>& notes, const ChartSelection& selection)
{
    std::vector<std::size_t> indices;
    indices.reserve(selection.notes().size());
    std::size_t note_index = 0;
    for (const ChartNoteKey& key : selection.notes())
    {
        while (note_index < notes.size() &&
               ChartNoteKey{notes[note_index].position, notes[note_index].string} < key)
        {
            ++note_index;
        }
        if (note_index < notes.size() &&
            ChartNoteKey{notes[note_index].position, notes[note_index].string} == key)
        {
            indices.push_back(note_index);
        }
    }
    return indices;
}

// Chart notes are sorted by (position, string), so an onset group is one contiguous run.
std::vector<ChartNoteKey> chartOnsetGroupKeys(
    const std::vector<common::core::ChartNote>& notes, common::core::GridPosition position)
{
    const auto group =
        std::ranges::equal_range(notes, position, std::less{}, &common::core::ChartNote::position);
    std::vector<ChartNoteKey> keys;
    keys.reserve(static_cast<std::size_t>(std::ranges::distance(group)));
    for (const common::core::ChartNote& note : group)
    {
        keys.push_back(ChartNoteKey{.position = note.position, .string = note.string});
    }
    return keys;
}

} // namespace rock_hero::editor::core
