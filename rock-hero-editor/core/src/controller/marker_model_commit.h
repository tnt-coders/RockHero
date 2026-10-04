/*!
\file marker_model_commit.h
\brief Definition of the one commit funnel every timeline-marker verb ends in.

Separate from editor_controller_impl.h because that header is a declaration surface only, and a
member template needs its body where every marker handler translation unit can see it.
*/

#pragma once

#include "controller/editor_controller_impl.h"
#include "controller/marker_model_edit.h"
#include "timeline/timeline_readdress.h"

#include <expected>
#include <memory>
#include <optional>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace rock_hero::editor::core
{

// Logs a refused marker edit: THE refusal record every marker verb leaves, whether the funnel below
// refused the produced model or a verb refused before it had a model to hand over, so the format
// lives once.
inline void logMarkerRefusal(const std::string_view label, const std::string_view detail)
{
    RH_LOG_WARNING("editor.marker", "Rejected marker edit label={:?} detail={:?}", label, detail);
}

// The funnel's front half: the produced model normalized, then validated, then compared with the
// one it would replace. Stated once so the commit and a pending entry's red box cannot disagree
// about what the commit would refuse.
template <typename Snapshot>
std::expected<Snapshot, ChartPlanRefusal> EditorController::Impl::judgeMarkerModel(
    const Snapshot& before, Snapshot after) const
{
    after.normalize();
    if (std::optional<std::string> violation = after.validate(session()); violation.has_value())
    {
        return std::unexpected{ChartPlanInvalid{std::move(*violation)}};
    }
    if (after == before)
    {
        return std::unexpected{ChartPlanNoChange{}};
    }
    return after;
}

// Commits one marker-model change as one undo entry and republishes. Every marker verb ends here,
// whatever kind of marker it authored, so the commit rules live here once: the produced model is
// normalized, then validated, and a model that breaks a rule is REFUSED whole — verbs build their
// result on a copy, so a refusal leaves the live model untouched by construction and needs no
// restore. A verb that changed nothing records nothing.
//
// THE PUBLISH RULE, for every marker verb: publish only when the model, the selection or the cursor
// actually changed. A refusal of any kind changes none of the three, and neither does a commit that
// matched the model it found, so both leave the view exactly as they found it and push nothing. The
// one publish a landed commit makes therefore always carries a real change, and a verb that selects
// what it just made publishes that selection itself, afterwards.
template <typename Snapshot>
EditorController::Impl::MarkerCommit EditorController::Impl::commitMarkerModel(
    Snapshot before, Snapshot after, std::string label)
{
    std::expected<Snapshot, ChartPlanRefusal> judged = judgeMarkerModel(before, std::move(after));
    if (!judged.has_value())
    {
        if (const auto* const invalid = std::get_if<ChartPlanInvalid>(&judged.error()))
        {
            logMarkerRefusal(label, invalid->reason);
            return MarkerCommit::Refused;
        }
        return MarkerCommit::Unchanged;
    }

    if (!judged->applyTo(m_session))
    {
        logMarkerRefusal(label, "no model to apply onto");
        return MarkerCommit::Refused;
    }

    pushUndoEntry(
        std::make_unique<MarkerModelEdit<Snapshot>>(
            std::move(before), std::move(*judged), std::move(label)));
    finishMarkerCommit();
    return MarkerCommit::Landed;
}

// The marker move verbs' commit: one held run of Alt+arrow presses on one selected marker is ONE
// undo entry, describing the model before the run's first press → the model now (the run's law is
// stated at m_marker_move_origin). A press that starts a run is an ordinary funnel commit, which
// arms the run when it recorded an entry; a press that continues one is judged against the run's
// ORIGIN by the funnel's own front half and replaces the run's entry — or, where it walks the run
// back to its origin, drops it, so a run that went nowhere leaves no Ctrl+Z that changes nothing.
// Either way the moved marker ends selected, made here rather than by the verb, because a
// selection change ends the run and only this commit may survive its own. A refused press leaves
// the model, the entry and the run exactly as they stood.
template <typename Snapshot>
bool EditorController::Impl::commitMarkerMove(
    Snapshot before, Snapshot after, std::string label, const MarkerSelection& landed)
{
    // Held aside, so this press's own landing select below does not end the run it continues.
    const bool continues = markerMoveRunOrigin<Snapshot>() != nullptr;
    std::optional<MarkerModelSnapshot> held = std::exchange(m_marker_move_origin, std::nullopt);
    const Snapshot* const origin = continues ? std::get_if<Snapshot>(&*held) : nullptr;

    if (origin == nullptr)
    {
        const MarkerCommit committed =
            commitMarkerModel(before, std::move(after), std::move(label));
        if (committed == MarkerCommit::Refused)
        {
            return false;
        }
        selectMarker(landed);
        if (committed == MarkerCommit::Landed)
        {
            m_marker_move_origin = std::move(before);
        }
        return true;
    }

    std::expected<Snapshot, ChartPlanRefusal> judged = judgeMarkerModel(*origin, std::move(after));
    if (!judged.has_value())
    {
        if (const auto* const invalid = std::get_if<ChartPlanInvalid>(&judged.error()))
        {
            logMarkerRefusal(label, invalid->reason);
            m_marker_move_origin = std::move(held);
            return false;
        }
    }
    // A press that walks the run back to its origin lands the origin itself.
    const bool walked_back = !judged.has_value();
    const Snapshot* const landing = judged.has_value() ? &*judged : origin;
    if (!landing->applyTo(m_session))
    {
        logMarkerRefusal(label, "no model to apply onto");
        m_marker_move_origin = std::move(held);
        return false;
    }

    if (judged.has_value())
    {
        static_cast<void>(replaceUndoTop(
            std::make_unique<MarkerModelEdit<Snapshot>>(
                *origin, std::move(*judged), std::move(label))));
    }
    else
    {
        static_cast<void>(dropUndoTop());
    }
    finishMarkerCommit();
    selectMarker(landed);
    if (!walked_back)
    {
        m_marker_move_origin = std::move(held);
    }
    return true;
}

// THE ORDER RULE of a grid commit, for every model that addresses the rest (a tempo map, a meter
// list): the cursor's place is read at the tick BEFORE the commit; the landed marker is selected
// next, because selecting dissolves an armed caret by seeking to its OLD token, which under the new
// grid is other music; and the cursor moves to its place carried onto the new grid last, so that
// move is the last word. The cursor is carried as one more slice, down the path the content took
// (readdressedPosition with the verb's content map), so it lands on what it stood on: a kept place
// where the grid is truth or a meter edit holds time, a scaled one under Scale Tempo. The landed
// marker is selected only where it still stands, since normalization may have dropped it, and a
// landed commit leaves its marker selected or nothing, the caret dissolving as a select dissolves
// it: where every address moved, the old selection would name other music. The publish follows the
// funnel's publish rule: only when the model landed or a marker was selected.
template <typename Snapshot>
void EditorController::Impl::commitGridModel(
    Snapshot before, Snapshot after, std::string label, const MarkerRow landed_row,
    const std::optional<common::core::GridPosition> landed, const ContentMap& content)
{
    const std::optional<common::core::GridPosition> place =
        cursorPosition(common::core::g_tick_quantum_note_value);
    const common::core::TempoMap old_map = session().song().tempo_map;
    const MarkerCommit committed =
        commitMarkerModel(std::move(before), std::move(after), std::move(label));
    if (committed == MarkerCommit::Refused)
    {
        return;
    }
    const bool selected = landed.has_value() && selectMarkerStartingAt(landed_row, *landed);
    if (committed == MarkerCommit::Landed && !selected)
    {
        dissolveChartCaretInPlace();
        clearSelection();
    }
    if (committed == MarkerCommit::Landed && place.has_value())
    {
        moveCursorTo(readdressedPosition(old_map, session().song().tempo_map, content, *place));
    }
    if (selected || committed == MarkerCommit::Landed)
    {
        updateView();
    }
}

// A run continues only while its origin is held, the history's top is still the run's own entry,
// and the origin is a model of this kind.
template <typename Snapshot> const Snapshot* EditorController::Impl::markerMoveRunOrigin() const
{
    if (!m_marker_move_origin.has_value() || !m_undo_history.canCoalesceTop())
    {
        return nullptr;
    }
    return std::get_if<Snapshot>(&*m_marker_move_origin);
}

template <typename Snapshot>
const Snapshot& EditorController::Impl::runBase(const Snapshot& before) const
{
    const Snapshot* const origin = markerMoveRunOrigin<Snapshot>();
    return origin != nullptr ? *origin : before;
}

} // namespace rock_hero::editor::core
