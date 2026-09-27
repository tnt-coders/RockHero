/*!
\file edit_focus.h
\brief What an undo or redo transition leaves for the reader to see.
*/

#pragma once

#include "chart/chart_selection.h"
#include "controller/editor_selection.h"

#include <algorithm>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <string>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Chart objects a transition wrote back, and the object its change begins at.

The selection is exactly what the transition wrote back — the notes an undo restored, the notes a
redo re-inserted — narrowed to the keyframes that changed on a note rewritten in place with its
head untouched, since such a change lives along the ring rather than at the head. It may be empty,
when the transition only took objects away (undoing an insert, redoing a delete); the front is then
the first object it took, whose caret slot still shows where the change was.
*/
struct ChartEditFocus
{
    /*! \brief What the transition wrote back, in chart order. */
    std::vector<ChartSelectionKey> selected;

    /*! \brief First object of the change: the first key above, or the first object taken away. */
    ChartSelectionKey front{ChartNoteKey{}};
};

/*!
\brief A timeline marker a transition changed.

The marker the transition left holding the change, or none when it took one away (the marker is
then shown by its former start, where Delete would have left the cursor).
*/
struct MarkerEditFocus
{
    /*! \brief The marker to select, or nothing when the transition removed it. */
    std::variant<
        std::monostate, SongSectionSelection, FretHandPositionSelection, ToneRegionSelection>
        marker;

    /*! \brief Start of the changed marker, where the cursor goes to show it. */
    common::core::GridPosition start{};
};

/*!
\brief A tone-automation point a transition changed, on the lane that holds it.

The lane caret lands on the position and selects the point standing there, if the transition left
one.
*/
struct AutomationEditFocus
{
    /*! \brief Live plugin instance owning the lane's parameter. */
    std::string instance_id;

    /*! \brief Parameter id within that plugin. */
    std::string param_id;

    /*! \brief Position of the point the transition wrote or removed. */
    common::core::GridPosition position{};
};

/*!
\brief What an undo or redo transition brings into focus, per kind of edit; nothing for an edit
that is not on the timeline.
*/
using EditFocus =
    std::variant<std::monostate, ChartEditFocus, MarkerEditFocus, AutomationEditFocus>;

/*!
\brief One record a transition changed, and whether the side it landed on still holds it.
\tparam Record Marker or point record type.
*/
template <typename Record> struct ChangedRecord
{
    /*! \brief The changed record, as the side holding it stores it. */
    const Record* record;

    /*! \brief True when the landed side holds it (added or rewritten), false when removed. */
    bool present;
};

/*!
\brief The first record a transition changed between two whole-model snapshots of one kind.

A record the landed side holds and the replaced side does not is what the transition added or
rewrote, and wins; failing that, the first record the replaced side held that the landed side lost
is what it removed. A move is both, and reports the record at its new place. Linear searches rather
than a sorted merge: a model holds at most a few hundred small records and this runs once per
transition.

\param landed Records as the transition leaves them.
\param replaced Records as they stood before it.
\return The changed record, or nothing when both sides hold the same records.
*/
template <typename Record>
[[nodiscard]] std::optional<ChangedRecord<Record>> firstChangedRecord(
    const std::vector<Record>& landed, const std::vector<Record>& replaced)
{
    for (const Record& record : landed)
    {
        if (std::ranges::find(replaced, record) == replaced.end())
        {
            return ChangedRecord<Record>{.record = &record, .present = true};
        }
    }
    for (const Record& record : replaced)
    {
        if (std::ranges::find(landed, record) == landed.end())
        {
            return ChangedRecord<Record>{.record = &record, .present = false};
        }
    }
    return std::nullopt;
}

} // namespace rock_hero::editor::core
