#include "engine_impl.h"

#include <chrono>

namespace rock_hero::common::audio
{

namespace
{

// Message-thread cadence for playback republishing. Render loops consume snapshots through the
// extrapolator, whose capture-stamp arithmetic makes cadence jitter harmless, so a modest fixed
// rate is enough; the audio-graph tap remains the recorded escalation path if measurements ever
// disagree (plan 12 decision 13).
constexpr int g_republish_hz = 60;

// File-local juce::Timer adapter driving the clock republish tick while playing. It holds a
// plain callback instead of naming Engine::Impl because Impl is a private nested type this
// non-member class cannot reach; the owning Impl builds the callback over itself. Owned through
// a plain juce::Timer pointer so the private header stays declaration-only.
class ClockRepublishTimer final : public juce::Timer
{
public:
    explicit ClockRepublishTimer(std::function<void()> tick)
        : m_tick(std::move(tick))
    {}

    // Runs on the message thread at the republish cadence, only while the transport plays.
    void timerCallback() override
    {
        m_tick();
    }

private:
    std::function<void()> m_tick;
};

} // namespace

// Reads the RockHero-owned atomic clock storage; never traverses Tracktion state. This is what
// makes the port's wait-free any-thread contract hold regardless of playback-context rebuilds.
PlaybackClockSnapshot Engine::snapshot() const noexcept
{
    return m_impl->m_playback_clock.snapshot();
}

// Publishes a message-thread boundary value (construction, arrangement load, seek, play, pause,
// stop) so the clock is useful before the first audio block and after playback ends. Boundary
// publishes also own the republish timer's lifecycle: it runs exactly while playing.
void Engine::Impl::publishClockBoundary(common::core::TimePosition position)
{
    m_playback_clock.publishPosition(position, std::chrono::steady_clock::now().time_since_epoch());
    const bool playing = currentTransportState().playing;
    m_playback_clock.publishPlaying(playing);

    if (m_clock_republish_timer == nullptr)
    {
        // The raw `this` capture is safe: the timer is owned by this Impl and is retired in
        // ~Engine before any teardown, so a tick can never outlive its owner.
        m_clock_republish_timer =
            std::make_unique<ClockRepublishTimer>([this] { publishAudibleTimeNow(); });
    }
    if (playing)
    {
        if (!m_clock_republish_timer->isTimerRunning())
        {
            m_clock_republish_timer->startTimerHz(g_republish_hz);
        }
    }
    else
    {
        m_clock_republish_timer->stopTimer();
    }
}

// Republishes audible playback time while playing. The read is the same lifetime-safe pattern
// Engine::position() proves out: the playback context pointer is only ever mutated on this
// (message) thread, and getAudibleTimelineTime() is one null check plus one atomic load
// (source-verified against the vendored engine; findings recorded in plan 12's inventory).
void Engine::Impl::publishAudibleTimeNow()
{
    auto& transport = m_edit->getTransport();
    double position_seconds = transport.getPosition().inSeconds();
    if (transport.isPlaying())
    {
        if (auto* const playback_context = transport.getCurrentPlaybackContext();
            playback_context != nullptr)
        {
            position_seconds = playback_context->getAudibleTimelineTime().inSeconds();
        }
    }

    m_playback_clock.publishPosition(
        common::core::TimePosition{clampToLoadedRange(position_seconds)},
        std::chrono::steady_clock::now().time_since_epoch());
}

} // namespace rock_hero::common::audio
