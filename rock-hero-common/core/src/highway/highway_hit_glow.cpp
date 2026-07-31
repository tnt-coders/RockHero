#include <algorithm>
#include <rock_hero/common/core/highway/highway_hit_glow.h>

namespace rock_hero::common::core
{

namespace
{

// Floor for the clamped release as a fraction of the onset spacing: when onsets land closer than
// the trough guard allows, splitting the gap evenly keeps both a visible pop and a dark trough.
constexpr double g_min_release_spacing_fraction = 0.5;

} // namespace

double highwayHitGlowIntensity(const double since_seconds, const double release_seconds) noexcept
{
    if (since_seconds < 0.0 || release_seconds <= 0.0 || since_seconds >= release_seconds)
    {
        return 0.0;
    }
    // Smoothstep over the remaining fraction: full at the crossing with zero initial slope (a
    // momentary bright hold), an even mid fade, and a soft landing at zero — a gradual dissolve
    // rather than a blink (user direction 2026-07-30 replacing the front-loaded quadratic).
    const double remaining = 1.0 - (since_seconds / release_seconds);
    return remaining * remaining * (3.0 - (2.0 * remaining));
}

double highwayHitGlowRelease(
    const double nominal_release_seconds, const double trough_guard_seconds,
    const double spacing_seconds) noexcept
{
    if (spacing_seconds <= 0.0)
    {
        return nominal_release_seconds;
    }
    // An infinite spacing falls through cleanly: both candidates are infinite and the nominal
    // release wins the min.
    return std::min(
        nominal_release_seconds,
        std::max(
            spacing_seconds - trough_guard_seconds,
            spacing_seconds * g_min_release_spacing_fraction));
}

} // namespace rock_hero::common::core
