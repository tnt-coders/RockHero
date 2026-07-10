/*!
\file playback_clock_extrapolator.h
\brief Consumer-side smoothing policy turning clock snapshots into per-frame monotonic time.
*/

#pragma once

#include <chrono>
#include <rock_hero/common/audio/clock/playback_clock_snapshot.h>
#include <rock_hero/common/core/timeline/timeline.h>

namespace rock_hero::common::audio
{

/*! \brief Tunable feel parameters for PlaybackClockExtrapolator. */
struct PlaybackClockExtrapolationPolicy
{
    /*!
    \brief Drift slew limit as a fraction of elapsed frame time.

    While playing continuously, the output corrects toward the published target by at most this
    fraction of each frame delta, so small publisher jitter never shows as visible speed change.
    */
    double max_correction_rate{0.05};

    /*! \brief Output snaps straight to the target once it lags or leads by more than this. */
    common::core::TimeDuration snap_threshold{0.120};
};

/*!
\brief Turns playback-clock snapshots plus a consumer's monotonic clock into smooth frame time.

Pure, deterministic, and allocation-free: time is injected, so tests drive it with synthetic
snapshots and timestamps. Create one instance per consumer thread; there is no internal
synchronization, and the smoothing state is meaningful only against one frame cadence.

Policy rules (normative for every render-side consumer):
- While playing, the target is `position + (now - capture_time) * rate`, elapsed clamped
  non-negative; an unpublished capture stamp (zero) makes the target the raw position.
- Paused or stopped output is the published position exactly; a seek while paused lands exactly.
- The output snaps to the target on the first call, on a playing-state transition, and whenever
  the target and previous output diverge beyond the snap threshold (seeks, loop wraps, stalls).
- Otherwise the output advances by `frame_delta * rate` with the residual slewed toward the
  target under max_correction_rate, and never moves backwards during continuous playback.
*/
class PlaybackClockExtrapolator
{
public:
    /*!
    \brief Creates an extrapolator with the given feel policy.
    \param policy Tunable smoothing parameters; defaults are the project starting values.
    */
    explicit PlaybackClockExtrapolator(PlaybackClockExtrapolationPolicy policy = {});

    /*!
    \brief Consumes the latest snapshot and the consumer's monotonic now; returns smoothed time.
    \param snapshot Latest playback-clock snapshot available to this consumer.
    \param monotonic_now Consumer-thread steady-clock timestamp for this frame.
    \return Smoothed, render-ready playback time for this frame.
    */
    [[nodiscard]] common::core::TimePosition advance(
        const PlaybackClockSnapshot& snapshot, std::chrono::nanoseconds monotonic_now);

    /*! \brief Forgets all smoothing state so the next advance() snaps like a first call. */
    void reset() noexcept;

private:
    PlaybackClockExtrapolationPolicy m_policy;

    // Smoothing state: previous output and its frame timestamp, plus the playing flag observed
    // on the previous call so start/resume transitions snap.
    bool m_has_output{false};
    bool m_was_playing{false};
    common::core::TimePosition m_last_output{};
    std::chrono::nanoseconds m_last_now{0};
};

} // namespace rock_hero::common::audio
