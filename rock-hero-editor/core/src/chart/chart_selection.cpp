#include "chart/chart_selection.h"

#include <algorithm>

namespace rock_hero::editor::core
{

void ChartSelection::clear() noexcept
{
    m_notes.clear();
}

void ChartSelection::replaceWith(const ChartNoteKey& key)
{
    m_notes.assign(1, key);
}

void ChartSelection::add(const ChartNoteKey& key)
{
    const auto insert_at = std::ranges::lower_bound(m_notes, key);
    if (insert_at == m_notes.end() || *insert_at != key)
    {
        m_notes.insert(insert_at, key);
    }
}

void ChartSelection::toggle(const ChartNoteKey& key)
{
    const auto found = std::ranges::lower_bound(m_notes, key);
    if (found != m_notes.end() && *found == key)
    {
        m_notes.erase(found);
        return;
    }
    m_notes.insert(found, key);
}

void ChartSelection::applyBox(const std::vector<ChartNoteKey>& keys, bool extend)
{
    if (!extend)
    {
        m_notes.clear();
    }
    for (const ChartNoteKey& key : keys)
    {
        add(key);
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

} // namespace rock_hero::editor::core
