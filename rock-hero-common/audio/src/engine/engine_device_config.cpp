#include "device/null_audio_device.h"
#include "engine_impl.h"
#include "shared/device_state_xml.h"

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

// Reports whether restoring the serialized state would reproduce the device that is already open,
// so the restore's re-open and monitoring-graph rebuild would be pure cost. Only open HARDWARE can
// match: createStateXml() names the saved choice whatever runs, so while the silent device runs a
// restore of that same choice must still open it.
//
// The current state is taken from the device manager's own createStateXml() rather than
// getAudioDeviceSetup(), and both XMLs are reconstructed the same way before comparing. This keeps
// the comparison symmetric: JUCE's updateXml() omits the channel mask for a "use default channels"
// setup, so an XML round-trip parses the "11" default, whereas getAudioDeviceSetup() reports the
// hardware's actual active channel mask (verified in juce_AudioDeviceManager.cpp
// updateCurrentSetup, updateXml). Comparing against getAudioDeviceSetup() directly would therefore
// spuriously differ for every default-channel route and defeat the skip. Reconstructing both sides
// compares the persisted forms that restore actually reproduces, so equal persisted forms
// mean an equal restore outcome.
[[nodiscard]] bool activeDeviceMatchesSerializedState(
    juce::AudioDeviceManager& device_manager, const juce::XmlElement& xml)
{
    if (!hardwareDeviceOpen(device_manager))
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

// Reports whether JUCE's own disconnect handler (AudioDeviceManager::audioDeviceListChanged) just
// fell back to a default device after the user's saved device vanished. That fallback is hard-coded
// inside vendored JUCE with selectDefaultDeviceOnFailure=true and cannot be suppressed in-band, so
// the engine detects and undoes it to honor the same no-fallback policy the startup restore uses.
//
// The fallback opens the default with treatAsChosenDevice=false, so it never calls updateXml():
// createStateXml() (backed by lastExplicitSettings) still names the user's chosen route, while
// getAudioDeviceSetup()/getCurrentAudioDeviceType() report the fallback device. A genuine device
// choice goes through setAudioDeviceSetup(.., treatAsChosenDevice=true) -> updateXml(), which keeps
// the two in agreement, so a divergence between the saved-choice identity and the live-device
// identity is exactly a fallback. Identity is compared on device type + input/output names only,
// not the full setup, to avoid the default-channel-mask asymmetry that
// activeDeviceMatchesSerializedState() documents.
// Callers ask it only while hardware is open.
[[nodiscard]] bool juceFellBackFromExplicitChoice(
    juce::AudioDeviceManager& device_manager, const juce::XmlElement& saved)
{
    const juce::AudioDeviceManager::AudioDeviceSetup saved_setup =
        reconstructDeviceSetupFromXml(saved);
    const juce::AudioDeviceManager::AudioDeviceSetup live_setup =
        device_manager.getAudioDeviceSetup();

    return saved.getStringAttribute("deviceType") != device_manager.getCurrentAudioDeviceType() ||
           saved_setup.inputDeviceName != live_setup.inputDeviceName ||
           saved_setup.outputDeviceName != live_setup.outputDeviceName;
}

// The disconnect notice used when the saved device vanished: nothing attempted an open, so no
// backend diagnostic exists to report. Every open attempt records the backend's own text instead.
constexpr const char* g_disconnected_reason = "Disconnected";

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
    enforceDevicePolicy();
    m_live_input_monitoring_enabled = false;
    m_calibration_input_monitoring_enabled = false;
    detachInstrumentMonitoringRoute();
    m_engine->getDeviceManager().dispatchPendingUpdates();
    m_audio_device_listeners.call(
        &IAudioDeviceConfiguration::Listener::onAudioDeviceConfigurationChanged);
}

// THE device policy, run at construction and after every JUCE device-configuration change: a device
// always runs, and the only audible one it may be is the user's saved choice. Anything else --
// nothing at all, or the default JUCE's own disconnect handler opened (hard-coded, not
// suppressible in-band) -- is replaced by the silent device, so the playhead, the plugins and their
// editors keep working with the audio going nowhere. The saved choice survives in
// lastExplicitSettings either way.
//
// Losing the hardware pauses playback in both products: an immediate, unmistakable sign that the
// device just went (user, 2026-10-01), and the game's stop, since it detects notes. It is an edge,
// not a level -- playback the user starts on the silent device keeps playing.
//
// It never reopens hardware: automatic reopening required a speculative driver probe that crashed
// flaky ASIO drivers (docs/plans/todo/safe-device-auto-reopen.md). Every reopen is an explicit
// application of the saved route.
void Engine::Impl::enforceDevicePolicy()
{
    juce::AudioDeviceManager& device_manager = m_engine->getDeviceManager().deviceManager;
    const std::unique_ptr<juce::XmlElement> saved = device_manager.createStateXml();
    const bool on_saved_hardware =
        hardwareDeviceOpen(device_manager) &&
        (saved == nullptr || !juceFellBackFromExplicitChoice(device_manager, *saved));

    // Paused before the silent device starts: Tracktion restarts every playing context on a device
    // start (DeviceManager::prepareToStart), so a transport still flagged playing would resume.
    if (m_hardware_open && !on_saved_hardware && m_edit->getTransport().isPlaying())
    {
        pausePlayback();
    }
    m_hardware_open = on_saved_hardware;

    if (on_saved_hardware)
    {
        m_device_unavailable_reason.clear();
        return;
    }

    if (saved != nullptr && m_device_unavailable_reason.empty())
    {
        // No open attempt recorded a backend diagnostic, so the saved device simply went.
        m_device_unavailable_reason = g_disconnected_reason;
    }

    if (!isNullAudioDevice(device_manager.getCurrentAudioDevice()))
    {
        openNullAudioDevice(device_manager);
    }
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

// Applies the JUCE device route captured by a previous session, without device fallback.
std::expected<DeviceRestoreOutcome, AudioDeviceConfigurationError> Engine::
    restoreSerializedDeviceState(const std::string& serialized_state)
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
        m_impl->m_device_unavailable_reason.clear();
        return DeviceRestoreOutcome::Opened;
    }

    // The route becomes the saved choice whether or not it opens; the device policy then runs,
    // so a route that stayed closed leaves the silent device running at once and a route that
    // opened is recorded as the hardware whose loss pauses playback. First-run auto-detection is
    // unaffected: it runs through the bare initialise(1, 2) in the Engine constructor, and this
    // restore path is only reached when a non-empty saved route exists.
    // The backend's diagnostic is recorded before the policy runs, so the policy's own
    // "Disconnected" stands in only when no open attempt explained the closure.
    const juce::String error_text = openRouteWithoutFallback(device_manager, *xml);
    m_impl->m_device_unavailable_reason = error_text.toStdString();
    m_impl->enforceDevicePolicy();
    if (error_text.isNotEmpty())
    {
        // The route was applied but the hardware stayed closed -- the designed no-fallback outcome,
        // reported in the value channel rather than as an error so callers keep the saved choice.
        // The backend's own diagnostic is recorded for the status snapshot, so the status text
        // can name the real cause. The failed initialise() already posted a
        // device-change message, which drives the same async monitoring teardown a mid-session
        // disconnect does, so the synchronous monitoring rebuild below is correctly skipped on
        // this branch.
        return DeviceRestoreOutcome::DeviceUnavailable;
    }

    auto route_result = m_impl->rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        return std::unexpected{AudioDeviceConfigurationError{
            AudioDeviceConfigurationErrorCode::RestoreFailed, route_result.error().message
        }};
    }

    return DeviceRestoreOutcome::Opened;
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

// Captures open-hardware timing and route details through the JUCE device manager. The silent
// device reads as closed hardware, carrying the recorded unavailable reason so status consumers
// can explain why the user's device is not running.
AudioDeviceStatus Engine::currentDeviceStatus() const
{
    if (!hardwareDeviceOpen(m_impl->m_engine->getDeviceManager().deviceManager))
    {
        AudioDeviceStatus closed_status;
        closed_status.unavailable_reason = m_impl->m_device_unavailable_reason;
        return closed_status;
    }

    auto* const current_device =
        m_impl->m_engine->getDeviceManager().deviceManager.getCurrentAudioDevice();
    const double sample_rate_hz = current_device->getCurrentSampleRate();

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
        .unavailable_reason = {},
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

    InputDeviceIdentity identity{
        .backend_name = device_manager.getCurrentAudioDeviceType().toStdString(),
        .input_device_name = setup.inputDeviceName.toStdString(),
        .input_channel_index = first_channel,
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
