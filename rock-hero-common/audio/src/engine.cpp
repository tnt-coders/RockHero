#include "engine.h"

#include "tracktion_live_wave_device_mapping.h"
#include "tracktion_thumbnail.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Records recoverable live-route failures without turning an internal bind into a public error.
void logLiveInstrumentMonitoringFailure(const juce::String& message)
{
    juce::Logger::writeToLog("Rock Hero live instrument monitoring: " + message);
}

// Converts the project-owned compact channel role into the Tracktion channel identifier.
[[nodiscard]] juce::AudioChannelSet::ChannelType toTracktionChannelRole(
    LiveInstrumentChannelRole role) noexcept
{
    switch (role)
    {
        case LiveInstrumentChannelRole::Left:
        {
            return juce::AudioChannelSet::left;
        }
        case LiveInstrumentChannelRole::Right:
        {
            return juce::AudioChannelSet::right;
        }
    }

    return juce::AudioChannelSet::unknown;
}

// Converts the testable Rock Hero route description into Tracktion's wave-device type.
[[nodiscard]] tracktion::WaveDeviceDescription toTracktionWaveDeviceDescription(
    const LiveInstrumentWaveDescription& description)
{
    std::vector<tracktion::ChannelIndex> channels;
    channels.reserve(description.channels.size());

    for (const LiveInstrumentChannelDescription& channel : description.channels)
    {
        channels.emplace_back(channel.compact_device_channel, toTracktionChannelRole(channel.role));
    }

    return tracktion::WaveDeviceDescription{
        description.name, channels.data(), static_cast<int>(channels.size()), true
    };
}

// Describes the single live input and stereo output that Rock Hero exposes to Tracktion.
class RockHeroEngineBehaviour final : public tracktion::EngineBehaviour
{
public:
    // Lets Engine construct its edit before explicitly opening the device manager.
    bool autoInitialiseDeviceManager() override
    {
        return false;
    }

    // Rock Hero supplies compact wave-device descriptions for the staged JUCE route.
    bool isDescriptionOfWaveDevicesSupported() override
    {
        return true;
    }

    // Converts the currently open JUCE device masks into Tracktion-visible wave devices.
    void describeWaveDevices(
        std::vector<tracktion::WaveDeviceDescription>& descriptions, juce::AudioIODevice& device,
        bool is_input) override
    {
        const std::optional<LiveInstrumentWaveDeviceDescriptions> wave_descriptions =
            createTracktionLiveWaveDeviceDescriptions(
                device.getName(),
                device.getActiveInputChannels(),
                device.getActiveOutputChannels(),
                device.getInputChannelNames(),
                device.getOutputChannelNames());
        if (!wave_descriptions.has_value())
        {
            return;
        }

        descriptions.push_back(toTracktionWaveDeviceDescription(
            is_input ? wave_descriptions->input : wave_descriptions->output));
    }
};

// Opens an asset through Tracktion only long enough to validate it and read its duration.
[[nodiscard]] std::optional<common::core::TimeDuration> readAudioDuration(
    tracktion::Engine& engine, const common::core::AudioAsset& audio_asset)
{
    const auto path_text = audio_asset.path.wstring();
    const juce::File file{juce::String{path_text.c_str()}};
    if (!file.existsAsFile())
    {
        return std::nullopt;
    }

    const tracktion::AudioFile audio_file(engine, file);
    if (!audio_file.isValid())
    {
        return std::nullopt;
    }

    const common::core::TimeDuration asset_duration{audio_file.getLength()};
    if (asset_duration.seconds <= 0.0)
    {
        return std::nullopt;
    }

    return asset_duration;
}

} // namespace

// Private Tracktion/JUCE adapter state hidden behind Engine's public pimpl boundary.
struct Engine::Impl : public juce::ChangeListener, public juce::ValueTree::Listener
{
private:
    friend class Engine;

    // Tracktion runtime root that owns device and plugin infrastructure.
    std::unique_ptr<tracktion::Engine> m_engine;

    // Two-track edit used for backing playback and live instrument monitoring.
    std::unique_ptr<tracktion::Edit> m_edit;

    // Stable ID for the Tracktion track that owns arrangement backing clips.
    tracktion::EditItemID m_backing_track_id;

    // Stable ID for the Tracktion track that owns live input and future plugin FX.
    tracktion::EditItemID m_live_instrument_track_id;

    // Duration of the loaded audio, used to clamp seeks and detect end-of-file.
    double m_loaded_length_seconds{0.0};

    // Last coarse state used to suppress duplicate listener notifications.
    TransportState m_last_notified_transport_state{};

    // Message-thread listener list for the project-owned ITransport listener surface.
    juce::ListenerList<ITransport::Listener> m_transport_listeners;

    // Message-thread listener list for audio-device configuration changes.
    juce::ListenerList<IAudioDeviceConfiguration::Listener> m_audio_device_listeners;

    // Derives the current coarse transport state directly from Tracktion state.
    [[nodiscard]] TransportState currentTransportState() const noexcept
    {
        return TransportState{
            .playing = m_edit->getTransport().isPlaying(),
        };
    }

    // Creates the edit and gives its two audio tracks explicit product roles.
    void createEdit()
    {
        m_edit = tracktion::Edit::createSingleTrackEdit(*m_engine);
        auto audio_tracks = tracktion::getAudioTracks(*m_edit);
        tracktion::AudioTrack* const backing_track = audio_tracks.getFirst();
        jassert(backing_track != nullptr);

        if (backing_track != nullptr)
        {
            backing_track->setName("Backing");
            m_backing_track_id = backing_track->itemID;
        }

        const tracktion::AudioTrack::Ptr live_track = m_edit->insertNewAudioTrack(
            tracktion::TrackInsertPoint::getEndOfTracks(*m_edit), nullptr);
        jassert(live_track != nullptr);

        if (live_track != nullptr)
        {
            live_track->setName("Live Instrument");
            m_live_instrument_track_id = live_track->itemID;
        }

        m_edit->playInStopEnabled = true;
    }

    // Derives coarse transport state from Tracktion and notifies listeners when it changes.
    void updateTransportState()
    {
        const TransportState current_state = currentTransportState();
        if (m_last_notified_transport_state == current_state)
        {
            return;
        }

        // Project-owned transport listeners observe only coarse transport state. Position is
        // intentionally excluded so view code polls it at render cadence without forcing callbacks
        // on every playhead tick.
        m_last_notified_transport_state = current_state;
        m_transport_listeners.call(
            &ITransport::Listener::onTransportStateChanged, m_last_notified_transport_state);
    }

    // Mirrors Tracktion transport and audio-device broadcasts into the project-owned surfaces.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override
    {
        if (source == &m_engine->getDeviceManager().deviceManager)
        {
            applyLiveInstrumentMonitoringRoute();
            m_audio_device_listeners.call(
                &IAudioDeviceConfiguration::Listener::onAudioDeviceConfigurationChanged);
            return;
        }
        updateTransportState();
    }

    // Tracktion publishes playhead movement through the transport ValueTree. The coarse state
    // surface ignores ordinary movement, but this hook still detects automatic end-of-file stops.
    void valueTreePropertyChanged(
        juce::ValueTree& /*tree*/, const juce::Identifier& property) override
    {
        if (property != tracktion::IDs::position)
        {
            return;
        }

        if (shouldStopAtLoadedEnd(currentBackendPosition()))
        {
            stopAndReturnToStart();
        }
    }

    // Returns the timeline position the playback backend is currently producing, in seconds.
    //
    // During playback, the audible-timeline time trails the transport head by buffer latency and
    // best matches what leaves the output device. While stopped, live monitoring can keep a
    // Tracktion context allocated, so stopped reads must use the transport head instead of treating
    // the mere presence of a context as evidence of backing-track playback.
    [[nodiscard]] double currentBackendPosition() const
    {
        auto& transport = m_edit->getTransport();
        if (transport.isPlaying())
        {
            if (auto* const playback_context = transport.getCurrentPlaybackContext();
                playback_context != nullptr)
            {
                return playback_context->getAudibleTimelineTime().inSeconds();
            }
        }
        return transport.getPosition().inSeconds();
    }

    // Keeps externally requested positions inside the current loaded file duration.
    [[nodiscard]] double clampToLoadedRange(double seconds) const noexcept
    {
        if (m_loaded_length_seconds <= 0.0)
        {
            return std::max(0.0, seconds);
        }

        return std::clamp(seconds, 0.0, m_loaded_length_seconds);
    }

    // Returns the Tracktion audio track that owns backing arrangement clips.
    [[nodiscard]] tracktion::AudioTrack* backingTrack() const
    {
        return tracktion::findAudioTrackForID(*m_edit, m_backing_track_id);
    }

    // Returns the Tracktion audio track that receives the selected live input.
    [[nodiscard]] tracktion::AudioTrack* liveInstrumentTrack() const
    {
        return tracktion::findAudioTrackForID(*m_edit, m_live_instrument_track_id);
    }

    // Detects the moment Tracktion playback has reached or passed the loaded audio duration.
    [[nodiscard]] bool shouldStopAtLoadedEnd(double raw_position_seconds) const
    {
        return m_loaded_length_seconds > 0.0 && m_edit->getTransport().isPlaying() &&
               raw_position_seconds >= m_loaded_length_seconds;
    }

    // Stops Tracktion without discarding recording state while preserving playback nodes.
    void pauseTransport()
    {
        constexpr bool discard_recordings = false;
        constexpr bool clear_devices = false;
        m_edit->getTransport().stop(discard_recordings, clear_devices);
    }

    // Stops backing playback for the Stop button while keeping live monitoring graph state alive.
    void stopTransportForPlaybackReset()
    {
        constexpr bool discard_recordings = false;
        constexpr bool clear_devices = false;
        m_edit->getTransport().stop(discard_recordings, clear_devices);
    }

    // Stops Tracktion and tears down the active playback graph for graph mutation or shutdown.
    void stopTransportAndReleaseContext()
    {
        constexpr bool discard_recordings = false;
        constexpr bool clear_devices = true;
        auto& transport = m_edit->getTransport();
        transport.stop(discard_recordings, clear_devices);
        transport.freePlaybackContext();
    }

    // Removes live input assignments on the live track from the current playback context.
    void clearLiveInstrumentInputAssignments()
    {
        tracktion::AudioTrack* const live_track = liveInstrumentTrack();
        if (live_track == nullptr)
        {
            return;
        }

        m_edit->getEditInputDevices().clearAllInputs(*live_track, nullptr);
    }

    // Finds the generated Tracktion wave input that corresponds to the selected JUCE mono input.
    [[nodiscard]] tracktion::WaveInputDevice* findLiveInstrumentWaveInput(
        const LiveInstrumentWaveDescription& description) const
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

    // Clears any live route that can be reached through the current Tracktion playback context.
    void detachLiveInstrumentMonitoringRoute(const juce::String& reason)
    {
        logLiveInstrumentMonitoringFailure(reason);

        m_engine->getDeviceManager().dispatchPendingUpdates();
        m_edit->getTransport().ensureContextAllocated(true);
        clearLiveInstrumentInputAssignments();
        m_edit->getTransport().ensureContextAllocated(true);
    }

    // Binds the selected app-local mono input to the live instrument Tracktion track.
    void applyLiveInstrumentMonitoringRoute()
    {
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

        tracktion::AudioTrack* const live_track = liveInstrumentTrack();
        if (live_track == nullptr)
        {
            logLiveInstrumentMonitoringFailure("live instrument track is missing");
            return;
        }

        tracktion::DeviceManager& tracktion_device_manager = m_engine->getDeviceManager();
        juce::AudioIODevice* const current_device =
            tracktion_device_manager.deviceManager.getCurrentAudioDevice();
        if (current_device == nullptr)
        {
            detachLiveInstrumentMonitoringRoute("no current audio device");
            return;
        }

        const std::optional<LiveInstrumentWaveDeviceDescriptions> wave_descriptions =
            createTracktionLiveWaveDeviceDescriptions(
                current_device->getName(),
                current_device->getActiveInputChannels(),
                current_device->getActiveOutputChannels(),
                current_device->getInputChannelNames(),
                current_device->getOutputChannelNames());
        if (!wave_descriptions.has_value())
        {
            detachLiveInstrumentMonitoringRoute(
                "selected route is not one mono input and one stereo output pair");
            return;
        }

        tracktion_device_manager.dispatchPendingUpdates();

        auto& transport = m_edit->getTransport();
        transport.ensureContextAllocated(true);
        clearLiveInstrumentInputAssignments();

        tracktion::WaveInputDevice* const wave_input =
            findLiveInstrumentWaveInput(wave_descriptions->input);
        if (wave_input == nullptr)
        {
            logLiveInstrumentMonitoringFailure("selected mono input is not available to Tracktion");
            transport.ensureContextAllocated(true);
            return;
        }

        wave_input->setStereoPair(false);

        tracktion::InputDeviceInstance* const input_instance =
            m_edit->getCurrentInstanceForInputDevice(wave_input);
        if (input_instance == nullptr)
        {
            logLiveInstrumentMonitoringFailure("selected mono input has no playback instance");
            transport.ensureContextAllocated(true);
            return;
        }

        const auto target_result =
            input_instance->setTarget(live_track->itemID, true, nullptr, std::optional<int>{0});
        if (!target_result)
        {
            logLiveInstrumentMonitoringFailure(
                "could not assign live input to track: " + target_result.error());
            transport.ensureContextAllocated(true);
            return;
        }

        input_instance->setRecordingEnabled(live_track->itemID, false);
        wave_input->setMonitorMode(tracktion::InputDevice::MonitorMode::on);
        transport.ensureContextAllocated(true);
    }

    // Applies Stop-button semantics programmatically when playback reaches the loaded file end.
    // Tracktion's ChangeBroadcaster and ValueTree listeners propagate these mutations back
    // through our own callbacks; no manual listener firing needed.
    void stopAndReturnToStart()
    {
        auto& transport = m_edit->getTransport();
        stopTransportForPlaybackReset();
        transport.setPosition(tracktion::TimePosition{});
    }
};

// Creates the Tracktion engine and a minimal two-track edit for playback and live monitoring.
Engine::Engine()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->m_engine = std::make_unique<tracktion::Engine>(
        "RockHero", nullptr, std::make_unique<RockHeroEngineBehaviour>());

    // createSingleTrackEdit already provides one AudioTrack ready for media.
    m_impl->createEdit();

    // Start with one live input and stereo output; the dialog can reconfigure either at runtime.
    m_impl->m_engine->getDeviceManager().initialise(1, 2);
    m_impl->applyLiveInstrumentMonitoringRoute();

    auto& device_manager = m_impl->m_engine->getDeviceManager().deviceManager;
    device_manager.addChangeListener(m_impl.get());

    // TransportControl derives from juce::ChangeBroadcaster and notifies on any transport
    // state change; Impl::changeListenerCallback filters to genuine play/pause transitions.
    m_impl->m_edit->getTransport().addChangeListener(m_impl.get());

    // Tracktion mirrors live playhead position into this public ValueTree property from its
    // transport loop. Listening here keeps the adapter event-driven from the UI perspective.
    m_impl->m_edit->getTransport().state.addListener(m_impl.get());

    // Seeds the project-owned state from the freshly created empty edit.
    m_impl->updateTransportState();
}

// Stops transport activity before destroying Tracktion objects in dependency order.
Engine::~Engine()
{
    if (m_impl->m_engine)
    {
        auto& device_manager = m_impl->m_engine->getDeviceManager().deviceManager;
        device_manager.removeChangeListener(m_impl.get());
    }

    if (m_impl->m_edit)
    {
        m_impl->m_edit->getTransport().state.removeListener(m_impl.get());
        m_impl->m_edit->getTransport().removeChangeListener(m_impl.get());
        m_impl->stopTransportAndReleaseContext();
    }

    m_impl->m_edit.reset();
    m_impl->m_engine.reset();
}

// Registers a project-owned transport listener that observes the message-thread snapshot.
void Engine::addListener(ITransport::Listener& listener)
{
    m_impl->m_transport_listeners.add(&listener);
}

// Removes a previously registered project-owned transport listener.
void Engine::removeListener(ITransport::Listener& listener)
{
    m_impl->m_transport_listeners.remove(&listener);
}

// Starts Tracktion transport playback from the current edit position.
void Engine::play()
{
    auto& transport = m_impl->m_edit->getTransport();
    if (m_impl->m_loaded_length_seconds > 0.0 &&
        transport.getPosition().inSeconds() >= m_impl->m_loaded_length_seconds)
    {
        transport.setPosition(tracktion::TimePosition{});
    }

    transport.play(false);
    m_impl->updateTransportState();
}

// Stops playback and resets Tracktion's transport position to the start.
void Engine::stop()
{
    m_impl->stopAndReturnToStart();
    m_impl->updateTransportState();
}

// Pauses playback without resetting position so the user can resume from the same point.
void Engine::pause()
{
    m_impl->pauseTransport();
    m_impl->updateTransportState();
}

// Moves Tracktion transport to the requested timeline position. Position-only motion is observed
// through position(), not through the coarse state listener surface.
void Engine::seek(common::core::TimePosition position)
{
    const double clamped_seconds = m_impl->clampToLoadedRange(position.seconds);
    m_impl->m_edit->getTransport().setPosition(
        tracktion::TimePosition::fromSeconds(clamped_seconds));
}

// Returns the current project-owned state directly from the Tracktion adapter state.
TransportState Engine::state() const noexcept
{
    return m_impl->currentTransportState();
}

// Reads the timeline position for render-cadence cursor drawing. Delegates to the backend
// position helper, which uses audible time only while backing playback is running.
common::core::TimePosition Engine::position() const noexcept
{
    return common::core::TimePosition{m_impl->clampToLoadedRange(m_impl->currentBackendPosition())};
}

// Validates every arrangement audio file and records the accepted backend durations.
bool Engine::prepareSong(common::core::Song& song)
{
    for (common::core::Arrangement& arrangement : song.arrangements)
    {
        if (arrangement.audio_asset.path.empty())
        {
            return false;
        }

        const auto audio_duration = readAudioDuration(*m_impl->m_engine, arrangement.audio_asset);
        if (!audio_duration.has_value())
        {
            return false;
        }

        arrangement.audio_duration = *audio_duration;
    }

    return true;
}

// Makes the prepared arrangement active on the Tracktion backing audio track.
bool Engine::setActiveArrangement(const common::core::Arrangement& arrangement)
{
    auto* track = m_impl->backingTrack();
    if (track == nullptr)
    {
        return false;
    }

    if (arrangement.audio_asset.path.empty() || arrangement.audio_duration.seconds <= 0.0)
    {
        return false;
    }

    const auto path_text = arrangement.audio_asset.path.wstring();
    const juce::File file{juce::String{path_text.c_str()}};
    if (!file.existsAsFile())
    {
        return false;
    }

    // Candidate is valid; stop playback and clear nodes before replacing Tracktion's edit graph.
    auto& transport = m_impl->m_edit->getTransport();
    m_impl->stopTransportAndReleaseContext();

    const auto start = tracktion::TimePosition{};
    const auto length = tracktion::TimeDuration::fromSeconds(arrangement.audio_duration.seconds);
    const tracktion::ClipPosition wave_clip_position{
        .time = {start, start + length}, .offset = tracktion::TimeDuration{}
    };

    // Final trailing argument asks Tracktion to replace any existing media on the track.
    const auto wave_clip =
        track->insertWaveClip(file.getFileNameWithoutExtension(), file, wave_clip_position, true);
    if (wave_clip == nullptr)
    {
        m_impl->applyLiveInstrumentMonitoringRoute();
        m_impl->updateTransportState();
        return false;
    }

    m_impl->m_loaded_length_seconds = arrangement.audio_duration.seconds;
    transport.looping = false;
    transport.setPosition(tracktion::TimePosition{});
    m_impl->applyLiveInstrumentMonitoringRoute();
    m_impl->updateTransportState();
    return true;
}

// Clears the backing track so closed projects do not leave stale media in Tracktion.
void Engine::clearActiveArrangement()
{
    auto& transport = m_impl->m_edit->getTransport();
    m_impl->stopTransportAndReleaseContext();
    transport.setPosition(tracktion::TimePosition{});

    if (auto* track = m_impl->backingTrack(); track != nullptr)
    {
        const juce::Array<tracktion::Clip*> clips = track->getClips();
        for (tracktion::Clip* clip : clips)
        {
            if (clip != nullptr)
            {
                clip->removeFromParent();
            }
        }
    }

    m_impl->m_loaded_length_seconds = 0.0;
    m_impl->applyLiveInstrumentMonitoringRoute();
    m_impl->updateTransportState();
}

// Exposes the JUCE device manager so settings UI can host the stock device selector directly.
juce::AudioDeviceManager& Engine::deviceManager() noexcept
{
    return m_impl->m_engine->getDeviceManager().deviceManager;
}

// Returns the currently open device name through the JUCE device manager.
std::optional<std::string> Engine::currentDeviceName() const
{
    const auto* const current_device =
        m_impl->m_engine->getDeviceManager().deviceManager.getCurrentAudioDevice();
    if (current_device == nullptr)
    {
        return std::nullopt;
    }

    const juce::String name = current_device->getName();
    if (name.isEmpty())
    {
        return std::nullopt;
    }

    return name.toStdString();
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

// Creates an IThumbnail wrapper without exposing Tracktion types through public UI-facing headers.
std::unique_ptr<IThumbnail> Engine::createThumbnail(juce::Component& owner)
{
    return std::make_unique<TracktionThumbnail>(*m_impl->m_engine, owner);
}

} // namespace rock_hero::common::audio
