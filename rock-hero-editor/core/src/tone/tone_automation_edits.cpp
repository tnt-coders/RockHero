#include "tone/tone_automation_edits.h"

#include <string>

namespace rock_hero::editor::core
{

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
    EditorEditContext& context,
    const std::vector<common::audio::AutomationCurvePoint>& points) const
{
    const auto written = context.tone_automation.writeParameterCurve(
        tone_document_ref, instance_id, param_id, points);
    if (!written.has_value())
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }
    return std::expected<void, EditorUndoFailureCode>{};
}

} // namespace rock_hero::editor::core
