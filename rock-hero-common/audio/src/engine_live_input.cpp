#include "engine_impl.h"

namespace rock_hero::common::audio
{

namespace
{

// Maps structural live-rig failures into the narrower live-input setup surface.
[[nodiscard]] LiveInputError liveInputErrorFromLiveRigError(const LiveRigError& error)
{
    switch (error.code)
    {
        case LiveRigErrorCode::MessageThreadRequired:
        {
            return LiveInputError{LiveInputErrorCode::MessageThreadRequired, error.message};
        }
        case LiveRigErrorCode::TrackMissing:
        {
            return LiveInputError{LiveInputErrorCode::TrackMissing, error.message};
        }
        default:
        {
            return LiveInputError{LiveInputErrorCode::CouldNotSetInputGain, error.message};
        }
    }
}

} // namespace

// Reads the current input gain from the structural live-rig gain plugin.
Gain Engine::inputGain() const
{
    return m_impl->readGainFromPlugin(m_impl->m_input_gain_plugin_id);
}

// Sets the input gain on the structural live-rig gain plugin before the signal chain.
std::expected<void, LiveInputError> Engine::setInputGain(Gain gain)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveInputError{LiveInputErrorCode::MessageThreadRequired}};
    }

    gain = clampGain(gain);
    auto applied = m_impl->applyGainToPlugin(m_impl->m_input_gain_plugin_id, gain);
    if (!applied.has_value())
    {
        return std::unexpected{liveInputErrorFromLiveRigError(applied.error())};
    }

    return {};
}

// Reads the live input meter used by the calibration window.
AudioMeterLevel Engine::rawInputMeterLevel() const
{
    return m_impl->rawInputMeterLevel();
}

// Reports whether calibrated live input is currently routed through the chain.
bool Engine::liveInputMonitoringEnabled() const
{
    return m_impl->m_live_input_monitoring_enabled;
}

// Enables or disables processed live input monitoring without changing transport playback.
std::expected<void, LiveInputError> Engine::setLiveInputMonitoringEnabled(bool enabled)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveInputError{LiveInputErrorCode::MessageThreadRequired}};
    }

    return m_impl->setMonitoringChannelEnabled(
        MonitorChannel::LiveInput,
        enabled,
        currentInputDeviceIdentity().has_value(),
        "live input enable rollback failed");
}

// Reports whether unprocessed calibration input is currently routed directly to output.
bool Engine::calibrationInputMonitoringEnabled() const
{
    return m_impl->m_calibration_input_monitoring_enabled;
}

// Enables or disables direct calibration monitoring without changing transport playback.
std::expected<void, LiveInputError> Engine::setCalibrationInputMonitoringEnabled(bool enabled)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveInputError{LiveInputErrorCode::MessageThreadRequired}};
    }

    return m_impl->setMonitoringChannelEnabled(
        MonitorChannel::Calibration,
        enabled,
        currentInputDeviceIdentity().has_value(),
        "calibration monitoring enable rollback failed");
}

} // namespace rock_hero::common::audio
