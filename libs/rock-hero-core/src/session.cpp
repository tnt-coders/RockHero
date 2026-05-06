#include "session.h"

#include <utility>

namespace rock_hero::core
{

namespace
{

// Derives the displayed timeline from the current arrangement audio.
[[nodiscard]] TimeRange calculateTimeline(const Arrangement& arrangement) noexcept
{
    return arrangement.audioTimelineRange();
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

// Exposes current-arrangement timeline mapping without letting callers mutate it independently.
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

// Commits a fully prepared song once the selected arrangement has playable audio.
bool Session::loadSong(Song song, std::size_t selected_arrangement_index)
{
    if (song.chart.arrangements.empty() ||
        selected_arrangement_index >= song.chart.arrangements.size())
    {
        return false;
    }

    const Arrangement& selected_arrangement = song.chart.arrangements[selected_arrangement_index];
    if (!selected_arrangement.hasAudio())
    {
        return false;
    }

    const TimeRange timeline = calculateTimeline(selected_arrangement);
    m_song = std::move(song);
    m_current_arrangement_index = selected_arrangement_index;
    m_timeline = timeline;
    return true;
}

} // namespace rock_hero::core
