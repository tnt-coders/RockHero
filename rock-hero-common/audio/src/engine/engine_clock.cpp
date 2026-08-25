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

// The one authority for a playhead discontinuity (construction, arrangement load, arrangement
// clear, seek, play, pause, stop). It publishes a message-thread boundary value so the clock is
// useful before the first audio block and after playback ends, and resyncs the tone rig, which
// follows its automation curves only while the graph renders blocks. Anything else that must
// track the playhead across a jump belongs here too, for the same reason: this is the only place
// that knows every jump happened.
//
// The playing flag is deliberately NOT that fact, and its authority is syncClockPlayingState()
// below rather than this function: playback also stops where the playhead does not move -- a
// live-rig clear, save, or load releases the playback context in place -- and no boundary fires
// there, so updateTransportState() has to carry the flag. This function syncs it as well because
// the end-of-file auto-stop publishes a boundary without notifying anyone synchronously.
void Engine::Impl::publishClockBoundary(common::core::TimePosition position)
{
    m_playback_clock.publishPosition(position, std::chrono::steady_clock::now().time_since_epoch());
    syncClockPlayingState(currentTransportState().playing);

    // Costs one predicate on a tone-less session; a loaded rig gets its parameters dragged to the
    // new position, which is what keeps rack values and the editor's readouts off a stale value
    // after a stop or a load.
    resyncToneAutomation(position);
}

// The one authority for the clock's playing flag and the republish timer, which are a single fact
// -- "is the graph moving the playhead right now" -- and so are published together. Both call
// sites reach it because neither alone sees every transition: a boundary publish covers the
// auto-stop, which notifies nobody synchronously, and updateTransportState() covers a stop or a
// resume issued outside the transport port, where the playhead never moves and no boundary fires.
void Engine::Impl::syncClockPlayingState(bool playing)
{
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

// Republishes audible playback time while playing, through the same audible-time authority
// Engine::position() reads, so the clock's consumers and the editor cursor cannot drift apart.
void Engine::Impl::publishAudibleTimeNow()
{
    m_playback_clock.publishPosition(
        audiblePositionNow(), std::chrono::steady_clock::now().time_since_epoch());
}

} // namespace rock_hero::common::audio
