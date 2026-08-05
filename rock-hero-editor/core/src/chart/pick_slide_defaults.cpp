#include "chart/pick_slide_defaults.h"

namespace rock_hero::editor::core
{

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
    note.slides = {common::core::SlideWaypoint{.offset = note.sustain, .fret = target}};
}

} // namespace rock_hero::editor::core
