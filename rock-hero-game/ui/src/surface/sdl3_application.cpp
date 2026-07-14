#include "surface/sdl3_application.h"

#include "surface/juce_message_pump.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/game/core/frame_clock/frame_clock.h>

namespace rock_hero::game::ui
{

namespace
{

// Per-frame JUCE dispatch bound. The gate soak measured a real-world per-frame maximum of 3
// messages; this is a safety valve against a pathological self-posting burst, not a tuned value.
constexpr int g_max_juce_messages_per_frame = 256;

// Emits the Phase 3 timing channels: a per-frame trace record (dormant at the logger's default
// Info runtime level; the --dev flag lowers the level to Trace) and a once-per-second pacing
// summary at Info so steady-state logs stay readable. mirror_age_ns logs -1 while the playback
// clock has never published. Returns the pacing summary when this frame closed a window so the
// overlay can display it.
std::optional<core::FramePacingSummary> logFrameInstrumentation(
    const std::chrono::nanoseconds cpu_frame_time, const core::FrameClockSample& frame_sample,
    const std::chrono::nanoseconds frame_boundary_time,
    const std::chrono::nanoseconds previous_frame_boundary, core::FramePacingStats& pacing_stats)
{
    const std::chrono::nanoseconds boundary_delta =
        previous_frame_boundary == std::chrono::nanoseconds{0}
            ? std::chrono::nanoseconds{0}
            : frame_boundary_time - previous_frame_boundary;
    const std::int64_t mirror_age_ns = frame_sample.snapshot_age.has_value()
                                           ? frame_sample.snapshot_age->count()
                                           : std::int64_t{-1};

    RH_LOG_TRACE(
        "game.frame",
        "frame boundary_ns={} delta_ns={} bgfx_cpu_ns={} song_time_s={} mirror_age_ns={} "
        "playing={}",
        frame_boundary_time.count(),
        boundary_delta.count(),
        cpu_frame_time.count(),
        frame_sample.song_time.seconds,
        mirror_age_ns,
        frame_sample.playing);

    const std::optional<core::FramePacingSummary> summary = pacing_stats.record(boundary_delta);
    if (summary.has_value())
    {
        RH_LOG_INFO(
            "game.frame",
            "pacing frames={} window_ms={} avg_ns={} min_ns={} max_ns={} mirror_age_ns={}",
            summary->frame_count,
            std::chrono::duration_cast<std::chrono::milliseconds>(summary->window_duration).count(),
            summary->average_delta.count(),
            summary->min_delta.count(),
            summary->max_delta.count(),
            mirror_age_ns);
    }
    return summary;
}

} // namespace

SDL3Application::SDL3Application(std::optional<std::uint64_t> frame_limit)
    : m_frame_limit(frame_limit)
{}

int SDL3Application::run()
{
    if (const std::optional<int> init_result = onInit(); init_result.has_value())
    {
        return *init_result;
    }

    // The L2 frame loop: freshest input first, JUCE callbacks settled before the frame is built,
    // then the vsynced present paces the whole loop.
    while (true)
    {
        if (onInput() == FrameControl::Quit)
        {
            break;
        }

        drainPendingJuceMessages(g_max_juce_messages_per_frame);

        const FrameTiming timing = onFrame(m_last_pacing_summary);
        ++m_frames_submitted;

        // Post-frame stamp: the frame boundary that paces the loop. It is a pacing anchor, not
        // photon time — bgfx presents the PREVIOUS frame at the top of each submit, and DXGI
        // queues presents ahead; plan 13's video-offset calibration owns that quasi-constant
        // chain (magnitudes recorded in the plan-20 Phase 3 record).
        const std::chrono::nanoseconds frame_boundary_time =
            std::chrono::steady_clock::now().time_since_epoch();
        const std::optional<core::FramePacingSummary> summary = logFrameInstrumentation(
            timing.cpu_frame_time,
            timing.sample,
            frame_boundary_time,
            m_previous_frame_boundary,
            m_pacing_stats);
        if (summary.has_value())
        {
            m_last_pacing_summary = summary;
        }
        m_previous_frame_boundary = frame_boundary_time;

        if (m_frame_limit.has_value() && m_frames_submitted >= *m_frame_limit)
        {
            break;
        }
    }

    onShutdown();
    return 0;
}

} // namespace rock_hero::game::ui
