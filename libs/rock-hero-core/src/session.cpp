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

// Assigns ids centrally so tracks keep stable identities even when their contents change.
TrackId Session::addTrack(std::string name)
{
    auto& track = m_tracks.emplace_back();
    track.id = m_next_track_id;
    ++m_next_track_id.value;
    track.name = std::move(name);
    return track.id;
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

// Commits backend-accepted audio content and its timeline as one session-state update.
bool Session::commitTrackAudioAsset(TrackId id, AudioAsset audio_asset, TimeRange timeline_range)
{
    auto* track = findTrackById(m_tracks, id);
    if (track == nullptr)
    {
        return false;
    }

    track->audio_asset = std::move(audio_asset);
    m_timeline = timeline_range;
    return true;
}

} // namespace rock_hero::core
