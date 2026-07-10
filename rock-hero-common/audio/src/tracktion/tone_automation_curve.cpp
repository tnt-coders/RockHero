#include "tracktion/tone_automation_curve.h"

#include <algorithm>
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

// Number of discrete values a stepped parameter exposes (0 when continuous), so the editor can
// snap automation drags to real states like a DAW. Tracktion's own AutomatableParameter::
// isDiscrete()/getNumberOfStates() read VST2-only VSTXML metadata, so a VST3 toggle (e.g. a "wah
// mode") reports as continuous.
//
// Hosted VST2/VST3 parameters are HostedAudioProcessorParameters, not AudioProcessorParameterWithID,
// so Tracktion's own paramID-lookup fails and it stores each parameter's plain JUCE array index as
// the (numeric) paramID (tracktion_ExternalPlugin.cpp:798-804). Reach the JUCE parameter by that
// index and read its isDiscrete()/getNumSteps(), which carry the real VST3 stepCount
// (juce_VST3PluginFormatImpl.h:2085-2095) despite the WithID gap. Runs on the message thread per the
// Tracktion automation contract.
[[nodiscard]] int discreteValueCount(const tracktion::AutomatableParameter& parameter)
{
    if (parameter.isDiscrete())
    {
        return parameter.getNumberOfStates();
    }

    auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(parameter.getPlugin());
    if (external_plugin == nullptr)
    {
        return 0;
    }
    const juce::AudioPluginInstance* const instance = external_plugin->getAudioPluginInstance();
    if (instance == nullptr)
    {
        // Still async-loading; treat as continuous until the instance exists.
        return 0;
    }

    // The numeric paramID is the parameter's index into the plugin instance's parameter array.
    if (const juce::String& param_id = parameter.paramID;
        param_id.isNotEmpty() && param_id.containsOnly("0123456789"))
    {
        const int index = param_id.getIntValue();
        if (index >= 0 && index < instance->getParameters().size())
        {
            if (const juce::AudioProcessorParameter* const juce_parameter =
                    instance->getParameters()[index];
                juce_parameter != nullptr)
            {
                if (juce_parameter->isDiscrete() && juce_parameter->getNumSteps() >= 2)
                {
                    return juce_parameter->getNumSteps();
                }
            }
        }
    }

    return 0;
}

// Reads every automatable fact the editor needs about one plugin parameter into its view-facing
// descriptor. The discrete flag, step labels, and step count all derive from one value count so
// they cannot disagree.
[[nodiscard]] AutomatableParamInfo readAutomatableParamInfo(
    const tracktion::AutomatableParameter& parameter, const std::string& instance_id,
    const juce::String& group_name, const std::string& plugin_name)
{
    const int value_count = discreteValueCount(parameter);

    // Step labels come only from Tracktion's VSTXML metadata, and its getAllLabels() dereferences
    // that (VST2-only) param pointer without a null guard — unlike its sibling getLabelForValue().
    // isDiscrete() returns `param != nullptr && ...`, so it is the exact guard that makes the call
    // safe. A VST3 discrete param reports its step count through discreteValueCount()'s index path
    // but carries no VSTXML, so it stays unlabelled; snapping needs only the count.
    std::vector<std::string> labels;
    if (parameter.isDiscrete())
    {
        for (const juce::String& label : parameter.getAllLabels())
        {
            labels.push_back(label.toStdString());
        }
    }

    const std::optional<float> default_value = parameter.getDefaultValue();
    return AutomatableParamInfo{
        .instance_id = instance_id,
        .param_id = parameter.paramID.toStdString(),
        .name = parameter.getParameterName().toStdString(),
        .group = group_name.toStdString(),
        .is_discrete = value_count >= 2,
        .discrete_value_count = value_count,
        .labels = std::move(labels),
        .default_norm_value =
            default_value.has_value() ? parameter.valueRange.convertTo0to1(*default_value) : 0.0F,
        .current_norm_value = parameter.valueRange.convertTo0to1(parameter.getCurrentValue()),
        .plugin_name = plugin_name,
    };
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

        out.push_back(readAutomatableParamInfo(parameter, instance_id, group_name, plugin_name));
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

std::optional<std::string> formatPluginParameterValue(
    tracktion::Plugin& plugin, const std::string& param_id, float norm_value)
{
    const tracktion::AutomatableParameter::Ptr parameter =
        plugin.getAutomatableParameterByID(juce::String{param_id});
    if (parameter == nullptr)
    {
        return std::nullopt;
    }

    // Mirror Tracktion's own value-with-label formatting so the readout matches how the plugin
    // presents the parameter, without moving the live value: convert out of [0, 1], render, and
    // append the unit label unless the rendered text already carries it.
    const float value = parameter->valueRange.convertFrom0to1(norm_value);
    juce::String text = parameter->valueToString(value);
    if (const juce::String label = parameter->getLabel();
        label.isNotEmpty() && !text.endsWith(label))
    {
        text << ' ' << label;
    }
    return text.toStdString();
}

std::optional<float> parsePluginParameterValue(
    tracktion::Plugin& plugin, const std::string& param_id, const std::string& text)
{
    const tracktion::AutomatableParameter::Ptr parameter =
        plugin.getAutomatableParameterByID(juce::String{param_id});
    if (parameter == nullptr)
    {
        return std::nullopt;
    }

    // Exact inverse of formatPluginParameterValue: the parameter interprets the text the way the
    // plugin parses typed values (hosted parameters route through the plugin's own text-to-value
    // handler; Tracktion's default falls back to a plain numeric read), then the range normalises
    // the result. Clamp because plugin parsers may return values outside the automatable range.
    const float value = parameter->stringToValue(juce::String{text});
    return std::clamp(parameter->valueRange.convertTo0to1(value), 0.0F, 1.0F);
}

} // namespace rock_hero::common::audio
