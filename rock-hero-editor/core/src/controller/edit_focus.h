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
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <string>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Where the charter stood in the chart on one side of an edit.

A chart edit records one as it begins and one as its verb leaves the chart, and a transition
restores the side it lands on, so undo puts the charter back exactly where the edit found them and
redo where the edit left them. Recorded rather than read off the edit's diff: a diff says what
changed, never where the charter stood, and the two part ways where one edit rewrites a note two
ways (cutting a ring shortens the note the caret was on a keyframe of).

The slot is the first selected object's, or, with nothing selected, the caret's own — the empty
slot a digit inserted at, or the one a delete emptied.
*/
struct ChartEditFocus
{
    /*! \brief The selection, in chart order; empty where the keyboard stood on an empty slot. */
    std::vector<ChartSelectionKey> selected;

    /*! \brief Slot the keyboard position stands on. */
    ChartSlotKey slot{};

    /*! \brief Face of the object there the caret stands on. */
    ChartCaretFace face{ChartCaretFace::Mark};
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
