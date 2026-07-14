#include "engine_impl.h"

#include <algorithm>

namespace rock_hero::common::audio
{

TransportState Engine::Impl::currentTransportState() const noexcept
{
    return TransportState{
        .playing = m_edit->getTransport().isPlaying(),
    };
}

void Engine::Impl::updateTransportState()
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

void Engine::Impl::valueTreePropertyChanged(
    juce::ValueTree& /*tree*/, const juce::Identifier& property)
{
    if (property != tracktion::IDs::position)
    {
        return;
    }

    const double position_seconds = m_edit->getTransport().getPosition().inSeconds();
    if (loadedAudioEndReached(position_seconds))
    {
        stopTransport();
    }
}

double Engine::Impl::clampToLoadedRange(double seconds) const noexcept
{
    if (m_loaded_length_seconds <= 0.0)
    {
        return std::max(0.0, seconds);
    }

    return std::clamp(seconds, 0.0, m_loaded_length_seconds);
}

bool Engine::Impl::loadedAudioEndReached(double position_seconds) const
{
    return m_loaded_length_seconds > 0.0 && m_edit->getTransport().isPlaying() &&
           position_seconds >= m_loaded_length_seconds;
}

void Engine::Impl::stopTracktionPlayback()
{
    constexpr bool discard_recordings = false;
    constexpr bool clear_devices = false;
    m_edit->getTransport().stop(discard_recordings, clear_devices);
}

void Engine::Impl::pauseTransport()
{
    stopTracktionPlayback();
}

void Engine::Impl::stopTransportAndReleaseContext()
{
    constexpr bool discard_recordings = false;
    constexpr bool clear_devices = true;
    auto& transport = m_edit->getTransport();
    m_input_meter_reader.detach();
    m_output_meter_reader.detach();
    m_master_meter_reader.detach();
    transport.stop(discard_recordings, clear_devices);
    transport.freePlaybackContext();
}

void Engine::Impl::stopTransport()
{
    auto& transport = m_edit->getTransport();
    stopTracktionPlayback();
    transport.setPosition(tracktion::TimePosition{});

    // Publishing here covers both Engine::stop() and the automatic end-of-file stop, which share
    // this path (decision: boundary publishes fire wherever the operation already lives).
    publishClockBoundary(common::core::TimePosition{});
}

void Engine::Impl::disengageLoop()
{
    // Flag and stored points clear together: loop state persists in the edit's TRANSPORT tree
    // (no engine path ever resets it), so a flag-only clear would leave a stale region that
    // resurrects on a later engage or leaks into the next arrangement. Disengaging mid-play is
    // continuous — the backend poll preserves the current position when looping turns off.
    auto& transport = m_edit->getTransport();
    transport.looping = false;
    transport.setLoopRange({});
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
    m_impl->publishClockBoundary(
        common::core::TimePosition{m_impl->clampToLoadedRange(
            transport.getPosition().inSeconds())});
}

// Stops playback and resets Tracktion's transport position to the start.
void Engine::stop()
{
    m_impl->stopTransport();
    m_impl->updateTransportState();
}

// Pauses playback without resetting position so the user can resume from the same point.
void Engine::pause()
{
    m_impl->pauseTransport();
    m_impl->updateTransportState();
    m_impl->publishClockBoundary(
        common::core::TimePosition{m_impl->clampToLoadedRange(
            m_impl->m_edit->getTransport().getPosition().inSeconds())});
}

// Moves Tracktion transport to the requested timeline position. Position-only motion is observed
// through position(), not through the coarse state listener surface.
void Engine::seek(common::core::TimePosition position)
{
    const double clamped_seconds = m_impl->clampToLoadedRange(position.seconds);
    m_impl->m_edit->getTransport().setPosition(
        tracktion::TimePosition::fromSeconds(clamped_seconds));
    m_impl->publishClockBoundary(common::core::TimePosition{clamped_seconds});
}

// v1 accepts exactly 1.0: real speed control arrives with practice mode's time-stretch work over
// the proxy-off backing clip. Rejecting loudly here (instead of silently ignoring the factor)
// keeps early consumers from shipping code that believes speed changes worked.
std::expected<void, TransportError> Engine::setPlaybackSpeed(double factor)
{
    if (factor != 1.0)
    {
        return std::unexpected{TransportError{
            TransportErrorCode::SpeedNotSupported,
            "Playback speed other than 1.0 is not supported yet",
        }};
    }

    m_impl->m_playback_speed = factor;
    return {};
}

// Reports the port-level speed factor; always 1.0 until practice-speed support lands.
double Engine::playbackSpeed() const noexcept
{
    return m_impl->m_playback_speed;
}

// Engages Tracktion transport looping over the normalized, content-clamped region. Range is
// written before the looping flag so the flag's first playhead push already carries the new
// region; position is deliberately untouched (performPlay snaps into the region on the next
// play, and a mid-play engage clamps within one ~200 ms backend poll).
std::expected<void, TransportError> Engine::setLoopRegion(common::core::TimeRange region)
{
    // Normalize before validating so a reversed drag is a usable region, never an error.
    const double normalized_start = std::min(region.start.seconds, region.end.seconds);
    const double normalized_end = std::max(region.start.seconds, region.end.seconds);

    // Clamp into the loaded content: Tracktion accepts loop points far beyond the audio, but the
    // engine's end-of-content auto-stop fires before such a loop end could ever be reached.
    const double clamped_start = m_impl->clampToLoadedRange(normalized_start);
    const double clamped_end = m_impl->clampToLoadedRange(normalized_end);

    // The 0.1 s port minimum keeps every scattered backend minimum unreachable: the 0.01 s play
    // refusal (which pops a stock warning bubble and transiently reports isPlaying() == true),
    // the 1 ms poll clamp, and the playhead's 50-sample floor.
    if (clamped_end - clamped_start < g_minimum_loop_region_duration.seconds)
    {
        return std::unexpected{TransportError{TransportErrorCode::LoopRegionTooShort}};
    }

    auto& transport = m_impl->m_edit->getTransport();
    transport.setLoopRange(
        {tracktion::TimePosition::fromSeconds(clamped_start),
         tracktion::TimePosition::fromSeconds(clamped_end)});
    transport.looping = true;
    return {};
}

// Disengages looping without touching position or play state. Delegates to the shared helper so
// the stored backend loop points can never outlive the flag.
void Engine::clearLoopRegion()
{
    m_impl->disengageLoop();
}

// Reads back through Tracktion's getLoopRange() (never the raw loop points) because only the
// getter normalizes point order. Disengaged looping reports no region even if stale points
// linger in the edit state.
std::optional<common::core::TimeRange> Engine::loopRegion() const noexcept
{
    auto& transport = m_impl->m_edit->getTransport();
    if (!transport.looping)
    {
        return std::nullopt;
    }

    const auto loop_range = transport.getLoopRange();
    return common::core::TimeRange{
        .start = common::core::TimePosition{loop_range.getStart().inSeconds()},
        .end = common::core::TimePosition{loop_range.getEnd().inSeconds()},
    };
}

// Returns the current project-owned state directly from the Tracktion adapter state.
TransportState Engine::state() const noexcept
{
    return m_impl->currentTransportState();
}

// Reads audible playback time while running so Tracktion's post-seek UI hold does not stall the
// editor cursor for the first few frames after a live seek.
common::core::TimePosition Engine::position() const noexcept
{
    auto& transport = m_impl->m_edit->getTransport();
    double position_seconds = transport.getPosition().inSeconds();
    if (transport.isPlaying())
    {
        if (auto* const playback_context = transport.getCurrentPlaybackContext();
            playback_context != nullptr)
        {
            position_seconds = playback_context->getAudibleTimelineTime().inSeconds();
        }
    }

    return common::core::TimePosition{m_impl->clampToLoadedRange(position_seconds)};
}

} // namespace rock_hero::common::audio
