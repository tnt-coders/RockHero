/*!
\file plugin_state_memento.h
\brief Parses opaque plugin-state mementos back into Tracktion plugin trees.

Shared by the plugin host and the live-rig chain-restore path, so memento validation has one
parser.
*/

#pragma once

#include <expected>
#include <juce_data_structures/juce_data_structures.h>
#include <rock_hero/common/audio/plugin/plugin_host_error.h>
#include <rock_hero/common/audio/plugin/plugin_instance_state.h>
#include <string>

namespace rock_hero::common::audio
{

/*!
\brief Parses a memento payload into Tracktion's plugin-state tree.

Rejects empty, oversized, unparsable, and non-external-plugin payloads so callers can trust the
returned tree to be a restorable ExternalPlugin state.

\param state Opaque memento captured from a live plugin.
\return Parsed plugin state tree, or the restore failure to report.
*/
[[nodiscard]] std::expected<juce::ValueTree, PluginHostError> pluginStateTreeFromMemento(
    const PluginInstanceState& state);

/*!
\brief Reads the runtime instance id encoded in a captured Tracktion plugin tree.
\param plugin_state Captured plugin state tree.
\return Encoded instance id, or empty when the tree carries none.
*/
[[nodiscard]] std::string pluginInstanceIdFromState(const juce::ValueTree& plugin_state);

} // namespace rock_hero::common::audio
