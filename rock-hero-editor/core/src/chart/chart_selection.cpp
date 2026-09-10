#include "chart/chart_selection.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
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

void ChartSelection::toggleAll(const std::vector<ChartSelectionKey>& keys)
{
    const bool all_selected =
        std::ranges::all_of(keys, [this](const ChartSelectionKey& key) { return contains(key); });
    for (const ChartSelectionKey& key : keys)
    {
        if (all_selected)
        {
            toggle(key);
        }
        else
        {
            add(key);
        }
    }
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

// Genuinely non-throwing: visitSequence dispatches through get_if rather than the
// potentially-throwing std::visit, so no bad_variant_access path reaches this noexcept query.
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
        all.emplace_back(ChartNoteKey{.slot = slot});
    }
    for (const ChartKeyframeKey& keyframe : m_keyframes)
    {
        all.emplace_back(keyframe);
    }
    return all;
}

bool ChartSelection::empty() const noexcept
{
    return m_notes.empty() && m_keyframes.empty();
}

ChartSlotKey chartCaretSlotFor(
    const common::core::TempoMap& tempo_map, const ChartSelectionKey& key)
{
    if (const auto* const note = std::get_if<ChartNoteKey>(&key))
    {
        return note->slot;
    }
    const auto& keyframe = std::get<ChartKeyframeKey>(key);
    return ChartSlotKey{
        .position =
            common::core::advanceGridPosition(tempo_map, keyframe.note.position, keyframe.offset),
        .string = keyframe.note.string,
    };
}

std::vector<std::size_t> selectedNoteIndices(
    const std::vector<common::core::ChartNote>& notes, const ChartSelection& selection)
{
    return slotIndicesForKeys(notes, selection.notes());
}

// The keyframe keys are sorted by (note slot, offset) and the note stream by slot, so one forward
// cursor walks both — the same linear merge every other key resolution here is, with the offset
// lookup inside the note it lands on.
std::vector<ChartKeyframeRef> keyframeIndicesForKeys(
    const std::vector<common::core::ChartNote>& notes,
    const std::vector<common::core::NoteViewState>& drawn,
    const std::span<const ChartKeyframeKey> keys)
{
    std::vector<ChartKeyframeRef> located;
    located.reserve(keys.size());
    std::size_t note_index = 0;
    for (const ChartKeyframeKey& key : keys)
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

std::vector<ChartKeyframeRef> selectedKeyframeIndices(
    const std::vector<common::core::ChartNote>& notes,
    const std::vector<common::core::NoteViewState>& drawn, const ChartSelection& selection)
{
    return keyframeIndicesForKeys(notes, drawn, selection.keyframes());
}

// The stream is sorted by (position, string), so an onset group is one contiguous run and this is
// one equal_range — the group is an instant, not an array. Silently-held stops fall inside it with
// no case of their own, which is the whole point of their living in the note stream.
//
// A keyframe's group is the keyframes at its instant across every note that carries one. Those are
// not contiguous — each rides its own note at its own offset — so this walks the notes whose onset
// precedes the instant (a keyframe lies strictly past its onset, so no later note can hold one) and
// asks each for a keyframe at exactly that beat distance. Notes struck at the instant are not
// members: a keyframe sits along a ring, not at an onset, in either direction.
std::vector<ChartSelectionKey> chartOnsetGroupKeys(
    const common::core::TempoMap& tempo_map, const std::vector<common::core::ChartNote>& notes,
    const ChartSelectionKey& key)
{
    const auto* const note_key = std::get_if<ChartNoteKey>(&key);
    if (note_key == nullptr)
    {
        const common::core::GridPosition instant = chartCaretSlotFor(tempo_map, key).position;
        std::vector<ChartSelectionKey> keys;
        for (const common::core::ChartNote& note : notes)
        {
            if (!(note.position < instant))
            {
                break;
            }
            const common::core::Fraction offset =
                common::core::beatDistance(tempo_map, note.position, instant);
            if (std::ranges::find(note.keyframes, offset, &common::core::Keyframe::offset) !=
                note.keyframes.end())
            {
                keys.emplace_back(ChartKeyframeKey{.note = chartSlotKeyOf(note), .offset = offset});
            }
        }
        return keys;
    }
    const auto note_group = std::ranges::equal_range(
        notes, note_key->slot.position, std::less{}, &common::core::ChartNote::position);
    std::vector<ChartSelectionKey> keys;
    keys.reserve(static_cast<std::size_t>(std::ranges::distance(note_group)));
    for (const common::core::ChartNote& note : note_group)
    {
        keys.emplace_back(ChartNoteKey{.slot = chartSlotKeyOf(note)});
    }
    return keys;
}

} // namespace rock_hero::editor::core
