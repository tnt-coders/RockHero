/*!
\file frame_clock.h
\brief Pure per-frame consumption of the playback clock plus frame-pacing statistics.
*/

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <rock_hero/common/audio/clock/playback_clock_extrapolator.h>
#include <rock_hero/common/audio/clock/playback_clock_snapshot.h>
#include <rock_hero/common/core/timeline/timeline.h>

namespace rock_hero::game::core
{

/*! \brief One frame's timing readout: render-ready song time plus instrumentation values. */
struct FrameClockSample
{
    /*! \brief Smoothed, render-ready playback time for this frame. */
    common::core::TimePosition song_time{};

    /*! \brief Elapsed monotonic time since the previous sample; zero on the first sample. */
    std::chrono::nanoseconds frame_delta{0};

    /*!
    \brief Age of the consumed snapshot (sample time minus its publish stamp).

    Empty until the clock publishes its first snapshot — a default snapshot carries a zero
    capture stamp, which means "nothing published", not "published at epoch".
    */
    std::optional<std::chrono::nanoseconds> snapshot_age;

    /*! \brief True when the transport was playing at the snapshot's publish time. */
    bool playing{false};
};

/*!
\brief Turns per-frame clock snapshots and monotonic timestamps into a frame timing readout.

Pure and deterministic: time arrives as arguments, never from a clock read, so tests drive it
with synthetic snapshots and timestamps. This is the game's only sanctioned path from the
playback clock to render-side song time — the frame loop must never derive song time from wall
clock or frame counting (architecture.md "Timing and Latency").

Create one instance per consuming loop; the smoothing state is meaningful only against a single
frame cadence, and there is no internal synchronization.
*/
class FrameClock
{
public:
    /*!
    \brief Creates a frame clock with the given extrapolation feel policy.
    \param policy Smoothing parameters; defaults are the plan-12 starting values.
    */
    explicit FrameClock(common::audio::PlaybackClockExtrapolationPolicy policy = {});

    /*!
    \brief Consumes this frame's snapshot and timestamp; returns the frame's timing readout.

    \param snapshot Latest playback-clock snapshot available to the loop.
    \param monotonic_now Loop-thread steady-clock timestamp for this frame.
    \return Song time plus the frame-delta and snapshot-age instrumentation values.
    */
    [[nodiscard]] FrameClockSample sample(
        const common::audio::PlaybackClockSnapshot& snapshot,
        std::chrono::nanoseconds monotonic_now);

    /*! \brief Forgets all state so the next sample() behaves like a first call. */
    void reset() noexcept;

private:
    common::audio::PlaybackClockExtrapolator m_extrapolator;

    // Previous sample's timestamp for frame-delta computation; absent until the first sample.
    bool m_has_previous{false};
    std::chrono::nanoseconds m_previous_now{0};
};

/*! \brief Aggregated frame pacing over one summary window. */
struct FramePacingSummary
{
    /*! \brief Number of frames aggregated into this summary. */
    std::int64_t frame_count{0};

    /*! \brief Sum of the aggregated frame deltas (the window's real duration). */
    std::chrono::nanoseconds window_duration{0};

    /*! \brief Smallest frame delta in the window. */
    std::chrono::nanoseconds min_delta{0};

    /*! \brief Largest frame delta in the window. */
    std::chrono::nanoseconds max_delta{0};

    /*! \brief Mean frame delta across the window. */
    std::chrono::nanoseconds average_delta{0};
};

/*!
\brief Accumulates frame deltas into periodic pacing summaries.

Keeps per-frame instrumentation cheap to log: the loop records every delta, and a bounded
summary (count, min, max, mean) emerges once per window instead of one log line per frame
carrying the aggregate burden. Pure: the window closes based on accumulated deltas, never on a
clock read.
*/
class FramePacingStats
{
public:
    /*!
    \brief Creates a pacing accumulator.
    \param summary_window Accumulated frame time that closes a window and emits a summary.
    */
    explicit FramePacingStats(std::chrono::nanoseconds summary_window = std::chrono::seconds{1});

    /*!
    \brief Records one frame delta; returns a summary when this delta closes the window.

    Non-positive deltas (the frame clock's first sample) are ignored — they carry no pacing
    information and would corrupt the minimum.

    \param frame_delta Elapsed time of the frame being recorded.
    \return The closed window's summary, or empty while the window is still filling.
    */
    [[nodiscard]] std::optional<FramePacingSummary> record(std::chrono::nanoseconds frame_delta);

private:
    std::chrono::nanoseconds m_summary_window;

    // Accumulation state for the currently filling window.
    std::int64_t m_frame_count{0};
    std::chrono::nanoseconds m_accumulated{0};
    std::chrono::nanoseconds m_min_delta{0};
    std::chrono::nanoseconds m_max_delta{0};
};

} // namespace rock_hero::game::core
