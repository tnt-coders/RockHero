#include "chart/pick_slide_defaults.h"

#include <algorithm>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>

namespace rock_hero::editor::core
{

bool pickSlideDefaultUpward(const int start_fret, const int capo) noexcept
{
    return (start_fret - pickSlideDefaultLowFret(capo)) < g_pick_slide_minimum_travel;
}

int pickSlideDefaultLowFret(const int capo) noexcept
{
    return std::max(common::core::firstPlayableFret(capo), g_pick_slide_default_low_fret);
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

void applyDefaultPickSlidePath(common::core::ChartNote& note, const bool upward, const int capo)
{
    if (note.sustain.numerator <= 0)
    {
        note.sustain = common::core::g_minimum_slide_window;
    }
    // The low endpoint is the capo-floored one the direction chooser already reasons with, so a
    // downward scrape under a high capo terminates at the first playable fret rather than at a
    // fret the chart rules refuse.
    const int low_fret = pickSlideDefaultLowFret(capo);
    int target = upward ? g_pick_slide_default_high_fret : low_fret;
    if (target == note.fret)
    {
        target = upward ? low_fret : g_pick_slide_default_high_fret;
    }
    // The gesture is the required slide-out terminal; turnaround waypoints are authored later.
    note.slides.clear();
    note.slide_out = common::core::SlideOut{.offset = note.sustain, .fret = target};
}

} // namespace rock_hero::editor::core
