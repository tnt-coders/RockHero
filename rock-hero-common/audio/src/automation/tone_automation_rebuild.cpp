#include "rock_hero/common/audio/automation/tone_automation_rebuild.h"

#include <expected>
#include <rock_hero/common/core/shared/logger.h>

namespace rock_hero::common::audio
{

std::unordered_map<std::string, ToneAutomationBinding> makeToneAutomationBindings(
    std::span<const LoadedToneChainIdentities> tone_chains)
{
    std::unordered_map<std::string, ToneAutomationBinding> bindings;
    for (const LoadedToneChainIdentities& chain : tone_chains)
    {
        for (const LoadedTonePluginIdentity& plugin : chain.plugins)
        {
            // A plugin without a minted durable id cannot be referenced by persisted automation.
            if (plugin.stable_id.empty())
            {
                continue;
            }
            bindings.insert_or_assign(
                plugin.stable_id,
                ToneAutomationBinding{
                    .instance_id = plugin.instance_id,
                    .tone_document_ref = chain.tone_document_ref,
                });
        }
    }
    return bindings;
}

std::vector<AutomationCurvePoint> derivedToneCurvePoints(
    const common::core::TempoMap& tempo_map,
    std::span<const common::core::ToneAutomationPoint> points)
{
    std::vector<AutomationCurvePoint> curve_points;
    curve_points.reserve(points.size());
    for (const common::core::ToneAutomationPoint& point : points)
    {
        curve_points.push_back(
            AutomationCurvePoint{
                .seconds = tempo_map.secondsAtNote(
                    point.position.measure, point.position.beat, point.position.offset),
                .norm_value = point.norm_value,
                .curve_shape = point.curve_shape,
            });
    }
    return curve_points;
}

void writeDerivedToneCurve(
    IToneAutomation& tone_automation, const common::core::TempoMap& tempo_map,
    const std::string& tone_document_ref, const std::string& instance_id,
    const std::string& param_id, std::span<const common::core::ToneAutomationPoint> points)
{
    const std::vector<AutomationCurvePoint> curve_points =
        derivedToneCurvePoints(tempo_map, points);
    const auto written =
        tone_automation.writeParameterCurve(tone_document_ref, instance_id, param_id, curve_points);
    if (!written.has_value())
    {
        // Best-effort by design: the model is the truth and the next load rebuild reconciles the
        // cache, but a failed rewrite is still worth a trace.
        RH_LOG_WARNING(
            "audio.automation",
            "Could not rewrite derived automation curve tone={:?} param={:?} detail={:?}",
            tone_document_ref,
            param_id,
            written.error().message);
    }
}

void rebuildToneAutomationCurves(
    IToneAutomation& tone_automation,
    std::span<const common::core::ToneParameterAutomation> automation,
    const common::core::TempoMap& tempo_map,
    const std::unordered_map<std::string, ToneAutomationBinding>& bindings)
{
    for (const common::core::ToneParameterAutomation& entry : automation)
    {
        const auto binding = bindings.find(entry.plugin_id);
        if (binding == bindings.end() || binding->second.instance_id.empty())
        {
            continue;
        }
        writeDerivedToneCurve(
            tone_automation,
            tempo_map,
            binding->second.tone_document_ref,
            binding->second.instance_id,
            entry.param_id,
            entry.points);
    }
}

} // namespace rock_hero::common::audio
