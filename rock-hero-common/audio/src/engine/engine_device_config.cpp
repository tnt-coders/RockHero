#include "engine_impl.h"

namespace rock_hero::common::audio
{

namespace
{

// Converts JUCE sample-count latency to the millisecond value shown in editor status text.
[[nodiscard]] double samplesToMilliseconds(int sample_count, double sample_rate_hz) noexcept
{
    if (sample_count <= 0 || sample_rate_hz <= 0.0)
    {
        return 0.0;
    }

    return static_cast<double>(sample_count) * 1000.0 / sample_rate_hz;
}

// Reconstructs the AudioDeviceSetup encoded by a serialized device-state XML. This mirrors, field
// for field, the reconstruction JUCE's AudioDeviceManager::initialiseFromXML performs for the same
// blob, so two device-state XMLs compare equal (through AudioDeviceSetup::operator==) exactly when
// a restore of one would reproduce the other. Keeping the extraction local avoids reaching into
// JUCE's private initialiseFromXML while staying tied to the same attributes.
[[nodiscard]] juce::AudioDeviceManager::AudioDeviceSetup reconstructDeviceSetupFromXml(
    const juce::XmlElement& xml)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;

    if (xml.getStringAttribute("audioDeviceName").isNotEmpty())
    {
        setup.inputDeviceName = setup.outputDeviceName = xml.getStringAttribute("audioDeviceName");
    }
    else
    {
        setup.inputDeviceName = xml.getStringAttribute("audioInputDeviceName");
        setup.outputDeviceName = xml.getStringAttribute("audioOutputDeviceName");
    }

    setup.bufferSize = xml.getIntAttribute("audioDeviceBufferSize", setup.bufferSize);
    setup.sampleRate = xml.getDoubleAttribute("audioDeviceRate", setup.sampleRate);
    setup.inputChannels.parseString(xml.getStringAttribute("audioDeviceInChans", "11"), 2);
    setup.outputChannels.parseString(xml.getStringAttribute("audioDeviceOutChans", "11"), 2);
    setup.useDefaultInputChannels = !xml.hasAttribute("audioDeviceInChans");
    setup.useDefaultOutputChannels = !xml.hasAttribute("audioDeviceOutChans");

    return setup;
}

// Reports whether restoring the serialized state would reproduce the device that is already open,
// so the restore's re-open and monitoring-graph rebuild would be pure cost. A closed or absent
// device is never a match, so callers only ever skip work against an equal, already-live route.
//
// The current state is taken from the device manager's own createStateXml() rather than
// getAudioDeviceSetup(), and both XMLs are reconstructed the same way before comparing. This keeps
// the comparison symmetric: JUCE's updateXml() omits the channel mask for a "use default channels"
// setup, so an XML round-trip parses the "11" default, whereas getAudioDeviceSetup() reports the
// hardware's actual active channel mask (verified in juce_AudioDeviceManager.cpp updateCurrentSetup,
// updateXml). Comparing against getAudioDeviceSetup() directly would therefore spuriously differ for
// every default-channel route and defeat the skip. Reconstructing both sides compares the persisted
// forms that restore actually reproduces, so equal persisted forms mean an equal restore outcome.
[[nodiscard]] bool activeDeviceMatchesSerializedState(
    juce::AudioDeviceManager& device_manager, const juce::XmlElement& xml)
{
    juce::AudioIODevice* const current_device = device_manager.getCurrentAudioDevice();
    if (current_device == nullptr || !current_device->isOpen())
    {
        return false;
    }

    const std::unique_ptr<juce::XmlElement> current_xml = device_manager.createStateXml();
    if (current_xml == nullptr)
    {
        return false;
    }

    return reconstructDeviceSetupFromXml(xml) == reconstructDeviceSetupFromXml(*current_xml);
}

} // namespace

void Engine::Impl::scheduleAudioDeviceConfigurationRefresh()
{
    if (m_audio_device_configuration_refresh_pending)
    {
        return;
    }

    m_audio_device_configuration_refresh_pending = true;
    const std::weak_ptr<bool> alive_source{m_alive};
    const bool refresh_posted = juce::MessageManager::callAsync([this, alive = alive_source] {
        if (alive.expired())
        {
            return;
        }

        m_audio_device_configuration_refresh_pending = false;
        handleAudioDeviceConfigurationRefresh();
    });
    if (refresh_posted)
    {
        return;
    }

    m_audio_device_configuration_refresh_pending = false;
    logInstrumentMonitoringFailure("audio device refresh could not be posted");
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        handleAudioDeviceConfigurationRefresh();
    }
}

void Engine::Impl::handleAudioDeviceConfigurationRefresh()
{
    m_live_input_monitoring_enabled = false;
    m_calibration_input_monitoring_enabled = false;
    detachInstrumentMonitoringRoute();
    m_engine->getDeviceManager().dispatchPendingUpdates();
    m_audio_device_listeners.call(
        &IAudioDeviceConfiguration::Listener::onAudioDeviceConfigurationChanged);
}

void Engine::Impl::attachMeterReader(MeterReader& reader, tracktion::LevelMeterPlugin* meter)
{
    if (meter != nullptr)
    {
        reader.attach(&meter->measurer);
    }
    else
    {
        reader.detach();
    }
}

AudioMeterSnapshot Engine::Impl::audioMeterSnapshot() const
{
    attachMeterReader(m_input_meter_reader, findStructuralMeterPlugin(m_input_meter_plugin_id));
    attachMeterReader(m_output_meter_reader, findStructuralMeterPlugin(m_output_meter_plugin_id));
    attachMeterReader(
        m_master_meter_reader, findStructuralMasterMeterPlugin(m_master_meter_plugin_id));

    return AudioMeterSnapshot{
        .live_rig_input = m_input_meter_reader.read(),
        .live_rig_output = m_output_meter_reader.read(),
        .master_output = m_master_meter_reader.read(),
    };
}

// Exposes the JUCE device manager so settings UI can present and apply hardware route choices.
juce::AudioDeviceManager& Engine::deviceManager() noexcept
{
    return m_impl->m_engine->getDeviceManager().deviceManager;
}

// Restores the JUCE device manager state captured by a previous editor session.
std::expected<void, AudioDeviceConfigurationError> Engine::restoreSerializedDeviceState(
    const std::string& serialized_state)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{
            AudioDeviceConfigurationError{AudioDeviceConfigurationErrorCode::MessageThreadRequired}
        };
    }

    const std::unique_ptr<juce::XmlElement> xml =
        juce::parseXML(juce::String{serialized_state.c_str()});
    if (xml == nullptr)
    {
        return std::unexpected{
            AudioDeviceConfigurationError{AudioDeviceConfigurationErrorCode::InvalidSerializedState}
        };
    }

    juce::AudioDeviceManager& device_manager = m_impl->m_engine->getDeviceManager().deviceManager;

    // Skip-if-unchanged: when the target route already matches the open device, the initialise()
    // call would re-enumerate MIDI around JUCE's own no-op device open, and the unconditional
    // monitoring-graph rebuild below would allocate a fresh Tracktion playback context for nothing.
    // Returning here elides both, so a same-device restore (e.g. a game route imported from this
    // editor's own route) costs nothing.
    if (activeDeviceMatchesSerializedState(device_manager, *xml))
    {
        return {};
    }

    const juce::String error_text = device_manager.initialise(1, 2, xml.get(), true);
    if (error_text.isNotEmpty())
    {
        return std::unexpected{AudioDeviceConfigurationError{
            AudioDeviceConfigurationErrorCode::RestoreFailed, error_text.toStdString()
        }};
    }

    auto route_result = m_impl->rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        return std::unexpected{AudioDeviceConfigurationError{
            AudioDeviceConfigurationErrorCode::RestoreFailed, route_result.error().message
        }};
    }

    return {};
}

// Captures the JUCE device manager state as an opaque string for editor settings.
std::optional<std::string> Engine::serializedDeviceState() const
{
    const std::unique_ptr<juce::XmlElement> xml =
        m_impl->m_engine->getDeviceManager().deviceManager.createStateXml();
    if (xml == nullptr)
    {
        return std::nullopt;
    }

    return xml->toString().toStdString();
}

// Reports whether restoring the given serialized route would be a no-op against the open device.
// Shares the parse-and-compare helper the restore path uses, so the pre-check the editor makes to
// choose an instant vs. behind-the-overlay toggle can never disagree with what the restore does.
bool Engine::deviceStateMatchesActive(const std::string& serialized_state) const
{
    const std::unique_ptr<juce::XmlElement> xml =
        juce::parseXML(juce::String{serialized_state.c_str()});
    if (xml == nullptr)
    {
        return false;
    }

    return activeDeviceMatchesSerializedState(
        m_impl->m_engine->getDeviceManager().deviceManager, *xml);
}

// Captures open-device timing and route details through the JUCE device manager.
AudioDeviceStatus Engine::currentDeviceStatus() const
{
    auto* const current_device =
        m_impl->m_engine->getDeviceManager().deviceManager.getCurrentAudioDevice();
    if (current_device == nullptr || !current_device->isOpen())
    {
        return {};
    }

    const double sample_rate_hz = current_device->getCurrentSampleRate();
    if (sample_rate_hz <= 0.0)
    {
        return {};
    }

    return AudioDeviceStatus{
        .open = true,
        .device_name = current_device->getName().toStdString(),
        .backend_name = current_device->getTypeName().toStdString(),
        .sample_rate_hz = sample_rate_hz,
        .bit_depth = current_device->getCurrentBitDepth(),
        .input_channels = current_device->getActiveInputChannels().countNumberOfSetBits(),
        .output_channels = current_device->getActiveOutputChannels().countNumberOfSetBits(),
        .buffer_size_samples = current_device->getCurrentBufferSizeSamples(),
        .input_latency_ms =
            samplesToMilliseconds(current_device->getInputLatencyInSamples(), sample_rate_hz),
        .output_latency_ms =
            samplesToMilliseconds(current_device->getOutputLatencyInSamples(), sample_rate_hz),
    };
}

// Captures the one-channel physical input route used to validate calibration state.
std::optional<InputDeviceIdentity> Engine::currentInputDeviceIdentity() const
{
    if (m_impl->m_audio_device_configuration_refresh_pending)
    {
        return std::nullopt;
    }

    const auto& device_manager = m_impl->m_engine->getDeviceManager().deviceManager;
    auto* const current_device = device_manager.getCurrentAudioDevice();
    if (current_device == nullptr || !current_device->isOpen())
    {
        return std::nullopt;
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    device_manager.getAudioDeviceSetup(setup);
    if (setup.inputDeviceName.isEmpty())
    {
        return std::nullopt;
    }

    const juce::BigInteger active_inputs = current_device->getActiveInputChannels();
    const int first_channel = active_inputs.findNextSetBit(0);
    if (first_channel < 0 || active_inputs.findNextSetBit(first_channel + 1) >= 0)
    {
        return std::nullopt;
    }

    const juce::StringArray input_channel_names = current_device->getInputChannelNames();
    juce::String input_channel_name;
    if (first_channel < input_channel_names.size())
    {
        input_channel_name = input_channel_names[first_channel];
    }

    InputDeviceIdentity identity{
        .backend_name = device_manager.getCurrentAudioDeviceType().toStdString(),
        .input_device_name = setup.inputDeviceName.toStdString(),
        .input_channel_index = first_channel,
        .input_channel_name = input_channel_name.toStdString(),
    };
    if (!isValidInputDeviceIdentity(identity))
    {
        return std::nullopt;
    }

    return identity;
}

// Registers a project-owned device-configuration listener.
void Engine::addListener(IAudioDeviceConfiguration::Listener& listener)
{
    m_impl->m_audio_device_listeners.add(&listener);
}

// Removes a previously registered device-configuration listener.
void Engine::removeListener(IAudioDeviceConfiguration::Listener& listener)
{
    m_impl->m_audio_device_listeners.remove(&listener);
}

// Reads Tracktion meter clients through the pimpl without exposing Tracktion meter types.
AudioMeterSnapshot Engine::audioMeterSnapshot() const
{
    return m_impl->audioMeterSnapshot();
}

} // namespace rock_hero::common::audio
