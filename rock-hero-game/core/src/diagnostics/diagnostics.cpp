#include "diagnostics/diagnostics.h"

#include <utility>

namespace rock_hero::game::core
{

// Dev mode starts with the overlay visible — launching with the flag is itself the request to
// see the diagnostics; the toggle exists to get them out of the way.
DiagnosticsController::DiagnosticsController(const bool dev_mode)
    : m_state{.dev_mode = dev_mode, .overlay_visible = dev_mode, .autoplay_enabled = false}
{}

const DiagnosticsState& DiagnosticsController::state() const noexcept
{
    return m_state;
}

void DiagnosticsController::toggleOverlay()
{
    if (m_state.dev_mode)
    {
        m_state.overlay_visible = !m_state.overlay_visible;
    }
}

void DiagnosticsController::toggleAutoplay()
{
    if (m_state.dev_mode)
    {
        m_state.autoplay_enabled = !m_state.autoplay_enabled;
    }
}

void DiagnosticsController::requestChartReload()
{
    if (m_state.dev_mode)
    {
        m_pending_intents.emplace_back(ReloadChartIntent{});
    }
}

void DiagnosticsController::requestSeekToSection(const std::size_t section_index)
{
    if (m_state.dev_mode)
    {
        m_pending_intents.emplace_back(SeekToSectionIntent{.section_index = section_index});
    }
}

std::vector<DiagnosticsIntent> DiagnosticsController::takePendingIntents()
{
    return std::exchange(m_pending_intents, {});
}

ChartSourceWatcher::ChartSourceWatcher(const std::chrono::nanoseconds settle_interval)
    : m_settle_interval{settle_interval}
{}

bool ChartSourceWatcher::update(
    const std::optional<std::chrono::nanoseconds> source_stamp, const std::chrono::nanoseconds now)
{
    if (!source_stamp.has_value())
    {
        return false;
    }

    // First observation primes the baseline: the consumer already loaded this content.
    if (!m_baseline_stamp.has_value())
    {
        m_baseline_stamp = source_stamp;
        return false;
    }

    if (*source_stamp == *m_baseline_stamp)
    {
        // Back to the held content (or never changed): nothing pending.
        m_pending_stamp.reset();
        return false;
    }

    // A still-moving stamp restarts the settle timer; only a stable one is worth reloading.
    if (!m_pending_stamp.has_value() || *m_pending_stamp != *source_stamp)
    {
        m_pending_stamp = source_stamp;
        m_pending_since = now;
        return false;
    }

    if (now - m_pending_since < m_settle_interval)
    {
        return false;
    }

    m_baseline_stamp = m_pending_stamp;
    m_pending_stamp.reset();
    return true;
}

} // namespace rock_hero::game::core
