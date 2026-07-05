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
    m_impl->stopTransport();
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
