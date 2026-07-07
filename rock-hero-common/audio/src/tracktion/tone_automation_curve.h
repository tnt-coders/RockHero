/*!
\file tone_automation_curve.h
\brief Tracktion-side reading and writing of tone-chain plugin parameter automation curves.
*/

#pragma once

#include <optional>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <span>
#include <string>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace rock_hero::common::audio
{

/*!
\brief Lists the automatable parameters of every plugin in a tone chain.

Walks each plugin's grouped parameter tree, tagging every parameter with its owning plugin's
instance id and group name, and skipping the synthetic dry/wet mix parameters. Values are
normalised to `[0, 1]`.

\param chain Ordered tone-chain plugins to enumerate.
\return The chain's automatable parameters in plugin-then-tree order.
*/
[[nodiscard]] std::vector<AutomatableParamInfo> listChainAutomatableParameters(
    std::span<const tracktion::Plugin::Ptr> chain);

/*!
\brief Reads one plugin parameter's automation curve as normalised, seconds-based points.

\param plugin Plugin owning the parameter.
\param param_id Parameter id within the plugin.
\return The curve points in ascending time (empty when the parameter has no curve), or `nullopt`
        when \p param_id does not resolve on the plugin.
*/
[[nodiscard]] std::optional<std::vector<AutomationCurvePoint>> readPluginParameterCurve(
    tracktion::Plugin& plugin, const std::string& param_id);

/*!
\brief Replaces one plugin parameter's automation curve with the supplied points.

Clears the existing curve and rebuilds it from \p points (an empty span removes the curve). RockHero
owns undo, so a null undo manager is passed to the backend.

\param plugin Plugin owning the parameter.
\param param_id Parameter id within the plugin.
\param points Replacement curve points, normalised value, in ascending time.
\return True on success, or false when \p param_id does not resolve on the plugin.
*/
[[nodiscard]] bool writePluginParameterCurve(
    tracktion::Plugin& plugin, const std::string& param_id,
    std::span<const AutomationCurvePoint> points);

} // namespace rock_hero::common::audio
