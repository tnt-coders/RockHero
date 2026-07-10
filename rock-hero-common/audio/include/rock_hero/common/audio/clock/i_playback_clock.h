/*!
\file i_playback_clock.h
\brief Read-only, any-thread playback-time telemetry port.
*/

#pragma once

#include <rock_hero/common/audio/clock/playback_clock_snapshot.h>

namespace rock_hero::common::audio
{

/*!
\brief Project-owned read-only playback-time boundary.

This is the timing surface render loops, scoring, and calibration sample. It is deliberately
separate from ITransport: transport verbs are side-effecting message-thread commands, while this
port is pure telemetry that any thread may read at its own cadence.

\note snapshot() is callable from any thread. Implementations must be wait-free: no locks, no
allocation, no framework calls, no traversal of backend runtime state — reads come from
project-owned atomic storage only.
*/
class IPlaybackClock
{
public:
    /*! \brief Destroys the playback clock interface. */
    virtual ~IPlaybackClock() = default;

    /*!
    \brief Reads the most recently published playback-time snapshot.
    \return Copy of the latest published snapshot; a default snapshot before any publish.
    */
    [[nodiscard]] virtual PlaybackClockSnapshot snapshot() const noexcept = 0;

protected:
    /*! \brief Creates the playback clock interface. */
    IPlaybackClock() = default;

    /*! \brief Copies the playback clock interface. */
    IPlaybackClock(const IPlaybackClock&) = default;

    /*! \brief Moves the playback clock interface. */
    IPlaybackClock(IPlaybackClock&&) = default;

    /*!
    \brief Assigns the playback clock interface from another playback clock interface.
    \return Reference to this playback clock interface.
    */
    IPlaybackClock& operator=(const IPlaybackClock&) = default;

    /*!
    \brief Move-assigns the playback clock interface from another playback clock interface.
    \return Reference to this playback clock interface.
    */
    IPlaybackClock& operator=(IPlaybackClock&&) = default;
};

} // namespace rock_hero::common::audio
