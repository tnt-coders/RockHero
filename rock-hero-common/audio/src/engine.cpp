#include "engine.h"

#include "tracktion_thumbnail.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::common::audio
{

namespace
{

constexpr float g_live_monitor_gain{1.0F};
constexpr int g_live_monitor_output_channels{2};

// Adds the currently open input's first channel to the main output when enabled. Configuration of
// the actual device, channel selection, and buffer size lives in juce::AudioDeviceManager.
class LiveGuitarMonitor final : public juce::AudioIODeviceCallback
{
public:
    // Enables or disables dry monitoring without reallocating or touching the device graph.
    void setEnabled(bool enabled) noexcept
    {
        m_enabled.store(enabled, std::memory_order_release);
    }

    // Reports the audio-thread-visible monitoring flag for message-thread state derivation.
    [[nodiscard]] bool isEnabled() const noexcept
    {
        return m_enabled.load(std::memory_order_acquire);
    }

    // Adds the input signal to the stereo output buffer when enabled.
    void audioDeviceIOCallbackWithContext(
        const float* const* input_channel_data, int num_input_channels,
        float* const* output_channel_data, int num_output_channels, int num_samples,
        const juce::AudioIODeviceCallbackContext& /*context*/) override
    {
        if (!m_enabled.load(std::memory_order_acquire) || input_channel_data == nullptr ||
            output_channel_data == nullptr || num_input_channels <= 0)
        {
            return;
        }

        const float* const source = input_channel_data[0];
        if (source == nullptr)
        {
            return;
        }

        const int monitored_output_channels =
            std::min(num_output_channels, g_live_monitor_output_channels);
        for (int output_channel = 0; output_channel < monitored_output_channels; ++output_channel)
        {
            float* const output = output_channel_data[output_channel];
            if (output == nullptr)
            {
                continue;
            }

            for (int sample = 0; sample < num_samples; ++sample)
            {
                output[sample] += source[sample] * g_live_monitor_gain;
            }
        }
    }

    // The monitor has no sample-rate-dependent state to initialize.
    void audioDeviceAboutToStart(juce::AudioIODevice* /*device*/) override
    {}

    // The monitor keeps its enabled flag across device restarts.
    void audioDeviceStopped() override
    {}

private:
    // Read directly on the audio thread; writes happen on the message thread.
    std::atomic<bool> m_enabled{false};
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

    // Dry monitoring callback registered with JUCE's audio-device manager.
    LiveGuitarMonitor m_live_guitar_monitor;

    // Tracktion runtime root that owns device and plugin infrastructure.
    std::unique_ptr<tracktion::Engine> m_engine;

    // Single-track edit used for current early backing-track playback.
    std::unique_ptr<tracktion::Edit> m_edit;

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
    // While a Tracktion playback context exists, the value is the audible-timeline time leaving
    // the output device right now. That trails the transport head by buffer latency, and matching
    // it is what makes the user-visible cursor and end-of-file detection agree with what the user
    // actually hears.
    //
    // Audible is returned regardless of transport.isPlaying() because during a Tracktion
    // device-list rebuild (for example, the first hardware-MIDI rescan after engine startup) the
    // play flag flips false transiently while the context stays valid. Falling back to the
    // transport head in that window would jump the cursor forward by buffer latency; reading
    // audible directly keeps the cursor in sync with what is actually leaving the device.
    //
    // When no playback context exists, no audio is being produced, and the head equals the
    // user-visible cursor anyway, so it is returned as the only available value.
    [[nodiscard]] double currentBackendPosition() const
    {
        auto& transport = m_edit->getTransport();
        if (auto* const playback_context = transport.getCurrentPlaybackContext();
            playback_context != nullptr)
        {
            return playback_context->getAudibleTimelineTime().inSeconds();
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

    // Returns the single Tracktion audio track used for the current arrangement.
    [[nodiscard]] tracktion::AudioTrack* currentAudioTrack() const
    {
        auto audio_tracks = tracktion::getAudioTracks(*m_edit);
        return audio_tracks.getFirst();
    }

    // Detects the moment Tracktion playback has reached or passed the loaded audio duration.
    [[nodiscard]] bool shouldStopAtLoadedEnd(double raw_position_seconds) const
    {
        return m_loaded_length_seconds > 0.0 && m_edit->getTransport().isPlaying() &&
               raw_position_seconds >= m_loaded_length_seconds;
    }

    // Stops Tracktion without discarding recording state while preserving playback nodes for
    // pause/resume. This is intentionally not used for app-level Stop, where stale graph state
    // should not survive the reset back to the start.
    void pauseTransport()
    {
        constexpr bool discard_recordings = false;
        constexpr bool clear_devices = false;
        m_edit->getTransport().stop(discard_recordings, clear_devices);
    }

    // Stops Tracktion and tears down the active playback graph so buffered audio from the old
    // playhead position cannot leak into the next playback start.
    void stopTransport()
    {
        constexpr bool discard_recordings = false;
        constexpr bool clear_devices = true;
        auto& transport = m_edit->getTransport();
        transport.stop(discard_recordings, clear_devices);
        transport.freePlaybackContext();
    }

    // Applies Stop-button semantics programmatically when playback reaches the loaded file end.
    // Tracktion's ChangeBroadcaster and ValueTree listeners propagate these mutations back
    // through our own callbacks; no manual listener firing needed.
    void stopAndReturnToStart()
    {
        auto& transport = m_edit->getTransport();
        stopTransport();
        transport.setPosition(tracktion::TimePosition{});
    }
};

// Creates the Tracktion engine and a minimal single-track edit for early playback support.
Engine::Engine()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->m_engine = std::make_unique<tracktion::Engine>("RockHero");

    // Start with stereo output and one input slot; the dialog can reconfigure either at runtime.
    m_impl->m_engine->getDeviceManager().initialise(1, 2);

    auto& device_manager = m_impl->m_engine->getDeviceManager().deviceManager;
    device_manager.addAudioCallback(&m_impl->m_live_guitar_monitor);
    device_manager.addChangeListener(m_impl.get());

    // createSingleTrackEdit already provides one AudioTrack ready for media.
    m_impl->m_edit = tracktion::Edit::createSingleTrackEdit(*m_impl->m_engine);

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
    if (m_impl->m_edit)
    {
        m_impl->m_edit->getTransport().state.removeListener(m_impl.get());
        m_impl->m_edit->getTransport().removeChangeListener(m_impl.get());
        m_impl->stopTransport();
    }

    if (m_impl->m_engine)
    {
        auto& device_manager = m_impl->m_engine->getDeviceManager().deviceManager;
        device_manager.removeChangeListener(m_impl.get());
        device_manager.removeAudioCallback(&m_impl->m_live_guitar_monitor);
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
// position helper, which prefers the audible-timeline time when a Tracktion playback context
// exists and rides through brief context teardowns from Tracktion device-list rebuilds.
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

// Makes the prepared arrangement active on the single Tracktion arrangement audio track.
bool Engine::setActiveArrangement(const common::core::Arrangement& arrangement)
{
    auto* track = m_impl->currentAudioTrack();
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
    m_impl->stopTransport();

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
        m_impl->updateTransportState();
        return false;
    }

    m_impl->m_loaded_length_seconds = arrangement.audio_duration.seconds;
    transport.looping = false;
    transport.setPosition(tracktion::TimePosition{});
    m_impl->updateTransportState();
    return true;
}

// Clears the single arrangement track so closed projects do not leave stale media in Tracktion.
void Engine::clearActiveArrangement()
{
    auto& transport = m_impl->m_edit->getTransport();
    m_impl->stopTransport();
    transport.setPosition(tracktion::TimePosition{});

    if (auto* track = m_impl->currentAudioTrack(); track != nullptr)
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

// Enables the dry monitor callback against whatever device is currently configured.
void Engine::enableGuitarMonitoring()
{
    m_impl->m_live_guitar_monitor.setEnabled(true);
}

// Stops the dry monitoring callback while leaving the device configuration untouched.
void Engine::disableGuitarMonitoring()
{
    m_impl->m_live_guitar_monitor.setEnabled(false);
}

// Returns the dry monitor flag without querying JUCE or Tracktion device state.
bool Engine::isGuitarMonitoringEnabled() const noexcept
{
    return m_impl->m_live_guitar_monitor.isEnabled();
}

// Creates an IThumbnail wrapper without exposing Tracktion types through public UI-facing headers.
std::unique_ptr<IThumbnail> Engine::createThumbnail(juce::Component& owner)
{
    return std::make_unique<TracktionThumbnail>(*m_impl->m_engine, owner);
}

} // namespace rock_hero::common::audio
