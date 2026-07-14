#include "menu/song_select_menu.h"

#include <utility>

namespace rock_hero::game::core
{

SongSelectMenu::SongSelectMenu(LibraryIndex library)
    : m_library(std::move(library))
{}

std::size_t SongSelectMenu::stepIndex(
    const std::size_t index, const std::size_t count, const bool forward)
{
    if (count == 0)
    {
        return 0;
    }
    if (forward)
    {
        return (index + 1) % count;
    }
    return (index + count - 1) % count;
}

void SongSelectMenu::handle(const MenuAction action)
{
    if (m_screen == SongSelectScreen::SongList)
    {
        switch (action)
        {
            case MenuAction::NavigateUp:
                m_song_index = stepIndex(m_song_index, m_library.entries.size(), false);
                break;
            case MenuAction::NavigateDown:
                m_song_index = stepIndex(m_song_index, m_library.entries.size(), true);
                break;
            case MenuAction::Accept:
                if (!m_library.entries.empty())
                {
                    m_arrangement_index = 0;
                    m_screen = SongSelectScreen::ArrangementList;
                }
                break;
            case MenuAction::NavigateLeft:
            case MenuAction::NavigateRight:
            case MenuAction::Back:
            case MenuAction::PauseMenu:
            case MenuAction::Rescan:
                break;
        }
        return;
    }

    const LibraryEntry* const song = currentSong();
    const std::size_t arrangement_count = song != nullptr ? song->arrangements.size() : 0;
    switch (action)
    {
        case MenuAction::NavigateUp:
            m_arrangement_index = stepIndex(m_arrangement_index, arrangement_count, false);
            break;
        case MenuAction::NavigateDown:
            m_arrangement_index = stepIndex(m_arrangement_index, arrangement_count, true);
            break;
        case MenuAction::Accept:
            if (song != nullptr && m_arrangement_index < arrangement_count)
            {
                m_launch = SongSelectLaunch{
                    .package_path = song->package_path,
                    .arrangement_id = song->arrangements[m_arrangement_index].id,
                };
            }
            break;
        case MenuAction::Back:
            m_screen = SongSelectScreen::SongList;
            break;
        case MenuAction::NavigateLeft:
        case MenuAction::NavigateRight:
        case MenuAction::PauseMenu:
        case MenuAction::Rescan:
            break;
    }
}

SongSelectScreen SongSelectMenu::screen() const noexcept
{
    return m_screen;
}

const LibraryIndex& SongSelectMenu::library() const noexcept
{
    return m_library;
}

std::size_t SongSelectMenu::selectedSongIndex() const noexcept
{
    return m_song_index;
}

std::size_t SongSelectMenu::selectedArrangementIndex() const noexcept
{
    return m_arrangement_index;
}

const LibraryEntry* SongSelectMenu::currentSong() const
{
    if (m_song_index >= m_library.entries.size())
    {
        return nullptr;
    }
    return &m_library.entries[m_song_index];
}

std::span<const LibraryArrangementSummary> SongSelectMenu::currentArrangements() const
{
    const LibraryEntry* const song = currentSong();
    if (song == nullptr)
    {
        return {};
    }
    return song->arrangements;
}

std::optional<SongSelectLaunch> SongSelectMenu::takeLaunch()
{
    return std::exchange(m_launch, std::nullopt);
}

} // namespace rock_hero::game::core
