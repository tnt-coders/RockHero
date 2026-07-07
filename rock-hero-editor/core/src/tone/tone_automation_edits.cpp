#include "tone/tone_automation_edits.h"

#include "tone/tone_automation_projection.h"

#include <algorithm>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/shared/logger.h>
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
    EditorEditContext& context, const std::string& tone_document_ref,
    const std::string& instance_id, const std::string& param_id,
    const std::vector<common::core::ToneAutomationPoint>& points)
{
    const common::core::TempoMap& tempo_map = context.session.song().tempo_map;
    std::vector<common::audio::AutomationCurvePoint> curve_points;
    curve_points.reserve(points.size());
    for (const common::core::ToneAutomationPoint& point : points)
    {
        curve_points.push_back(
            common::audio::AutomationCurvePoint{
                .seconds = secondsAtGridPosition(tempo_map, point.position),
                .norm_value = point.norm_value,
                .curve_shape = point.curve_shape,
            });
    }

    const auto written = context.tone_automation.writeParameterCurve(
        tone_document_ref, instance_id, param_id, curve_points);
    if (!written.has_value())
    {
        // Best-effort by design: the model is the truth and the next load rebuild reconciles the
        // cache, but a failed rewrite is still worth a trace.
        RH_LOG_WARNING(
            "editor.tone",
            "Could not rewrite derived automation curve tone={:?} param={:?} detail={:?}",
            tone_document_ref,
            param_id,
            written.error().message);
    }
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
