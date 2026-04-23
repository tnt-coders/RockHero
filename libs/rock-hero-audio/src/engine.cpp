#include "engine.h"

#include "tracktion_thumbnail.h"

#include <algorithm>
#include <atomic>
#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::audio
{

// Private Tracktion/JUCE adapter state hidden behind Engine's public pimpl boundary.
struct Engine::Impl : public juce::ChangeListener, public juce::ValueTree::Listener
{
private:
    friend class Engine;

    // Tracktion runtime root that owns device and plugin infrastructure.
    std::unique_ptr<tracktion::Engine> m_engine;

    // Single-track edit used for current early backing-track playback.
    std::unique_ptr<tracktion::Edit> m_edit;

    // Lock-free cache of transport seconds for UI drawing and future realtime readers.
    std::atomic<double> m_transport_position{0.0};

    // Duration of the loaded clip, used to clamp seeks and detect end-of-file.
    double m_loaded_length_seconds{0.0};

    // Message-thread listener list for engine state changes.
    juce::ListenerList<Engine::Listener> m_listeners;

    // Last published playing state, used to suppress duplicate transition notifications.
    bool m_last_known_playing{false};

    // Fires subscribed callbacks only on genuine play/pause transitions.
    void changeListenerCallback(juce::ChangeBroadcaster* /*source*/) override
    {
        const bool playing = m_edit->getTransport().isPlaying();
        if (playing == m_last_known_playing)
        {
            return;
        }

        m_last_known_playing = playing;
        m_listeners.call(&Engine::Listener::enginePlayingStateChanged, playing);
    }

    // Tracktion publishes playhead movement through the transport ValueTree. React to that
    // event stream so UI state follows playback without owning a polling timer.
    void valueTreePropertyChanged(
        juce::ValueTree& /*tree*/, const juce::Identifier& property) override
    {
        if (property != tracktion::IDs::position)
        {
            return;
        }

        const double raw_position_seconds = m_edit->getTransport().getPosition().inSeconds();
        const double clamped_seconds = ClampToLoadedRange(raw_position_seconds);
        const double previous =
            m_transport_position.exchange(clamped_seconds, std::memory_order_relaxed);
        if (previous != clamped_seconds)
        {
            m_listeners.call(&Engine::Listener::engineTransportPositionChanged, clamped_seconds);
        }

        if (ShouldStopAtLoadedEnd(raw_position_seconds))
        {
            StopAndReturnToStart();
        }
    }

    // Keeps externally requested positions inside the current loaded file duration.
    [[nodiscard]] double ClampToLoadedRange(double seconds) const noexcept
    {
        if (m_loaded_length_seconds <= 0.0)
        {
            return std::max(0.0, seconds);
        }

        return std::clamp(seconds, 0.0, m_loaded_length_seconds);
    }

    // Detects the moment Tracktion playback has reached or passed the loaded clip duration.
    [[nodiscard]] bool ShouldStopAtLoadedEnd(double raw_position_seconds) const
    {
        return m_loaded_length_seconds > 0.0 && m_edit->getTransport().isPlaying() &&
               raw_position_seconds >= m_loaded_length_seconds;
    }

    // Applies Stop-button semantics programmatically when playback reaches the loaded file end.
    // Tracktion's ChangeBroadcaster and ValueTree listeners propagate these mutations back
    // through our own callbacks; no manual listener firing needed.
    void StopAndReturnToStart()
    {
        auto& transport = m_edit->getTransport();
        transport.stop(false, false);
        transport.setPosition(tracktion::TimePosition{});
    }
};

// Creates the Tracktion engine and a minimal single-track edit for early playback support.
Engine::Engine()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->m_engine = std::make_unique<tracktion::Engine>("RockHero");

    // Stereo output, no input for now (ASIO guitar input comes later).
    m_impl->m_engine->getDeviceManager().initialise(0, 2);

    // createSingleTrackEdit already provides one AudioTrack ready for clips.
    m_impl->m_edit = tracktion::Edit::createSingleTrackEdit(*m_impl->m_engine);

    // TransportControl derives from juce::ChangeBroadcaster and notifies on any transport
    // state change; Impl::changeListenerCallback filters to genuine play/pause transitions.
    m_impl->m_edit->getTransport().addChangeListener(m_impl.get());

    // Tracktion mirrors live playhead position into this public ValueTree property from its
    // transport loop. Listening here keeps the adapter event-driven from the UI perspective.
    m_impl->m_edit->getTransport().state.addListener(m_impl.get());
}

// Stops transport activity before destroying Tracktion objects in dependency order.
Engine::~Engine()
{
    if (m_impl->m_edit)
    {
        m_impl->m_edit->getTransport().state.removeListener(m_impl.get());
        m_impl->m_edit->getTransport().removeChangeListener(m_impl.get());
        m_impl->m_edit->getTransport().stop(false, false);
    }

    m_impl->m_edit.reset();
    m_impl->m_engine.reset();
}

// Registers a message-thread listener for filtered engine transport events.
void Engine::addListener(Listener* listener)
{
    m_impl->m_listeners.add(listener);
}

// Removes a previously registered engine transport listener.
void Engine::removeListener(Listener* listener)
{
    m_impl->m_listeners.remove(listener);
}

// Replaces the single backing-track clip while keeping graph mutation on the message thread.
bool Engine::loadFile(const juce::File& file)
{
    if (!file.existsAsFile())
    {
        return false;
    }

    auto* track = tracktion::getAudioTracks(*m_impl->m_edit)[0];
    if (track == nullptr)
    {
        return false;
    }

    const tracktion::AudioFile audio_file(*m_impl->m_engine, file);
    if (!audio_file.isValid())
    {
        return false;
    }

    // Candidate is valid; now safe to stop playback and mutate the edit.
    m_impl->m_edit->getTransport().stop(false, false);

    const auto length = tracktion::TimeDuration::fromSeconds(audio_file.getLength());
    const auto start = tracktion::TimePosition{};
    const tracktion::ClipPosition position{
        .time = {start, start + length}, .offset = tracktion::TimeDuration{}
    };

    // Final trailing argument asks Tracktion to replace any existing clips on the track.
    const auto clip =
        track->insertWaveClip(file.getFileNameWithoutExtension(), file, position, true);
    if (clip == nullptr)
    {
        return false;
    }

    m_impl->m_loaded_length_seconds = audio_file.getLength();

    auto& transport = m_impl->m_edit->getTransport();
    transport.looping = false;
    transport.setPosition(tracktion::TimePosition{});
    return true;
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
}

// Stops playback and resets both Tracktion and cached transport positions to the start.
void Engine::stop()
{
    m_impl->StopAndReturnToStart();
}

// Pauses playback without resetting position so the user can resume from the same point.
void Engine::pause()
{
    // stop(false, false): do not discard recording, do not clear recordings.
    // Does not reset transport position; that is the semantic difference from stop().
    m_impl->m_edit->getTransport().stop(false, false);
}

// Moves Tracktion transport and the UI-readable cache to the requested song time.
void Engine::seek(double seconds)
{
    const double clamped_seconds = m_impl->ClampToLoadedRange(seconds);
    m_impl->m_edit->getTransport().setPosition(
        tracktion::TimePosition::fromSeconds(clamped_seconds));
}

// Reads Tracktion transport state for UI controls that need current playback status.
bool Engine::isPlaying() const
{
    return m_impl->m_edit->getTransport().isPlaying();
}

// Returns the lock-free cached transport position for UI painting and future realtime readers.
double Engine::getTransportPosition() const noexcept
{
    return m_impl->m_transport_position.load(std::memory_order_relaxed);
}

// Creates a thumbnail wrapper without exposing Tracktion types through public UI-facing headers.
std::unique_ptr<Thumbnail> Engine::createThumbnail(juce::Component& owner)
{
    return std::make_unique<TracktionThumbnail>(*m_impl->m_engine, owner);
}

} // namespace rock_hero::audio
