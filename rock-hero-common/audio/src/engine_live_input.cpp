#include "engine_impl.h"
#include "shared/audio_path_util.h"

#include <algorithm>
#include <utility>

namespace rock_hero::common::audio
{

// Records recoverable instrument-route failures without turning an internal bind into a public
// error.
void logInstrumentMonitoringFailure(const juce::String& message)
{
    RH_LOG_WARNING(
        "audio.instrument_monitoring",
        "Instrument monitoring route failed detail={:?}",
        message.toStdString());
}

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

// Converts a route-bind failure into the live-input error surface used by calibration setup.
[[nodiscard]] LiveInputError liveInputRouteUnavailable(const juce::String& message)
{
    logInstrumentMonitoringFailure(message);
    return LiveInputError{
        LiveInputErrorCode::InputRouteUnavailable,
        ("Live input route is unavailable: " + message).toStdString(),
    };
}

} // namespace

void Engine::Impl::clearInstrumentInputAssignments()
{
    if (tracktion::AudioTrack* const backing_track = backingTrack(); backing_track != nullptr)
    {
        m_edit->getEditInputDevices().clearAllInputs(*backing_track, nullptr);
    }

    if (tracktion::AudioTrack* const instrument_track = instrumentTrack();
        instrument_track != nullptr)
    {
        m_edit->getEditInputDevices().clearAllInputs(*instrument_track, nullptr);
    }
}

void Engine::Impl::detachInstrumentMonitoringRoute()
{
    auto& transport = m_edit->getTransport();
    const bool should_release_context = !transport.isPlaying();
    if (should_release_context && m_edit->getCurrentPlaybackContext() == nullptr)
    {
        // clearAllInputs enumerates only the current playback context. Allocate a stopped
        // context long enough to remove persisted targets, then release it below.
        transport.ensureContextAllocated(true);
    }

    clearInstrumentInputAssignments();
    m_raw_input_meter_reader.detach();

    if (should_release_context)
    {
        m_input_meter_reader.detach();
        m_output_meter_reader.detach();
        m_master_meter_reader.detach();
        transport.freePlaybackContext();
    }
}

LiveInputError Engine::Impl::failInstrumentMonitoringRoute(const juce::String& reason)
{
    detachInstrumentMonitoringRoute();
    return liveInputRouteUnavailable(reason);
}

tracktion::WaveInputDevice* Engine::Impl::findInstrumentWaveInput(
    const InstrumentWaveDescription& description) const
{
    const std::vector<tracktion::WaveInputDevice*> wave_inputs =
        m_engine->getDeviceManager().getWaveInputDevices();

    const auto matching_input = std::ranges::find_if(
        wave_inputs, [&description](const tracktion::WaveInputDevice* wave_input) {
            return wave_input != nullptr && wave_input->getName() == description.name;
        });

    if (matching_input == wave_inputs.end())
    {
        return nullptr;
    }

    return *matching_input;
}

tracktion::WaveInputDevice* Engine::Impl::currentInstrumentWaveInput() const
{
    const tracktion::DeviceManager& tracktion_device_manager = m_engine->getDeviceManager();
    juce::AudioIODevice* const current_device =
        tracktion_device_manager.deviceManager.getCurrentAudioDevice();
    if (current_device == nullptr)
    {
        return nullptr;
    }

    const std::optional<InstrumentWaveDeviceDescriptions> wave_descriptions =
        createTracktionInstrumentWaveDeviceDescriptions(
            current_device->getName(),
            current_device->getActiveInputChannels(),
            current_device->getActiveOutputChannels(),
            current_device->getInputChannelNames(),
            current_device->getOutputChannelNames());
    if (!wave_descriptions.has_value())
    {
        return nullptr;
    }

    return findInstrumentWaveInput(wave_descriptions->input);
}

std::expected<void, LiveInputError> Engine::Impl::applyInstrumentMonitoringRoute()
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveInputError{LiveInputErrorCode::MessageThreadRequired}};
    }

    if (!m_live_input_monitoring_enabled && !m_calibration_input_monitoring_enabled)
    {
        detachInstrumentMonitoringRoute();
        return {};
    }

    const tracktion::AudioTrack* const monitoring_target =
        m_calibration_input_monitoring_enabled ? backingTrack() : instrumentTrack();
    if (monitoring_target == nullptr)
    {
        return std::unexpected{liveInputRouteUnavailable(
            m_calibration_input_monitoring_enabled ? "backing track is missing"
                                                   : "instrument track is missing")};
    }

    tracktion::DeviceManager& tracktion_device_manager = m_engine->getDeviceManager();
    juce::AudioIODevice* const current_device =
        tracktion_device_manager.deviceManager.getCurrentAudioDevice();
    if (current_device == nullptr)
    {
        return std::unexpected{failInstrumentMonitoringRoute("no current audio device")};
    }

    const std::optional<InstrumentWaveDeviceDescriptions> wave_descriptions =
        createTracktionInstrumentWaveDeviceDescriptions(
            current_device->getName(),
            current_device->getActiveInputChannels(),
            current_device->getActiveOutputChannels(),
            current_device->getInputChannelNames(),
            current_device->getOutputChannelNames());
    if (!wave_descriptions.has_value())
    {
        return std::unexpected{failInstrumentMonitoringRoute(
            "selected route is not one mono input and one stereo output pair")};
    }

    tracktion_device_manager.dispatchPendingUpdates();

    tracktion::WaveInputDevice* const wave_input =
        findInstrumentWaveInput(wave_descriptions->input);
    if (wave_input == nullptr)
    {
        return std::unexpected{failInstrumentMonitoringRoute(
            "selected mono input is not available to Tracktion")};
    }

    clearInstrumentInputAssignments();

    auto& transport = m_edit->getTransport();
    transport.ensureContextAllocated(true);
    wave_input->setStereoPair(false);

    tracktion::InputDeviceInstance* const input_instance =
        m_edit->getCurrentInstanceForInputDevice(wave_input);
    if (input_instance == nullptr)
    {
        transport.ensureContextAllocated(true);
        return std::unexpected{liveInputRouteUnavailable(
            "selected mono input has no playback instance")};
    }

    const auto target_result =
        input_instance->setTarget(monitoring_target->itemID, true, nullptr, std::optional<int>{0});
    if (!target_result)
    {
        transport.ensureContextAllocated(true);
        return std::unexpected{liveInputRouteUnavailable(
            "could not assign live input to monitoring track: " + target_result.error())};
    }

    input_instance->setRecordingEnabled(monitoring_target->itemID, false);
    wave_input->setMonitorMode(
        (m_live_input_monitoring_enabled || m_calibration_input_monitoring_enabled)
            ? tracktion::InputDevice::MonitorMode::on
            : tracktion::InputDevice::MonitorMode::off);
    transport.ensureContextAllocated(true);
    return {};
}

std::expected<void, LiveInputError> Engine::Impl::rebuildInstrumentMonitoringGraph()
{
    auto route_result = applyInstrumentMonitoringRoute();
    updateTransportState();
    return route_result;
}

void Engine::Impl::rebuildInstrumentMonitoringGraphBestEffort(std::string_view context)
{
    auto route_result = rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        logInstrumentMonitoringFailure(
            toJuceString(context) + ": " + toJuceString(route_result.error().message));
    }
}

std::expected<void, LiveInputError> Engine::Impl::setMonitoringChannelEnabled(
    MonitorChannel channel, bool enabled, bool input_device_available,
    std::string_view rollback_context)
{
    const MonitoringFlags current{
        .live_input = m_live_input_monitoring_enabled,
        .calibration = m_calibration_input_monitoring_enabled
    };
    const std::optional<MonitoringFlags> requested =
        monitoringFlagsForRequest(current, channel, enabled, input_device_available);

    if (!requested.has_value())
    {
        // No input device to route from: force both modes off and report the route failure.
        m_live_input_monitoring_enabled = false;
        m_calibration_input_monitoring_enabled = false;
        // If monitoring was already off, there is no route to tear down. Tracktion graph
        // rebuilds can allocate playback contexts, so skip them when no state changes.
        if (current.live_input || current.calibration)
        {
            rebuildInstrumentMonitoringGraphBestEffort(rollback_context);
        }
        return std::unexpected{LiveInputError{LiveInputErrorCode::InputRouteUnavailable}};
    }

    if (requested->live_input == current.live_input &&
        requested->calibration == current.calibration)
    {
        // Lifecycle gates can repeat monitoring requests that leave both flags unchanged.
        // Rebuilding Tracktion routing in that case does work for an identical graph.
        return {};
    }

    m_live_input_monitoring_enabled = requested->live_input;
    m_calibration_input_monitoring_enabled = requested->calibration;

    auto route_result = rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        LiveInputError route_error = std::move(route_result.error());
        if (enabled)
        {
            m_live_input_monitoring_enabled = false;
            m_calibration_input_monitoring_enabled = false;
            rebuildInstrumentMonitoringGraphBestEffort(rollback_context);
        }
        return std::unexpected{std::move(route_error)};
    }

    return {};
}

AudioMeterLevel Engine::Impl::rawInputMeterLevel() const
{
    if (m_audio_device_configuration_refresh_pending)
    {
        return {};
    }

    if (tracktion::WaveInputDevice* const wave_input = currentInstrumentWaveInput();
        wave_input != nullptr)
    {
        m_raw_input_meter_reader.attach(&wave_input->levelMeasurer);
    }
    else
    {
        m_raw_input_meter_reader.detach();
    }

    return m_raw_input_meter_reader.read();
}

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
