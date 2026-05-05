#include "edit_coordinator.h"

#include <cassert>

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

// Lets the backend accept the audio first, then commits the accepted values into Session.
bool EditCoordinator::setArrangementAudio(const core::AudioAsset& audio_asset)
{
    auto audio_duration = m_edit.get().loadAudio(audio_asset);
    if (!audio_duration.has_value())
    {
        return false;
    }

    const bool committed =
        m_session.setCurrentArrangementAudio(audio_asset, audio_duration.value());
    assert(committed && "Session rejected backend-accepted arrangement audio");
    if (!committed)
    {
        return false;
    }

    return true;
}

} // namespace rock_hero::ui
