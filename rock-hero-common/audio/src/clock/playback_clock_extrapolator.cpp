#include "clock/playback_clock_extrapolator.h"

#include <algorithm>
#include <cmath>

namespace rock_hero::common::audio
{

// Stores the policy by value; the extrapolator itself carries only plain smoothing state.
PlaybackClockExtrapolator::PlaybackClockExtrapolator(PlaybackClockExtrapolationPolicy policy)
    : m_policy{policy}
{}

// One frame step. See the header for the normative policy rules this implements.
common::core::TimePosition PlaybackClockExtrapolator::advance(
    const PlaybackClockSnapshot& snapshot, std::chrono::nanoseconds monotonic_now)
{
    // Target: published position, advanced by publish-to-now elapsed time while playing. A zero
    // capture stamp means nothing was published yet, so the raw position is the target.
    double target_seconds = snapshot.position.seconds;
    if (snapshot.playing && snapshot.monotonic_capture_time.count() != 0)
    {
        const double elapsed_seconds = std::max(
            0.0,
            std::chrono::duration<double>(monotonic_now - snapshot.monotonic_capture_time).count());
        target_seconds += elapsed_seconds * snapshot.playback_rate;
    }

    if (!snapshot.playing)
    {
        // Paused/stopped holds the published position exactly; the next resume snaps via the
        // playing-state transition rule below.
        m_has_output = true;
        m_was_playing = false;
        m_last_output = snapshot.position;
        m_last_now = monotonic_now;
        return m_last_output;
    }

    const bool first_call = !m_has_output;
    const bool playback_started = !m_was_playing;
    const bool beyond_snap_threshold =
        m_has_output &&
        std::abs(target_seconds - m_last_output.seconds) > m_policy.snap_threshold.seconds;
    if (first_call || playback_started || beyond_snap_threshold)
    {
        m_has_output = true;
        m_was_playing = true;
        m_last_output = common::core::TimePosition{target_seconds};
        m_last_now = monotonic_now;
        return m_last_output;
    }

    // Continuous playback: advance by the frame delta at the published rate, then slew the
    // residual toward the target under the correction cap so jitter never reads as speed change.
    const double frame_delta_seconds =
        std::max(0.0, std::chrono::duration<double>(monotonic_now - m_last_now).count());
    double output_seconds = m_last_output.seconds + frame_delta_seconds * snapshot.playback_rate;
    const double residual = target_seconds - output_seconds;
    const double max_correction = m_policy.max_correction_rate * frame_delta_seconds;
    output_seconds += std::clamp(residual, -max_correction, max_correction);

    // Regression clamps to hold: only the snap rules above may move the output backwards.
    output_seconds = std::max(output_seconds, m_last_output.seconds);

    m_was_playing = true;
    m_last_output = common::core::TimePosition{output_seconds};
    m_last_now = monotonic_now;
    return m_last_output;
}

// Forgetting the state makes the next advance() behave exactly like a first call (snap).
void PlaybackClockExtrapolator::reset() noexcept
{
    m_has_output = false;
    m_was_playing = false;
    m_last_output = common::core::TimePosition{};
    m_last_now = std::chrono::nanoseconds{0};
}

} // namespace rock_hero::common::audio
