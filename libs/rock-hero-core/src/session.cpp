#include "session.h"

#include <algorithm>
#include <utility>

namespace rock_hero::core
{

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

// Finds the mutable track so session commands can update one track without exposing the vector.
Track* Session::findTrack(TrackId id) noexcept
{
    const auto track_matches_id = [id](const Track& track) noexcept { return track.id == id; };
    const auto position = std::ranges::find_if(m_tracks, track_matches_id);
    return position == m_tracks.end() ? nullptr : &(*position);
}

// Shares lookup behavior with the mutable overload while preserving const-correct access.
const Track* Session::findTrack(TrackId id) const noexcept
{
    const auto track_matches_id = [id](const Track& track) noexcept { return track.id == id; };
    const auto position = std::ranges::find_if(m_tracks, track_matches_id);
    return position == m_tracks.end() ? nullptr : &(*position);
}

// Assigns ids centrally so tracks keep stable identities even when their contents change.
TrackId Session::addTrack(std::string name, std::optional<AudioAsset> audio_asset)
{
    auto& track = m_tracks.emplace_back();
    track.id = m_next_track_id;
    ++m_next_track_id.value;
    track.name = std::move(name);
    track.audio_asset = std::move(audio_asset);
    return track.id;
}

// Commits backend-accepted audio content and its timeline as one session-state update.
bool Session::commitTrackAudioAsset(TrackId id, AudioAsset audio_asset, TimeRange timeline_range)
{
    auto* track = findTrack(id);
    if (track == nullptr)
    {
        return false;
    }

    track->audio_asset = std::move(audio_asset);
    m_timeline = timeline_range;
    return true;
}

} // namespace rock_hero::core
