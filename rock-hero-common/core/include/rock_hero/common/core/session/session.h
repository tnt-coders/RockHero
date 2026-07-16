/*!
\file session.h
\brief Editable session state for the current song workspace.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/timeline.h>
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
    \brief Returns mutable access to the current arrangement's tone schedule.

    The narrowest mutation surface the tone editing workflow needs; broader arrangement fields
    stay read-only through this session.

    \return Current arrangement's tone track, or null when no arrangement is loaded.
    */
    [[nodiscard]] ToneTrack* currentToneTrack() noexcept;

    /*!
    \brief Returns mutable access to the current arrangement's tone catalog.

    Parallels currentToneTrack() as the second narrow mutation surface the tone editing workflow
    needs: renaming a tone and creating a tone-change region both edit the catalog, while broader
    arrangement fields stay read-only through this session.

    \return Current arrangement's tone catalog, or null when no arrangement is loaded.
    */
    [[nodiscard]] std::vector<Tone>* currentToneCatalog() noexcept;

    /*!
    \brief Returns mutable access to the current arrangement's plugin-parameter automation.

    The third narrow mutation surface the tone editing workflow needs: automation points are the
    persisted musical truth the automation lanes edit, while broader arrangement fields stay
    read-only through this session.

    \return Current arrangement's automation entries, or null when no arrangement is loaded.
    */
    [[nodiscard]] std::vector<ToneParameterAutomation>* currentToneAutomation() noexcept;

    /*!
    \brief Returns mutable access to the current arrangement's chart.

    The fourth narrow mutation surface: chart editing mutates through this accessor while broader
    arrangement fields stay read-only through this session.

    \note Acquiring mutable access counts as an edit — every non-null return advances
          chartRevision(), so a projection cache keyed on the revision can never draw a stale
          chart; the cost of an acquisition that ends up not editing is one lazy rebuild.

    \return Current arrangement's chart, or null when no arrangement or no chart is loaded.
    */
    [[nodiscard]] Chart* currentChart() noexcept;

    /*!
    \brief Returns the chart revision counter that keys chart-projection caches.

    Advances every time currentChart() hands out mutable access. View-state caches pair it with
    the arrangement id so chart edits invalidate memoized projections without any explicit
    notification path that could be forgotten.

    \return Monotonic count of mutable chart acquisitions.
    */
    [[nodiscard]] std::uint64_t chartRevision() const noexcept;

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

    // Monotonic count of mutable chart acquisitions; deliberately survives loadSong/reset so it
    // can never repeat a value a cache might still hold. Projection caches pair it with the
    // arrangement id.
    std::uint64_t m_chart_revision{0};
};

} // namespace rock_hero::common::core
