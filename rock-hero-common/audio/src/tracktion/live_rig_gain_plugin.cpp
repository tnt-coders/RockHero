#include "tracktion/live_rig_gain_plugin.h"

#include <cmath>

namespace rock_hero::common::audio
{

namespace
{

constexpr double g_smoothing_ramp_seconds{0.02};
constexpr float g_linear_gain_change_epsilon{0.000001f};

[[nodiscard]] const juce::Identifier& gainDbProperty()
{
    static const juce::Identifier g_property{"gainDb"};
    return g_property;
}

} // namespace

const char* LiveRigGainPlugin::xmlTypeName = "rockHeroLiveRigGain";

// Creates the Tracktion plugin state for a hidden live-rig gain stage.
juce::ValueTree LiveRigGainPlugin::createState()
{
    juce::ValueTree state{tracktion::IDs::PLUGIN};
    state.setProperty(tracktion::IDs::type, xmlTypeName, nullptr);
    state.setProperty(gainDbProperty(), static_cast<float>(defaultGainDb()), nullptr);
    return state;
}

// Wires CachedValue state to the Tracktion ValueTree and prepares a realtime gain target.
LiveRigGainPlugin::LiveRigGainPlugin(tracktion::PluginCreationInfo info)
    : tracktion::Plugin{std::move(info)}
{
    m_gain_db.referTo(
        state, gainDbProperty(), getUndoManager(), static_cast<float>(defaultGainDb()));

    const Gain initial_gain = clampGain(Gain{static_cast<double>(m_gain_db.get())});
    m_gain_db = static_cast<float>(initial_gain.db);
    setTargetGainDb(static_cast<float>(initial_gain.db));
    const float initial_linear_gain = targetLinearGain();
    setSmoothedGainTarget(initial_linear_gain);
    m_smoothed_gain.setCurrentAndTargetValue(initial_linear_gain);
}

// Notifies Tracktion listeners before destruction.
LiveRigGainPlugin::~LiveRigGainPlugin()
{
    notifyListenersOfDeletion();
}

// Returns the descriptive name used for Tracktion diagnostics.
juce::String LiveRigGainPlugin::getName() const
{
    return "Rock Hero Live Rig Gain";
}

// Returns the private plugin type string stored in Tracktion state.
juce::String LiveRigGainPlugin::getPluginType()
{
    return xmlTypeName;
}

// Returns the plugin vendor for Tracktion diagnostics.
juce::String LiveRigGainPlugin::getVendor()
{
    return "Rock Hero";
}

// Returns the compact label used in Tracktion plugin lists.
juce::String LiveRigGainPlugin::getShortName(int suggested_length)
{
    juce::ignoreUnused(suggested_length);
    return "LiveGain";
}

// Returns the selectable description used by Tracktion internals.
juce::String LiveRigGainPlugin::getSelectableDescription()
{
    return getName();
}

// Keeps this private structural plugin out of rack workflows.
bool LiveRigGainPlugin::canBeAddedToRack()
{
    return false;
}

// Keeps structural plugin placement under the live-rig adapter.
bool LiveRigGainPlugin::canBeMoved()
{
    return false;
}

// Avoids per-block CPU timing overhead for this tiny utility plugin.
bool LiveRigGainPlugin::shouldMeasureCpuUsage() const noexcept
{
    return false;
}

// Preserves the input channel count so the structural node does not alter routing.
int LiveRigGainPlugin::getNumOutputChannelsGivenInputs(int num_input_channels)
{
    return num_input_channels;
}

// Prepares smoothing and starts from the current gain target.
void LiveRigGainPlugin::initialise(const tracktion::PluginInitialisationInfo& info)
{
    m_smoothed_gain.reset(info.sampleRate, g_smoothing_ramp_seconds);
    m_last_target_linear_gain = targetLinearGain();
    m_smoothed_gain.setCurrentAndTargetValue(m_last_target_linear_gain);
}

// Handles Tracktion graph reuse without allocating or mutating graph state.
void LiveRigGainPlugin::initialiseWithoutStopping(const tracktion::PluginInitialisationInfo& info)
{
    juce::ignoreUnused(info);
}

// Releases playback resources. This plugin only owns scalar realtime state.
void LiveRigGainPlugin::deinitialise()
{}

// Applies the same gain ramp to every channel while advancing the smoother once per block.
void LiveRigGainPlugin::applyToBuffer(const tracktion::PluginRenderContext& context)
{
    if (!isEnabled() || context.destBuffer == nullptr || context.bufferNumSamples <= 0)
    {
        return;
    }

    const float linear_gain = targetLinearGain();
    setSmoothedGainTarget(linear_gain);

    juce::AudioBuffer<float>& buffer = *context.destBuffer;
    const int num_channels = buffer.getNumChannels();
    if (num_channels <= 0)
    {
        m_smoothed_gain.skip(context.bufferNumSamples);
        return;
    }

    const auto gain_state = m_smoothed_gain;
    for (int channel = 0; channel < num_channels; ++channel)
    {
        auto channel_gain = gain_state;
        channel_gain.applyGain(
            buffer.getWritePointer(channel, context.bufferStartSample), context.bufferNumSamples);
    }
    m_smoothed_gain.skip(context.bufferNumSamples);
}

// Restores the persisted dB value and refreshes the realtime target.
void LiveRigGainPlugin::restorePluginStateFromValueTree(const juce::ValueTree& tree)
{
    tracktion::copyPropertiesToCachedValues(tree, m_gain_db);

    const Gain restored_gain = clampGain(Gain{static_cast<double>(m_gain_db.get())});
    m_gain_db = static_cast<float>(restored_gain.db);
    setTargetGainDb(static_cast<float>(restored_gain.db));
    setSmoothedGainTarget(targetLinearGain());
}

// Stores the clamped dB value in Tracktion state and realtime target memory.
void LiveRigGainPlugin::setGain(Gain gain)
{
    const Gain clamped_gain = clampGain(gain);
    const auto gain_db = static_cast<float>(clamped_gain.db);
    m_gain_db = gain_db;
    setTargetGainDb(gain_db);
    changed();
}

// Returns the latest clamped dB value visible to both message and audio threads.
Gain LiveRigGainPlugin::gain() const noexcept
{
    return clampGain(Gain{static_cast<double>(m_target_gain_db.load(std::memory_order_acquire))});
}

// Converts the latest target dB value to linear gain for audio processing.
float LiveRigGainPlugin::targetLinearGain() const noexcept
{
    const float gain_db = m_target_gain_db.load(std::memory_order_acquire);
    return juce::Decibels::decibelsToGain(gain_db);
}

// Stores the audio-thread target gain in dB.
void LiveRigGainPlugin::setTargetGainDb(float gain_db) noexcept
{
    m_target_gain_db.store(gain_db, std::memory_order_release);
}

// Updates the smoother target only when the value has changed meaningfully.
void LiveRigGainPlugin::setSmoothedGainTarget(float linear_gain) noexcept
{
    if (std::abs(linear_gain - m_last_target_linear_gain) <= g_linear_gain_change_epsilon)
    {
        return;
    }

    m_smoothed_gain.setTargetValue(linear_gain);
    m_last_target_linear_gain = linear_gain;
}

// Keeps realtime state synchronized when Tracktion undo mutates the backing ValueTree directly.
void LiveRigGainPlugin::valueTreePropertyChanged(
    juce::ValueTree& changed_tree, const juce::Identifier& changed_property)
{
    if (changed_tree == state && changed_property == gainDbProperty())
    {
        m_gain_db.forceUpdateOfCachedValue();
        const Gain changed_gain = clampGain(Gain{static_cast<double>(m_gain_db.get())});
        setTargetGainDb(static_cast<float>(changed_gain.db));
        setSmoothedGainTarget(targetLinearGain());
    }

    tracktion::Plugin::valueTreePropertyChanged(changed_tree, changed_property);
}

} // namespace rock_hero::common::audio
