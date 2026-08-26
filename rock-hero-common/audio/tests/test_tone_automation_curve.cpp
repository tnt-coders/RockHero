#include "tracktion/engine_behaviors.h"
#include "tracktion/tone_automation_curve.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
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

// Resolves a chain stand-in's first automatable parameter, so the anchor cases can drive the tone
// state's own value through it and read the backend curve the seam wrote.
[[nodiscard]] tracktion::AutomatableParameter::Ptr firstParameter(tracktion::Plugin& plugin)
{
    const std::vector<tracktion::Plugin::Ptr> chain{tracktion::Plugin::Ptr{&plugin}};
    return plugin.getAutomatableParameterByID(
        juce::String{listChainAutomatableParameters(chain).front().param_id});
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
        AutomationCurvePoint{.seconds = 1.0, .norm_value = 0.25F},
        AutomationCurvePoint{.seconds = 1.5, .norm_value = 0.75F},
        AutomationCurvePoint{.seconds = 3.0, .norm_value = 1.0F},
    };
    REQUIRE(writePluginParameterCurve(*plugin, param_id, points));

    // The seam prepends the lane's anchor, so the read-back is the anchor followed by every
    // authored point, unchanged.
    const std::optional<std::vector<AutomationCurvePoint>> read_back =
        readPluginParameterCurve(*plugin, param_id);
    REQUIRE(read_back.has_value());
    if (read_back.has_value())
    {
        REQUIRE(read_back->size() == points.size() + 1);
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            CHECK(read_back->at(index + 1).seconds == Catch::Approx(points[index].seconds));
            CHECK(read_back->at(index + 1).norm_value == Catch::Approx(points[index].norm_value));
        }
    }

    // Segment shape left the port's vocabulary, so it is asserted against the backend curve where
    // the write seam derives it: this stand-in is continuous, so every segment must be exactly a
    // linear ramp — the anchor's included, which is what makes the anchor compose with the shape
    // derivation instead of carrying a shape of its own. A discreteValueCount change that starts
    // reporting steps for a plain knob fails loudly here instead of silently re-shaping every
    // continuous curve.
    const tracktion::AutomatableParameter::Ptr parameter =
        plugin->getAutomatableParameterByID(juce::String{param_id});
    REQUIRE(parameter != nullptr);
    const tracktion::AutomationCurve& curve = parameter->getCurve();
    REQUIRE(curve.getNumPoints() == static_cast<int>(points.size()) + 1);
    for (int index = 0; index < curve.getNumPoints(); ++index)
    {
        CHECK_THAT(curve.getPointCurve(index), Catch::Matchers::WithinULP(0.0F, 0));
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
        AutomationCurvePoint{.seconds = 0.5, .norm_value = 0.3F},
        AutomationCurvePoint{.seconds = 2.0, .norm_value = 0.6F},
    };
    REQUIRE(writePluginParameterCurve(*plugin, param_id, points));
    REQUIRE(writePluginParameterCurve(*plugin, param_id, {}));

    const std::optional<std::vector<AutomationCurvePoint>> read_back =
        readPluginParameterCurve(*plugin, param_id);
    REQUIRE(read_back.has_value());
    if (read_back.has_value())
    {
        CHECK(read_back->empty());
    }
}

// The discrete composition: an anchor written with the stepped parameter's derived hold shape sits
// at the tone state's value and steps exactly at the first authored point. This also pins the
// vendored-engine contract that makes it work — a segment whose EARLIER point carries curve shape
// +1 holds that point's value across the whole segment and steps at the later point, never before
// it. The discrete branch of writePluginParameterCurve cannot run headlessly (it needs a hosted
// plugin reporting discrete steps), so the arrangement is built by hand here; that the anchor
// really does take the derived shape is asserted on the continuous side of the round-trip test
// above, which checks the shape of every written point including the anchor's.
TEST_CASE(
    "A stepped anchor holds the tone-state value until the first authored point",
    "[audio][tone-automation]")
{
    const ToneAutomationHarness harness;
    const tracktion::Plugin::Ptr plugin = createChainStandIn(*harness.edit);
    const std::vector<tracktion::Plugin::Ptr> chain{plugin};
    const std::string param_id = listChainAutomatableParameters(chain).front().param_id;
    const tracktion::AutomatableParameter::Ptr parameter =
        plugin->getAutomatableParameterByID(juce::String{param_id});
    REQUIRE(parameter != nullptr);

    // The anchor at the origin carrying the tone state's value, then the first authored point.
    constexpr float anchor_value = 0.25F;
    constexpr float authored_value = 0.75F;
    tracktion::AutomationCurve& curve = parameter->getCurve();
    curve.clear(nullptr);
    curve.addPoint(tracktion::TimePosition{}, anchor_value, 1.0F, nullptr);
    curve.addPoint(tracktion::TimePosition::fromSeconds(1.0), authored_value, 1.0F, nullptr);

    // The default is only returned for an empty curve, so reading it back would be a test bug.
    constexpr float unused_default = -1.0F;
    const auto value_at = [&curve](double seconds) {
        return curve.getValueAt(tracktion::TimePosition::fromSeconds(seconds), unused_default);
    };

    CHECK_THAT(value_at(0.5), Catch::Matchers::WithinULP(anchor_value, 0));
    CHECK_THAT(value_at(0.999), Catch::Matchers::WithinULP(anchor_value, 0));
    CHECK_THAT(value_at(1.0), Catch::Matchers::WithinULP(anchor_value, 0));
    CHECK_THAT(value_at(1.001), Catch::Matchers::WithinULP(authored_value, 0));
}

TEST_CASE(
    "A lane's written curve is anchored at the parameter's tone-state value",
    "[audio][tone-automation]")
{
    const ToneAutomationHarness harness;
    const tracktion::Plugin::Ptr plugin = createChainStandIn(*harness.edit);
    const tracktion::AutomatableParameter::Ptr parameter = firstParameter(*plugin);
    REQUIRE(parameter != nullptr);

    // The tone state's value for this parameter — what the saved document restores it to, and
    // deliberately not the value the single authored point will hold.
    constexpr float tone_state_value = 0.2F;
    constexpr float authored_value = 0.9F;
    parameter->setNormalisedParameter(tone_state_value, juce::dontSendNotification);

    const std::vector<AutomationCurvePoint> points{
        AutomationCurvePoint{.seconds = 2.0, .norm_value = authored_value},
    };
    REQUIRE(writePluginParameterCurve(*plugin, parameter->paramID.toStdString(), points));

    // One authored point yields two backend points: the anchor at the origin, then the point.
    const std::optional<std::vector<AutomationCurvePoint>> read_back =
        readPluginParameterCurve(*plugin, parameter->paramID.toStdString());
    REQUIRE(read_back.has_value());
    if (read_back.has_value())
    {
        REQUIRE(read_back->size() == 2);
        CHECK(read_back->front().seconds == Catch::Approx(0.0));
        CHECK(read_back->front().norm_value == Catch::Approx(tone_state_value));
        CHECK(read_back->back().seconds == Catch::Approx(2.0));
        CHECK(read_back->back().norm_value == Catch::Approx(authored_value));
    }

    // The retroactive lone point is gone by construction: before the authored point the parameter
    // ramps up FROM the tone state's value instead of already sitting at the point's future value.
    // Asserted by ordering, because the ramp interpolates in the parameter's own (possibly skewed)
    // range while the port speaks normalised values.
    const tracktion::AutomationCurve& curve = parameter->getCurve();
    constexpr float unused_default = -1.0F;
    const auto norm_value_at = [&curve, &parameter](double seconds) {
        return parameter->valueRange.convertTo0to1(
            curve.getValueAt(tracktion::TimePosition::fromSeconds(seconds), unused_default));
    };
    CHECK_THAT(norm_value_at(0.0), Catch::Matchers::WithinAbs(tone_state_value, 1e-5));
    CHECK(norm_value_at(1.0) > tone_state_value);
    CHECK(norm_value_at(1.0) < authored_value);
    CHECK_THAT(norm_value_at(2.0), Catch::Matchers::WithinAbs(authored_value, 1e-5));
}

TEST_CASE(
    "The anchor reads the tone state, not the value the curve is driving",
    "[audio][tone-automation]")
{
    const ToneAutomationHarness harness;
    const tracktion::Plugin::Ptr plugin = createChainStandIn(*harness.edit);
    const tracktion::AutomatableParameter::Ptr parameter = firstParameter(*plugin);
    REQUIRE(parameter != nullptr);

    constexpr float tone_state_value = 0.2F;
    constexpr float authored_value = 0.9F;
    parameter->setNormalisedParameter(tone_state_value, juce::dontSendNotification);

    const std::string param_id = parameter->paramID.toStdString();
    const std::vector<AutomationCurvePoint> points{
        AutomationCurvePoint{.seconds = 2.0, .norm_value = authored_value},
    };
    REQUIRE(writePluginParameterCurve(*plugin, param_id, points));

    // Drive the PLAYED value away from the tone state's the way playback does — the curve reaches
    // its authored value at 2 s while the tone state still says what the knob was set to. Nothing
    // else in this suite separates the two, because setNormalisedParameter writes both.
    parameter->updateToFollowCurve(tracktion::TimePosition::fromSeconds(2.0));
    CHECK_THAT(
        parameter->valueRange.convertTo0to1(parameter->getCurrentValue()),
        Catch::Matchers::WithinAbs(authored_value, 1e-5));

    // The read every lane anchors on is unmoved. This is the whole point of sourcing it from the
    // explicit value: taken from getCurrentValue() it would report the curve's 0.9 here, and the
    // lane would draw its start wherever the playhead happened to be.
    const std::optional<float> baseline = readPluginParameterBaselineNormValue(*plugin, param_id);
    REQUIRE(baseline.has_value());
    if (baseline.has_value())
    {
        CHECK_THAT(*baseline, Catch::Matchers::WithinAbs(tone_state_value, 1e-5));
    }

    // And a rewrite in that state still anchors on the tone state, so a points edit made mid-
    // playback cannot bake the played value in as the lane's start.
    REQUIRE(writePluginParameterCurve(*plugin, param_id, points));
    const std::optional<std::vector<AutomationCurvePoint>> read_back =
        readPluginParameterCurve(*plugin, param_id);
    REQUIRE(read_back.has_value());
    if (read_back.has_value())
    {
        REQUIRE(read_back->size() == 2);
        CHECK(read_back->front().norm_value == Catch::Approx(tone_state_value));
    }
}

TEST_CASE(
    "A lane with one authored point reaches the audio-thread automation stream",
    "[audio][tone-automation]")
{
    const ToneAutomationHarness harness;
    const tracktion::Plugin::Ptr plugin = createChainStandIn(*harness.edit);
    const tracktion::AutomatableParameter::Ptr parameter = firstParameter(*plugin);
    REQUIRE(parameter != nullptr);

    const std::vector<AutomationCurvePoint> points{
        AutomationCurvePoint{.seconds = 2.0, .norm_value = 0.9F},
    };
    REQUIRE(writePluginParameterCurve(*plugin, parameter->paramID.toStdString(), points));

    // The stream Tracktion builds for playback discards any curve of one point or fewer
    // (AutomationIterator::isEmpty), which is what left a single-point lane silent. The anchor
    // makes the curve two points, so the stream is real and the parameter is genuinely automated.
    const tracktion::AutomationIterator stream{*parameter};
    CHECK_FALSE(stream.isEmpty());

    // Negative control: the same authored point WITHOUT the anchor is exactly what gets discarded,
    // so the assertion above is discriminating rather than always true.
    tracktion::AutomationCurve& curve = parameter->getCurve();
    curve.clear(nullptr);
    curve.addPoint(
        tracktion::TimePosition::fromSeconds(2.0),
        parameter->valueRange.convertFrom0to1(0.9F),
        0.0F,
        nullptr);
    const tracktion::AutomationIterator unanchored{*parameter};
    CHECK(unanchored.isEmpty());
}

TEST_CASE(
    "A lane authored only at the origin stays a two-point constant", "[audio][tone-automation]")
{
    const ToneAutomationHarness harness;
    const tracktion::Plugin::Ptr plugin = createChainStandIn(*harness.edit);
    const tracktion::AutomatableParameter::Ptr parameter = firstParameter(*plugin);
    REQUIRE(parameter != nullptr);

    // Dragging the anchor authors a real point at the lane's start, which then states the lane's
    // value there instead of the tone state's. The anchor still writes, on the same slot, so the
    // constant survives the stream's two-point requirement.
    constexpr float tone_state_value = 0.2F;
    constexpr float authored_value = 0.9F;
    parameter->setNormalisedParameter(tone_state_value, juce::dontSendNotification);

    const std::vector<AutomationCurvePoint> points{
        AutomationCurvePoint{.seconds = 0.0, .norm_value = authored_value},
    };
    REQUIRE(writePluginParameterCurve(*plugin, parameter->paramID.toStdString(), points));

    const std::optional<std::vector<AutomationCurvePoint>> read_back =
        readPluginParameterCurve(*plugin, parameter->paramID.toStdString());
    REQUIRE(read_back.has_value());
    if (read_back.has_value())
    {
        REQUIRE(read_back->size() == 2);
        for (const AutomationCurvePoint& point : *read_back)
        {
            CHECK(point.seconds == Catch::Approx(0.0));
            CHECK(point.norm_value == Catch::Approx(authored_value));
        }
    }

    const tracktion::AutomationIterator stream{*parameter};
    CHECK_FALSE(stream.isEmpty());

    const tracktion::AutomationCurve& curve = parameter->getCurve();
    constexpr float unused_default = -1.0F;
    CHECK_THAT(
        parameter->valueRange.convertTo0to1(
            curve.getValueAt(tracktion::TimePosition::fromSeconds(5.0), unused_default)),
        Catch::Matchers::WithinAbs(authored_value, 1e-5));
}

TEST_CASE("Unresolved parameter ids fail cleanly", "[audio][tone-automation]")
{
    const ToneAutomationHarness harness;
    const tracktion::Plugin::Ptr plugin = createChainStandIn(*harness.edit);

    CHECK_FALSE(readPluginParameterCurve(*plugin, "not-a-real-parameter").has_value());

    const std::vector<AutomationCurvePoint> points{
        AutomationCurvePoint{.seconds = 0.0, .norm_value = 0.5F},
    };
    CHECK_FALSE(writePluginParameterCurve(*plugin, "not-a-real-parameter", points));
}

} // namespace rock_hero::common::audio
