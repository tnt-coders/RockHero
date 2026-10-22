#include "engine_impl.h"
#include "tracktion/tone_automation_curve.h"

#include <iterator>
#include <juce_events/juce_events.h>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Finds a loaded tone branch by its canonical tone document reference, or null when not loaded.
[[nodiscard]] const ToneRackBranch* findToneBranch(
    const std::optional<ToneRack>& tone_rack, const std::string& tone_document_ref)
{
    if (!tone_rack.has_value())
    {
        return nullptr;
    }
    for (const ToneRackBranch& branch : tone_rack->branches)
    {
        if (branch.tone_document_ref == tone_document_ref)
        {
            return &branch;
        }
    }
    return nullptr;
}

// Finds an automatable plugin on a branch by its runtime instance id — a chain plugin or the
// tone's output stage, which exposes the Volume and Pan lanes — or null when absent.
[[nodiscard]] tracktion::Plugin* findBranchPlugin(
    const ToneRackBranch& branch, const std::string& instance_id)
{
    const juce::String target_id{instance_id};
    for (const tracktion::Plugin::Ptr& plugin : branch.chain)
    {
        if (plugin != nullptr && plugin->itemID.toString() == target_id)
        {
            return plugin.get();
        }
    }
    if (branch.output != nullptr && branch.output->itemID.toString() == target_id)
    {
        return branch.output;
    }
    return nullptr;
}

} // namespace

std::expected<std::vector<AutomatableParamInfo>, ToneAutomationError> Engine::
    listAutomatableParameters(const std::string& tone_document_ref) const
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{ToneAutomationError{ToneAutomationErrorCode::MessageThreadRequired}};
    }

    const ToneRackBranch* const branch = findToneBranch(m_impl->m_tone_rack, tone_document_ref);
    if (branch == nullptr)
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::ToneNotLoaded, "Tone is not loaded: " + tone_document_ref
        }};
    }

    // The output stage lists last, in signal order after the chain, so the picker offers Volume
    // and Pan under "Output" even for a tone with no plugins at all.
    std::vector<AutomatableParamInfo> parameters = listChainAutomatableParameters(branch->chain);
    if (branch->output != nullptr)
    {
        std::vector<AutomatableParamInfo> output_parameters =
            listPluginAutomatableParameters(*branch->output);
        parameters.insert(
            parameters.end(),
            std::make_move_iterator(output_parameters.begin()),
            std::make_move_iterator(output_parameters.end()));
    }
    return parameters;
}

std::expected<std::vector<AutomationCurvePoint>, ToneAutomationError> Engine::readParameterCurve(
    const std::string& tone_document_ref, const std::string& instance_id,
    const std::string& param_id) const
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{ToneAutomationError{ToneAutomationErrorCode::MessageThreadRequired}};
    }

    const ToneRackBranch* const branch = findToneBranch(m_impl->m_tone_rack, tone_document_ref);
    if (branch == nullptr)
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::ToneNotLoaded, "Tone is not loaded: " + tone_document_ref
        }};
    }

    const tracktion::Plugin* const plugin = findBranchPlugin(*branch, instance_id);
    if (plugin == nullptr)
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    std::optional<std::vector<AutomationCurvePoint>> points =
        readPluginParameterCurve(*plugin, param_id);
    if (!points.has_value())
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::ParameterNotFound, "Parameter was not found: " + param_id
        }};
    }
    return std::move(*points);
}

std::expected<float, ToneAutomationError> Engine::readParameterBaselineNormValue(
    const std::string& tone_document_ref, const std::string& instance_id,
    const std::string& param_id) const
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{ToneAutomationError{ToneAutomationErrorCode::MessageThreadRequired}};
    }

    const ToneRackBranch* const branch = findToneBranch(m_impl->m_tone_rack, tone_document_ref);
    if (branch == nullptr)
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::ToneNotLoaded, "Tone is not loaded: " + tone_document_ref
        }};
    }

    const tracktion::Plugin* const plugin = findBranchPlugin(*branch, instance_id);
    if (plugin == nullptr)
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    const std::optional<float> value = readPluginParameterBaselineNormValue(*plugin, param_id);
    if (!value.has_value())
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::ParameterNotFound, "Parameter was not found: " + param_id
        }};
    }
    return *value;
}

std::expected<std::string, ToneAutomationError> Engine::formatParameterValue(
    const std::string& tone_document_ref, const std::string& instance_id,
    const std::string& param_id, float norm_value) const
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{ToneAutomationError{ToneAutomationErrorCode::MessageThreadRequired}};
    }

    const ToneRackBranch* const branch = findToneBranch(m_impl->m_tone_rack, tone_document_ref);
    if (branch == nullptr)
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::ToneNotLoaded, "Tone is not loaded: " + tone_document_ref
        }};
    }

    const tracktion::Plugin* const plugin = findBranchPlugin(*branch, instance_id);
    if (plugin == nullptr)
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    std::optional<std::string> text = formatPluginParameterValue(*plugin, param_id, norm_value);
    if (!text.has_value())
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::ParameterNotFound, "Parameter was not found: " + param_id
        }};
    }
    return std::move(*text);
}

std::expected<float, ToneAutomationError> Engine::parseParameterValue(
    const std::string& tone_document_ref, const std::string& instance_id,
    const std::string& param_id, const std::string& text) const
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{ToneAutomationError{ToneAutomationErrorCode::MessageThreadRequired}};
    }

    const ToneRackBranch* const branch = findToneBranch(m_impl->m_tone_rack, tone_document_ref);
    if (branch == nullptr)
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::ToneNotLoaded, "Tone is not loaded: " + tone_document_ref
        }};
    }

    const tracktion::Plugin* const plugin = findBranchPlugin(*branch, instance_id);
    if (plugin == nullptr)
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    const std::optional<float> value = parsePluginParameterValue(*plugin, param_id, text);
    if (!value.has_value())
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::ParameterNotFound, "Parameter was not found: " + param_id
        }};
    }
    return *value;
}

std::expected<void, ToneAutomationError> Engine::writeParameterCurve(
    const std::string& tone_document_ref, const std::string& instance_id,
    const std::string& param_id, std::span<const AutomationCurvePoint> points)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{ToneAutomationError{ToneAutomationErrorCode::MessageThreadRequired}};
    }

    const ToneRackBranch* const branch = findToneBranch(m_impl->m_tone_rack, tone_document_ref);
    if (branch == nullptr)
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::ToneNotLoaded, "Tone is not loaded: " + tone_document_ref
        }};
    }

    const tracktion::Plugin* const plugin = findBranchPlugin(*branch, instance_id);
    if (plugin == nullptr)
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    if (!writePluginParameterCurve(*plugin, param_id, points))
    {
        return std::unexpected{ToneAutomationError{
            ToneAutomationErrorCode::ParameterNotFound, "Parameter was not found: " + param_id
        }};
    }
    return {};
}

} // namespace rock_hero::common::audio
