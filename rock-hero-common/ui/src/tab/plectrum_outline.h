/*!
\file plectrum_outline.h
\brief The 2D pick-scrape head's silhouette, as measured off the shipped note atlas.
*/

#pragma once

#include <array>
#include <juce_graphics/juce_graphics.h>

namespace rock_hero::common::ui
{

// Half of the plectrum silhouette, measured off the pick-slide cell of the shipped note atlas
// (g_head_cell_pick_slide) at its 0.5-coverage line — the same level the atlas's own
// fracture is pinned to — in units of the head's extent, with the silhouette's box center at the
// origin.
//
// A hand-kept table of a measurement, which is exactly the shape that drifts when the art is
// rebaked — so test_tab_paint_core.cpp re-measures the shipped PNG against every row here. The
// highway graduated its own head measurements to load time (head_art_profile.h); this stays a
// table because the lane is JUCE-painted wherever it appears and has no atlas load of its own,
// and the test is what keeps the table honest.
//
// Only the RIGHT half is stored, as the chain from the blunt top edge's right corner down to the
// tip. The art is mirror-symmetric to the last measured sample (every boundary sample's mirror
// lands on another sample, worst distance 0.000000 px), so mirroring this chain at draw time makes
// the two sides exact by construction instead of asking two authored halves to agree.
//
// The x values carry the aspect. The cell measures 31.000 x 32.997 px at that level, so scaling
// BOTH axes by the extent fits the silhouette's HEIGHT to the extent and leaves its width at
// 0.9395 of it, the art's own proportion. The head therefore stands exactly as tall as the round
// head it replaces and 6% narrower, which leaves the lane's vertical collision budget alone.
//
// A rounded triangle is not a substitute for the table: the silhouette keeps widening for nine
// rows below its topmost ink, and its widest row sits 0.1515 of the height ABOVE the box center,
// so its mass is upper-heavy in a way no three-corner rounded triangle reproduces. Sixteen points
// hold the measured outline to 0.0898 px at a 25 px note height and 0.0449 px at 12.
inline constexpr std::array<juce::Point<float>, 16> g_plectrum_half_outline{
    juce::Point<float>{0.03031f, -0.50000f}, // the blunt top edge's right corner
    juce::Point<float>{0.12122f, -0.49511f},
    juce::Point<float>{0.24245f, -0.46738f},
    juce::Point<float>{0.33336f, -0.43214f},
    juce::Point<float>{0.36367f, -0.40898f},
    // NOLINTNEXTLINE(modernize-use-std-numbers) — a measured coordinate that happens to sit
    // within the check's tolerance of log10(e).
    juce::Point<float>{0.43366f, -0.33332f},
    juce::Point<float>{0.46071f, -0.27271f},
    juce::Point<float>{0.46821f, -0.24240f},
    juce::Point<float>{0.46974f, -0.15148f}, // widest row
    juce::Point<float>{0.46054f, -0.09087f},
    juce::Point<float>{0.40451f, 0.03035f},
    juce::Point<float>{0.31390f, 0.18188f},
    juce::Point<float>{0.25008f, 0.27280f},
    juce::Point<float>{0.08321f, 0.45463f},
    juce::Point<float>{0.03031f, 0.49559f},
    juce::Point<float>{0.00000f, 0.50000f}, // the tip, on the mirror axis
};

} // namespace rock_hero::common::ui
