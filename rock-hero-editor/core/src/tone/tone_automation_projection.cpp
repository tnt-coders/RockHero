#include "tone/tone_automation_projection.h"

#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <utility>

namespace rock_hero::editor::core
{

ToneAutomationViewState toneAutomationViewStateFor(
    const common::audio::IToneAutomation& tone_automation,
    const std::string& selected_tone_document_ref)
{
    ToneAutomationViewState state;
    state.tone_document_ref = selected_tone_document_ref;
    if (selected_tone_document_ref.empty())
    {
        return state;
    }

    const auto parameters = tone_automation.listAutomatableParameters(selected_tone_document_ref);
    if (!parameters.has_value())
    {
        // The tone is not loaded (or no longer loaded); leave the reference with no lanes.
        return state;
    }

    for (const common::audio::AutomatableParamInfo& parameter : *parameters)
    {
        const auto curve = tone_automation.readParameterCurve(
            selected_tone_document_ref, parameter.instance_id, parameter.param_id);
        if (!curve.has_value() || curve->empty())
        {
            // A parameter with no authored curve has no lane until the user opens one.
            continue;
        }

        ToneAutomationLaneViewState lane;
        lane.instance_id = parameter.instance_id;
        lane.param_id = parameter.param_id;
        lane.name = parameter.name;
        lane.is_discrete = parameter.is_discrete;
        lane.resolved = true;
        lane.points.reserve(curve->size());
        for (const common::audio::AutomationCurvePoint& point : *curve)
        {
            lane.points.push_back(
                ToneAutomationPointViewState{
                    .seconds = point.seconds,
                    .norm_value = point.norm_value,
                    .curve_shape = point.curve_shape,
                });
        }
        state.lanes.push_back(std::move(lane));
    }
    return state;
}

} // namespace rock_hero::editor::core
