/*!
\file song_select_menu.h
\brief Headless state machine for picking a song and arrangement to play.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <rock_hero/game/core/input/menu_action.h>
#include <rock_hero/game/core/library/library_index.h>
#include <span>
#include <string>

namespace rock_hero::game::core
{

/*! \brief Which list the song-select menu is showing. */
enum class SongSelectScreen : std::uint8_t
{
    /*! \brief The list of songs in the library. */
    SongList,

    /*! \brief The list of arrangements for the chosen song. */
    ArrangementList,
};

/*! \brief The song and arrangement the player confirmed for play. */
struct SongSelectLaunch
{
    /*! \brief Absolute path to the chosen `.rock` package. */
    std::filesystem::path package_path;

    /*! \brief Chosen arrangement id; empty selects the song's first arrangement. */
    std::string arrangement_id;
};

/*!
\brief Drives song then arrangement selection over a scanned library.

Pure and headless: navigation and selection are method calls driven by resolved MenuAction values,
and the state (which screen, which row) is observable so a presenter renders it. Accepting an
arrangement produces a one-shot launch request the shell drains to start a gameplay session. Empty
libraries are handled — every navigation and accept is a safe no-op.
*/
class SongSelectMenu
{
public:
    /*!
    \brief Creates the menu over a scanned library, starting on the song list at the first row.
    \param library The scanned song library to browse.
    */
    explicit SongSelectMenu(LibraryIndex library);

    /*!
    \brief Applies one resolved menu action (navigate, accept, or back).
    \param action The action to apply.
    */
    void handle(MenuAction action);

    /*! \brief The list currently shown. */
    [[nodiscard]] SongSelectScreen screen() const noexcept;

    /*! \brief The scanned library, for rendering the song list. */
    [[nodiscard]] const LibraryIndex& library() const noexcept;

    /*! \brief Index of the highlighted song row. */
    [[nodiscard]] std::size_t selectedSongIndex() const noexcept;

    /*! \brief Index of the highlighted arrangement row (meaningful on the arrangement screen). */
    [[nodiscard]] std::size_t selectedArrangementIndex() const noexcept;

    /*!
    \brief The song currently highlighted (song list) or chosen (arrangement list).
    \return The entry, or nullptr when the library is empty.
    */
    [[nodiscard]] const LibraryEntry* currentSong() const;

    /*!
    \brief The arrangements of the current song, for rendering the arrangement list.
    \return The arrangement summaries, or empty when there is no current song.
    */
    [[nodiscard]] std::span<const LibraryArrangementSummary> currentArrangements() const;

    /*!
    \brief Takes the pending launch request produced by accepting an arrangement.
    \return The launch request once, then nothing until another arrangement is accepted.
    */
    [[nodiscard]] std::optional<SongSelectLaunch> takeLaunch();

private:
    // Wraps an index within a count (no-op when count is zero), for list navigation.
    [[nodiscard]] static std::size_t stepIndex(std::size_t index, std::size_t count, bool forward);

    LibraryIndex m_library;
    SongSelectScreen m_screen{SongSelectScreen::SongList};
    std::size_t m_song_index{0};
    std::size_t m_arrangement_index{0};
    std::optional<SongSelectLaunch> m_launch;
};

} // namespace rock_hero::game::core
