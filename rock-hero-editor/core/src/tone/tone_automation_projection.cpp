#include "tone/tone_automation_projection.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

double secondsAtGridPosition(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& position)
{
    return tempo_map.secondsAtNote(position.measure, position.beat, position.offset);
}

float toneAutomationCurveValueAt(
    const std::vector<common::core::ToneAutomationPoint>& points,
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& position,
    bool is_discrete, float fallback_value)
{
    if (points.empty())
    {
        return fallback_value;
    }
    const double seconds = secondsAtGridPosition(tempo_map, position);
    if (seconds <= secondsAtGridPosition(tempo_map, points.front().position))
    {
        return points.front().norm_value;
    }
    if (seconds >= secondsAtGridPosition(tempo_map, points.back().position))
    {
        return points.back().norm_value;
    }
    for (std::size_t index = 1; index < points.size(); ++index)
    {
        const double next_seconds = secondsAtGridPosition(tempo_map, points[index].position);
        if (seconds > next_seconds)
        {
            continue;
        }
        const common::core::ToneAutomationPoint& previous = points[index - 1];
        if (is_discrete)
        {
            // The drawn discrete curve holds the previous state until the next point.
            return previous.norm_value;
        }
        const double previous_seconds = secondsAtGridPosition(tempo_map, previous.position);
        const double span = next_seconds - previous_seconds;
        const float mix =
            span > 0.0 ? static_cast<float>((seconds - previous_seconds) / span) : 1.0F;
        return std::lerp(previous.norm_value, points[index].norm_value, mix);
    }
    return points.back().norm_value;
}

ToneAutomationViewState makeToneAutomationViewState(
    const common::core::Arrangement& arrangement, const common::core::TempoMap& tempo_map,
    const std::string& selected_tone_document_ref,
    const std::unordered_map<std::string, common::audio::ToneAutomationBinding>& bindings,
    const std::vector<OpenAutomationLane>& open_lanes,
    const common::audio::IToneAutomation& tone_automation,
    const AutomationPointSelection* selected_point)
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
    const auto parameter_for =
        [&parameters](
            const std::string& instance_id,
            const std::string& param_id) -> const common::audio::AutomatableParamInfo* {
        for (const common::audio::AutomatableParamInfo& parameter : parameters)
        {
            if (parameter.instance_id == instance_id && parameter.param_id == param_id)
            {
                return &parameter;
            }
        }
        return nullptr;
    };

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
        if (const common::audio::AutomatableParamInfo* const parameter =
                parameter_for(lane.instance_id, entry.param_id);
            parameter != nullptr)
        {
            lane.name = parameter->name;
            lane.plugin_name = parameter->plugin_name;
            lane.is_discrete = parameter->is_discrete;
            lane.discrete_value_count = parameter->discrete_value_count;
            lane.live_norm_value = parameter->current_norm_value;
            lane.default_norm_value = parameter->default_norm_value;
            lane.resolved = true;
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

    // Open lanes without authored points follow: they track the parameter's live value until the
    // first point is authored, at which point the model lane above subsumes them. Open lanes are
    // keyed by durable plugin id and resolve to the live instance through the bindings, so they
    // survive rig reloads (which recreate every plugin instance).
    for (const OpenAutomationLane& open_lane : open_lanes)
    {
        if (open_lane.tone_document_ref != selected_tone_document_ref)
        {
            continue;
        }
        const auto binding = bindings.find(open_lane.plugin_id);
        const std::string instance_id =
            binding != bindings.end() ? binding->second.instance_id : std::string{};
        const bool already_laned = std::ranges::any_of(
            state.lanes, [&instance_id, &open_lane](const ToneAutomationLaneViewState& lane) {
                return lane.instance_id == instance_id && lane.param_id == open_lane.param_id;
            });
        if (already_laned)
        {
            continue;
        }

        ToneAutomationLaneViewState lane;
        lane.instance_id = instance_id;
        lane.param_id = open_lane.param_id;
        lane.name = open_lane.param_id;
        lane.resolved = false;
        if (const common::audio::AutomatableParamInfo* const parameter =
                parameter_for(instance_id, open_lane.param_id);
            parameter != nullptr)
        {
            lane.name = parameter->name;
            lane.plugin_name = parameter->plugin_name;
            lane.is_discrete = parameter->is_discrete;
            lane.discrete_value_count = parameter->discrete_value_count;
            lane.live_norm_value = parameter->current_norm_value;
            lane.default_norm_value = parameter->default_norm_value;
            lane.resolved = true;
        }
        state.lanes.push_back(std::move(lane));
    }

    // The "+" picker offers every listed parameter that has no lane yet; picking one opens it.
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
                .plugin_name = parameter.plugin_name,
            });
    }

    // Re-resolve the durable selection against the lanes actually published: a selection whose
    // point vanished (undo, lane removal, tone switch) publishes as nothing instead of pointing
    // at the wrong glyph — the same self-healing rule chart selection indices follow.
    if (selected_point != nullptr)
    {
        for (std::size_t lane_index = 0; lane_index < state.lanes.size(); ++lane_index)
        {
            const ToneAutomationLaneViewState& lane = state.lanes[lane_index];
            if (lane.instance_id != selected_point->instance_id ||
                lane.param_id != selected_point->param_id)
            {
                continue;
            }
            for (std::size_t point_index = 0; point_index < lane.points.size(); ++point_index)
            {
                if (lane.points[point_index].position == selected_point->position)
                {
                    state.selected_point = ToneAutomationSelectedPointRef{
                        .lane_index = lane_index, .point_index = point_index
                    };
                    break;
                }
            }
            break;
        }
    }
    return state;
}

} // namespace rock_hero::editor::core
