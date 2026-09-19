#include "tracktion/tone_branch_gain_plugin.h"

#include <rock_hero/common/core/tone/tone_schedule.h>

namespace rock_hero::common::audio
{

namespace
{

// De-zippers block-rate automation updates only; the audible switch ramp is authored in the baked
// curve, so this must stay below the baked crossfade or the smoother would stretch it. Derived from
// the crossfade's one authority at half its length, which leaves the de-zipper settled well inside
// the ramp instead of trailing it.
constexpr double g_smoothing_ramp_seconds{core::g_tone_switch_ramp_seconds / 2.0};

[[nodiscard]] const juce::Identifier& branchGainProperty()
{
    static const juce::Identifier g_property{"branchGain"};
    return g_property;
}

[[nodiscard]] const juce::Identifier& outputGainDbProperty()
{
    static const juce::Identifier g_property{"outputGainDb"};
    return g_property;
}

// Minimal parameter subclass following the engine's internal-plugin pattern: the subclass exists
// so destruction notifies listeners before the owning plugin's members are torn down.
struct BranchGainParameter final : public tracktion::AutomatableParameter
{
    explicit BranchGainParameter(tracktion::Plugin& owner)
        : tracktion::AutomatableParameter(
              "branchGain", "Branch Gain", owner, juce::Range<float>{0.0f, 1.0f})
    {}

    ~BranchGainParameter() override
    {
        notifyListenersOfDeletion();
    }

    BranchGainParameter(const BranchGainParameter&) = delete;
    BranchGainParameter(BranchGainParameter&&) = delete;
    BranchGainParameter& operator=(const BranchGainParameter&) = delete;
    BranchGainParameter& operator=(BranchGainParameter&&) = delete;
};

// Adopts the new parameter straight into the reference-counted pointer the plugin stores.
[[nodiscard]] tracktion::AutomatableParameter::Ptr makeBranchGainParameter(tracktion::Plugin& owner)
{
    return tracktion::AutomatableParameter::Ptr{new BranchGainParameter{owner}};
}

} // namespace

const char* ToneBranchGainPlugin::xmlTypeName = "rockHeroToneBranchGain";

// Creates the Tracktion plugin state for a tone branch gain stage that starts audible at unity.
juce::ValueTree ToneBranchGainPlugin::createState()
{
    juce::ValueTree state{tracktion::IDs::PLUGIN};
    state.setProperty(tracktion::IDs::type, xmlTypeName, nullptr);
    state.setProperty(branchGainProperty(), 1.0f, nullptr);
    state.setProperty(outputGainDbProperty(), static_cast<float>(defaultGainDb()), nullptr);
    return state;
}

// Wires the automatable gain parameter to the ValueTree-backed branch gain value, and the tone's
// authored level to its own ValueTree-backed value.
ToneBranchGainPlugin::ToneBranchGainPlugin(tracktion::PluginCreationInfo info)
    : tracktion::Plugin{std::move(info)}
    , m_branch_gain_parameter(makeBranchGainParameter(*this))
{
    m_branch_gain.referTo(state, branchGainProperty(), getUndoManager(), 1.0f);
    addAutomatableParameter(m_branch_gain_parameter);
    m_branch_gain_parameter->attachToCurrentValue(m_branch_gain);

    m_output_gain_db.referTo(
        state, outputGainDbProperty(), getUndoManager(), static_cast<float>(defaultGainDb()));
    const Gain initial_gain = clampGain(Gain{static_cast<double>(m_output_gain_db.get())});
    m_output_gain_db = static_cast<float>(initial_gain.db);
    setTargetOutputGainDb(static_cast<float>(initial_gain.db));

    m_smoothed_gain.setCurrentAndTargetValue(m_branch_gain.get() * targetOutputLinearGain());
}

// Detaches the parameter before members are destroyed, then notifies Tracktion listeners.
ToneBranchGainPlugin::~ToneBranchGainPlugin()
{
    notifyListenersOfDeletion();
    m_branch_gain_parameter->detachFromCurrentValue();
}

// Returns the descriptive name used for Tracktion diagnostics.
juce::String ToneBranchGainPlugin::getName() const
{
    return "Rock Hero Tone Branch Gain";
}

// Returns the private plugin type string stored in Tracktion state.
juce::String ToneBranchGainPlugin::getPluginType()
{
    return xmlTypeName;
}

// Returns the plugin vendor for Tracktion diagnostics.
juce::String ToneBranchGainPlugin::getVendor()
{
    return "Rock Hero";
}

// Returns the compact label used in Tracktion plugin lists.
juce::String ToneBranchGainPlugin::getShortName(int suggested_length)
{
    juce::ignoreUnused(suggested_length);
    return "ToneGain";
}

// Returns the selectable description used by Tracktion internals.
juce::String ToneBranchGainPlugin::getSelectableDescription()
{
    return getName();
}

// The branch gain exists to terminate rack branches.
bool ToneBranchGainPlugin::canBeAddedToRack()
{
    return true;
}

// Keeps structural plugin placement under the multi-tone rack adapter.
bool ToneBranchGainPlugin::canBeMoved()
{
    return false;
}

// Avoids per-block CPU timing overhead for this tiny utility plugin.
bool ToneBranchGainPlugin::shouldMeasureCpuUsage() const noexcept
{
    return false;
}

// Preserves the input channel count so the structural node does not alter routing.
int ToneBranchGainPlugin::getNumOutputChannelsGivenInputs(int num_input_channels)
{
    return num_input_channels;
}

// Prepares smoothing and starts from the current parameter value scaled by the tone's level.
void ToneBranchGainPlugin::initialise(const tracktion::PluginInitialisationInfo& info)
{
    m_smoothed_gain.reset(info.sampleRate, g_smoothing_ramp_seconds);
    m_smoothed_gain.setCurrentAndTargetValue(
        m_branch_gain_parameter->getCurrentValue() * targetOutputLinearGain());
}

// Handles Tracktion graph reuse without allocating or mutating graph state.
void ToneBranchGainPlugin::initialiseWithoutStopping(
    const tracktion::PluginInitialisationInfo& info)
{
    juce::ignoreUnused(info);
}

// Releases playback resources. This plugin only owns scalar realtime state.
void ToneBranchGainPlugin::deinitialise()
{}

// Applies the automated branch gain, scaled by this tone's authored level, to every channel with
// per-sample de-zipper smoothing. The parameter stream was already advanced to this block's start
// by applyToBufferWithAutomation. One smoother carries both factors, so a level set while the
// schedule is ramping a switch resolves into the same ramp rather than fighting it.
void ToneBranchGainPlugin::applyToBuffer(const tracktion::PluginRenderContext& context)
{
    if (!isEnabled() || context.destBuffer == nullptr || context.bufferNumSamples <= 0)
    {
        return;
    }

    m_smoothed_gain.setTargetValue(
        m_branch_gain_parameter->getCurrentValue() * targetOutputLinearGain());

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

// Restores the persisted branch gain value; automation curves restore through base plugin state.
//
// The parameter pull is required: a direct CachedValue write deliberately does not refresh the
// attached parameter (tracktion_AutomatableParameter.cpp declines updateParameterFromValue in
// valueTreePropertyChanged), so without it applyToBuffer would keep re-asserting the pre-restore
// value -- Tracktion's own VolumeAndPanPlugin restore does the same pull. The smoother stays
// untouched: it holds four non-atomic fields the audio thread is writing in applyToBuffer, and
// applyToBuffer re-targets from this parameter every block anyway.
void ToneBranchGainPlugin::restorePluginStateFromValueTree(const juce::ValueTree& tree)
{
    tracktion::copyPropertiesToCachedValues(tree, m_branch_gain, m_output_gain_db);
    m_branch_gain_parameter->updateFromAttachedValue();

    // The level needs no parameter pull (it is not attached to one) but does need its realtime
    // mirror refreshed, exactly as LiveRigGainPlugin refreshes its own on restore.
    const Gain restored_gain = clampGain(Gain{static_cast<double>(m_output_gain_db.get())});
    m_output_gain_db = static_cast<float>(restored_gain.db);
    setTargetOutputGainDb(static_cast<float>(restored_gain.db));
}

// Exposes the parameter so the rack adapter can bake schedules and toggle preview bypass.
tracktion::AutomatableParameter::Ptr ToneBranchGainPlugin::branchGainParameter() const
{
    return m_branch_gain_parameter;
}

// Stores the clamped level in Tracktion state and in the audio thread's target memory.
void ToneBranchGainPlugin::setOutputGain(Gain gain)
{
    const Gain clamped_gain = clampGain(gain);
    const auto gain_db = static_cast<float>(clamped_gain.db);
    m_output_gain_db = gain_db;
    setTargetOutputGainDb(gain_db);
    changed();
}

// Returns the latest clamped level visible to both message and audio threads.
Gain ToneBranchGainPlugin::outputGain() const noexcept
{
    return clampGain(
        Gain{static_cast<double>(m_target_output_gain_db.load(std::memory_order_acquire))});
}

// Converts the latest target level to linear gain for audio processing.
float ToneBranchGainPlugin::targetOutputLinearGain() const noexcept
{
    return juce::Decibels::decibelsToGain(m_target_output_gain_db.load(std::memory_order_acquire));
}

// Stores the audio-thread target level in dB.
void ToneBranchGainPlugin::setTargetOutputGainDb(float gain_db) noexcept
{
    m_target_output_gain_db.store(gain_db, std::memory_order_release);
}

// Keeps the realtime target synchronized when Tracktion undo mutates the backing ValueTree
// directly, exactly as the rig's own gain stages do.
void ToneBranchGainPlugin::valueTreePropertyChanged(
    juce::ValueTree& changed_tree, const juce::Identifier& changed_property)
{
    if (changed_tree == state && changed_property == outputGainDbProperty())
    {
        m_output_gain_db.forceUpdateOfCachedValue();
        const Gain changed_gain = clampGain(Gain{static_cast<double>(m_output_gain_db.get())});
        setTargetOutputGainDb(static_cast<float>(changed_gain.db));
    }

    tracktion::Plugin::valueTreePropertyChanged(changed_tree, changed_property);
}

} // namespace rock_hero::common::audio
