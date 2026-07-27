#include "preview/preview_time_model.h"

#include <cmath>

namespace rock_hero::editor::ui
{

namespace
{

// Paused navigation glides: the displayed time settles exponentially toward the marker-rule
// target with this time constant (~95% settled after three constants), so a caret step reads as
// motion down the highway rather than a cut. Frame-rate independent via 1 - exp(-dt / tau); within
// the snap tolerance the glide pins to the target so it provably terminates.
constexpr double g_paused_glide_time_constant_seconds = 0.06;
constexpr double g_paused_glide_snap_seconds = 1.0e-3;

} // namespace

double PreviewTimeModel::advance(
    const common::audio::PlaybackClockSnapshot& snapshot, const double paused_target_seconds,
    const std::chrono::nanoseconds now, const double dt_seconds)
{
    if (snapshot.playing)
    {
        const double song_seconds = m_extrapolator.advance(snapshot, now).seconds;
        // Pin the glide to the played time so a pause resumes seamlessly from the stop point.
        m_displayed_seconds = song_seconds;
        return song_seconds;
    }

    m_extrapolator.reset();
    if (!m_displayed_seconds.has_value())
    {
        m_displayed_seconds = paused_target_seconds;
    }
    else
    {
        const double mix = 1.0 - std::exp(-dt_seconds / g_paused_glide_time_constant_seconds);
        *m_displayed_seconds += (paused_target_seconds - *m_displayed_seconds) * mix;
        if (std::abs(paused_target_seconds - *m_displayed_seconds) < g_paused_glide_snap_seconds)
        {
            m_displayed_seconds = paused_target_seconds;
        }
    }
    return *m_displayed_seconds;
}

void PreviewTimeModel::resetForSnap() noexcept
{
    m_extrapolator.reset();
    m_displayed_seconds.reset();
}

} // namespace rock_hero::editor::ui
