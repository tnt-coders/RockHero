#include "engine.h"

#include "tracktion_thumbnail.h"

#include <algorithm>
#include <optional>
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

    // Session track id currently represented by the one Tracktion audio track.
    std::optional<core::TrackId> m_single_track_id;

    // Duration of the loaded clip, used to clamp seeks and detect end-of-file.
    double m_loaded_length_seconds{0.0};

    // Last coarse state used to suppress duplicate listener notifications.
    TransportState m_last_notified_transport_state{};

    // Message-thread listener list for the project-owned ITransport listener surface.
    juce::ListenerList<ITransport::Listener> m_transport_listeners;

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

    // Mirrors Tracktion transport change broadcasts into the project-owned state snapshot.
    void changeListenerCallback(juce::ChangeBroadcaster* /*source*/) override
    {
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

        const double raw_position_seconds = m_edit->getTransport().getPosition().inSeconds();
        if (shouldStopAtLoadedEnd(raw_position_seconds))
        {
            stopAndReturnToStart();
        }
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

    // The current adapter can only represent one project track in Tracktion.
    [[nodiscard]] bool canLoadTrack(core::TrackId track_id) const noexcept
    {
        if (!track_id.isValid())
        {
            return false;
        }

        const core::TrackId mapped_track_id = m_single_track_id.value_or(track_id);
        return mapped_track_id == track_id;
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
    // stop(false, false): do not discard recording, do not clear recordings.
    // Does not reset transport position; that is the semantic difference from stop().
    m_impl->m_edit->getTransport().stop(false, false);
    m_impl->updateTransportState();
}

// Moves Tracktion transport to the requested timeline position. Position-only motion is observed
// through position(), not through the coarse state listener surface.
void Engine::seek(core::TimePosition position)
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

// Reads Tracktion directly for smooth cursor rendering instead of returning cached state.
core::TimePosition Engine::position() const noexcept
{
    const double raw_position_seconds = m_impl->m_edit->getTransport().getPosition().inSeconds();
    return core::TimePosition{m_impl->clampToLoadedRange(raw_position_seconds)};
}

// Adapts the framework-free edit port onto the current single-file load helper.
std::optional<core::AudioClip> Engine::loadAudioAsset(
    core::TrackId track_id, const core::AudioAsset& audio_asset, core::TimePosition position)
{
    if (!m_impl->canLoadTrack(track_id))
    {
        return std::nullopt;
    }

    // TODO: Route this through a project-owned edit-history boundary once undo/redo lands.
    // The history surface should own transaction semantics instead of exposing Tracktion or JUCE
    // undo primitives through Rock Hero interfaces.
    const auto path_text = audio_asset.path.wstring();
    const juce::File file{juce::String{path_text.c_str()}};
    if (!file.existsAsFile())
    {
        return std::nullopt;
    }

    // The current single-file transport path still assumes the loaded clip begins at zero.
    // Nonzero placement needs a wider transport timeline model before it is safe to accept.
    if (position != core::TimePosition{})
    {
        return std::nullopt;
    }

    auto* track = tracktion::getAudioTracks(*m_impl->m_edit)[0];
    if (track == nullptr)
    {
        return std::nullopt;
    }

    const tracktion::AudioFile audio_file(*m_impl->m_engine, file);
    if (!audio_file.isValid())
    {
        return std::nullopt;
    }

    const core::TimeDuration asset_duration{audio_file.getLength()};
    if (asset_duration.seconds <= 0.0)
    {
        return std::nullopt;
    }

    core::AudioClip audio_clip{
        .id = core::AudioClipId{},
        .asset = audio_asset,
        .asset_duration = asset_duration,
        .source_range =
            core::TimeRange{
                .start = core::TimePosition{},
                .end = core::TimePosition{asset_duration.seconds},
            },
        .position = position,
    };

    // Candidate is valid; stop playback before replacing clips in Tracktion's edit graph.
    auto& transport = m_impl->m_edit->getTransport();
    transport.stop(false, false);

    const auto start = tracktion::TimePosition::fromSeconds(position.seconds);
    const auto length = tracktion::TimeDuration::fromSeconds(asset_duration.seconds);
    const tracktion::ClipPosition clip_position{
        .time = {start, start + length}, .offset = tracktion::TimeDuration{}
    };

    // Final trailing argument asks Tracktion to replace any existing clips on the track.
    const auto clip =
        track->insertWaveClip(file.getFileNameWithoutExtension(), file, clip_position, true);
    if (clip == nullptr)
    {
        m_impl->updateTransportState();
        return std::nullopt;
    }

    m_impl->m_loaded_length_seconds = audio_clip.timelineRange().end.seconds;
    m_impl->m_single_track_id = track_id;
    transport.looping = false;
    transport.setPosition(tracktion::TimePosition{});
    m_impl->updateTransportState();
    return audio_clip;
}

// Creates an IThumbnail wrapper without exposing Tracktion types through public UI-facing headers.
std::unique_ptr<IThumbnail> Engine::createThumbnail(juce::Component& owner)
{
    return std::make_unique<TracktionThumbnail>(*m_impl->m_engine, owner);
}

} // namespace rock_hero::audio
