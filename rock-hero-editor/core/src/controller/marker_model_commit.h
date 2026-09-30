/*!
\file marker_model_commit.h
\brief Definition of the one commit funnel every timeline-marker verb ends in.

Separate from editor_controller_impl.h because that header is a declaration surface only, and a
member template needs its body where every marker handler translation unit can see it.
*/

#pragma once

#include "controller/editor_controller_impl.h"
#include "controller/marker_model_edit.h"

#include <expected>
#include <memory>
#include <optional>
#include <rock_hero/common/core/shared/logger.h>
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
// what it just made publishes that selection itself, afterwards. Returns false only for a refusal.
template <typename Snapshot>
bool EditorController::Impl::commitMarkerModel(Snapshot before, Snapshot after, std::string label)
{
    std::expected<Snapshot, ChartPlanRefusal> judged = judgeMarkerModel(before, std::move(after));
    if (!judged.has_value())
    {
        if (const auto* const invalid = std::get_if<ChartPlanInvalid>(&judged.error()))
        {
            logMarkerRefusal(label, invalid->reason);
            return false;
        }
        return true;
    }

    if (!judged->applyTo(m_session))
    {
        logMarkerRefusal(label, "no model to apply onto");
        return false;
    }

    pushUndoEntry(
        std::make_unique<MarkerModelEdit<Snapshot>>(
            std::move(before), std::move(*judged), std::move(label)));
    releaseMarkerSelectionNamingNothing();
    syncAudibleTone();
    updateView();
    return true;
}

} // namespace rock_hero::editor::core
