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

// Derives the project timeline from stored clips instead of from backend internals.
[[nodiscard]] TimeRange calculateTimeline(const std::vector<Track>& tracks) noexcept
{
    TimeRange timeline{};
    bool found_clip = false;

    for (const Track& track : tracks)
    {
        if (!track.audio_clip.has_value())
        {
            continue;
        }

        const TimeRange clip_range = track.audio_clip->timelineRange();
        if (!found_clip)
        {
            timeline = clip_range;
            found_clip = true;
            continue;
        }

        timeline.start.seconds = std::min(timeline.start.seconds, clip_range.start.seconds);
        timeline.end.seconds = std::max(timeline.end.seconds, clip_range.end.seconds);
    }

    return timeline;
}

// Prevents one durable AudioClipId from being attached to multiple different tracks.
[[nodiscard]] bool clipIdBelongsToAnotherTrack(
    const std::vector<Track>& tracks, TrackId track_id, AudioClipId audio_clip_id) noexcept
{
    return std::ranges::any_of(tracks, [track_id, audio_clip_id](const Track& track) {
        return track.id != track_id && track.audio_clip.has_value() &&
               track.audio_clip->id == audio_clip_id;
    });
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
bool Session::addTrack(TrackId id, TrackData track_data)
{
    if (!id.isValid() || findTrackById(m_tracks, id) != nullptr)
    {
        return false;
    }

    auto& track = m_tracks.emplace_back();
    track.id = id;
    track.name = std::move(track_data.name);

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

// Allocates durable identity before an edit reaches the backend; failed edits leave gaps.
AudioClipId Session::allocateAudioClipId() noexcept
{
    const AudioClipId audio_clip_id = m_next_audio_clip_id;
    ++m_next_audio_clip_id.value;
    return audio_clip_id;
}

// Stores the current single clip payload for a track and refreshes the aggregate timeline.
bool Session::setAudioClip(TrackId id, AudioClipId audio_clip_id, AudioClipData audio_clip_data)
{
    if (!audio_clip_id.isValid() || clipIdBelongsToAnotherTrack(m_tracks, id, audio_clip_id))
    {
        return false;
    }

    auto* track = findTrackById(m_tracks, id);
    if (track == nullptr)
    {
        return false;
    }

    track->audio_clip = AudioClip{
        .id = audio_clip_id,
        .asset = std::move(audio_clip_data.asset),
        .asset_duration = audio_clip_data.asset_duration,
        .source_range = audio_clip_data.source_range,
        .position = audio_clip_data.position,
    };
    m_timeline = calculateTimeline(m_tracks);
    return true;
}

} // namespace rock_hero::core
