#include "tracktion/live_rig_gain_plugin.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>

namespace rock_hero::common::audio
{

namespace
{

// Owns the minimum Tracktion objects needed to construct a private plugin.
struct LiveRigGainPluginHarness
{
    juce::ScopedJuceInitialiser_GUI gui;
    tracktion::Engine engine{"RockHeroLiveRigGainPluginTest"};
    std::unique_ptr<tracktion::Edit> edit{tracktion::Edit::createSingleTrackEdit(engine)};
};

// Creates the private plugin with default Tracktion state.
[[nodiscard]] tracktion::Plugin::Ptr createTestPlugin(tracktion::Edit& edit)
{
    const juce::ValueTree state = LiveRigGainPlugin::createState();
    tracktion::EditItemID::readOrCreateNewID(edit, state);
    return tracktion::Plugin::Ptr{
        new LiveRigGainPlugin{tracktion::PluginCreationInfo{edit, state, true}}
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

// Verifies the plugin stores +24 dB without inheriting Tracktion's +6 dB fader cap.
TEST_CASE("LiveRigGainPlugin stores plus twenty four dB", "[audio][live-rig-gain-plugin]")
{
    const LiveRigGainPluginHarness harness;
    const tracktion::Plugin::Ptr plugin = createTestPlugin(*harness.edit);
    auto* const gain_plugin = dynamic_cast<LiveRigGainPlugin*>(plugin.get());
    REQUIRE(gain_plugin != nullptr);

    gain_plugin->setGain(Gain{24.0});

    CHECK(gain_plugin->gain().db == Catch::Approx(24.0));
}

// Verifies the plugin clamps values through the shared project-owned gain policy.
TEST_CASE("LiveRigGainPlugin clamps gain", "[audio][live-rig-gain-plugin]")
{
    const LiveRigGainPluginHarness harness;
    const tracktion::Plugin::Ptr plugin = createTestPlugin(*harness.edit);
    auto* const gain_plugin = dynamic_cast<LiveRigGainPlugin*>(plugin.get());
    REQUIRE(gain_plugin != nullptr);

    gain_plugin->setGain(Gain{25.0});
    CHECK(gain_plugin->gain().db == Catch::Approx(maximumGainDb()));

    gain_plugin->setGain(Gain{-25.0});
    CHECK(gain_plugin->gain().db == Catch::Approx(minimumGainDb()));
}

// Verifies Tracktion undo/redo refresh both CachedValue state and the realtime atomic target.
TEST_CASE("LiveRigGainPlugin tracks undo-restored gain", "[audio][live-rig-gain-plugin]")
{
    const LiveRigGainPluginHarness harness;
    const tracktion::Plugin::Ptr plugin = createTestPlugin(*harness.edit);
    auto* const gain_plugin = dynamic_cast<LiveRigGainPlugin*>(plugin.get());
    REQUIRE(gain_plugin != nullptr);

    juce::UndoManager& undo_manager = harness.edit->getUndoManager();
    undo_manager.clearUndoHistory();
    undo_manager.beginNewTransaction("live-rig-gain");

    gain_plugin->setGain(Gain{-4.0});
    REQUIRE(gain_plugin->gain().db == Catch::Approx(-4.0));

    CHECK(undo_manager.undo());
    CHECK(gain_plugin->gain().db == Catch::Approx(defaultGainDb()));

    CHECK(undo_manager.redo());
    CHECK(gain_plugin->gain().db == Catch::Approx(-4.0));
}

// Verifies +24 dB processing applies the expected linear multiplier to every channel.
TEST_CASE("LiveRigGainPlugin processes plus twenty four dB", "[audio][live-rig-gain-plugin]")
{
    const LiveRigGainPluginHarness harness;
    const tracktion::Plugin::Ptr plugin = createTestPlugin(*harness.edit);
    auto* const gain_plugin = dynamic_cast<LiveRigGainPlugin*>(plugin.get());
    REQUIRE(gain_plugin != nullptr);

    gain_plugin->setGain(Gain{24.0});
    gain_plugin->initialise(
        tracktion::PluginInitialisationInfo{
            .startTime = tracktion::TimePosition::fromSeconds(0.0),
            .sampleRate = 48'000.0,
            .blockSizeSamples = 4,
        });

    juce::AudioBuffer<float> buffer{2, 4};
    buffer.setSample(0, 0, 0.025f);
    buffer.setSample(0, 1, -0.05f);
    buffer.setSample(0, 2, 0.0125f);
    buffer.setSample(0, 3, -0.025f);
    buffer.setSample(1, 0, -0.025f);
    buffer.setSample(1, 1, 0.05f);
    buffer.setSample(1, 2, -0.0125f);
    buffer.setSample(1, 3, 0.025f);

    const juce::AudioBuffer<float> original_buffer{buffer};
    tracktion::MidiMessageArray midi_messages;
    const tracktion::PluginRenderContext context = renderContext(buffer, midi_messages);
    gain_plugin->applyToBuffer(context);

    const float expected_gain = juce::Decibels::decibelsToGain(24.0f);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            CHECK(
                buffer.getSample(channel, sample) ==
                Catch::Approx(original_buffer.getSample(channel, sample) * expected_gain));
        }
    }
}

} // namespace rock_hero::common::audio
