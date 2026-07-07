#include "tracktion/plugin_state_hygiene.h"

#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::common::audio
{

void stripAutomationCurves(juce::ValueTree& plugin_state)
{
    for (int index = plugin_state.getNumChildren(); --index >= 0;)
    {
        if (plugin_state.getChild(index).hasType(tracktion::IDs::AUTOMATIONCURVE))
        {
            plugin_state.removeChild(index, nullptr);
        }
    }
}

void stripTempoRemapFlag(juce::ValueTree& plugin_state)
{
    plugin_state.removeProperty(tracktion::IDs::remapOnTempoChange, nullptr);
}

} // namespace rock_hero::common::audio
