#include "edit_coordinator.h"

#include <algorithm>
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

namespace
{

// Resolves persisted arrangement IDs to the current song order, falling back to the first item.
[[nodiscard]] std::size_t resolveSelectedArrangement(
    const core::Song& song, const std::optional<std::string>& selected_arrangement)
{
    if (!selected_arrangement.has_value())
    {
        return 0;
    }

    const auto found = std::ranges::find_if(
        song.chart.arrangements, [&selected_arrangement](const core::Arrangement& arrangement) {
            return arrangement.id == *selected_arrangement;
        });
    if (found == song.chart.arrangements.end())
    {
        return 0;
    }

    return static_cast<std::size_t>(std::distance(song.chart.arrangements.begin(), found));
}

} // namespace

// Lets the backend accept the selected arrangement audio, then commits the song into Session.
bool EditCoordinator::loadSong(
    core::Song song, const std::optional<std::string>& selected_arrangement)
{
    if (song.chart.arrangements.empty())
    {
        return false;
    }

    const std::size_t selected_index = resolveSelectedArrangement(song, selected_arrangement);
    core::Arrangement& arrangement = song.chart.arrangements[selected_index];
    if (!arrangement.audio_asset.has_value())
    {
        return false;
    }

    const auto audio_duration = m_edit.get().loadAudio(*arrangement.audio_asset);
    if (!audio_duration.has_value() || audio_duration->seconds <= 0.0)
    {
        return false;
    }

    arrangement.audio_duration = audio_duration.value();

    const bool committed = m_session.loadSong(std::move(song), selected_index);
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
