/*!
\file chart_selection.h
\brief Headless chart selection state for the tablature editing surface.
*/

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <rock_hero/common/core/chart/chart.h>
#include <span>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Stable identity of one authored chart object's place: its musical position plus its string.

The chart's SLOT (\ref rock_hero::common::core::chartSlotOrderLess), and both authored per-string
arrays are keyed by it — the notes and the hold markers — with validation keeping them disjoint and
each array holding one slot at most once. Stable across unrelated edits, unlike projection indices,
which shift whenever an earlier record is inserted or removed.

The slot says WHERE, never WHICH ARRAY: \ref ChartSelectionKey pairs it with the kind, so a selected
object names its own array instead of the chart being re-consulted to say which one holds the slot —
which also keeps each kind's keys in one sorted sequence, the shape every resolution here is a
linear merge over.
*/
struct ChartSlotKey
{
    /*! \brief Musical onset position. */
    common::core::GridPosition position{};

    /*! \brief One-based string, counted from the lowest-pitched string. */
    int string{1};

    /*!
    \brief Orders two slot keys by (position, string), the chart's own slot order.
    \param lhs Left-hand key.
    \param rhs Right-hand key.
    \return Ordering of lhs relative to rhs.
    */
    friend constexpr std::strong_ordering operator<=>(
        const ChartSlotKey& lhs, const ChartSlotKey& rhs) noexcept = default;

    /*!
    \brief Compares two slot keys for equal value.
    \param lhs Left-hand key.
    \param rhs Right-hand key.
    \return True when both keys store equal values.
    */
    friend constexpr bool operator==(const ChartSlotKey& lhs, const ChartSlotKey& rhs) noexcept =
        default;
};

/*!
\brief Which of the chart's authored arrays one selected object lives in.

The selection unit, widened past the note when hold markers became authorable. Each kind is one
array the editor's verbs act on, and every selection question — membership, deletion, the range
verbs' operand — is asked per kind, so a verb that has no meaning for a kind simply reads that
kind's empty operand rather than carrying a guard.

Growth is by enumerator plus one arm of \ref ChartSelection::slotsFor and nothing else — for a
selectable the SLOT names. It buys nothing for one that the slot cannot name: a note's own
waypoints are many per note and identified by (slot, offset), so making them selectable widens the
KEY, and every mutation here plus each `slotsFor` arm is written over `std::vector<ChartSlotKey>`.
The unified waypoint model inherits the kind axis, not a free extension point.
*/
enum class ChartSelectableKind : std::uint8_t
{
    /*! \brief A sounding onset in \ref rock_hero::common::core::Chart::notes. */
    Note,

    /*! \brief A silently-held stop in \ref rock_hero::common::core::Chart::hold_markers. */
    HoldMarker,
};

/*! \brief Stable identity of one selectable chart object: which array holds it, and where. */
struct ChartSelectionKey
{
    /*! \brief Array the object lives in. */
    ChartSelectableKind kind{ChartSelectableKind::Note};

    /*! \brief The object's slot inside that array. */
    ChartSlotKey slot{};

    /*!
    \brief Orders two selection keys by (kind, slot).
    \param lhs Left-hand key.
    \param rhs Right-hand key.
    \return Ordering of lhs relative to rhs.
    */
    friend constexpr std::strong_ordering operator<=>(
        const ChartSelectionKey& lhs, const ChartSelectionKey& rhs) noexcept = default;

    /*!
    \brief Compares two selection keys for equal value.
    \param lhs Left-hand key.
    \param rhs Right-hand key.
    \return True when both keys store equal values.
    */
    friend constexpr bool operator==(
        const ChartSelectionKey& lhs, const ChartSelectionKey& rhs) noexcept = default;
};

/*!
\brief The slot one chart note occupies.
\param note Note to key.
\return The note's slot key.
*/
[[nodiscard]] constexpr ChartSlotKey chartSlotKeyOf(const common::core::ChartNote& note) noexcept
{
    return ChartSlotKey{.position = note.position, .string = note.string};
}

/*!
\brief The slot one hold marker occupies.
\param marker Hold marker to key.
\return The marker's slot key.
*/
[[nodiscard]] constexpr ChartSlotKey chartSlotKeyOf(
    const common::core::ChartHoldMarker& marker) noexcept
{
    return ChartSlotKey{.position = marker.position, .string = marker.string};
}

/*!
\brief The one editor-wide chart selection, kept sorted per kind in the chart's slot order.

One selection over more than one authored array. Each kind's keys live in their own sorted-unique
sequence so key-to-index resolution against that array stays a linear merge, and so a verb reads
its own kind's operand as a plain vector rather than filtering a mixed one. Every mutation is
kind-agnostic — one sorted-unique primitive, chosen by \ref slotsFor — so a new kind adds an
enumerator and one arm, never a branch per verb.
*/
class ChartSelection
{
public:
    /*! \brief Removes every selected object, of every kind. */
    void clear() noexcept;

    /*!
    \brief Replaces the whole selection with one object.
    \param key Object becoming the sole selection.
    */
    void replaceWith(const ChartSelectionKey& key);

    /*!
    \brief Replaces the whole selection with a batch of objects (onset-group click).
    \param keys Objects becoming the selection, in any order; duplicates collapse.
    */
    void replaceWith(const std::vector<ChartSelectionKey>& keys);

    /*!
    \brief Adds one object to the selection; already-selected objects stay selected.

    Production selection growth goes through \ref applyBox (the marquee) and \ref toggle
    (Ctrl+click); this remains as their shared primitive and as a test convenience.

    \param key Object to add.
    */
    void add(const ChartSelectionKey& key);

    /*!
    \brief Toggles one object's membership (Ctrl-click).
    \param key Object whose membership flips.
    */
    void toggle(const ChartSelectionKey& key);

    /*!
    \brief Replaces or extends the selection with a batch of objects (marquee commit).
    \param keys Objects the marquee boxed, in any order.
    \param extend True to add to the existing selection instead of replacing it.
    */
    void applyBox(const std::vector<ChartSelectionKey>& keys, bool extend);

    /*!
    \brief Reports whether an object is selected.
    \param key Object to look up.
    \return True when the object is in the selection.
    */
    [[nodiscard]] bool contains(const ChartSelectionKey& key) const noexcept;

    /*!
    \brief The selected notes' slots in ascending chart slot order.
    \return Sorted unique selected note slots.
    */
    [[nodiscard]] const std::vector<ChartSlotKey>& notes() const noexcept;

    /*!
    \brief The selected hold markers' slots in ascending chart slot order.
    \return Sorted unique selected hold-marker slots.
    */
    [[nodiscard]] const std::vector<ChartSlotKey>& holdMarkers() const noexcept;

    /*!
    \brief The whole selection as kind-tagged keys, notes first and each kind in slot order.

    For the callers that must compare or carry a selection ACROSS kinds — the chart verbs'
    coalescing window, whose proof is "the selection is still the one the last press acted on".
    A note-only proof would call the window dead exactly where a verb moved an object from one
    array to the other, which is the one case its reversal exists for.

    \return Every selected object's key.
    */
    [[nodiscard]] std::vector<ChartSelectionKey> keys() const;

    /*!
    \brief Reports whether nothing at all is selected.
    \return True when no kind holds a selected object.
    */
    [[nodiscard]] bool empty() const noexcept;

private:
    // The sequence one kind's keys live in: the single place the kind maps onto storage, so the
    // mutations above stay written once and a caller never sees which member it landed in. A
    // static template over the selection rather than a const/non-const pair, so the mapping is
    // stated exactly once and const-ness rides through deduction instead of a const_cast.
    template <typename Selection>
    [[nodiscard]] static auto& slotsFor(
        Selection& selection, const ChartSelectableKind kind) noexcept
    {
        switch (kind)
        {
            case ChartSelectableKind::Note:
            {
                return selection.m_notes;
            }
            case ChartSelectableKind::HoldMarker:
            {
                return selection.m_hold_markers;
            }
        }
        // Total above; answering a value outside the enum with the note sequence would be a
        // selection that silently landed in the wrong array.
        std::unreachable();
    }

    // Each sorted unique by (position, string); every mutation preserves the invariant.
    std::vector<ChartSlotKey> m_notes{};
    std::vector<ChartSlotKey> m_hold_markers{};
};

/*!
\brief Resolves sorted slot keys to indices in one of the chart's slot-sorted authored arrays.

THE key resolution, and it is the same merge for every authored array: the notes and the hold
markers are both sorted by (position, string) and the tab projection preserves each order one to
one, so the returned indices address the projection directly and one linear merge answers every
key. Keys that no longer match a record resolve to nothing and are skipped. Every other
key-to-record question (\ref selectedNoteIndices, \ref notesForKeys) is this merge read a
different way — it used to be written three times for notes alone, and a per-array copy would put
that back.

\tparam Record Authored chart record type; \ref chartSlotKeyOf must name its slot.
\param records Authored array sorted by (position, string).
\param keys Keys to resolve, sorted-unique in chart slot order.
\return Ascending indices of the records the keys still name.
*/
template <typename Record>
[[nodiscard]] std::vector<std::size_t> slotIndicesForKeys(
    const std::vector<Record>& records, const std::span<const ChartSlotKey> keys)
{
    std::vector<std::size_t> indices;
    indices.reserve(keys.size());
    std::size_t record_index = 0;
    for (const ChartSlotKey& key : keys)
    {
        while (record_index < records.size() && chartSlotKeyOf(records[record_index]) < key)
        {
            ++record_index;
        }
        if (record_index < records.size() && chartSlotKeyOf(records[record_index]) == key)
        {
            indices.push_back(record_index);
        }
    }
    return indices;
}

/*!
\brief Resolves a selection's note keys to indices in the chart's sorted note stream.

\param notes Chart note stream sorted by (position, string).
\param selection Selection whose note keys are resolved.
\return Ascending projection indices of the selected notes that still exist.
*/
[[nodiscard]] std::vector<std::size_t> selectedNoteIndices(
    const std::vector<common::core::ChartNote>& notes, const ChartSelection& selection);

/*!
\brief Resolves a selection's hold-marker keys to indices in the chart's sorted marker array.

\param markers Chart hold markers sorted by (position, string).
\param selection Selection whose hold-marker keys are resolved.
\return Ascending projection indices of the selected markers that still exist.
*/
[[nodiscard]] std::vector<std::size_t> selectedHoldMarkerIndices(
    const std::vector<common::core::ChartHoldMarker>& markers, const ChartSelection& selection);

/*!
\brief Copies the notes that sorted keys still name, in chart order.

\param notes Chart note stream sorted by (position, string).
\param keys Keys to resolve, sorted-unique in chart order.
\return The named notes, in stream order; keys naming nothing are skipped.
*/
[[nodiscard]] std::vector<common::core::ChartNote> notesForKeys(
    const std::vector<common::core::ChartNote>& notes, std::span<const ChartSlotKey> keys);

/*!
\brief Collects the keys of every authored object sharing one onset — the chord unit of the
containment hierarchy.

Selection granularity follows the containment hierarchy
(docs/plans/in-progress/chart-span-and-selection-model.md §7): a single click selects the
individual object, a DOUBLE click selects the whole onset group this collects — the double-click
path is the sole consumer (the caret's re-derivation deliberately selects the single object
under it, never the group).

Hold markers at the onset join the group, because the group is the HAND's unit at that instant: a
barre stop the charter marked silently is a member of the shape the strum takes, so the verbs that
read both arrays — move and delete — carry it with the chord instead of leaving it behind on a slot
no span reaches any more. The fret verb is the one that does NOT yet carry it; the gap and why it
was left rather than improvised are stated at \ref planRetypeFrets.

\param notes Chart note stream sorted by (position, string).
\param markers Chart hold markers sorted by (position, string).
\param position Onset whose group is collected.
\return Keys of every note and marker at the onset, notes first, each in chart slot order.
*/
[[nodiscard]] std::vector<ChartSelectionKey> chartOnsetGroupKeys(
    const std::vector<common::core::ChartNote>& notes,
    const std::vector<common::core::ChartHoldMarker>& markers, common::core::GridPosition position);

} // namespace rock_hero::editor::core
