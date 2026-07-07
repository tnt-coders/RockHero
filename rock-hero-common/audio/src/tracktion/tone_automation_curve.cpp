#include "tracktion/tone_automation_curve.h"

#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// The two synthetic mix parameters every external plugin exposes; never useful to automate here.
[[nodiscard]] bool isSyntheticMixParameter(const juce::String& param_id)
{
    return param_id == "dry level" || param_id == "wet level";
}

// Walks one plugin's grouped parameter tree, appending each real automatable parameter (with its
// group name) to the output. Groups nest, so this recurses; the synthetic dry/wet mix params are
// skipped.
void collectAutomatableParameters(
    const tracktion::AutomatableParameterTree::TreeNode& node, const juce::String& group_name,
    const std::string& instance_id, const std::string& plugin_name,
    std::vector<AutomatableParamInfo>& out)
{
    for (const tracktion::AutomatableParameterTree::TreeNode* const sub_node : node.subNodes)
    {
        if (sub_node == nullptr)
        {
            continue;
        }
        if (sub_node->type == tracktion::AutomatableParameterTree::Group)
        {
            collectAutomatableParameters(
                *sub_node, sub_node->getGroupName(), instance_id, plugin_name, out);
            continue;
        }
        if (sub_node->parameter == nullptr)
        {
            continue;
        }

        const tracktion::AutomatableParameter& parameter = *sub_node->parameter;
        if (isSyntheticMixParameter(parameter.paramID))
        {
            continue;
        }

        std::vector<std::string> labels;
        if (parameter.isDiscrete())
        {
            for (const juce::String& label : parameter.getAllLabels())
            {
                labels.push_back(label.toStdString());
            }
        }

        const std::optional<float> default_value = parameter.getDefaultValue();
        out.push_back(
            AutomatableParamInfo{
                .instance_id = instance_id,
                .param_id = parameter.paramID.toStdString(),
                .name = parameter.getParameterName().toStdString(),
                .group = group_name.toStdString(),
                .is_discrete = parameter.isDiscrete(),
                .labels = std::move(labels),
                .default_norm_value = default_value.has_value()
                                          ? parameter.valueRange.convertTo0to1(*default_value)
                                          : 0.0F,
                .current_norm_value =
                    parameter.valueRange.convertTo0to1(parameter.getCurrentValue()),
                .plugin_name = plugin_name,
            });
    }
}

} // namespace

std::vector<AutomatableParamInfo> listChainAutomatableParameters(
    std::span<const tracktion::Plugin::Ptr> chain)
{
    std::vector<AutomatableParamInfo> parameters;
    for (const tracktion::Plugin::Ptr& plugin : chain)
    {
        if (plugin == nullptr)
        {
            continue;
        }
        const std::string instance_id = plugin->itemID.toString().toStdString();
        const std::string plugin_name = plugin->getName().toStdString();
        collectAutomatableParameters(
            *plugin->getParameterTree().rootNode,
            juce::String{},
            instance_id,
            plugin_name,
            parameters);
    }
    return parameters;
}

std::optional<std::vector<AutomationCurvePoint>> readPluginParameterCurve(
    tracktion::Plugin& plugin, const std::string& param_id)
{
    const tracktion::AutomatableParameter::Ptr parameter =
        plugin.getAutomatableParameterByID(juce::String{param_id});
    if (parameter == nullptr)
    {
        return std::nullopt;
    }

    const tracktion::AutomationCurve& curve = parameter->getCurve();
    std::vector<AutomationCurvePoint> points;
    const int point_count = curve.getNumPoints();
    points.reserve(static_cast<std::size_t>(point_count));
    for (int index = 0; index < point_count; ++index)
    {
        // The curve is time-based (its parameter builds it with TimeBase::time), so the point time
        // is directly seconds; the value is parameter-native and normalised to [0, 1] for the port.
        points.push_back(
            AutomationCurvePoint{
                .seconds = curve.getPointTime(index).inSeconds(),
                .norm_value = parameter->valueRange.convertTo0to1(curve.getPointValue(index)),
                .curve_shape = curve.getPointCurve(index),
            });
    }
    return points;
}

bool writePluginParameterCurve(
    tracktion::Plugin& plugin, const std::string& param_id,
    std::span<const AutomationCurvePoint> points)
{
    const tracktion::AutomatableParameter::Ptr parameter =
        plugin.getAutomatableParameterByID(juce::String{param_id});
    if (parameter == nullptr)
    {
        return false;
    }

    // RockHero owns undo through point-list mementos, so every backend edit passes a null undo
    // manager. Clearing then re-adding the whole point list is the simplest correct write.
    tracktion::AutomationCurve& curve = parameter->getCurve();
    curve.clear(nullptr);
    for (const AutomationCurvePoint& point : points)
    {
        curve.addPoint(
            tracktion::EditPosition{tracktion::TimePosition::fromSeconds(point.seconds)},
            parameter->valueRange.convertFrom0to1(point.norm_value),
            point.curve_shape,
            nullptr);
    }
    return true;
}

std::optional<float> readPluginParameterNormValue(
    tracktion::Plugin& plugin, const std::string& param_id)
{
    const tracktion::AutomatableParameter::Ptr parameter =
        plugin.getAutomatableParameterByID(juce::String{param_id});
    if (parameter == nullptr)
    {
        return std::nullopt;
    }
    return parameter->getCurrentNormalisedValue();
}

} // namespace rock_hero::common::audio
