#include "tone/tone_automation_edits.h"

#include <algorithm>
#include <rock_hero/common/audio/automation/tone_automation_rebuild.h>
#include <rock_hero/common/core/session/session.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

bool applyToneAutomationModel(
    common::core::Session& session, const std::string& plugin_id, const std::string& param_id,
    const std::vector<common::core::ToneAutomationPoint>& points)
{
    std::vector<common::core::ToneParameterAutomation>* const automation =
        session.currentToneAutomation();
    if (automation == nullptr)
    {
        return false;
    }

    const auto entry = std::ranges::find_if(
        *automation, [&](const common::core::ToneParameterAutomation& candidate) {
            return candidate.plugin_id == plugin_id && candidate.param_id == param_id;
        });
    if (points.empty())
    {
        if (entry != automation->end())
        {
            automation->erase(entry);
        }
        return true;
    }

    if (entry != automation->end())
    {
        entry->points = points;
        return true;
    }
    automation->push_back(
        common::core::ToneParameterAutomation{
            .plugin_id = plugin_id,
            .param_id = param_id,
            .points = points,
        });
    return true;
}

void rewriteDerivedToneCurve(
    const EditorEditContext& context, const std::string& tone_document_ref,
    const std::string& instance_id, const std::string& param_id,
    const std::vector<common::core::ToneAutomationPoint>& points)
{
    // Thin context adapter over the shared derive-and-write (the same conversion the game's
    // post-load rebuild uses, so both products bake identical curves).
    common::audio::writeDerivedToneCurve(
        context.tone_automation,
        context.session.song().tempo_map,
        tone_document_ref,
        instance_id,
        param_id,
        points);
}

std::expected<void, EditorUndoFailureCode> ToneAutomationPointsEdit::undo(
    EditorEditContext& context) const
{
    return applyPoints(context, before);
}

std::expected<void, EditorUndoFailureCode> ToneAutomationPointsEdit::redo(
    EditorEditContext& context) const
{
    return applyPoints(context, after);
}

std::string ToneAutomationPointsEdit::label() const
{
    return "Automate " + (param_name.empty() ? std::string{"Parameter"} : param_name);
}

std::expected<void, EditorUndoFailureCode> ToneAutomationPointsEdit::applyPoints(
    EditorEditContext& context, const std::vector<common::core::ToneAutomationPoint>& points) const
{
    if (!applyToneAutomationModel(context.session, plugin_id, param_id, points))
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }
    rewriteDerivedToneCurve(context, tone_document_ref, instance_id, param_id, points);
    return std::expected<void, EditorUndoFailureCode>{};
}

} // namespace rock_hero::editor::core
