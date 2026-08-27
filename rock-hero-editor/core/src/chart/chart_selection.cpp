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
    m_waypoints.clear();
}

void ChartSelection::replaceWith(const ChartSelectionKey& key)
{
    clear();
    visitSequence(
        *this, key, [](auto& sequence, const auto& element) { sequence.assign(1, element); });
}

void ChartSelection::replaceWith(const std::vector<ChartSelectionKey>& keys)
{
    applyBox(keys, false);
}

void ChartSelection::add(const ChartSelectionKey& key)
{
    visitSequence(*this, key, [](auto& sequence, const auto& element) {
        const auto insert_at = std::ranges::lower_bound(sequence, element);
        if (insert_at == sequence.end() || *insert_at != element)
        {
            sequence.insert(insert_at, element);
        }
    });
}

void ChartSelection::toggle(const ChartSelectionKey& key)
{
    visitSequence(*this, key, [](auto& sequence, const auto& element) {
        const auto found = std::ranges::lower_bound(sequence, element);
        if (found != sequence.end() && *found == element)
        {
            sequence.erase(found);
            return;
        }
        sequence.insert(found, element);
    });
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

// `std::visit` is not itself noexcept, but this variant can never be valueless: every alternative
// is a trivially copyable aggregate, so no alternative's move can throw and leave it empty.
bool ChartSelection::contains(const ChartSelectionKey& key) const noexcept
{
    return visitSequence(*this, key, [](const auto& sequence, const auto& element) {
        return std::ranges::binary_search(sequence, element);
    });
}

const std::vector<ChartSlotKey>& ChartSelection::notes() const noexcept
{
    return m_notes;
}

const std::vector<ChartSlotKey>& ChartSelection::holdMarkers() const noexcept
{
    return m_hold_markers;
}

const std::vector<ChartWaypointKey>& ChartSelection::waypoints() const noexcept
{
    return m_waypoints;
}

std::vector<ChartSelectionKey> ChartSelection::keys() const
{
    std::vector<ChartSelectionKey> all;
    all.reserve(m_notes.size() + m_hold_markers.size() + m_waypoints.size());
    for (const ChartSlotKey& slot : m_notes)
    {
        all.push_back(ChartNoteKey{.slot = slot});
    }
    for (const ChartSlotKey& slot : m_hold_markers)
    {
        all.push_back(ChartHoldMarkerKey{.slot = slot});
    }
    for (const ChartWaypointKey& waypoint : m_waypoints)
    {
        all.push_back(waypoint);
    }
    return all;
}

bool ChartSelection::empty() const noexcept
{
    return m_notes.empty() && m_hold_markers.empty() && m_waypoints.empty();
}

std::optional<ChartSlotKey> chartCaretSlotFor(const ChartSelectionKey& key)
{
    if (const auto* const note = std::get_if<ChartNoteKey>(&key))
    {
        return note->slot;
    }
    if (const auto* const marker = std::get_if<ChartHoldMarkerKey>(&key))
    {
        return marker->slot;
    }
    return std::nullopt;
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

// The waypoint keys are sorted by (note slot, offset) and the note stream by slot, so one forward
// cursor walks both — the same linear merge every other key resolution here is, with the offset
// lookup inside the note it lands on.
std::vector<ChartWaypointRef> selectedWaypointIndices(
    const std::vector<common::core::ChartNote>& notes,
    const std::vector<common::core::NoteViewState>& drawn, const ChartSelection& selection)
{
    std::vector<ChartWaypointRef> located;
    located.reserve(selection.waypoints().size());
    std::size_t note_index = 0;
    for (const ChartWaypointKey& key : selection.waypoints())
    {
        while (note_index < notes.size() && chartSlotKeyOf(notes[note_index]) < key.note)
        {
            ++note_index;
        }
        if (note_index >= notes.size() || !(chartSlotKeyOf(notes[note_index]) == key.note) ||
            note_index >= drawn.size())
        {
            continue;
        }
        const std::vector<common::core::SlideViewState>& waypoints = drawn[note_index].slides;
        const auto found =
            std::ranges::find(waypoints, key.offset, &common::core::SlideViewState::offset);
        if (found == waypoints.end())
        {
            continue;
        }
        located.push_back(
            ChartWaypointRef{
                .note_index = note_index,
                .waypoint_index =
                    static_cast<std::size_t>(std::ranges::distance(waypoints.begin(), found)),
            });
    }
    return located;
}

// Both authored arrays are sorted by (position, string), so each onset group is one contiguous
// run — the same equal_range over each, because the group is an instant and not an array.
// Waypoints are deliberately not collected: a waypoint sits along a ring rather than at the onset,
// so it is not a member of the hand's unit at that instant.
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
        keys.push_back(ChartNoteKey{.slot = chartSlotKeyOf(note)});
    }
    for (const common::core::ChartHoldMarker& marker : marker_group)
    {
        keys.push_back(ChartHoldMarkerKey{.slot = chartSlotKeyOf(marker)});
    }
    return keys;
}

} // namespace rock_hero::editor::core
