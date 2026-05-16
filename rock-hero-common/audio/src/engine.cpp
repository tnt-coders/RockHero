#include "engine.h"

#include "tracktion_instrument_wave_device_mapping.h"
#include "tracktion_thumbnail.h"

#include <algorithm>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Records recoverable instrument-route failures without turning an internal bind into a public
// error.
void logInstrumentMonitoringFailure(const juce::String& message)
{
    juce::Logger::writeToLog("Rock Hero instrument monitoring: " + message);
}

// Converts a standard filesystem path into the JUCE file type required by Tracktion/JUCE APIs.
[[nodiscard]] juce::File toJuceFile(const std::filesystem::path& path)
{
    const auto path_text = path.wstring();
    return juce::File{juce::String{path_text.c_str()}};
}

// Builds the opaque project-owned candidate that UI and core callers can pass back to the host.
[[nodiscard]] PluginCandidate makePluginCandidate(
    const juce::PluginDescription& description, const std::filesystem::path& plugin_path)
{
    return PluginCandidate{
        .id = description.createIdentifierString().toStdString(),
        .name = description.name.toStdString(),
        .manufacturer = description.manufacturerName.toStdString(),
        .format_name = description.pluginFormatName.toStdString(),
        .file_path = plugin_path,
    };
}

// Converts the project-owned compact channel role into the Tracktion channel identifier.
[[nodiscard]] juce::AudioChannelSet::ChannelType toTracktionChannelRole(
    InstrumentChannelRole role) noexcept
{
    switch (role)
    {
        case InstrumentChannelRole::Left:
        {
            return juce::AudioChannelSet::left;
        }
        case InstrumentChannelRole::Right:
        {
            return juce::AudioChannelSet::right;
        }
    }

    return juce::AudioChannelSet::unknown;
}

// Converts the testable Rock Hero route description into Tracktion's wave-device type.
[[nodiscard]] tracktion::WaveDeviceDescription toTracktionWaveDeviceDescription(
    const InstrumentWaveDescription& description)
{
    std::vector<tracktion::ChannelIndex> channels;
    channels.reserve(description.channels.size());

    for (const InstrumentChannelDescription& channel : description.channels)
    {
        channels.emplace_back(channel.compact_device_channel, toTracktionChannelRole(channel.role));
    }

    return tracktion::WaveDeviceDescription{
        description.name, channels.data(), static_cast<int>(channels.size()), true
    };
}

// Describes the single instrument input and stereo output that Rock Hero exposes to Tracktion.
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
        const std::optional<InstrumentWaveDeviceDescriptions> wave_descriptions =
            createTracktionInstrumentWaveDeviceDescriptions(
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
    const juce::File file = toJuceFile(audio_asset.path);
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

    // Two-track edit used for backing playback and instrument monitoring.
    std::unique_ptr<tracktion::Edit> m_edit;

    // Stable ID for the Tracktion track that owns arrangement backing clips.
    tracktion::EditItemID m_backing_track_id;

    // Stable ID for the Tracktion track that owns instrument input and future plugin FX.
    tracktion::EditItemID m_instrument_track_id;

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

        if (backing_track != nullptr)
        {
            backing_track->setName("Backing");
            m_backing_track_id = backing_track->itemID;
        }
        else
        {
            logInstrumentMonitoringFailure("backing track was not created");
        }

        const tracktion::AudioTrack::Ptr instrument_track = m_edit->insertNewAudioTrack(
            tracktion::TrackInsertPoint::getEndOfTracks(*m_edit), nullptr);

        if (instrument_track != nullptr)
        {
            instrument_track->setName("Instrument");
            m_instrument_track_id = instrument_track->itemID;
        }
        else
        {
            logInstrumentMonitoringFailure("instrument track was not created");
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
            applyInstrumentMonitoringRoute();
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
    // best matches what leaves the output device. While stopped, instrument monitoring can keep a
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

    // Returns the Tracktion audio track that receives the selected instrument input.
    [[nodiscard]] tracktion::AudioTrack* instrumentTrack() const
    {
        return tracktion::findAudioTrackForID(*m_edit, m_instrument_track_id);
    }

    // Looks up a previously scanned plugin candidate without exposing JUCE descriptions publicly.
    [[nodiscard]] std::unique_ptr<juce::PluginDescription> findKnownPlugin(
        const std::string& plugin_id) const
    {
        return m_engine->getPluginManager().knownPluginList.getTypeForIdentifierString(
            juce::String{plugin_id});
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

    // Stops backing playback for the Stop button while keeping instrument monitoring graph state
    // alive.
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

    // Removes instrument input assignments on the instrument track from the current playback
    // context.
    void clearInstrumentInputAssignments()
    {
        tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            return;
        }

        m_edit->getEditInputDevices().clearAllInputs(*instrument_track, nullptr);
    }

    // Finds the generated Tracktion wave input that corresponds to the selected JUCE mono input.
    [[nodiscard]] tracktion::WaveInputDevice* findInstrumentWaveInput(
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

    // Clears any instrument route that can be reached through the current Tracktion playback
    // context.
    void detachInstrumentMonitoringRoute(const juce::String& reason)
    {
        logInstrumentMonitoringFailure(reason);

        m_engine->getDeviceManager().dispatchPendingUpdates();
        m_edit->getTransport().ensureContextAllocated(true);
        clearInstrumentInputAssignments();
        m_edit->getTransport().ensureContextAllocated(true);
    }

    // Binds the selected app-local mono input to the instrument Tracktion track.
    void applyInstrumentMonitoringRoute()
    {
        if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
        {
            logInstrumentMonitoringFailure(
                "instrument route binding was requested off the message thread");
            return;
        }

        const tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            logInstrumentMonitoringFailure("instrument track is missing");
            return;
        }

        tracktion::DeviceManager& tracktion_device_manager = m_engine->getDeviceManager();
        juce::AudioIODevice* const current_device =
            tracktion_device_manager.deviceManager.getCurrentAudioDevice();
        if (current_device == nullptr)
        {
            detachInstrumentMonitoringRoute("no current audio device");
            return;
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
            detachInstrumentMonitoringRoute(
                "selected route is not one mono input and one stereo output pair");
            return;
        }

        tracktion_device_manager.dispatchPendingUpdates();

        auto& transport = m_edit->getTransport();
        transport.ensureContextAllocated(true);
        clearInstrumentInputAssignments();

        tracktion::WaveInputDevice* const wave_input =
            findInstrumentWaveInput(wave_descriptions->input);
        if (wave_input == nullptr)
        {
            logInstrumentMonitoringFailure("selected mono input is not available to Tracktion");
            transport.ensureContextAllocated(true);
            return;
        }

        wave_input->setStereoPair(false);

        tracktion::InputDeviceInstance* const input_instance =
            m_edit->getCurrentInstanceForInputDevice(wave_input);
        if (input_instance == nullptr)
        {
            logInstrumentMonitoringFailure("selected mono input has no playback instance");
            transport.ensureContextAllocated(true);
            return;
        }

        const auto target_result = input_instance->setTarget(
            instrument_track->itemID, true, nullptr, std::optional<int>{0});
        if (!target_result)
        {
            logInstrumentMonitoringFailure(
                "could not assign instrument input to track: " + target_result.error());
            transport.ensureContextAllocated(true);
            return;
        }

        input_instance->setRecordingEnabled(instrument_track->itemID, false);
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

    // Restores the instrument monitoring context after plugin-list graph mutation or failed
    // insertion.
    void rebuildInstrumentMonitoringGraph()
    {
        applyInstrumentMonitoringRoute();
        updateTransportState();
    }
};

// Creates the Tracktion engine and a minimal two-track edit for playback and instrument monitoring.
Engine::Engine()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->m_engine = std::make_unique<tracktion::Engine>(
        "RockHero", nullptr, std::make_unique<RockHeroEngineBehaviour>());

    // createSingleTrackEdit already provides one AudioTrack ready for media.
    m_impl->createEdit();

    // Start with one instrument input and stereo output; the dialog can reconfigure either at
    // runtime.
    m_impl->m_engine->getDeviceManager().initialise(1, 2);
    m_impl->applyInstrumentMonitoringRoute();

    auto& device_manager = m_impl->m_engine->getDeviceManager().deviceManager;
    device_manager.addChangeListener(m_impl.get());

    // TransportControl derives from juce::ChangeBroadcaster and notifies on any transport
    // state change; Impl::changeListenerCallback filters to genuine play/pause transitions.
    m_impl->m_edit->getTransport().addChangeListener(m_impl.get());

    // Tracktion mirrors current playhead position into this public ValueTree property from its
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

    const juce::File file = toJuceFile(arrangement.audio_asset.path);
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
        m_impl->applyInstrumentMonitoringRoute();
        m_impl->updateTransportState();
        return false;
    }

    m_impl->m_loaded_length_seconds = arrangement.audio_duration.seconds;
    transport.looping = false;
    transport.setPosition(tracktion::TimePosition{});
    m_impl->applyInstrumentMonitoringRoute();
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
    m_impl->applyInstrumentMonitoringRoute();
    m_impl->updateTransportState();
}

// Scans one plugin file through Tracktion's JUCE-backed known-plugin list.
std::expected<std::vector<PluginCandidate>, PluginHostError> Engine::scanPluginFile(
    const std::filesystem::path& plugin_path)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    const juce::File plugin_file = toJuceFile(plugin_path);
    if (plugin_path.empty() || !plugin_file.exists())
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::MissingPluginFile,
            "Plugin file does not exist: " + plugin_path.string()
        }};
    }

    try
    {
        constexpr auto* vst3_format_name = "VST3";
        auto& plugin_manager = m_impl->m_engine->getPluginManager();
        const juce::String& file_or_identifier = plugin_file.getFullPathName();
        juce::OwnedArray<juce::PluginDescription> found_descriptions;
        bool has_vst3_format = false;

        for (juce::AudioPluginFormat* const format :
             plugin_manager.pluginFormatManager.getFormats())
        {
            if (format == nullptr || format->getName() != vst3_format_name)
            {
                continue;
            }

            has_vst3_format = true;
            if (!format->fileMightContainThisPluginType(file_or_identifier))
            {
                continue;
            }

            plugin_manager.knownPluginList.scanAndAddFile(
                file_or_identifier, true, found_descriptions, *format);
        }

        if (!has_vst3_format)
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginScanFailed,
                "VST3 plugin hosting is not enabled in this build"
            }};
        }

        plugin_manager.knownPluginList.scanFinished();

        std::vector<PluginCandidate> candidates;
        candidates.reserve(static_cast<std::size_t>(found_descriptions.size()));

        for (const juce::PluginDescription* description : found_descriptions)
        {
            if (description != nullptr && description->pluginFormatName == vst3_format_name)
            {
                candidates.push_back(makePluginCandidate(*description, plugin_path));
            }
        }

        if (candidates.empty())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::NoCompatiblePlugin,
                "No VST3 plugin was found in: " + plugin_path.string()
            }};
        }

        return candidates;
    }
    catch (const std::exception& error)
    {
        m_impl->m_engine->getPluginManager().knownPluginList.scanFinished();
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginScanFailed,
            std::string{"Plugin scan failed: "} + error.what()
        }};
    }
}

// Appends a selected known VST3 candidate to the instrument track's plugin list.
std::expected<PluginHandle, PluginHostError> Engine::addPlugin(const std::string& plugin_id)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    tracktion::AudioTrack* const instrument_track = m_impl->instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    const std::unique_ptr<juce::PluginDescription> description = m_impl->findKnownPlugin(plugin_id);
    if (description == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginNotFound, "Plugin candidate was not found: " + plugin_id
        }};
    }

    if (!instrument_track->pluginList.canInsertPlugin())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::PluginInsertionFailed}};
    }

    m_impl->stopTransportAndReleaseContext();

    const tracktion::Plugin::Ptr plugin = m_impl->m_edit->getPluginCache().createNewPlugin(
        tracktion::ExternalPlugin::xmlTypeName, *description);
    if (plugin == nullptr)
    {
        m_impl->rebuildInstrumentMonitoringGraph();
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginCreationFailed,
            "Could not create plugin: " + description->name.toStdString()
        }};
    }

    if (auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin.get());
        external_plugin != nullptr)
    {
        const juce::String load_error = external_plugin->getLoadError();
        if (load_error.isNotEmpty())
        {
            m_impl->rebuildInstrumentMonitoringGraph();
            return std::unexpected{
                PluginHostError{PluginHostErrorCode::PluginLoadFailed, load_error.toStdString()}
            };
        }
    }

    instrument_track->pluginList.insertPlugin(plugin, -1, nullptr);
    const int inserted_index = instrument_track->pluginList.indexOf(plugin.get());
    if (inserted_index < 0)
    {
        m_impl->rebuildInstrumentMonitoringGraph();
        return std::unexpected{PluginHostError{PluginHostErrorCode::PluginInsertionFailed}};
    }

    m_impl->rebuildInstrumentMonitoringGraph();
    return PluginHandle{
        .instance_id = plugin->itemID.toString().toStdString(),
        .plugin_id = plugin_id,
        .chain_index = static_cast<std::size_t>(inserted_index),
    };
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
