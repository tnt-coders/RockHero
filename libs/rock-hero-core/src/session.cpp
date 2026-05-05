#include "session.h"

#include <algorithm>
#include <utility>

namespace rock_hero::core
{

namespace
{

// Derives the project timeline from stored arrangement audio instead of from backend internals.
[[nodiscard]] TimeRange calculateTimeline(const std::vector<Arrangement>& arrangements) noexcept
{
    TimeRange timeline{};
    bool found_audio = false;

    for (const Arrangement& arrangement : arrangements)
    {
        if (!arrangement.hasAudio())
        {
            continue;
        }

        const TimeRange audio_range = arrangement.audioTimelineRange();
        if (!found_audio)
        {
            timeline = audio_range;
            found_audio = true;
            continue;
        }

        timeline.start.seconds = std::min(timeline.start.seconds, audio_range.start.seconds);
        timeline.end.seconds = std::max(timeline.end.seconds, audio_range.end.seconds);
    }

    return timeline;
}

} // namespace

// Exposes ordered arrangements without letting callers mutate the vector shape directly.
const std::vector<Arrangement>& Session::arrangements() const noexcept
{
    return m_arrangements;
}

// Exposes loaded-content timeline mapping without letting callers mutate it independently.
TimeRange Session::timeline() const noexcept
{
    return m_timeline;
}

// Returns the arrangement shown by the current single-arrangement editor surface.
const Arrangement* Session::currentArrangement() const noexcept
{
    return m_arrangements.empty() ? nullptr : &m_arrangements.front();
}

// Stores the current backing audio on the displayed arrangement and refreshes the timeline.
bool Session::setCurrentArrangementAudio(AudioAsset audio_asset, TimeDuration audio_duration)
{
    auto* arrangement = m_arrangements.empty() ? nullptr : &m_arrangements.front();
    if (arrangement == nullptr || audio_asset.path.empty() || audio_duration.seconds <= 0.0)
    {
        return false;
    }

    arrangement->audio_asset = std::move(audio_asset);
    arrangement->audio_duration = audio_duration;
    m_timeline = calculateTimeline(m_arrangements);
    return true;
}

} // namespace rock_hero::core
