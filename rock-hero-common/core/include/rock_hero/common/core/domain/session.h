/*!
\file session.h
\brief Editable session state for the current song workspace.
*/

#pragma once

#include <cstddef>
#include <rock_hero/common/core/domain/arrangement.h>
#include <rock_hero/common/core/domain/song.h>
#include <rock_hero/common/core/domain/timeline.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Editable in-memory session state.

Session owns the Song currently loaded into the editor workflow. It is deliberately headless so
controllers and tests can exercise session behavior without UI, audio devices, or Tracktion. The
current editor presents one arrangement at a time; before a project is opened, the session contains
no arrangements.
*/
class Session
{
public:
    /*! \brief Creates an empty session with no loaded project arrangements. */
    Session();

    /*! \brief Clears the current song and returns the session to the no-project state. */
    void reset();

    /*!
    \brief Returns the loaded song aggregate.
    \return Current song data owned by the session.
    */
    [[nodiscard]] const Song& song() const noexcept;

    /*!
    \brief Returns arrangements in project order.
    \return Ordered arrangements owned by the session.
    */
    [[nodiscard]] const std::vector<Arrangement>& arrangements() const noexcept;

    /*!
    \brief Returns the timeline range covered by the current arrangement content.
    \return Current arrangement timeline range.
    */
    [[nodiscard]] TimeRange timeline() const noexcept;

    /*!
    \brief Returns the arrangement currently displayed by the editor.
    \return Current arrangement, or nullptr when no project is loaded.
    */
    [[nodiscard]] const Arrangement* currentArrangement() const noexcept;

    /*!
    \brief Replaces the current session song.

    The selected arrangement index is stored as session state, not as persistent arrangement
    identity. The selected arrangement must exist and every arrangement must have playable audio.

    \param song Song aggregate to make current.
    \param selected_arrangement Arrangement index displayed by the editor.
    \return True when the song was accepted.
    */
    bool loadSong(Song song, std::size_t selected_arrangement);

private:
    // Song aggregate currently loaded into the editor session.
    Song m_song;

    // Index of the arrangement currently shown by the single-arrangement editor surface.
    std::size_t m_current_arrangement_index{0};

    // Canonical timeline range for the current arrangement content.
    TimeRange m_timeline{};
};

} // namespace rock_hero::common::core
