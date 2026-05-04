#include "edit_coordinator.h"

#include <cassert>
#include <optional>
#include <string>
#include <utility>

namespace rock_hero::ui
{

// Stores the audio port reference while owning the framework-free editor session.
EditCoordinator::EditCoordinator(audio::IEdit& edit) noexcept
    : m_edit(edit)
{}

// Exposes read-only session state so views and controllers cannot bypass edit coordination.
const core::Session& EditCoordinator::session() const noexcept
{
    return m_session;
}

// Allocates identity, binds the backend track, and commits the Session row only after success.
core::TrackId EditCoordinator::createTrack(const std::string& name)
{
    const core::TrackId track_id = m_session.allocateTrackId();
    auto track_spec = m_edit.get().provisionTrack(track_id, name);
    if (!track_spec.has_value())
    {
        return core::TrackId{};
    }

    const bool track_added = m_session.addTrack(track_id, std::move(track_spec).value());
    assert(track_added && "Session rejected a backend-accepted track id");
    if (!track_added)
    {
        return core::TrackId{};
    }

    return track_id;
}

// Allocates durable identity, lets the backend provision the clip, then commits the accepted value.
// Failed backend provisions intentionally leave the allocated id unused.
std::optional<core::AudioClipId> EditCoordinator::createAudioClip(
    core::TrackId track_id, const core::AudioAsset& audio_asset, core::TimePosition position)
{
    if (m_session.findTrack(track_id) == nullptr)
    {
        return std::nullopt;
    }

    const core::AudioClipId audio_clip_id = m_session.allocateAudioClipId();
    auto audio_clip_spec =
        m_edit.get().provisionAudioClip(track_id, audio_clip_id, audio_asset, position);
    if (!audio_clip_spec.has_value())
    {
        return std::nullopt;
    }

    const bool committed =
        m_session.setAudioClip(track_id, audio_clip_id, std::move(audio_clip_spec).value());
    assert(committed && "Session rejected a backend-accepted audio clip");
    if (!committed)
    {
        return std::nullopt;
    }

    return audio_clip_id;
}

} // namespace rock_hero::ui
