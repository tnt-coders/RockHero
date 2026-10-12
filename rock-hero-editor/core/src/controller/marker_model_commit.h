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

#include <expected>
#include <memory>
#include <optional>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <string>
#include <utility>
#include <variant>

namespace rock_hero::editor::core
{

// The marker models that state the grid itself and so address the rest — the ones commitGridModel
// lands, each stating how the keyboard crosses its landing (GridCarry). Every other model is
// content, which the Note Lock freezes.
template <typename Snapshot>
inline constexpr bool g_states_grid = requires { Snapshot::g_grid_carry; };

// THE WRITTEN FORM of a marker model: what the history and the file hold. Every model is its own,
// except the song timeline, which sheds a time signature restating its reign — that change is
// authoring state the editor holds only while its chip is selected (the keyframe commit law's
// meter row, dissolveRedundantMeters), so no entry ever holds it, and a verb that only planted or
// moved one writes as nothing.
template <typename Snapshot> [[nodiscard]] Snapshot writtenModel(Snapshot model)
{
    return model;
}

[[nodiscard]] inline SongTimelineSnapshot writtenModel(SongTimelineSnapshot model)
{
    model.timeline.tempo_map =
        common::core::withoutRedundantTimeSignatures(std::move(model.timeline.tempo_map));
    return model;
}

// The entry a landed model records, origin → now in the written form, or null where the two
// written forms agree: the model changed only in authoring state, which the history never holds.
template <typename Snapshot>
[[nodiscard]] std::unique_ptr<MarkerModelEdit<Snapshot>> writtenMarkerEdit(
    Snapshot before, Snapshot after, std::string label, const EntryFocus focus)
{
    Snapshot written_before = writtenModel(std::move(before));
    Snapshot written_after = writtenModel(std::move(after));
    if (written_after == written_before)
    {
        return nullptr;
    }
    return std::make_unique<MarkerModelEdit<Snapshot>>(
        std::move(written_before), std::move(written_after), std::move(label), focus);
}

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

// Lands one marker-model change, publishing nothing. Every marker verb lands here, whatever kind
// of marker it authored, so the commit rules live here once: the produced model is normalized,
// then validated, and a model that breaks a rule is REFUSED whole — verbs build their result on a
// copy, so a refusal leaves the live model untouched by construction and needs no restore. A verb
// that changed nothing records nothing, and so does one whose change writes as nothing
// (writtenMarkerEdit): the live model moved, the history did not.
template <typename Snapshot>
EditorController::Impl::MarkerCommit EditorController::Impl::landMarkerModel(
    Snapshot before, Snapshot after, std::string label, const EntryFocus focus)
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

    std::unique_ptr<MarkerModelEdit<Snapshot>> entry =
        writtenMarkerEdit(std::move(before), std::move(*judged), std::move(label), focus);
    if (entry == nullptr)
    {
        return MarkerCommit::Silent;
    }
    pushUndoEntry(std::move(entry));
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
    const MarkerCommit committed = landMarkerModel(
        std::move(before), std::move(after), std::move(label), EntryFocus::FirstChange);
    if (committed == MarkerCommit::Landed || committed == MarkerCommit::Silent)
    {
        finishMarkerCommit();
    }
    return committed;
}

// The marker move verbs' commit: one held run of Alt+arrow presses on one selected marker is ONE
// undo entry, describing the model before the run's first press → the model now (the run's law is
// stated at m_marker_move_origin). A press that starts a run is an ordinary funnel landing, which
// arms the run when it recorded an entry; a press that continues one is judged against the run's
// ORIGIN by the funnel's own front half and replaces the run's entry — or, where the entry would
// describe nothing, drops it and ends the run, since a run owns the history's top or nothing: the
// press walked the run back to its origin, or walked the marker onto authoring state the history
// does not hold (a meter restating its reign). So a run that went nowhere leaves no Ctrl+Z that
// changes nothing. Either way the moved marker ends selected, made here rather than by the verb,
// because a selection change ends the run and only this commit may survive its own — and selected
// BEFORE the landing publishes, because the landing select is a settle point and the moved marker
// must be the one in focus when it settles. A refused press leaves the model, the entry and the run
// exactly as they stood.
template <typename Snapshot>
bool EditorController::Impl::commitMarkerMove(
    Snapshot before, Snapshot after, std::string label, const MarkerSelection& landed)
{
    // Held aside, so this press's own landing select below does not end the run it continues.
    const bool continues = markerMoveRunOrigin<Snapshot>() != nullptr;
    std::optional<MarkerModelSnapshot> held = std::exchange(m_marker_move_origin, std::nullopt);
    const Snapshot* const origin =
        continues && held.has_value() ? std::get_if<Snapshot>(&*held) : nullptr;

    if (origin == nullptr)
    {
        const MarkerCommit committed =
            landMarkerModel(before, std::move(after), std::move(label), EntryFocus::FirstChange);
        if (committed == MarkerCommit::Refused)
        {
            return false;
        }
        selectMarker(landed);
        if (committed != MarkerCommit::Unchanged)
        {
            finishMarkerCommit();
        }
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
    const Snapshot* const landing = judged.has_value() ? &*judged : origin;
    if (!landing->applyTo(m_session))
    {
        m_refusal_log.markerEdit(label, "no model to apply onto");
        m_marker_move_origin = std::move(held);
        return false;
    }

    std::unique_ptr<MarkerModelEdit<Snapshot>> entry =
        judged.has_value()
            ? writtenMarkerEdit(
                  *origin, std::move(*judged), std::move(label), EntryFocus::FirstChange)
            : nullptr;
    const bool recorded = entry != nullptr;
    if (recorded)
    {
        static_cast<void>(replaceUndoTop(std::move(entry)));
    }
    else
    {
        static_cast<void>(dropUndoTop());
    }
    selectMarker(landed);
    finishMarkerCommit();
    if (recorded)
    {
        m_marker_move_origin = std::move(held);
    }
    return true;
}

// THE ORDER RULE of a grid commit, for every model that addresses the rest (a tempo map, a meter
// list): the cursor's place is read at the tick BEFORE the commit, and once the model has landed
// the cursor is carried onto the new grid FIRST, as the model's kind states
// (carryKeyboardAcrossGrid), so it stands on what it stood on. Carrying first is what demotes an
// armed caret onto its carried place: a select dissolves a caret by seeking to its address, and its
// OLD address under the new grid is other music. The landed marker is selected next, where the
// verb landed one, and otherwise nothing is: where every address moved, the old selection would
// name other music. That select is a settle point, so a meter restating its reign lands in focus
// and stays. Everything settles before the commit's one publish, which follows the funnel's publish
// rule: only when the model moved or a marker was selected. `focus` is what the entry's undo and
// redo bring into view (EntryFocus); their carry is the same one (applyPendingEdit).
template <typename Snapshot>
EditorController::Impl::MarkerCommit EditorController::Impl::commitGridModel(
    Snapshot before, Snapshot after, std::string label, const MarkerRow landed_row,
    const std::optional<common::core::GridPosition> landed, const EntryFocus focus)
{
    static_assert(g_states_grid<Snapshot>, "a grid commit lands a model that states the grid");
    const std::optional<common::core::GridPosition> place =
        cursorPosition(common::core::g_tick_quantum_note_value);
    const common::core::TempoMap old_map = session().song().tempo_map;
    const MarkerCommit committed =
        landMarkerModel(std::move(before), std::move(after), std::move(label), focus);
    if (committed == MarkerCommit::Refused)
    {
        return committed;
    }
    const bool moved = committed != MarkerCommit::Unchanged;
    if (moved)
    {
        carryKeyboardAcrossGrid(old_map, place, Snapshot::g_grid_carry);
    }
    const bool selected = landed.has_value() && selectMarkerStartingAt(landed_row, *landed);
    if (moved && !selected)
    {
        clearSelection();
    }
    if (selected || moved)
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
