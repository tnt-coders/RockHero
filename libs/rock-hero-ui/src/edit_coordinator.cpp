#include "edit_coordinator.h"

#include <cassert>
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

// Lets the backend accept the audio first, then commits the accepted value into Session.
bool EditCoordinator::setTrackAudio(core::TrackId track_id, const core::AudioAsset& audio_asset)
{
    if (m_session.findTrack(track_id) == nullptr)
    {
        return false;
    }

    auto track_audio = m_edit.get().provisionTrackAudio(track_id, audio_asset);
    if (!track_audio.has_value())
    {
        return false;
    }

    const bool committed = m_session.setTrackAudio(track_id, std::move(track_audio).value());
    assert(committed && "Session rejected backend-accepted track audio");
    if (!committed)
    {
        return false;
    }

    return true;
}

} // namespace rock_hero::ui
