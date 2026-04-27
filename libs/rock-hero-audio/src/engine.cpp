#include "engine.h"

#include "tracktion_thumbnail.h"

#include <algorithm>
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

    // Duration of the loaded clip, used to clamp seeks and detect end-of-file.
    double m_loaded_length_seconds{0.0};

    // Message-thread snapshot exposed through ITransport::state().
    TransportState m_transport_state{};

    // Message-thread listener list for the project-owned ITransport listener surface.
    juce::ListenerList<ITransport::Listener> m_transport_listeners;

    // Builds the current message-thread transport snapshot from Tracktion state and cached length.
    [[nodiscard]] TransportState makeTransportStateSnapshot() const
    {
        return TransportState{
            .playing = m_edit->getTransport().isPlaying(),
            .position = core::TimePosition{clampToLoadedRange(
                m_edit->getTransport().getPosition().inSeconds())},
            .duration = core::TimeDuration{m_loaded_length_seconds},
        };
    }

    // Applies a fresh snapshot and notifies listeners only for coarse fields that changed.
    void applyTransportStateSnapshot(const TransportState& next_state, bool notify_listeners)
    {
        const bool playing_changed = m_transport_state.playing != next_state.playing;
        const bool duration_changed = m_transport_state.duration != next_state.duration;

        m_transport_state = next_state;

        if (!notify_listeners)
        {
            return;
        }

        // Project-owned transport listeners observe coarse transitions only. Position remains part
        // of state() so view code can poll it at render cadence without forcing callbacks on every
        // playhead tick.
        if (playing_changed || duration_changed)
        {
            m_transport_listeners.call(
                &ITransport::Listener::onTransportStateChanged, m_transport_state);
        }
    }

    // Rebuilds the current transport snapshot from Tracktion state and publishes it.
    void refreshTransportState(bool notify_listeners)
    {
        applyTransportStateSnapshot(makeTransportStateSnapshot(), notify_listeners);
    }

    // Mirrors Tracktion transport change broadcasts into the project-owned state snapshot.
    void changeListenerCallback(juce::ChangeBroadcaster* /*source*/) override
    {
        refreshTransportState(true);
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
        if (shouldStopAtLoadedEnd(raw_position_seconds))
        {
            stopAndReturnToStart();
            return;
        }

        refreshTransportState(true);
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

    // Detects the moment Tracktion playback has reached or passed the loaded clip duration.
    [[nodiscard]] bool shouldStopAtLoadedEnd(double raw_position_seconds) const
    {
        return m_loaded_length_seconds > 0.0 && m_edit->getTransport().isPlaying() &&
               raw_position_seconds >= m_loaded_length_seconds;
    }

    // Applies Stop-button semantics programmatically when playback reaches the loaded file end.
    // Tracktion's ChangeBroadcaster and ValueTree listeners propagate these mutations back
    // through our own callbacks; no manual listener firing needed.
    void stopAndReturnToStart()
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

    // Seeds the project-owned snapshot from the freshly created empty edit.
    m_impl->refreshTransportState(false);
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
        m_impl->refreshTransportState(true);
        return false;
    }

    m_impl->m_loaded_length_seconds = audio_file.getLength();

    auto& transport = m_impl->m_edit->getTransport();
    transport.looping = false;
    transport.setPosition(tracktion::TimePosition{});
    m_impl->refreshTransportState(true);
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
    m_impl->refreshTransportState(true);
}

// Stops playback and resets Tracktion's transport position to the start.
void Engine::stop()
{
    m_impl->stopAndReturnToStart();
    m_impl->refreshTransportState(true);
}

// Pauses playback without resetting position so the user can resume from the same point.
void Engine::pause()
{
    // stop(false, false): do not discard recording, do not clear recordings.
    // Does not reset transport position; that is the semantic difference from stop().
    m_impl->m_edit->getTransport().stop(false, false);
    m_impl->refreshTransportState(true);
}

// Moves Tracktion transport to the requested timeline position and publishes the new snapshot.
void Engine::seek(core::TimePosition position)
{
    const double clamped_seconds = m_impl->clampToLoadedRange(position.seconds);
    m_impl->m_edit->getTransport().setPosition(
        tracktion::TimePosition::fromSeconds(clamped_seconds));
    m_impl->refreshTransportState(true);
}

// Returns the project-owned message-thread snapshot used by ITransport and edit call sites.
TransportState Engine::state() const
{
    return m_impl->m_transport_state;
}

// Reads Tracktion directly for smooth cursor rendering instead of returning the cached snapshot.
core::TimePosition Engine::position() const noexcept
{
    const double raw_position_seconds = m_impl->m_edit->getTransport().getPosition().inSeconds();
    return core::TimePosition{m_impl->clampToLoadedRange(raw_position_seconds)};
}

// Adapts the current framework-free edit port onto the single-file load helper.
bool Engine::setTrackAudioSource(core::TrackId track_id, const core::AudioAsset& audio_asset)
{
    static_cast<void>(track_id);

    // TODO: Route this through a project-owned edit-history boundary once undo/redo lands.
    // The history surface should own transaction semantics instead of exposing Tracktion or JUCE
    // undo primitives through Rock Hero interfaces.
    const auto path_text = audio_asset.path.wstring();
    const juce::File file{juce::String{path_text.c_str()}};
    return loadFile(file);
}

// Creates a thumbnail wrapper without exposing Tracktion types through public UI-facing headers.
std::unique_ptr<Thumbnail> Engine::createThumbnail(juce::Component& owner)
{
    return std::make_unique<TracktionThumbnail>(*m_impl->m_engine, owner);
}

} // namespace rock_hero::audio
