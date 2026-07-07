#include "session/session.h"

#include <algorithm>
#include <utility>

namespace rock_hero::common::core
{

namespace
{

// Derives the displayed timeline from the current arrangement audio. The timeline always begins
// at the song's first beat and runs through the end of the audio, which a positive asset offset
// pushes later (silence fills the gap before the audio starts).
[[nodiscard]] TimeRange calculateTimeline(const Arrangement& arrangement) noexcept
{
    return TimeRange{
        .start = TimePosition{},
        .end = arrangement.audioTimelineRange().end,
    };
}

// Reports whether backend audio loading prepared the arrangement for a playable session.
[[nodiscard]] bool hasLoadedAudio(const Arrangement& arrangement) noexcept
{
    return !arrangement.audio_asset.path.empty() && arrangement.audio_duration.seconds > 0.0;
}

// Reports whether every arrangement has passed backend audio validation before session commit.
[[nodiscard]] bool hasLoadedAudio(const Song& song)
{
    return std::ranges::all_of(song.arrangements, [](const Arrangement& arrangement) {
        return hasLoadedAudio(arrangement);
    });
}

} // namespace

// Starts empty until a real project file has been opened.
Session::Session()
{
    reset();
}

// Restores the no-project editor session after the current project is closed.
void Session::reset()
{
    m_song = Song{};
    m_current_arrangement_index = 0;
    m_timeline = TimeRange{};
}

// Exposes the full loaded song aggregate for read-only editor and test observation.
const Song& Session::song() const noexcept
{
    return m_song;
}

// Exposes ordered arrangements without letting callers mutate the vector shape directly.
const std::vector<Arrangement>& Session::arrangements() const noexcept
{
    return m_song.arrangements;
}

// Exposes current-arrangement timeline mapping without letting callers mutate it independently.
TimeRange Session::timeline() const noexcept
{
    return m_timeline;
}

// Returns the arrangement shown by the current single-arrangement editor surface.
const Arrangement* Session::currentArrangement() const noexcept
{
    if (m_current_arrangement_index >= m_song.arrangements.size())
    {
        return nullptr;
    }

    return &m_song.arrangements[m_current_arrangement_index];
}

// Commits a fully prepared song once every arrangement has backend-loaded audio.
bool Session::loadSong(Song song, std::size_t selected_arrangement)
{
    if (song.arrangements.empty() || selected_arrangement >= song.arrangements.size())
    {
        return false;
    }

    if (!hasLoadedAudio(song))
    {
        return false;
    }

    const Arrangement& arrangement = song.arrangements[selected_arrangement];
    const TimeRange timeline = calculateTimeline(arrangement);
    m_song = std::move(song);
    m_current_arrangement_index = selected_arrangement;
    m_timeline = timeline;
    return true;
}

// Returns mutable access to the current arrangement's tone schedule.
ToneTrack* Session::currentToneTrack() noexcept
{
    if (m_current_arrangement_index >= m_song.arrangements.size())
    {
        return nullptr;
    }

    return &m_song.arrangements[m_current_arrangement_index].tone_track;
}

// Returns mutable access to the current arrangement's tone catalog.
std::vector<Tone>* Session::currentToneCatalog() noexcept
{
    if (m_current_arrangement_index >= m_song.arrangements.size())
    {
        return nullptr;
    }

    return &m_song.arrangements[m_current_arrangement_index].tones;
}

} // namespace rock_hero::common::core
