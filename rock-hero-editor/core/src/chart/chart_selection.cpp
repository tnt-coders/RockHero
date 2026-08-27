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
    m_keyframes.clear();
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

const std::vector<ChartKeyframeKey>& ChartSelection::keyframes() const noexcept
{
    return m_keyframes;
}

std::vector<ChartSelectionKey> ChartSelection::keys() const
{
    std::vector<ChartSelectionKey> all;
    all.reserve(m_notes.size() + m_keyframes.size());
    for (const ChartSlotKey& slot : m_notes)
    {
        all.push_back(ChartNoteKey{.slot = slot});
    }
    for (const ChartKeyframeKey& keyframe : m_keyframes)
    {
        all.push_back(keyframe);
    }
    return all;
}

bool ChartSelection::empty() const noexcept
{
    return m_notes.empty() && m_keyframes.empty();
}

std::optional<ChartSlotKey> chartCaretSlotFor(const ChartSelectionKey& key)
{
    if (const auto* const note = std::get_if<ChartNoteKey>(&key))
    {
        return note->slot;
    }
    return std::nullopt;
}

std::vector<std::size_t> selectedNoteIndices(
    const std::vector<common::core::ChartNote>& notes, const ChartSelection& selection)
{
    return slotIndicesForKeys(notes, selection.notes());
}

// The keyframe keys are sorted by (note slot, offset) and the note stream by slot, so one forward
// cursor walks both — the same linear merge every other key resolution here is, with the offset
// lookup inside the note it lands on.
std::vector<ChartKeyframeRef> selectedKeyframeIndices(
    const std::vector<common::core::ChartNote>& notes,
    const std::vector<common::core::NoteViewState>& drawn, const ChartSelection& selection)
{
    std::vector<ChartKeyframeRef> located;
    located.reserve(selection.keyframes().size());
    std::size_t note_index = 0;
    for (const ChartKeyframeKey& key : selection.keyframes())
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
        const std::vector<common::core::KeyframeViewState>& keyframes = drawn[note_index].slides;
        const auto found =
            std::ranges::find(keyframes, key.offset, &common::core::KeyframeViewState::offset);
        if (found == keyframes.end())
        {
            continue;
        }
        located.push_back(
            ChartKeyframeRef{
                .note_index = note_index,
                .keyframe_index =
                    static_cast<std::size_t>(std::ranges::distance(keyframes.begin(), found)),
            });
    }
    return located;
}

// The stream is sorted by (position, string), so an onset group is one contiguous run and this is
// one equal_range — the group is an instant, not an array. Silently-held stops fall inside it with
// no case of their own, which is the whole point of their living in the note stream. Keyframes are
// deliberately not collected: a keyframe sits along a ring rather than at the onset, so it is not a
// member of the hand's unit at that instant.
std::vector<ChartSelectionKey> chartOnsetGroupKeys(
    const std::vector<common::core::ChartNote>& notes, const common::core::GridPosition position)
{
    const auto note_group =
        std::ranges::equal_range(notes, position, std::less{}, &common::core::ChartNote::position);
    std::vector<ChartSelectionKey> keys;
    keys.reserve(static_cast<std::size_t>(std::ranges::distance(note_group)));
    for (const common::core::ChartNote& note : note_group)
    {
        keys.push_back(ChartNoteKey{.slot = chartSlotKeyOf(note)});
    }
    return keys;
}

} // namespace rock_hero::editor::core
