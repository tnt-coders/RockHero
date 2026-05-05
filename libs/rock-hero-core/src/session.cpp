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

// Starts with one arrangement shell until a real project file has been opened.
Session::Session()
{
    m_song.chart.arrangements.push_back(Arrangement{});
}

// Exposes the full loaded song aggregate for read-only editor and test observation.
const Song& Session::song() const noexcept
{
    return m_song;
}

// Exposes ordered arrangements without letting callers mutate the vector shape directly.
const std::vector<Arrangement>& Session::arrangements() const noexcept
{
    return m_song.chart.arrangements;
}

// Exposes loaded-content timeline mapping without letting callers mutate it independently.
TimeRange Session::timeline() const noexcept
{
    return m_timeline;
}

// Returns the arrangement shown by the current single-arrangement editor surface.
const Arrangement* Session::currentArrangement() const noexcept
{
    if (m_current_arrangement_index >= m_song.chart.arrangements.size())
    {
        return nullptr;
    }

    return &m_song.chart.arrangements[m_current_arrangement_index];
}

// Commits a fully prepared song into the session and derives the display timeline from it.
bool Session::loadSong(Song song, std::size_t selected_arrangement_index)
{
    if (song.chart.arrangements.empty() ||
        selected_arrangement_index >= song.chart.arrangements.size())
    {
        return false;
    }

    m_song = std::move(song);
    m_current_arrangement_index = selected_arrangement_index;
    m_timeline = calculateTimeline(m_song.chart.arrangements);
    return true;
}

} // namespace rock_hero::core
