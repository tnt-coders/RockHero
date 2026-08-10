/*!
\file pick_slide_defaults.h
\brief One authority for the default pick-slide path shared by import and editing.

Guitar Pro carriers encode only a direction and the editor's attack verb starts from a note
with no path at all, so both synthesize the same corpus-derived default scrape from this seam —
the two can never drift apart.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>

namespace rock_hero::editor::core
{

/*!
\brief Sub-beat step keeping a degenerate gesture payload strictly after its predecessor.

The minimum span a glide, slide-out, or scrape leg may occupy: zero-length gestures have
nowhere to travel, so synthesis and compression floor on this window.
*/
inline constexpr common::core::Fraction g_minimum_slide_window{1, 8};

/*!
\brief Default scrape endpoints, corpus-derived (plan 55 Phase 2).

Down-slides overwhelmingly start at the neck's high end (~70% at fret 13 and above) and end
low (~80% at or below fret 7); up-slides mirror it.
*/
inline constexpr int g_pick_slide_default_high_fret{17};

/*! \copydoc g_pick_slide_default_high_fret */
inline constexpr int g_pick_slide_default_low_fret{3};

/*!
\brief Synthesizes the default scrape path onto a note, traveling away from its start fret.

Leaves `fret` alone — the note's fret is the path start — clears `slides`, and makes the whole
gesture the required unpitched `slide_out` terminal: its offset exactly at the sustain, its fret
the far default endpoint (the low end for a downward scrape, the high end for an upward one; a
start already sitting on the far endpoint travels to the other, so the path always moves). A zero
sustain first extends to the minimum slide window so the path has somewhere to go. Turnaround
waypoints are authored later, never synthesized here — a default scrape is one straight drag.

\param note Note receiving the path; the caller owns setting the attack itself.
\param upward True to scrape toward the neck's high end, false toward the low end.
*/
void applyDefaultPickSlidePath(common::core::ChartNote& note, bool upward);

} // namespace rock_hero::editor::core
