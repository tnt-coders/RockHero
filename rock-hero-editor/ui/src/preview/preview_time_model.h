/*!
\file preview_time_model.h
\brief Headless policy for the preview's displayed song time.
*/

#pragma once

#include <chrono>
#include <optional>
#include <rock_hero/common/audio/clock/playback_clock_extrapolator.h>
#include <rock_hero/common/audio/clock/playback_clock_snapshot.h>

namespace rock_hero::editor::ui
{

/*!
\brief Turns playback-clock snapshots into the preview's per-frame displayed song time.

While playing, the time is the plan-12 extrapolated clock (raw transport reads shimmer on a moving
field). While paused, it glides toward the caller-supplied marker target with a short exponential
settle so a caret step reads as motion down the highway rather than a cut; within a snap tolerance
the glide pins to the target so it provably terminates. Pausing pins the glide to the played time
so playback resumes seamlessly from the stop point.

Pure and injected-time: no clocks or ports are read here, so the paused/resume policy the preview
window depends on is unit-testable without a GPU frame callback. Owned by PreviewSurface, which
resolves the paused target (armed caret, else transport position) and feeds this model.
*/
class PreviewTimeModel
{
public:
    /*!
    \brief Advances the displayed time by one frame.
    \param snapshot Latest playback-clock snapshot.
    \param paused_target_seconds Marker-rule target used while paused; ignored while playing.
    \param now Consumer-thread steady-clock timestamp for this frame.
    \param dt_seconds Seconds since the previous frame; zero forces no glide progress this frame.
    \return The song time in seconds to render this frame.
    */
    [[nodiscard]] double advance(
        const common::audio::PlaybackClockSnapshot& snapshot, double paused_target_seconds,
        std::chrono::nanoseconds now, double dt_seconds);

    /*!
    \brief Forgets glide and extrapolation state so the next paused frame snaps to its target.

    Used on first bring-up and when resuming after a hidden gap, where a slew across the gap would
    read as a jump rather than settled motion.
    */
    void resetForSnap() noexcept;

private:
    common::audio::PlaybackClockExtrapolator m_extrapolator;

    // The paused glide's displayed time, settling toward the marker-rule target each frame; empty
    // forces a snap (first frame, and resumes after a hidden gap must not slew).
    std::optional<double> m_displayed_seconds;
};

} // namespace rock_hero::editor::ui
