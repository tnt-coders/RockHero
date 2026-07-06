#include "tracktion/tone_branch_gain_plugin.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>

namespace rock_hero::common::audio
{

namespace
{

// Owns the minimum Tracktion objects needed to construct a private plugin.
struct ToneBranchGainPluginHarness
{
    juce::ScopedJuceInitialiser_GUI gui;
    tracktion::Engine engine{"RockHeroToneBranchGainPluginTest"};
    std::unique_ptr<tracktion::Edit> edit{tracktion::Edit::createSingleTrackEdit(engine)};
};

// Creates the private plugin with default Tracktion state.
[[nodiscard]] tracktion::Plugin::Ptr createTestPlugin(tracktion::Edit& edit)
{
    const juce::ValueTree state = ToneBranchGainPlugin::createState();
    tracktion::EditItemID::readOrCreateNewID(edit, state);
    return tracktion::Plugin::Ptr{
        new ToneBranchGainPlugin{tracktion::PluginCreationInfo{edit, state, true}}
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

} // namespace

// Verifies the switch parameter is exposed with full-branch-audibility defaults.
TEST_CASE(
    "ToneBranchGainPlugin exposes an automatable zero-to-one gain",
    "[audio][tone-branch-gain-plugin]")
{
    const ToneBranchGainPluginHarness harness;
    const tracktion::Plugin::Ptr plugin = createTestPlugin(*harness.edit);
    auto* const branch_gain = dynamic_cast<ToneBranchGainPlugin*>(plugin.get());
    REQUIRE(branch_gain != nullptr);

    const tracktion::AutomatableParameter::Ptr parameter = branch_gain->branchGainParameter();
    REQUIRE(parameter != nullptr);
    CHECK(parameter->getCurrentValue() == Catch::Approx(1.0f));
    CHECK(parameter->getValueRange().getStart() == Catch::Approx(0.0f));
    CHECK(parameter->getValueRange().getEnd() == Catch::Approx(1.0f));
    CHECK(plugin->canBeAddedToRack());
    CHECK(tracktion::RackType::isPluginAllowed(plugin));
}

// Verifies the parameter follows baked curve points, interpolating between them.
TEST_CASE(
    "ToneBranchGainPlugin follows automation curve points", "[audio][tone-branch-gain-plugin]")
{
    const ToneBranchGainPluginHarness harness;
    const tracktion::Plugin::Ptr plugin = createTestPlugin(*harness.edit);
    auto* const branch_gain = dynamic_cast<ToneBranchGainPlugin*>(plugin.get());
    REQUIRE(branch_gain != nullptr);

    const tracktion::AutomatableParameter::Ptr parameter = branch_gain->branchGainParameter();
    tracktion::AutomationCurve& curve = parameter->getCurve();
    curve.addPoint(tracktion::TimePosition::fromSeconds(1.0), 1.0f, 0.0f, nullptr);
    curve.addPoint(tracktion::TimePosition::fromSeconds(2.0), 0.0f, 0.0f, nullptr);

    parameter->updateToFollowCurve(tracktion::TimePosition::fromSeconds(1.0));
    CHECK(parameter->getCurrentValue() == Catch::Approx(1.0f));

    parameter->updateToFollowCurve(tracktion::TimePosition::fromSeconds(1.5));
    CHECK(parameter->getCurrentValue() == Catch::Approx(0.5f).margin(0.001));

    parameter->updateToFollowCurve(tracktion::TimePosition::fromSeconds(2.0));
    CHECK(parameter->getCurrentValue() == Catch::Approx(0.0f).margin(0.001));
}

// Verifies curve bypass releases the parameter to directly-set preview values.
TEST_CASE(
    "ToneBranchGainPlugin curve bypass enables direct preview values",
    "[audio][tone-branch-gain-plugin]")
{
    const ToneBranchGainPluginHarness harness;
    const tracktion::Plugin::Ptr plugin = createTestPlugin(*harness.edit);
    auto* const branch_gain = dynamic_cast<ToneBranchGainPlugin*>(plugin.get());
    REQUIRE(branch_gain != nullptr);

    const tracktion::AutomatableParameter::Ptr parameter = branch_gain->branchGainParameter();
    tracktion::AutomationCurve& curve = parameter->getCurve();
    curve.addPoint(tracktion::TimePosition::fromSeconds(0.0), 0.0f, 0.0f, nullptr);
    curve.addPoint(tracktion::TimePosition::fromSeconds(10.0), 0.0f, 0.0f, nullptr);

    parameter->updateToFollowCurve(tracktion::TimePosition::fromSeconds(5.0));
    CHECK(parameter->getCurrentValue() == Catch::Approx(0.0f));

    curve.bypass = true;
    parameter->setParameter(1.0f, juce::dontSendNotification);
    parameter->updateToFollowCurve(tracktion::TimePosition::fromSeconds(5.0));
    CHECK(parameter->getCurrentValue() == Catch::Approx(1.0f));

    curve.bypass = false;
    parameter->updateToFollowCurve(tracktion::TimePosition::fromSeconds(5.0));
    CHECK(parameter->getCurrentValue() == Catch::Approx(0.0f));
}

// Verifies buffer processing scales samples by the current parameter value.
TEST_CASE("ToneBranchGainPlugin scales audio buffers", "[audio][tone-branch-gain-plugin]")
{
    const ToneBranchGainPluginHarness harness;
    const tracktion::Plugin::Ptr plugin = createTestPlugin(*harness.edit);
    auto* const branch_gain = dynamic_cast<ToneBranchGainPlugin*>(plugin.get());
    REQUIRE(branch_gain != nullptr);

    branch_gain->initialise(
        tracktion::PluginInitialisationInfo{
            .startTime = tracktion::TimePosition::fromSeconds(0.0),
            .sampleRate = 48'000.0,
            .blockSizeSamples = 480,
        });

    const tracktion::AutomatableParameter::Ptr parameter = branch_gain->branchGainParameter();
    parameter->setParameter(0.25f, juce::dontSendNotification);

    // The 5 ms de-zipper needs 240 samples at 48 kHz to reach a new target, so process one
    // settling block before the block under test.
    juce::AudioBuffer<float> buffer{2, 480};
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
        branch_gain->applyToBuffer(renderContext(buffer, midi_messages));
    }

    const int last_sample = buffer.getNumSamples() - 1;
    CHECK(buffer.getSample(0, last_sample) == Catch::Approx(0.25f).margin(0.001));
    CHECK(buffer.getSample(1, last_sample) == Catch::Approx(0.25f).margin(0.001));
}

} // namespace rock_hero::common::audio
