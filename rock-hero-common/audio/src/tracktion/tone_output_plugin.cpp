#include "tracktion/tone_output_plugin.h"

#include <algorithm>
#include <rock_hero/common/core/tone/tone_schedule.h>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// De-zippers block-rate parameter updates only, at the same length the branch gain uses so a level
// change during a switch settles inside the baked crossfade rather than trailing it.
constexpr double g_smoothing_ramp_seconds{core::g_tone_switch_ramp_seconds / 2.0};

// Minimal parameter subclass following the engine's internal-plugin pattern: destruction notifies
// listeners before the owning plugin's members are torn down, and the default is answered here
// because the base reads it from an attached value, which these parameters deliberately lack.
class ToneOutputParameter final : public tracktion::AutomatableParameter
{
public:
    ToneOutputParameter(
        const char* param_id, const char* name, tracktion::Plugin& owner, juce::Range<float> range,
        float default_value, juce::String label)
        : tracktion::AutomatableParameter(param_id, name, owner, range)
        , m_default_value{default_value}
        , m_label{std::move(label)}
    {}

    ~ToneOutputParameter() override
    {
        notifyListenersOfDeletion();
    }

    ToneOutputParameter(const ToneOutputParameter&) = delete;
    ToneOutputParameter(ToneOutputParameter&&) = delete;
    ToneOutputParameter& operator=(const ToneOutputParameter&) = delete;
    ToneOutputParameter& operator=(ToneOutputParameter&&) = delete;

    [[nodiscard]] std::optional<float> getDefaultValue() const override
    {
        return m_default_value;
    }

    [[nodiscard]] juce::String getLabel() override
    {
        return m_label;
    }

private:
    float m_default_value;
    juce::String m_label;
};

// Level in dB over the shared gain range. Its text form is the fader's: one decimal plus the dB
// label the base class appends.
[[nodiscard]] tracktion::AutomatableParameter::Ptr makeLevelParameter(tracktion::Plugin& owner)
{
    auto* const parameter = new ToneOutputParameter{
        ToneOutputPlugin::g_level_parameter_id,
        "Volume",
        owner,
        juce::Range<float>{
            static_cast<float>(minimumGainDb()), static_cast<float>(maximumGainDb())
        },
        static_cast<float>(defaultGainDb()),
        "dB",
    };
    parameter->valueToStringFunction = [](float value) { return juce::String{value, 1}; };
    return tracktion::AutomatableParameter::Ptr{parameter};
}

// Pan position over the shared pan range, read and written in the one C / L35 / R100 form.
[[nodiscard]] tracktion::AutomatableParameter::Ptr makePanParameter(tracktion::Plugin& owner)
{
    auto* const parameter = new ToneOutputParameter{
        ToneOutputPlugin::g_pan_parameter_id,
        "Pan",
        owner,
        juce::Range<float>{
            static_cast<float>(minimumPanPosition()), static_cast<float>(maximumPanPosition())
        },
        0.0f,
        {},
    };
    parameter->valueToStringFunction = [](float value) {
        return juce::String{formatPan(Pan{static_cast<double>(value)})};
    };
    parameter->stringToValueFunction = [](const juce::String& text) {
        // An unparseable entry is refused by resting at centre rather than at some arbitrary
        // numeric read of the text; the lane's typed entry clamps whatever comes back.
        return static_cast<float>(parsePan(text.toStdString()).value_or(Pan{}).position);
    };
    return tracktion::AutomatableParameter::Ptr{parameter};
}

// Exactly juce::dsp::PannerRule::balanced (juce_Panner.cpp, Panner::update): the near channel
// stays at unity and the far one attenuates linearly to silence at full deflection. Restated
// because Panner keeps its gains private and hard-codes a 50 ms ramp in reset(), five times the
// tone-switch crossfade this stage must settle inside.
[[nodiscard]] constexpr float leftPanGain(float pan) noexcept
{
    return std::min(1.0f, 1.0f - pan);
}

[[nodiscard]] constexpr float rightPanGain(float pan) noexcept
{
    return std::min(1.0f, 1.0f + pan);
}

} // namespace

const char* ToneOutputPlugin::xmlTypeName = "rockHeroToneOutput";

// Creates the Tracktion plugin state for a tone output stage; the values live on the parameters.
juce::ValueTree ToneOutputPlugin::createState()
{
    juce::ValueTree state{tracktion::IDs::PLUGIN};
    state.setProperty(tracktion::IDs::type, xmlTypeName, nullptr);
    return state;
}

// Registers both parameters so Tracktion evaluates their curves during playback.
ToneOutputPlugin::ToneOutputPlugin(tracktion::PluginCreationInfo info)
    : tracktion::Plugin{std::move(info)}
    , m_level_parameter(makeLevelParameter(*this))
    , m_pan_parameter(makePanParameter(*this))
{
    addAutomatableParameter(m_level_parameter);
    addAutomatableParameter(m_pan_parameter);
    setOutput(ToneOutput{});
}

// Notifies Tracktion listeners before members are destroyed.
ToneOutputPlugin::~ToneOutputPlugin()
{
    notifyListenersOfDeletion();
}

// The stage's name as the automation picker and lane labels show it, beside the panel's own
// "Output" title the fader and pan knob sit under.
juce::String ToneOutputPlugin::getName() const
{
    return "Output";
}

// Returns the private plugin type string stored in Tracktion state.
juce::String ToneOutputPlugin::getPluginType()
{
    return xmlTypeName;
}

// Returns the plugin vendor for Tracktion diagnostics.
juce::String ToneOutputPlugin::getVendor()
{
    return "Rock Hero";
}

// Returns the compact label used in Tracktion plugin lists.
juce::String ToneOutputPlugin::getShortName(int suggested_length)
{
    juce::ignoreUnused(suggested_length);
    return "ToneOut";
}

// Returns the selectable description used by Tracktion internals.
juce::String ToneOutputPlugin::getSelectableDescription()
{
    return getName();
}

// Avoids per-block CPU timing overhead for this tiny utility plugin.
bool ToneOutputPlugin::shouldMeasureCpuUsage() const noexcept
{
    return false;
}

// Preserves the input channel count so the structural node does not alter routing.
int ToneOutputPlugin::getNumOutputChannelsGivenInputs(int num_input_channels)
{
    return num_input_channels;
}

// Prepares smoothing and starts both channels at the current parameter values.
void ToneOutputPlugin::initialise(const tracktion::PluginInitialisationInfo& info)
{
    const float level_gain = juce::Decibels::decibelsToGain(m_level_parameter->getCurrentValue());
    const float pan = m_pan_parameter->getCurrentValue();
    auto& [left, right] = m_smoothed_channel_gains;
    left.reset(info.sampleRate, g_smoothing_ramp_seconds);
    left.setCurrentAndTargetValue(level_gain * leftPanGain(pan));
    right.reset(info.sampleRate, g_smoothing_ramp_seconds);
    right.setCurrentAndTargetValue(level_gain * rightPanGain(pan));
}

// Handles Tracktion graph reuse without allocating or mutating realtime state.
void ToneOutputPlugin::initialiseWithoutStopping(const tracktion::PluginInitialisationInfo& info)
{
    juce::ignoreUnused(info);
}

// Releases playback resources. This plugin only owns scalar realtime state.
void ToneOutputPlugin::deinitialise()
{}

// Applies level and pan to the stereo pair with per-sample de-zipper smoothing. The parameter
// streams were already advanced to this block's start by applyToBufferWithAutomation, so a playing
// Volume or Pan lane arrives through getCurrentValue() exactly as the branch gain's schedule does.
// Channels beyond the pair never occur (the rack wires exactly two pins) and are left untouched.
void ToneOutputPlugin::applyToBuffer(const tracktion::PluginRenderContext& context)
{
    if (!isEnabled() || context.destBuffer == nullptr || context.bufferNumSamples <= 0)
    {
        return;
    }

    const float level_gain = juce::Decibels::decibelsToGain(m_level_parameter->getCurrentValue());
    const float pan = m_pan_parameter->getCurrentValue();
    auto& [left, right] = m_smoothed_channel_gains;
    left.setTargetValue(level_gain * leftPanGain(pan));
    right.setTargetValue(level_gain * rightPanGain(pan));

    juce::AudioBuffer<float>& buffer = *context.destBuffer;
    int channel = 0;
    for (juce::SmoothedValue<float>& channel_gain : m_smoothed_channel_gains)
    {
        // A missing channel still advances its smoother so the pair stays in step.
        if (channel < buffer.getNumChannels())
        {
            channel_gain.applyGain(
                buffer.getWritePointer(channel, context.bufferStartSample),
                context.bufferNumSamples);
        }
        else
        {
            channel_gain.skip(context.bufferNumSamples);
        }
        ++channel;
    }
}

// Writes both values as the parameters' explicit values: the stored truth a lane anchors on,
// which a playing curve overrides at the block rate but never rewrites.
void ToneOutputPlugin::setOutput(ToneOutput output)
{
    const ToneOutput clamped = clampToneOutput(output);
    m_level_parameter->setParameter(static_cast<float>(clamped.level.db), juce::sendNotification);
    m_pan_parameter->setParameter(static_cast<float>(clamped.pan.position), juce::sendNotification);
    changed();
}

// Reads the explicit values, which survive playback untouched by the curves.
ToneOutput ToneOutputPlugin::output() const noexcept
{
    return clampToneOutput(
        ToneOutput{
            .level = Gain{static_cast<double>(m_level_parameter->getCurrentExplicitValue())},
            .pan = Pan{static_cast<double>(m_pan_parameter->getCurrentExplicitValue())},
        });
}

} // namespace rock_hero::common::audio
