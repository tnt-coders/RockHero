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
    if (common::core::endStatedFretOrNull(note) != nullptr)
    {
        // Already terminated: the terminal is the keyframe at the ring's end, and the gesture
        // follows whatever the sustain is. NOTE-LOCAL: clause 3 of the arrival relation excludes a
        // scrape on either side, so a scrape's end always slides out and there is nothing to
        // resolve (\ref common::core::arrivesIntoNextHead).
        return true;
    }
    // No terminal of its own: the path's last STATED FRET becomes the gesture's end, and that
    // statement leaves its own instant — never the end itself, which the early return above
    // handles — to be restated at the ring's end. A keyframe left bare stays as the leg boundary
    // it is: it ends a vibrato where one ran into it, and the commit law sweeps it where it says
    // nothing.
    for (auto keyframe = note.keyframes.rbegin(); keyframe != note.keyframes.rend(); ++keyframe)
    {
        // Bound to a local so the optional check and the access are provably the same object.
        std::optional<int>& fret = keyframe->fret;
        if (!fret.has_value())
        {
            continue;
        }
        const int terminal = *fret;
        fret.reset();
        common::core::setSlideOut(note, terminal);
        break;
    }
    // False leaves the note untouched for the default path: a note whose keyframes state only
    // bends or vibrates has no travel to rebuild a scrape from.
    return common::core::endStatedFretOrNull(note) != nullptr;
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
    // The gesture is the required terminal — the slide-out keyframe at the ring's end; turnaround
    // keyframes are authored later.
    note.keyframes.clear();
    common::core::setSlideOut(note, target);
}

} // namespace rock_hero::editor::core
