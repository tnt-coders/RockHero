#include "tracktion/tone_automation_curve.h"
#include "tracktion/tone_output_plugin.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Owns the minimum Tracktion objects needed to construct a private plugin.
struct ToneOutputPluginHarness
{
    juce::ScopedJuceInitialiser_GUI gui;
    tracktion::Engine engine{"RockHeroToneOutputPluginTest"};
    std::unique_ptr<tracktion::Edit> edit{tracktion::Edit::createSingleTrackEdit(engine)};
};

// Creates the private plugin with default Tracktion state.
[[nodiscard]] tracktion::Plugin::Ptr createTestPlugin(tracktion::Edit& edit)
{
    const juce::ValueTree state = ToneOutputPlugin::createState();
    tracktion::EditItemID::readOrCreateNewID(edit, state);
    return tracktion::Plugin::Ptr{
        new ToneOutputPlugin{tracktion::PluginCreationInfo{edit, state, true}}
    };
}

// Builds a render context for processing a deterministic audio block.
[[nodiscard]] tracktion::PluginRenderContext renderContext(
    juce::AudioBuffer<float>& buffer, tracktion::MidiMessageArray& midi_messages)
{
    return tracktion::PluginRenderContext{
        &buffer,
        juce::AudioChannelSet::stereo(),
        0,
        buffer.getNumSamples(),
        &midi_messages,
        0.0,
        tracktion::TimeRange{
            tracktion::TimePosition::fromSeconds(0.0),
            tracktion::TimeDuration::fromSeconds(
                static_cast<double>(buffer.getNumSamples()) / 48'000.0),
        },
        true,
        false,
        false,
        false,
    };
}

// Fills every channel with unity samples and runs enough blocks for the de-zipper (half the
// tone-switch ramp, shorter than one 480-sample block at 48 kHz) to settle on the target.
void processSettledUnityBlocks(ToneOutputPlugin& plugin, juce::AudioBuffer<float>& buffer)
{
    tracktion::MidiMessageArray midi_messages;
    for (int block = 0; block < 2; ++block)
    {
        buffer.clear();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            float* const samples = buffer.getWritePointer(channel);
            for (int index = 0; index < buffer.getNumSamples(); ++index)
            {
                samples[index] = 1.0f;
            }
        }
        plugin.applyToBuffer(renderContext(buffer, midi_messages));
    }
}

// Prepares the plugin at the test sample rate so its smoothers run.
void initialiseAt48k(ToneOutputPlugin& plugin)
{
    plugin.initialise(
        tracktion::PluginInitialisationInfo{
            .startTime = tracktion::TimePosition::fromSeconds(0.0),
            .sampleRate = 48'000.0,
            .blockSizeSamples = 480,
        });
}

} // namespace

// Verifies the stage exposes exactly its two parameters, with the shared ranges and defaults, as
// the automation listing the picker consumes.
TEST_CASE("ToneOutputPlugin lists Volume and Pan as its only parameters", "[audio][tone-output]")
{
    const ToneOutputPluginHarness harness;
    const tracktion::Plugin::Ptr plugin = createTestPlugin(*harness.edit);
    auto* const output = dynamic_cast<ToneOutputPlugin*>(plugin.get());
    REQUIRE(output != nullptr);
    CHECK(plugin->canBeAddedToRack());
    CHECK(tracktion::RackType::isPluginAllowed(plugin));
    CHECK(output->output() == ToneOutput{});

    const std::vector<AutomatableParamInfo> parameters = listPluginAutomatableParameters(*plugin);
    REQUIRE(parameters.size() == 2);
    CHECK(parameters[0].param_id == ToneOutputPlugin::g_level_parameter_id);
    CHECK(parameters[0].name == "Volume");
    CHECK(parameters[0].plugin_name == "Output");
    CHECK_FALSE(parameters[0].is_discrete);
    // 0 dB sits at the midpoint of the symmetric gain range.
    CHECK(parameters[0].default_norm_value == Catch::Approx(0.5f));
    CHECK(parameters[0].baseline_norm_value == Catch::Approx(0.5f));
    CHECK(parameters[1].param_id == ToneOutputPlugin::g_pan_parameter_id);
    CHECK(parameters[1].name == "Pan");
    CHECK(parameters[1].default_norm_value == Catch::Approx(0.5f));
    CHECK(parameters[1].baseline_norm_value == Catch::Approx(0.5f));
}

// Verifies the stored value is the parameters' explicit value, so a playing curve moves the live
// value without rewriting what the tone stores, and the lane readouts use the shared formats.
TEST_CASE(
    "ToneOutputPlugin keeps its stored stage apart from a playing curve", "[audio][tone-output]")
{
    const ToneOutputPluginHarness harness;
    const tracktion::Plugin::Ptr plugin = createTestPlugin(*harness.edit);
    auto* const output = dynamic_cast<ToneOutputPlugin*>(plugin.get());
    REQUIRE(output != nullptr);

    output->setOutput(ToneOutput{.level = Gain{-6.0}, .pan = Pan{-0.25}});
    CHECK(output->output() == ToneOutput{.level = Gain{-6.0}, .pan = Pan{-0.25}});
    CHECK(
        formatPluginParameterValue(*plugin, ToneOutputPlugin::g_level_parameter_id, 0.5f) ==
        std::optional<std::string>{"0.0 dB"});
    CHECK(
        formatPluginParameterValue(*plugin, ToneOutputPlugin::g_pan_parameter_id, 0.25f) ==
        std::optional<std::string>{"L50"});
    CHECK(
        parsePluginParameterValue(*plugin, ToneOutputPlugin::g_pan_parameter_id, "R100") ==
        std::optional{1.0f});

    const tracktion::AutomatableParameter::Ptr pan =
        plugin->getAutomatableParameterByID(ToneOutputPlugin::g_pan_parameter_id);
    REQUIRE(pan != nullptr);
    tracktion::AutomationCurve& curve = pan->getCurve();
    curve.addPoint(tracktion::TimePosition::fromSeconds(0.0), 1.0f, 0.0f, nullptr);
    curve.addPoint(tracktion::TimePosition::fromSeconds(2.0), 1.0f, 0.0f, nullptr);
    pan->updateToFollowCurve(tracktion::TimePosition::fromSeconds(1.0));
    CHECK(pan->getCurrentValue() == Catch::Approx(1.0f));
    CHECK(output->output().pan == Pan{-0.25});
}

// Verifies the balanced law: centre leaves both channels at the level, a hard pan silences only
// the far channel and leaves the near one at the level.
TEST_CASE("ToneOutputPlugin applies level and a balanced pan", "[audio][tone-output]")
{
    const ToneOutputPluginHarness harness;
    const tracktion::Plugin::Ptr plugin = createTestPlugin(*harness.edit);
    auto* const output = dynamic_cast<ToneOutputPlugin*>(plugin.get());
    REQUIRE(output != nullptr);
    initialiseAt48k(*output);
    juce::AudioBuffer<float> buffer{2, 480};
    const int last_sample = buffer.getNumSamples() - 1;

    output->setOutput(ToneOutput{.level = Gain{-6.0}, .pan = Pan{}});
    processSettledUnityBlocks(*output, buffer);
    const float half = juce::Decibels::decibelsToGain(-6.0f);
    CHECK(buffer.getSample(0, last_sample) == Catch::Approx(half).margin(0.001));
    CHECK(buffer.getSample(1, last_sample) == Catch::Approx(half).margin(0.001));

    output->setOutput(ToneOutput{.level = Gain{-6.0}, .pan = Pan{1.0}});
    processSettledUnityBlocks(*output, buffer);
    CHECK(buffer.getSample(0, last_sample) == Catch::Approx(0.0f).margin(0.001));
    CHECK(buffer.getSample(1, last_sample) == Catch::Approx(half).margin(0.001));

    output->setOutput(ToneOutput{.level = Gain{0.0}, .pan = Pan{-0.5}});
    processSettledUnityBlocks(*output, buffer);
    CHECK(buffer.getSample(0, last_sample) == Catch::Approx(1.0f).margin(0.001));
    CHECK(buffer.getSample(1, last_sample) == Catch::Approx(0.5f).margin(0.001));
}

} // namespace rock_hero::common::audio
