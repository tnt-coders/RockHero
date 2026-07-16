/*!
\file timing_window.h
\brief Pure hit-window math: expected note times, calibration shifts, and timing deltas.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/game/core/scoring/scoring_ruleset.h>

namespace rock_hero::game::core
{

/*!
\brief One note's onset hit window in song-time seconds, bounds inclusive.

The window lives in song time so it compares directly against playback-clock-correlated
observation times; the real-time feel is kept constant across playback speeds by scaling the
half-width when the window is built.
*/
struct HitWindow
{
    /*! \brief Earliest in-window song time in seconds. */
    double open_seconds{0.0};

    /*! \brief Latest in-window song time in seconds. */
    double close_seconds{0.0};

    /*!
    \brief Reports whether a song time falls inside the window.
    \param song_seconds Song time to test.
    \return True when the time is within the inclusive bounds.
    */
    [[nodiscard]] bool contains(double song_seconds) const noexcept;
};

/*!
\brief Builds the onset hit window for one charted note position.

The half-width is a real-time constant (the player's timing precision does not change with
playback speed), so it is scaled by the speed factor into song-time seconds: at half speed a
±100 ms real window spans ±50 ms of song time.

\param tempo_map Tempo map that resolves the note's musical position to song seconds.
\param position Charted onset position of the note.
\param ruleset Constants defining the real-time window half-width.
\param speed_factor Playback speed multiplier; 1.0 is full speed, 0.5 is half speed.
\return The note's inclusive hit window in song-time seconds.
*/
[[nodiscard]] HitWindow makeOnsetWindow(
    const common::core::TempoMap& tempo_map, common::core::GridPosition position,
    const ScoringRuleset& ruleset, double speed_factor);

/*!
\brief Maps an observed onset time to the player's intended song time.

This is the plan-13 effective-offset contract applied at consumption: the player's intent is
`t_play = t_in - audio_offset`. The offset is measured in real milliseconds, so it is scaled by
the speed factor into song-time seconds before subtracting.

\param observed_song_seconds Playback-clock-correlated song time of the detected onset.
\param audio_offset_ms Effective audio calibration offset in real milliseconds.
\param speed_factor Playback speed multiplier; 1.0 is full speed, 0.5 is half speed.
\return The intended song time in seconds, comparable against a note's hit window.
*/
[[nodiscard]] double playedSongTime(
    double observed_song_seconds, double audio_offset_ms, double speed_factor);

/*!
\brief Computes the signed real-time timing error of a played note.

\param expected_song_seconds The note's charted song time.
\param played_song_seconds The player's calibrated intent time from playedSongTime().
\param speed_factor Playback speed multiplier; 1.0 is full speed, 0.5 is half speed.
\return Timing error in real milliseconds; negative means the player was early.
*/
[[nodiscard]] double timingDeltaMs(
    double expected_song_seconds, double played_song_seconds, double speed_factor);

} // namespace rock_hero::game::core
