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
[[nodiscard]] bool juceFellBackFromExplicitChoice(
    juce::AudioDeviceManager& device_manager, const juce::XmlElement& saved)
{
    juce::AudioIODevice* const live_device = device_manager.getCurrentAudioDevice();
    if (live_device == nullptr || !live_device->isOpen())
    {
        // Nothing open to undo. Also covers a settings edit (device deliberately closed while
        // staging) and a fallback that itself found no openable device (already closed).
        return false;
    }

    const juce::AudioDeviceManager::AudioDeviceSetup saved_setup =
        reconstructDeviceSetupFromXml(saved);
    const juce::AudioDeviceManager::AudioDeviceSetup live_setup =
        device_manager.getAudioDeviceSetup();

    return saved.getStringAttribute("deviceType") != device_manager.getCurrentAudioDeviceType() ||
           saved_setup.inputDeviceName != live_setup.inputDeviceName ||
           saved_setup.outputDeviceName != live_setup.outputDeviceName;
}

// Reports whether the saved route's device is currently attached and initializable, so a replug
// can re-apply the saved route.
//
// The name check mirrors the availability check JUCE's setAudioDeviceSetup performs (a
// device-type match plus each non-empty device name present in that type's freshly scanned
// list). It is necessary but not sufficient: an ASIO driver keeps advertising its device names
// while the hardware is unplugged (the listing is registry-backed, not presence-backed), which
// would report the device permanently "present" and keep the absent->present replug gate from
// ever firing -- the device would stay closed forever after a reconnect. A non-open probe device
// settles it: constructing the device loads the driver and runs its init without opening an
// audio stream, and a failed init (hardware unplugged, or the device held by another
// application) leaves getLastError() non-empty from birth (verified in juce_ASIO_windows.cpp;
// the settings window's staged preview device relies on the same behavior). This runs only while
// the route is closed, so it never contends with an open device.
[[nodiscard]] bool savedDeviceIsPresent(
    juce::AudioDeviceManager& device_manager, const juce::XmlElement& saved)
{
    const juce::String type_name = saved.getStringAttribute("deviceType");
    juce::AudioIODeviceType* matching_type = nullptr;
    for (juce::AudioIODeviceType* type : device_manager.getAvailableDeviceTypes())
    {
        if (type->getTypeName() == type_name)
        {
            matching_type = type;
            break;
        }
    }

    if (matching_type == nullptr)
    {
        return false;
    }

    const juce::AudioDeviceManager::AudioDeviceSetup setup = reconstructDeviceSetupFromXml(saved);
    if (setup.inputDeviceName.isEmpty() && setup.outputDeviceName.isEmpty())
    {
        return false;
    }

    matching_type->scanForDevices();
    const bool input_present = setup.inputDeviceName.isEmpty() ||
                               matching_type->getDeviceNames(true).contains(setup.inputDeviceName);
    const bool output_present =
        setup.outputDeviceName.isEmpty() ||
        matching_type->getDeviceNames(false).contains(setup.outputDeviceName);
    if (!input_present || !output_present)
    {
        return false;
    }

    const std::unique_ptr<juce::AudioIODevice> probe{matching_type->createDevice(
        setup.outputDeviceName, setup.inputDeviceName)};
    return probe != nullptr && probe->getLastError().isEmpty();
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
    enforceNoFallbackDevicePolicy();
    m_live_input_monitoring_enabled = false;
    m_calibration_input_monitoring_enabled = false;
    detachInstrumentMonitoringRoute();
    m_engine->getDeviceManager().dispatchPendingUpdates();
    m_audio_device_listeners.call(
        &IAudioDeviceConfiguration::Listener::onAudioDeviceConfigurationChanged);
}

// Applies the no-fallback device policy after any JUCE device-configuration change. JUCE's own
// disconnect handler falls back to a default device (hard-coded, not suppressible in-band), so a
// detected fallback is undone here by closing the substitute -- the saved choice survives in
// lastExplicitSettings, matching the startup restore's behavior. Symmetrically, when the device is
// closed and the saved device transitions from absent to present, the saved route is re-applied so
// replugging the interface restores the user's device and never a different one.
//
// The absent->present transition gate is load-bearing twice over. A settings edit deliberately
// stages with the device closed while the saved device stays present, so no transition fires and
// the edit is left alone (a device replugged mid-edit is the one rare corner that would reopen
// under the dialog). And a failed reopen (driver busy) does not retry until the device disappears
// and returns again, which prevents broadcast-driven retry loops: a failing initialise() posts
// another change message, and an unconditional reopen here would chase it forever.
void Engine::Impl::enforceNoFallbackDevicePolicy()
{
    juce::AudioDeviceManager& device_manager = m_engine->getDeviceManager().deviceManager;
    const std::unique_ptr<juce::XmlElement> saved = device_manager.createStateXml();
    if (saved == nullptr)
    {
        // No explicit device choice exists (first run), so default auto-detection stands.
        m_saved_device_present = false;
        return;
    }

    if (juceFellBackFromExplicitChoice(device_manager, *saved))
    {
        // The saved device just vanished and JUCE opened a substitute; close it. This refresh pass
        // notifies listeners of the closed state, and the saved route stays persistable.
        RH_LOG_WARNING(
            "audio.device_policy",
            "Saved audio device vanished; closing JUCE's fallback substitute and keeping the "
            "saved choice");
        device_manager.closeAudioDevice();
        m_saved_device_present = false;
        return;
    }

    juce::AudioIODevice* const live_device = device_manager.getCurrentAudioDevice();
    if (live_device != nullptr && live_device->isOpen())
    {
        // The open device is the saved device (a fallback was ruled out above).
        m_saved_device_present = true;
        return;
    }

    const bool was_present = m_saved_device_present;
    m_saved_device_present = savedDeviceIsPresent(device_manager, *saved);
    if (m_saved_device_present && !was_present)
    {
        // Replug: re-apply the saved route with JUCE's fallback disabled so only the user's device
        // can open. On success the follow-up broadcast converges (open device == saved choice); on
        // failure the device stays closed until the next absent->present transition.
        RH_LOG_INFO(
            "audio.device_policy", "Saved audio device reappeared; re-applying the saved route");
        static_cast<void>(device_manager.initialise(1, 2, saved.get(), false));
        return;
    }

    if (m_saved_device_present && was_present)
    {
        // The stuck-closed corner: the device is closed but its driver still lists it, so the
        // absent->present retry gate never fires and nothing reopens automatically (a settings
        // edit staging with a deliberately closed device also lands here and must stay untouched).
        // Logged so a session sitting in this state is explainable from the log alone.
        RH_LOG_WARNING(
            "audio.device_policy",
            "Audio device is closed while the saved device is still listed; no automatic reopen "
            "(reopen through the audio settings or restart)");
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
        return DeviceRestoreOutcome::Opened;
    }

    // No fallback: a missing or unopenable saved device must close, never silently switch the user
    // to a different device. The false suppresses JUCE's select-default-on-failure, so the saved
    // route stays in lastExplicitSettings and serializedDeviceState() still returns the user's
    // choice on the next launch. First-run auto-detection is unaffected: it runs through the bare
    // initialise(1, 2) in the Engine constructor, and this restore path is only reached when a
    // non-empty saved route exists.
    const juce::String error_text = device_manager.initialise(1, 2, xml.get(), false);
    if (error_text.isNotEmpty())
    {
        // The route was applied but the device stayed closed -- the designed no-fallback outcome,
        // reported in the value channel rather than as an error so callers keep the saved choice.
        // The failed initialise() already posted a device-change message, which drives the same
        // async monitoring teardown a mid-session disconnect does, so the synchronous monitoring
        // rebuild below is correctly skipped on this branch.
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
