#include "audio_device_sample_rate_choice.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace rock_hero::editor::ui
{

namespace
{

constexpr double g_sample_rate_match_tolerance{0.001};

// True when the rate list contains a value close enough to be considered the same selection.
[[nodiscard]] bool containsSampleRate(const std::vector<double>& rates, double rate)
{
    return std::ranges::any_of(rates, [rate](double available) {
        return std::abs(available - rate) < g_sample_rate_match_tolerance;
    });
}

// Picks 48 kHz, then 44.1 kHz, then the first available rate as the studio-standard fallback.
[[nodiscard]] double fallbackRate(const std::vector<double>& rates)
{
    constexpr std::array preferred_rates{48000.0, 44100.0};
    for (const double rate : preferred_rates)
    {
        if (containsSampleRate(rates, rate))
        {
            return rate;
        }
    }
    return rates.empty() ? 0.0 : rates.front();
}

} // namespace

double chooseDeviceSampleRate(
    const std::vector<double>& available_rates, double staged_rate, double preview_device_rate,
    std::optional<double> active_route_rate)
{
    for (const double candidate : {staged_rate, preview_device_rate})
    {
        if (candidate > 0.0 && containsSampleRate(available_rates, candidate))
        {
            return candidate;
        }
    }

    if (active_route_rate.has_value() && *active_route_rate > 0.0 &&
        containsSampleRate(available_rates, *active_route_rate))
    {
        return *active_route_rate;
    }

    return fallbackRate(available_rates);
}

} // namespace rock_hero::editor::ui
