/*!
\file marker_model_commit.h
\brief Definition of the one commit funnel every timeline-marker verb ends in.

Separate from editor_controller_impl.h because that header is a declaration surface only, and a
member template needs its body where every marker handler translation unit can see it.
*/

#pragma once

#include "controller/editor_controller_impl.h"
#include "controller/marker_judge_context.h"
#include "controller/marker_model_edit.h"
#include "timeline/timeline_readdress.h"

#include <expected>
#include <memory>
#include <optional>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace rock_hero::editor::core
{

// The marker models that state the grid itself and so address the rest — the two commitGridModel
// lands. Every other model is content, which the Note Lock freezes.
template <typename Snapshot>
inline constexpr bool g_states_grid =
    std::is_same_v<Snapshot, TempoMapSnapshot> || std::is_same_v<Snapshot, SongTimelineSnapshot>;

// The funnel's front half: the produced model normalized, then validated, then compared with the
// one it would replace. Stated once so the commit and a pending entry's red box cannot disagree
// about what the commit would refuse. The Note Lock's refusal comes first, since it refuses the
// edit whatever model it produced.
template <typename Snapshot>
std::expected<Snapshot, ChartPlanRefusal> EditorController::Impl::judgeMarkerModel(
    const Snapshot& before, Snapshot after) const
{
    if constexpr (!g_states_grid<Snapshot>)
    {
        if (std::optional<ChartPlanInvalid> refusal = noteLockRefusal(); refusal.has_value())
        {
            return std::unexpected{std::move(*refusal)};
        }
    }
    after.normalize();
    if (std::optional<std::string> violation = after.validate(
            MarkerJudgeContext{.session = session(), .notes_layer = chartResolutions()});
        violation.has_value())
    {
        return std::unexpected{ChartPlanInvalid{std::move(*violation)}};
    }
    if (after == before)
    {
        return std::unexpected{ChartPlanNoChange{}};
    }
    return after;
}

// Lands one marker-model change as one undo entry, publishing nothing. Every marker verb lands
// here, whatever kind of marker it authored, so the commit rules live here once: the produced model
// is normalized, then validated, and a model that breaks a rule is REFUSED whole — verbs build
// their result on a copy, so a refusal leaves the live model untouched by construction and needs
// no restore. A verb that changed nothing records nothing.
template <typename Snapshot>
EditorController::Impl::MarkerCommit EditorController::Impl::landMarkerModel(
    Snapshot before, Snapshot after, std::string label)
{
    std::expected<Snapshot, ChartPlanRefusal> judged = judgeMarkerModel(before, std::move(after));
    if (!judged.has_value())
    {
        if (const auto* const invalid = std::get_if<ChartPlanInvalid>(&judged.error()))
        {
            m_refusal_log.markerEdit(label, invalid->reason);
            return MarkerCommit::Refused;
        }
        return MarkerCommit::Unchanged;
    }

    if (!judged->applyTo(m_session))
    {
        m_refusal_log.markerEdit(label, "no model to apply onto");
        return MarkerCommit::Refused;
    }

    pushUndoEntry(
        std::make_unique<MarkerModelEdit<Snapshot>>(
            std::move(before), std::move(*judged), std::move(label)));
    return MarkerCommit::Landed;
}

// Lands one marker-model change and republishes.
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
    const MarkerCommit committed =
        landMarkerModel(std::move(before), std::move(after), std::move(label));
    if (committed == MarkerCommit::Landed)
    {
        finishMarkerCommit();
    }
    return committed;
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
            m_refusal_log.markerEdit(label, invalid->reason);
            m_marker_move_origin = std::move(held);
            return false;
        }
    }
    // A press that walks the run back to its origin lands the origin itself.
    const bool walked_back = !judged.has_value();
    const Snapshot* const landing = judged.has_value() ? &*judged : origin;
    if (!landing->applyTo(m_session))
    {
        m_refusal_log.markerEdit(label, "no model to apply onto");
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
// list): the cursor's place is read at the tick BEFORE the commit, and once the model has landed
// the cursor is carried onto the new grid FIRST, as one more slice down the path the content took
// (readdressedPosition with the verb's content map), so it stands on what it stood on: a kept place
// where the grid is truth or a meter edit holds time, a time-kept one under the Note Lock, a scaled
// one under Scale Tempo. Carrying first is what demotes an armed caret onto its carried place:
// a select dissolves a caret by seeking to its address, and its OLD address under the new grid is
// other music. The landed marker is selected next, only where it still stands, since normalization
// may have dropped it, and otherwise nothing is: where every address moved, the old selection would
// name other music. The carry is no keyboard move, so nothing the commit does to where the keyboard
// stands asks the window to follow (m_published_keyboard_seconds). Everything settles before the
// commit's one publish, which follows the funnel's publish rule: only when the model landed or a
// marker was selected.
template <typename Snapshot>
EditorController::Impl::MarkerCommit EditorController::Impl::commitGridModel(
    Snapshot before, Snapshot after, std::string label, const MarkerRow landed_row,
    const std::optional<common::core::GridPosition> landed, const ContentMap& content)
{
    static_assert(g_states_grid<Snapshot>, "a grid commit lands a model that states the grid");
    const std::optional<common::core::GridPosition> place =
        cursorPosition(common::core::g_tick_quantum_note_value);
    const common::core::TempoMap old_map = session().song().tempo_map;
    const MarkerCommit committed =
        landMarkerModel(std::move(before), std::move(after), std::move(label));
    if (committed == MarkerCommit::Refused)
    {
        return committed;
    }
    if (committed == MarkerCommit::Landed)
    {
        // Absent only while the transport plays, when no caret can be armed.
        if (place.has_value())
        {
            carryCursorTo(
                readdressedPosition(old_map, session().song().tempo_map, content, *place));
        }
        m_published_keyboard_seconds.reset();
    }
    const bool selected = landed.has_value() && selectMarkerStartingAt(landed_row, *landed);
    if (committed == MarkerCommit::Landed && !selected)
    {
        clearSelection();
    }
    if (selected || committed == MarkerCommit::Landed)
    {
        finishMarkerCommit();
    }
    return committed;
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

} // namespace rock_hero::editor::core
