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
    m_hold_markers.clear();
}

void ChartSelection::replaceWith(const ChartSelectionKey& key)
{
    clear();
    slotsFor(*this, key.kind).assign(1, key.slot);
}

void ChartSelection::replaceWith(const std::vector<ChartSelectionKey>& keys)
{
    applyBox(keys, false);
}

void ChartSelection::add(const ChartSelectionKey& key)
{
    std::vector<ChartSlotKey>& slots = slotsFor(*this, key.kind);
    const auto insert_at = std::ranges::lower_bound(slots, key.slot);
    if (insert_at == slots.end() || *insert_at != key.slot)
    {
        slots.insert(insert_at, key.slot);
    }
}

void ChartSelection::toggle(const ChartSelectionKey& key)
{
    std::vector<ChartSlotKey>& slots = slotsFor(*this, key.kind);
    const auto found = std::ranges::lower_bound(slots, key.slot);
    if (found != slots.end() && *found == key.slot)
    {
        slots.erase(found);
        return;
    }
    slots.insert(found, key.slot);
}

void ChartSelection::applyBox(const std::vector<ChartSelectionKey>& keys, const bool extend)
{
    if (!extend)
    {
        clear();
    }
    for (const ChartSelectionKey& key : keys)
    {
        add(key);
    }
}

bool ChartSelection::contains(const ChartSelectionKey& key) const noexcept
{
    return std::ranges::binary_search(slotsFor(*this, key.kind), key.slot);
}

const std::vector<ChartSlotKey>& ChartSelection::notes() const noexcept
{
    return m_notes;
}

const std::vector<ChartSlotKey>& ChartSelection::holdMarkers() const noexcept
{
    return m_hold_markers;
}

std::vector<ChartSelectionKey> ChartSelection::keys() const
{
    std::vector<ChartSelectionKey> all;
    all.reserve(m_notes.size() + m_hold_markers.size());
    for (const ChartSlotKey& slot : m_notes)
    {
        all.push_back(ChartSelectionKey{.kind = ChartSelectableKind::Note, .slot = slot});
    }
    for (const ChartSlotKey& slot : m_hold_markers)
    {
        all.push_back(ChartSelectionKey{.kind = ChartSelectableKind::HoldMarker, .slot = slot});
    }
    return all;
}

bool ChartSelection::empty() const noexcept
{
    return m_notes.empty() && m_hold_markers.empty();
}

std::vector<std::size_t> selectedNoteIndices(
    const std::vector<common::core::ChartNote>& notes, const ChartSelection& selection)
{
    return slotIndicesForKeys(notes, selection.notes());
}

std::vector<std::size_t> selectedHoldMarkerIndices(
    const std::vector<common::core::ChartHoldMarker>& markers, const ChartSelection& selection)
{
    return slotIndicesForKeys(markers, selection.holdMarkers());
}

std::vector<common::core::ChartNote> notesForKeys(
    const std::vector<common::core::ChartNote>& notes, const std::span<const ChartSlotKey> keys)
{
    std::vector<common::core::ChartNote> named;
    const std::vector<std::size_t> indices = slotIndicesForKeys(notes, keys);
    named.reserve(indices.size());
    for (const std::size_t index : indices)
    {
        named.push_back(notes[index]);
    }
    return named;
}

// Both authored arrays are sorted by (position, string), so each onset group is one contiguous
// run — the same equal_range over each, because the group is an instant and not an array.
std::vector<ChartSelectionKey> chartOnsetGroupKeys(
    const std::vector<common::core::ChartNote>& notes,
    const std::vector<common::core::ChartHoldMarker>& markers,
    const common::core::GridPosition position)
{
    const auto note_group =
        std::ranges::equal_range(notes, position, std::less{}, &common::core::ChartNote::position);
    const auto marker_group = std::ranges::equal_range(
        markers, position, std::less{}, &common::core::ChartHoldMarker::position);
    std::vector<ChartSelectionKey> keys;
    keys.reserve(
        static_cast<std::size_t>(std::ranges::distance(note_group)) +
        static_cast<std::size_t>(std::ranges::distance(marker_group)));
    for (const common::core::ChartNote& note : note_group)
    {
        keys.push_back(
            ChartSelectionKey{
                .kind = ChartSelectableKind::Note,
                .slot = chartSlotKeyOf(note),
            });
    }
    for (const common::core::ChartHoldMarker& marker : marker_group)
    {
        keys.push_back(
            ChartSelectionKey{
                .kind = ChartSelectableKind::HoldMarker,
                .slot = chartSlotKeyOf(marker),
            });
    }
    return keys;
}

} // namespace rock_hero::editor::core
