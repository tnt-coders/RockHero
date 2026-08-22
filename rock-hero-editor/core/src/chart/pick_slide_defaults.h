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
\brief Default scrape endpoints, corpus-derived (plan 55 Phase 2).

Down-slides overwhelmingly start at the neck's high end (~70% at fret 13 and above) and end
low (~80% at or below fret 7); up-slides mirror it.
*/
inline constexpr int g_pick_slide_default_high_fret{17};

/*! \copydoc g_pick_slide_default_high_fret */
inline constexpr int g_pick_slide_default_low_fret{3};

/*!
\brief Default scrape sustain when the note being converted carries none: a quarter note.

Whole-note-referenced like every other duration bound (grid_arithmetic.h), never
signature-beat-referenced, so a scrape authored in 12/8 gets a real quarter note rather than an
eighth-note stub.

SIGNED 2026-08-18, and deliberately NOT the corpus median — recorded because the evidence was
weighed and set aside rather than missed. A 102-song survey measured real notated scrapes at a
median AND mode of 2 beats (~0.92 s), but from only 23 gestures across 14 files (13.7%), with
p10—p90 spanning 0.8 to 4.0 beats; the point estimate also moves to 3.0 beats once one song's
four-scrape burst is set aside, so the data cannot separate 2 from 3. Against that, a quarter
note is the natural starting length for any sustained operation and a tail is trivial to drag
longer, which makes the short default the cheaper mistake. What the corpus DOES establish firmly
is that the previous floor — an eighth of a beat, 16x below the median and half the shortest
scrape anyone charted — was wrong as a default.
*/
inline constexpr common::core::Fraction g_pick_slide_default_sustain_whole_note{1, 4};

/*!
\brief Returns the default scrape sustain in signature beats.

\param signature_denominator Note value that represents one beat (the signature's denominator).
\return The default as an exact beat fraction: one beat in x/4, two in x/8.
*/
[[nodiscard]] constexpr common::core::Fraction pickSlideDefaultSustainBeats(
    const int signature_denominator) noexcept
{
    return common::core::Fraction{
        signature_denominator * g_pick_slide_default_sustain_whole_note.numerator,
        g_pick_slide_default_sustain_whole_note.denominator
    };
}

/*!
\brief Fewest frets of downward travel that still reads as a scrape rather than a stub.

The flip point for the default direction. Measured against the DOWNWARD TARGET rather than the
nut, which is what makes the rule guarantee a gesture instead of merely room: a note at fret 5
has five frets of clearance to the nut but only two of travel to the low endpoint, and a
two-fret drag is not a scrape.
*/
inline constexpr int g_pick_slide_minimum_travel{5};

/*!
\brief Picks the default scrape direction for a note that carries no slide of its own.

A strong downward preference — the corpus is ~96% downward — flipped only when a downward drag
would have nowhere to go.

\param start_fret The note's own fret, which is the scrape's start.
\param capo The tuning's capo, below which no scrape may travel.
\return True to scrape toward the neck's high end.
*/
[[nodiscard]] bool pickSlideDefaultUpward(int start_fret, int capo) noexcept;

/*!
\brief The low default endpoint a scrape travels to, floored at the first playable fret.

One authority for the direction chooser and the path synthesis: every fret a slide gesture names
sits at or above `capo + 1` (the chart rules refuse a terminal on or below the capo), so the
corpus-derived low endpoint yields to the capo wherever the capo sits above it.

\param capo The tuning's capo.
\return The low endpoint fret for this capo.
*/
[[nodiscard]] int pickSlideDefaultLowFret(int capo) noexcept;

/*!
\brief Rebuilds an existing slide as a scrape path, keeping its frets and direction.

Preserves what the charter already drew rather than discarding it: the waypoints stay, and the
gesture's required terminal is pinned exactly at the sustain (a scrape rings no longer than its
travel). A path whose last leg is a plain waypoint promotes that waypoint to the terminal.

\param note Note to rebuild; its sustain must already be set.
\return False when the note carries no slide to convert, leaving it untouched for the default.
*/
bool convertSlideToScrapePath(common::core::ChartNote& note);

/*!
\brief Synthesizes the default scrape path onto a note, traveling away from its start fret.

Leaves `fret` alone — the note's fret is the path start — clears `slides`, and makes the whole
gesture the required unpitched `slide_out` terminal: its offset exactly at the sustain, its fret
the far default endpoint (the capo-floored low end for a downward scrape, the high end for an
upward one; a start already sitting on the far endpoint travels to the other, so the path always
moves). Turnaround waypoints are authored later, never synthesized here — a default scrape is one
straight drag.

\param note Note receiving the path; the caller owns setting the attack itself, and owns a ring
long enough to hold a gesture (\ref common::core::g_minimum_slide_window) — every note rings, so
there is no zero to extend here.
\param upward True to scrape toward the neck's high end, false toward the low end.
\param capo The tuning's capo, which floors the low endpoint (\ref pickSlideDefaultLowFret).
*/
void applyDefaultPickSlidePath(common::core::ChartNote& note, bool upward, int capo);

} // namespace rock_hero::editor::core
