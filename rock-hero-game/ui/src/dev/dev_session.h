/*!
\file dev_session.h
\brief Development fixture session: --dev-package loading, hot reload, seeks, stand-in clock.
*/

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/clock/playback_clock_snapshot.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/game/core/diagnostics/diagnostics.h>
#include <string>
#include <vector>

namespace rock_hero::game::ui
{

/*!
\brief Owns the --dev-package fixture for one shell run.

Everything here is development machinery that later plans replace wholesale: plan 26's library
supplies real song selection, plan 21's engine supplies the real playback clock. Until then this
unit loads the package's first charted arrangement, publishes a stand-in clock that plays from
load, watches the package source for edits (chart hot-reload, plan 20 Phase 4), and executes the
diagnostics layer's seek-to-section intents by re-anchoring that clock.
*/
class DevSession
{
public:
    /*!
    \brief Loads the package and starts the stand-in clock at song time zero.

    \param package_path .rock package file or unpacked package directory.
    \param lefty True to mirror the highway for left-handed display.
    \param now Monotonic timestamp anchoring the stand-in clock.
    \return The session, or empty when the package has no loadable charted arrangement.
    */
    [[nodiscard]] static std::optional<DevSession> create(
        std::filesystem::path package_path, bool lefty, std::chrono::nanoseconds now);

    /*!
    \brief Hands the initially loaded view state to the renderer exactly once.
    \return The loaded state on the first call; empty afterwards.
    */
    [[nodiscard]] std::optional<common::core::HighwayViewState> takeLoadedViewState();

    /*!
    \brief Polls the package source for settled edits (throttled internally).

    \param now Monotonic timestamp of this frame.
    \return A freshly reprojected view state when a settled change reloaded cleanly.
    */
    [[nodiscard]] std::optional<common::core::HighwayViewState> pollForReload(
        std::chrono::nanoseconds now);

    /*!
    \brief Reloads the package immediately (the explicit reload intent).

    \param now Monotonic timestamp of this frame.
    \return The reprojected view state, or empty when the reload failed (previous content stands).
    */
    [[nodiscard]] std::optional<common::core::HighwayViewState> reload(
        std::chrono::nanoseconds now);

    /*!
    \brief The stand-in playback clock snapshot for this frame.
    \param now Monotonic timestamp of this frame.
    \return A playing snapshot whose position advances in real time from the clock anchor.
    */
    [[nodiscard]] common::audio::PlaybackClockSnapshot clockSnapshotAt(
        std::chrono::nanoseconds now) const;

    /*!
    \brief Re-anchors the stand-in clock so song time equals the section's start.
    \param section_index Index into the loaded chart's section list; out of range is ignored.
    \param now Monotonic timestamp of this frame.
    */
    void seekToSection(std::size_t section_index, std::chrono::nanoseconds now);

    /*!
    \brief Index of the first section starting after the given song time.
    \param seconds Current song time.
    \return Section index, or empty when no later section exists.
    */
    [[nodiscard]] std::optional<std::size_t> sectionAfter(double seconds) const;

    /*!
    \brief Index of the nearest section starting before the given song time.

    A small grace window skips the section currently underway, so repeated presses step
    backwards instead of re-snapping to the same boundary.

    \param seconds Current song time.
    \return Section index, or empty when no earlier section exists.
    */
    [[nodiscard]] std::optional<std::size_t> sectionBefore(double seconds) const;

    /*!
    \brief Absolute start time of one loaded section, for driving a real transport seek.

    The stand-in clock's own seekToSection covers clock-only dev runs; gameplay-session runs
    seek the engine transport instead and need the target seconds.

    \param section_index Index into the loaded chart's section list.
    \return Section start in seconds, or empty when the index is out of range.
    */
    [[nodiscard]] std::optional<double> sectionStartSeconds(std::size_t section_index) const;

    /*!
    \brief Id of the arrangement the fixture chose for display.

    The gameplay session must load the SAME arrangement so the audible tone rig matches the
    chart on screen (the fixture prefers a charted guitar part; sessions default to the first
    arrangement otherwise).

    \return Chosen arrangement id, or empty before a successful load.
    */
    [[nodiscard]] const std::string& chosenArrangementId() const noexcept;

private:
    DevSession(std::filesystem::path package_path, bool lefty);

    // Reads the package and projects its first charted arrangement; records the chosen
    // arrangement id (non-const for exactly that reason).
    [[nodiscard]] std::optional<common::core::HighwayViewState> loadViewState();

    // Rebuilds the watched-source list (directory packages watch song.json plus every chart).
    void refreshWatchedFiles();

    // Newest write stamp across the watched sources; empty when any probe fails mid-replace.
    [[nodiscard]] std::optional<std::chrono::nanoseconds> probeSourceStamp() const;

    std::filesystem::path m_package_path;
    bool m_lefty{false};

    // Arrangement id the display projection chose; the session loads the same arrangement.
    std::string m_chosen_arrangement_id;

    std::vector<std::filesystem::path> m_watched_files;
    std::vector<common::core::HighwaySectionView> m_sections;
    std::optional<common::core::HighwayViewState> m_loaded_state;

    core::ChartSourceWatcher m_watcher;
    std::chrono::nanoseconds m_last_probe_time{0};

    // Monotonic time at which the stand-in clock reads song time zero.
    std::chrono::nanoseconds m_clock_anchor{0};
};

} // namespace rock_hero::game::ui
