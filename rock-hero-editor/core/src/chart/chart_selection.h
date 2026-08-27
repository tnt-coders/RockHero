/*!
\file chart_selection.h
\brief Headless chart selection state for the tablature editing surface.
*/

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Stable identity of one authored chart object's place: its musical position plus its string.

The chart's SLOT (\ref rock_hero::common::core::chartSlotOrderLess), and both authored per-string
arrays are keyed by it — the notes and the hold markers — with validation keeping them disjoint and
each array holding one slot at most once. Stable across unrelated edits, unlike projection indices,
which shift whenever an earlier record is inserted or removed.

The slot says WHERE, never WHAT: \ref ChartSelectionKey wraps it in an alternative naming the kind,
so a selected object states which array holds it instead of the chart being re-consulted — which
also keeps each kind's keys in one sorted sequence, the shape every resolution here is a linear
merge over.
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
\brief Which of the chart's two SLOT-KEYED authored arrays holds an object at one slot.

Not "every kind that can be selected" — waypoints are selectable and are in neither array. This is
the narrower question \ref rock_hero::editor::core::EditorController::Impl::chartSlotObject asks:
validation keeps these two arrays disjoint over one slot space, so a slot belongs to a note, to a
hold marker, or to nobody, and no caller has to decide a precedence. A waypoint SHARES its note's
slot rather than excluding it, which is exactly the shape that would owe a precedence rule — so it
stays out of this enum, and its identity carries the offset that tells it apart instead.
*/
enum class ChartSlotOccupant : std::uint8_t
{
    /*! \brief A sounding onset in \ref rock_hero::common::core::Chart::notes. */
    Note,

    /*! \brief A silently-held stop in \ref rock_hero::common::core::Chart::hold_markers. */
    HoldMarker,
};

/*! \brief Stable identity of one selected note: the slot it occupies. */
struct ChartNoteKey
{
    /*! \brief The note's slot in the chart's note stream. */
    ChartSlotKey slot{};

    /*!
    \brief Compares two note keys for equal value.
    \param lhs Left-hand key.
    \param rhs Right-hand key.
    \return True when both keys name the same slot.
    */
    friend constexpr bool operator==(const ChartNoteKey& lhs, const ChartNoteKey& rhs) noexcept =
        default;
};

/*! \brief Stable identity of one selected hold marker: the slot it occupies. */
struct ChartHoldMarkerKey
{
    /*! \brief The marker's slot in the chart's hold-marker array. */
    ChartSlotKey slot{};

    /*!
    \brief Compares two marker keys for equal value.
    \param lhs Left-hand key.
    \param rhs Right-hand key.
    \return True when both keys name the same slot.
    */
    friend constexpr bool operator==(
        const ChartHoldMarkerKey& lhs, const ChartHoldMarkerKey& rhs) noexcept = default;
};

/*!
\brief Stable identity of one selected waypoint: the note it rides, and where along that ring.

The slot alone cannot name it — a note carries many waypoints — so the identity is (note slot,
offset), which is what makes waypoints the reason the selection key became a sum rather than a
slot plus a kind. The offset and not an index: removing an earlier waypoint shifts every later
index and moves no offset, so an index-keyed selection would silently point at a different
waypoint after any edit that dropped one.

A key whose note or waypoint an edit removed simply resolves to nothing, exactly like a note key
whose note was deleted — which is also what carries the dissolve law's LINGER, since a waypoint
the editor emptied is gone from the chart while its key rides on to the next press.
*/
struct ChartWaypointKey
{
    /*! \brief Slot of the note the waypoint rides. */
    ChartSlotKey note{};

    /*! \brief The waypoint's beat-fraction offset from that note's onset. */
    common::core::Fraction offset{};

    /*!
    \brief Orders two waypoint keys by (note slot, offset) — the order the chart stores them in.
    \param lhs Left-hand key.
    \param rhs Right-hand key.
    \return Ordering of lhs relative to rhs.
    */
    friend constexpr std::strong_ordering operator<=>(
        const ChartWaypointKey& lhs, const ChartWaypointKey& rhs) noexcept = default;

    /*!
    \brief Compares two waypoint keys for equal value.
    \param lhs Left-hand key.
    \param rhs Right-hand key.
    \return True when both keys name the same waypoint.
    */
    friend constexpr bool operator==(
        const ChartWaypointKey& lhs, const ChartWaypointKey& rhs) noexcept = default;
};

/*!
\brief Stable identity of one selectable chart object, as the sum of what a selectable can be.

A sum rather than a kind tag beside a slot, because the three identities are not the same shape: a
note and a hold marker are named by a slot, a waypoint by a slot plus an offset. Carrying that
offset as a field only one kind uses would make "a note key with an offset" and "a waypoint key
without one" both spellable, and every reader would owe a rule about what those mean; as a sum
neither exists to be misread.

The sum itself carries EQUALITY only, which is the whole of what a caller across kinds needs: the
coalescing window's proof is "this is still the selection the last press acted on", one whole-vector
compare. Ordering stays inside each alternative's own sequence, where the element being ordered is
the slot for two of the kinds and the key itself for the waypoint — which is why only
\ref ChartWaypointKey declares an ordering. \ref ChartSelection::keys publishes in alternative
order, notes then markers then waypoints, each kind in its own.
*/
using ChartSelectionKey = std::variant<ChartNoteKey, ChartHoldMarkerKey, ChartWaypointKey>;

/*!
\brief The slot an armed caret would sit on for one selected object, or absent when none can.

The caret addresses a (position, string) slot, and its invariant is that the selection is exactly
what sits under it. A note or a hold marker OCCUPIES a slot, so selecting one arms the caret there.
A waypoint does not — it rides a note's ring at an offset — so arming anything for it would put the
caret on the note while the selection holds the waypoint, which is the invariant broken rather than
kept. Selecting one therefore demotes the marker to a cursor in place, exactly as every
multi-select gesture does.

\param key Selected object.

\return The slot to arm, or absent when the object occupies none.
*/
[[nodiscard]] std::optional<ChartSlotKey> chartCaretSlotFor(const ChartSelectionKey& key);

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
\brief The one editor-wide chart selection, kept sorted per kind in the chart's own order.

One selection over more than one kind of object. Each kind's keys live in their own sorted-unique
sequence so key-to-index resolution against the chart stays a linear merge, and so a verb reads
its own kind's operand as a plain vector rather than filtering a mixed one — a verb a kind has no
meaning for simply reads that kind's empty operand instead of carrying a guard. Every mutation is
kind-agnostic: one sorted-unique primitive per verb, dispatched by \ref visitSequence, which is
also the ONE place an alternative maps onto the sequence that stores it.
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
    \brief The selected waypoints in ascending (note slot, offset) order.

    The order the chart stores them in, so a planner walking the note stream and this list together
    walks both forward once. Keys naming a waypoint an edit removed stay until the selection next
    changes and resolve to nothing meanwhile — the dissolve law's linger.

    \return Sorted unique selected waypoint keys.
    */
    [[nodiscard]] const std::vector<ChartWaypointKey>& waypoints() const noexcept;

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
    // The single place an alternative maps onto the sequence that stores it and the element that
    // sequence holds, so every mutation above is written once over whatever that pair is and a
    // caller never sees which member it landed in. The alternative decides both at COMPILE time,
    // which is what lets the waypoint sequence hold a different element type than the two
    // slot-keyed ones without a mutation branching on kind.
    //
    // A static template over the selection rather than a const/non-const pair, so the mapping is
    // stated exactly once and const-ness rides through deduction instead of a const_cast.
    template <typename Selection, typename Visit>
    [[nodiscard]] static decltype(auto) visitSequence(
        Selection& selection, const ChartSelectionKey& key, const Visit& visit)
    {
        return std::visit(
            [&selection, &visit](const auto& alternative) {
                using Alternative = std::remove_cvref_t<decltype(alternative)>;
                if constexpr (std::is_same_v<Alternative, ChartNoteKey>)
                {
                    return visit(selection.m_notes, alternative.slot);
                }
                else if constexpr (std::is_same_v<Alternative, ChartHoldMarkerKey>)
                {
                    return visit(selection.m_hold_markers, alternative.slot);
                }
                else
                {
                    return visit(selection.m_waypoints, alternative);
                }
            },
            key);
    }

    // Each sorted unique in its own key's order; every mutation preserves the invariant.
    std::vector<ChartSlotKey> m_notes{};
    std::vector<ChartSlotKey> m_hold_markers{};
    std::vector<ChartWaypointKey> m_waypoints{};
};

/*!
\brief Resolves sorted slot keys to indices in one of the chart's slot-sorted authored arrays.

THE key resolution, and it is the same merge for every authored array: the notes and the hold
markers are both sorted by (position, string) and the tab projection preserves each order one to
one, so the returned indices address the projection directly and one linear merge answers every
key. Keys that no longer match a record resolve to nothing and are skipped. Every other
key-to-record question (\ref selectedNoteIndices, \ref recordsForKeys) is this merge read a
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
\brief Resolves a selection's waypoint keys to the DRAWN waypoints they name.

Two steps, because a waypoint's identity and its drawn place are two different things: the note
slot resolves against the authored stream exactly as a note key does, and the offset then picks the
projected entry out of that note's drawn waypoints. The offset is what makes the second step
possible at all — it is carried into \ref common::core::SlideViewState precisely so a selection can
point at a drawn mark without counting indices that shift.

Keys resolving to nothing are skipped, which covers both a waypoint an edit removed and one the
presentation trim clipped out of the drawn tail.

\param notes Chart note stream sorted by (position, string).
\param drawn Notes as the lane draws them, in the chart's own order (one to one with `notes`).
\param selection Selection whose waypoint keys are resolved.
\return The located waypoints, in the selection's own (note slot, offset) order.
*/
[[nodiscard]] std::vector<ChartWaypointRef> selectedWaypointIndices(
    const std::vector<common::core::ChartNote>& notes,
    const std::vector<common::core::NoteViewState>& drawn, const ChartSelection& selection);

/*!
\brief Copies the authored records that sorted keys still name, in chart order.

The snapshot every fret verb replans from, and a template for the same reason \ref
slotIndicesForKeys is one: the notes and the hold markers are the same question asked of two
arrays, and a per-array copy of this loop would be a second authority on which record a key names.

\tparam Record Authored chart record type; \ref chartSlotKeyOf must name its slot.
\param records Authored array sorted by (position, string).
\param keys Keys to resolve, sorted-unique in chart order.
\return The named records, in stream order; keys naming nothing are skipped.
*/
template <typename Record>
[[nodiscard]] std::vector<Record> recordsForKeys(
    const std::vector<Record>& records, const std::span<const ChartSlotKey> keys)
{
    std::vector<Record> named;
    const std::vector<std::size_t> indices = slotIndicesForKeys(records, keys);
    named.reserve(indices.size());
    for (const std::size_t index : indices)
    {
        named.push_back(records[index]);
    }
    return named;
}

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
