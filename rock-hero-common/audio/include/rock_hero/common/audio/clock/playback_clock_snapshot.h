/*!
\file playback_clock_snapshot.h
\brief Wait-free snapshot value of audio-derived playback time.
*/

#pragma once

#include <chrono>
#include <compare>
#include <rock_hero/common/core/timeline/timeline.h>

namespace rock_hero::common::audio
{

/*!
\brief One published reading of audio-derived playback time.

Snapshots are plain values copied out of the clock's atomic storage. Fields are published
independently, so a reader may observe position and playing from different audio blocks; consumers
that ever need a coherent multi-field record get it through a storage upgrade behind the same
port, not through this type.

The capture stamp lets consumers extrapolate: while playing, the current playback time is
approximately `position + (now - monotonic_capture_time) * playback_rate` on the same steady
clock.
*/
struct PlaybackClockSnapshot
{
    /*! \brief Audio-derived playback position on the song timeline. */
    common::core::TimePosition position{};

    /*!
    \brief Steady-clock stamp taken when this value was published.

    A zero stamp means nothing has been published yet; consumers treat the snapshot as a plain
    current value and skip extrapolation.
    */
    std::chrono::nanoseconds monotonic_capture_time{0};

    /*! \brief Playback speed factor; 1.0 until a product publishes real speed control. */
    double playback_rate{1.0};

    /*! \brief True when the transport was playing at publish time. */
    bool playing{false};

    /*!
    \brief Compares two snapshots by all stored fields.
    \param lhs Left-hand snapshot.
    \param rhs Right-hand snapshot.
    \return True when every stored field is equal.
    */
    // Not defaulted: the generated comparison would use direct floating-point == on
    // playback_rate, which is promoted to a build error by -Wfloat-equal under the shared
    // warning policy. std::is_eq(lhs <=> rhs) preserves exact equality semantics while avoiding
    // that compiler diagnostic (same rationale as TimePosition::operator==).
    friend constexpr bool operator==(
        const PlaybackClockSnapshot& lhs, const PlaybackClockSnapshot& rhs) noexcept
    {
        return lhs.position == rhs.position &&
               lhs.monotonic_capture_time == rhs.monotonic_capture_time &&
               std::is_eq(lhs.playback_rate <=> rhs.playback_rate) && lhs.playing == rhs.playing;
    }
};

} // namespace rock_hero::common::audio
