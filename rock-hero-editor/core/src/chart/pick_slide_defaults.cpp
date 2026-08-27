#include "chart/pick_slide_defaults.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/chart/chart_rules.h>

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
    if (note.slide_out.has_value())
    {
        // Already terminated, and a terminal ends the ring by definition, so there is nothing to
        // re-place: the gesture follows whatever the sustain is.
        return true;
    }
    // No terminal of its own: the path's last STATED FRET becomes the gesture's end, and that
    // statement leaves the array — with its keyframe when nothing else was stated there, and only
    // that one. A keyframe that ARRIVED stating nothing is illegal data the rules refuse rather
    // than litter to sweep up, which is the same reading stripKeyframeChannels takes (chart.h).
    for (auto keyframe = note.keyframes.rbegin(); keyframe != note.keyframes.rend(); ++keyframe)
    {
        // Bound to a local so the optional check and the access are provably the same object.
        std::optional<int>& fret = keyframe->fret;
        if (!fret.has_value())
        {
            continue;
        }
        note.slide_out = *fret;
        fret.reset();
        if (common::core::keyframeStatesNothing(*keyframe))
        {
            note.keyframes.erase(std::next(keyframe).base());
        }
        break;
    }
    // False leaves the note untouched for the default path: a note whose keyframes state only
    // bends or shakes has no travel to rebuild a scrape from.
    return note.slide_out.has_value();
}

void applyDefaultPickSlidePath(common::core::ChartNote& note, const bool upward, const int capo)
{
    // The low endpoint is the capo-floored one the direction chooser already reasons with, so a
    // downward scrape under a high capo terminates at the first playable fret rather than at a
    // fret the chart rules refuse.
    const int low_fret = pickSlideDefaultLowFret(capo);
    int target = upward ? g_pick_slide_default_high_fret : low_fret;
    if (target == note.fret)
    {
        target = upward ? low_fret : g_pick_slide_default_high_fret;
    }
    // The gesture is the required slide-out terminal; turnaround keyframes are authored later.
    note.keyframes.clear();
    note.slide_out = target;
}

} // namespace rock_hero::editor::core
