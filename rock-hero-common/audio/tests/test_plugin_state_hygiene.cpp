#include "tracktion/plugin_state_hygiene.h"

#include <catch2/catch_test_macros.hpp>
#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::common::audio
{

TEST_CASE(
    "stripAutomationCurves removes only AUTOMATIONCURVE children", "[audio][plugin-state-hygiene]")
{
    juce::ValueTree plugin_state{tracktion::IDs::PLUGIN};
    plugin_state.appendChild(juce::ValueTree{tracktion::IDs::AUTOMATIONCURVE}, nullptr);
    plugin_state.appendChild(juce::ValueTree{tracktion::IDs::MACROPARAMETERS}, nullptr);
    plugin_state.appendChild(juce::ValueTree{tracktion::IDs::AUTOMATIONCURVE}, nullptr);

    stripAutomationCurves(plugin_state);

    REQUIRE(plugin_state.getNumChildren() == 1);
    CHECK(plugin_state.getChild(0).hasType(tracktion::IDs::MACROPARAMETERS));
}

TEST_CASE("stripTempoRemapFlag removes only the remap property", "[audio][plugin-state-hygiene]")
{
    juce::ValueTree plugin_state{tracktion::IDs::PLUGIN};
    plugin_state.setProperty(tracktion::IDs::remapOnTempoChange, true, nullptr);
    plugin_state.setProperty(tracktion::IDs::enabled, true, nullptr);

    stripTempoRemapFlag(plugin_state);

    CHECK_FALSE(plugin_state.hasProperty(tracktion::IDs::remapOnTempoChange));
    CHECK(plugin_state.hasProperty(tracktion::IDs::enabled));
}

} // namespace rock_hero::common::audio
