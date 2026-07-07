#include "tone/tone_automation_projection.h"

#include <algorithm>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

double secondsAtGridPosition(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& position)
{
    const double global_beat =
        static_cast<double>(tempo_map.globalBeatIndex(position.measure, position.beat)) +
        position.offset.toDouble();
    return tempo_map.secondsAtGlobalBeatPosition(global_beat);
}

ToneAutomationViewState toneAutomationViewStateFor(
    const common::core::Arrangement& arrangement, const common::core::TempoMap& tempo_map,
    const std::string& selected_tone_document_ref,
    const std::unordered_map<std::string, ToneAutomationBinding>& bindings,
    const common::audio::IToneAutomation& tone_automation)
{
    ToneAutomationViewState state;
    state.tone_document_ref = selected_tone_document_ref;
    if (selected_tone_document_ref.empty())
    {
        return state;
    }

    // Parameter metadata is best-effort: a tone that is not loaded simply yields unresolved lanes,
    // but the failure is surfaced so the picker can say why it has nothing to offer.
    std::vector<common::audio::AutomatableParamInfo> parameters;
    if (auto listed = tone_automation.listAutomatableParameters(selected_tone_document_ref);
        listed.has_value())
    {
        parameters = std::move(*listed);
    }
    else
    {
        state.parameters_unavailable = true;
    }

    for (const common::core::ToneParameterAutomation& entry : arrangement.tone_automation)
    {
        const auto binding = bindings.find(entry.plugin_id);
        if (binding == bindings.end() ||
            binding->second.tone_document_ref != selected_tone_document_ref)
        {
            // Entries for other tones render under their own tone; entries with no runtime
            // binding at all stay persisted but have no lane to live in yet.
            continue;
        }

        ToneAutomationLaneViewState lane;
        lane.instance_id = binding->second.instance_id;
        lane.param_id = entry.param_id;
        lane.name = entry.param_id;
        lane.resolved = false;
        for (const common::audio::AutomatableParamInfo& parameter : parameters)
        {
            if (parameter.instance_id == lane.instance_id && parameter.param_id == entry.param_id)
            {
                lane.name = parameter.name;
                lane.is_discrete = parameter.is_discrete;
                lane.resolved = true;
                break;
            }
        }

        lane.points.reserve(entry.points.size());
        for (const common::core::ToneAutomationPoint& point : entry.points)
        {
            lane.points.push_back(
                ToneAutomationPointViewState{
                    .position = point.position,
                    .seconds = secondsAtGridPosition(tempo_map, point.position),
                    .norm_value = point.norm_value,
                    .curve_shape = point.curve_shape,
                });
        }
        state.lanes.push_back(std::move(lane));
    }

    // The "+" picker offers every listed parameter that has no lane yet; picking one seeds it.
    for (const common::audio::AutomatableParamInfo& parameter : parameters)
    {
        const bool already_laned =
            std::ranges::any_of(state.lanes, [&parameter](const ToneAutomationLaneViewState& lane) {
                return lane.instance_id == parameter.instance_id &&
                       lane.param_id == parameter.param_id;
            });
        if (already_laned)
        {
            continue;
        }
        state.available_parameters.push_back(
            ToneAutomationParamChoice{
                .instance_id = parameter.instance_id,
                .param_id = parameter.param_id,
                .name = parameter.name,
                .group = parameter.group,
            });
    }
    return state;
}

} // namespace rock_hero::editor::core
