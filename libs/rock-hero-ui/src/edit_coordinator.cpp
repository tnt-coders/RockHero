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

// Lets the backend accept the selected arrangement audio, then commits the song into Session.
bool EditCoordinator::loadSong(core::Song song, std::size_t selected_arrangement_index)
{
    if (song.chart.arrangements.empty() ||
        selected_arrangement_index >= song.chart.arrangements.size())
    {
        return false;
    }

    core::Arrangement& selected_arrangement = song.chart.arrangements[selected_arrangement_index];
    if (!selected_arrangement.audio_asset.has_value())
    {
        return false;
    }

    const auto audio_duration = m_edit.get().loadAudio(*selected_arrangement.audio_asset);
    if (!audio_duration.has_value() || audio_duration->seconds <= 0.0)
    {
        return false;
    }

    selected_arrangement.audio_duration = audio_duration.value();

    const bool committed = m_session.loadSong(std::move(song), selected_arrangement_index);
    assert(committed && "Session rejected backend-accepted project song");
    if (!committed)
    {
        return false;
    }

    return true;
}

// Clears backend media before resetting the framework-free session state.
void EditCoordinator::closeSong()
{
    m_edit.get().clearAudio();
    m_session.reset();
}

} // namespace rock_hero::ui
