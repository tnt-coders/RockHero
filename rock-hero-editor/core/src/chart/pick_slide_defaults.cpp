#include "chart/pick_slide_defaults.h"

#include <algorithm>

namespace rock_hero::editor::core
{

bool pickSlideDefaultUpward(const int start_fret, const int capo) noexcept
{
    const int low_target = std::max(capo + 1, g_pick_slide_default_low_fret);
    return (start_fret - low_target) < g_pick_slide_minimum_travel;
}

bool scrapePathIsConvertible(const common::core::ChartNote& note) noexcept
{
    int previous_fret = note.fret;
    for (const common::core::SlideWaypoint& waypoint : note.slides)
    {
        if (waypoint.fret == previous_fret)
        {
            return false;
        }
        previous_fret = waypoint.fret;
    }
    return !note.slide_out.has_value() || note.slide_out->fret != previous_fret;
}

bool convertSlideToScrapePath(common::core::ChartNote& note)
{
    if (note.slides.empty() && !note.slide_out.has_value())
    {
        return false;
    }
    if (note.slide_out.has_value())
    {
        note.slide_out->offset = note.sustain;
        return true;
    }
    // No terminal of its own: the path's last waypoint becomes the gesture's end.
    const common::core::SlideWaypoint terminal = note.slides.back();
    note.slides.pop_back();
    note.slide_out = common::core::SlideOut{.offset = note.sustain, .fret = terminal.fret};
    return true;
}

void applyDefaultPickSlidePath(common::core::ChartNote& note, const bool upward)
{
    if (note.sustain.numerator <= 0)
    {
        note.sustain = g_minimum_slide_window;
    }
    int target = upward ? g_pick_slide_default_high_fret : g_pick_slide_default_low_fret;
    if (target == note.fret)
    {
        target = upward ? g_pick_slide_default_low_fret : g_pick_slide_default_high_fret;
    }
    // The gesture is the required slide-out terminal; turnaround waypoints are authored later.
    note.slides.clear();
    note.slide_out = common::core::SlideOut{.offset = note.sustain, .fret = target};
}

} // namespace rock_hero::editor::core
