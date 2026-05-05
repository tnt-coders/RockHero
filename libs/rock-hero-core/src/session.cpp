#include "session.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace rock_hero::core
{

namespace
{

// Preserves constness while keeping TrackId lookup logic shared by read and mutation paths.
template <typename Tracks> [[nodiscard]] auto findTrackById(Tracks& tracks, TrackId id) noexcept
{
    const auto position = std::ranges::find(tracks, id, &Track::id);
    return position == tracks.end() ? nullptr : std::to_address(position);
}

// Derives the project timeline from stored audio instead of from backend internals.
[[nodiscard]] TimeRange calculateTimeline(const std::vector<Track>& tracks) noexcept
{
    TimeRange timeline{};
    bool found_audio = false;

    for (const Track& track : tracks)
    {
        if (!track.audio.has_value())
        {
            continue;
        }

        const TimeRange audio_range = track.audio->timelineRange();
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

// Exposes ordered track storage without letting callers mutate the vector shape directly.
const std::vector<Track>& Session::tracks() const noexcept
{
    return m_tracks;
}

// Exposes loaded-content timeline mapping without letting callers mutate it independently.
TimeRange Session::timeline() const noexcept
{
    return m_timeline;
}

// Provides read-only track lookup so callers can observe session state without bypassing commands.
const Track* Session::findTrack(TrackId id) const noexcept
{
    return findTrackById(m_tracks, id);
}

// Allocates durable ids centrally; failed backend edits may leave intentional gaps.
TrackId Session::allocateTrackId() noexcept
{
    const TrackId track_id = m_next_track_id;
    ++m_next_track_id.value;
    return track_id;
}

// Stores an already allocated track id without advancing the allocator.
bool Session::addTrack(TrackId id, TrackSpec track_spec)
{
    if (!id.isValid() || findTrackById(m_tracks, id) != nullptr)
    {
        return false;
    }

    auto& track = m_tracks.emplace_back();
    track.id = id;
    track.name = std::move(track_spec.name);

    return true;
}

// Updates only the user-visible name field through the Session mutation boundary.
bool Session::renameTrack(TrackId id, std::string name)
{
    auto* track = findTrackById(m_tracks, id);
    if (track == nullptr)
    {
        return false;
    }

    track->name = std::move(name);
    return true;
}

// Stores the current full-source audio for a track and refreshes the aggregate timeline.
bool Session::setTrackAudio(TrackId id, TrackAudio audio)
{
    auto* track = findTrackById(m_tracks, id);
    if (track == nullptr)
    {
        return false;
    }

    track->audio = std::move(audio);
    m_timeline = calculateTimeline(m_tracks);
    return true;
}

} // namespace rock_hero::core
