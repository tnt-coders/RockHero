#include "tracktion/engine_behaviors.h"
#include "tracktion/tone_automation_curve.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Owns the Tracktion objects needed to create Rock Hero's private plugin types headlessly.
struct ToneAutomationHarness
{
    juce::ScopedJuceInitialiser_GUI gui;
    tracktion::Engine engine{
        "RockHeroToneAutomationTest",
        nullptr,
        std::make_unique<RockHeroEngineBehavior>(),
    };
    std::unique_ptr<tracktion::Edit> edit{tracktion::Edit::createSingleTrackEdit(engine)};
};

// Creates an engine-internal stereo plugin (volume/pan) standing in for a VST chain entry; it has
// real automatable parameters, so no scanned VST is needed to exercise the automation adapter.
[[nodiscard]] tracktion::Plugin::Ptr createChainStandIn(tracktion::Edit& edit)
{
    return edit.getPluginCache().createNewPlugin(tracktion::VolumeAndPanPlugin::xmlTypeName, {});
}

} // namespace

TEST_CASE(
    "listChainAutomatableParameters enumerates a chain plugin's parameters",
    "[audio][tone-automation]")
{
    const ToneAutomationHarness harness;
    const tracktion::Plugin::Ptr plugin = createChainStandIn(*harness.edit);
    const std::vector<tracktion::Plugin::Ptr> chain{plugin};

    const std::vector<AutomatableParamInfo> parameters = listChainAutomatableParameters(chain);

    REQUIRE_FALSE(parameters.empty());
    const std::string expected_instance_id = plugin->itemID.toString().toStdString();
    for (const AutomatableParamInfo& parameter : parameters)
    {
        CHECK(parameter.instance_id == expected_instance_id);
        CHECK_FALSE(parameter.param_id.empty());
        CHECK_FALSE(parameter.name.empty());
        CHECK(parameter.default_norm_value >= 0.0F);
        CHECK(parameter.default_norm_value <= 1.0F);
    }
}

TEST_CASE(
    "writePluginParameterCurve and readPluginParameterCurve round-trip points",
    "[audio][tone-automation]")
{
    const ToneAutomationHarness harness;
    const tracktion::Plugin::Ptr plugin = createChainStandIn(*harness.edit);
    const std::vector<tracktion::Plugin::Ptr> chain{plugin};
    const std::vector<AutomatableParamInfo> parameters = listChainAutomatableParameters(chain);
    REQUIRE_FALSE(parameters.empty());
    const std::string param_id = parameters.front().param_id;

    const std::vector<AutomationCurvePoint> points{
        AutomationCurvePoint{.seconds = 0.0, .norm_value = 0.25F, .curve_shape = 0.0F},
        AutomationCurvePoint{.seconds = 1.5, .norm_value = 0.75F, .curve_shape = 0.0F},
        AutomationCurvePoint{.seconds = 3.0, .norm_value = 1.0F, .curve_shape = 0.0F},
    };
    REQUIRE(writePluginParameterCurve(*plugin, param_id, points));

    const std::optional<std::vector<AutomationCurvePoint>> read_back =
        readPluginParameterCurve(*plugin, param_id);
    REQUIRE(read_back.has_value());
    REQUIRE(read_back->size() == points.size());
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        CHECK(read_back->at(index).seconds == Catch::Approx(points[index].seconds));
        CHECK(read_back->at(index).norm_value == Catch::Approx(points[index].norm_value));
        CHECK(read_back->at(index).curve_shape == Catch::Approx(points[index].curve_shape));
    }
}

TEST_CASE(
    "writePluginParameterCurve with an empty span clears the curve", "[audio][tone-automation]")
{
    const ToneAutomationHarness harness;
    const tracktion::Plugin::Ptr plugin = createChainStandIn(*harness.edit);
    const std::vector<tracktion::Plugin::Ptr> chain{plugin};
    const std::string param_id = listChainAutomatableParameters(chain).front().param_id;

    const std::vector<AutomationCurvePoint> points{
        AutomationCurvePoint{.seconds = 0.5, .norm_value = 0.3F, .curve_shape = 0.0F},
        AutomationCurvePoint{.seconds = 2.0, .norm_value = 0.6F, .curve_shape = 0.0F},
    };
    REQUIRE(writePluginParameterCurve(*plugin, param_id, points));
    REQUIRE(writePluginParameterCurve(*plugin, param_id, {}));

    const std::optional<std::vector<AutomationCurvePoint>> read_back =
        readPluginParameterCurve(*plugin, param_id);
    REQUIRE(read_back.has_value());
    CHECK(read_back->empty());
}

TEST_CASE("Unresolved parameter ids fail cleanly", "[audio][tone-automation]")
{
    const ToneAutomationHarness harness;
    const tracktion::Plugin::Ptr plugin = createChainStandIn(*harness.edit);

    CHECK_FALSE(readPluginParameterCurve(*plugin, "not-a-real-parameter").has_value());

    const std::vector<AutomationCurvePoint> points{
        AutomationCurvePoint{.seconds = 0.0, .norm_value = 0.5F, .curve_shape = 0.0F},
    };
    CHECK_FALSE(writePluginParameterCurve(*plugin, "not-a-real-parameter", points));
}

} // namespace rock_hero::common::audio
