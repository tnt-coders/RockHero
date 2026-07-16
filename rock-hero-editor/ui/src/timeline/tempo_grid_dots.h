/*!
\file tempo_grid_dots.h
\brief Shared dotted tempo-grid painter for the timeline canvas and the ruler's grid header.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief Draws subdivision, beat, and measure grid dots from per-rank column positions.

One dot column per grid line, restricted to the current paint's repaint clip: the clip only trims
which columns and dot rows get drawn, never the geometry itself, so results stay stable regardless
of how much of the surface repaints. The scrolling canvas and the pinned ruler's grid header both
draw through this one painter, which is what keeps the header reading as a seamless continuation
of the canvas grid.

\param g Graphics context of the component being painted.
\param subdivision_grid_x Subdivision-rank column x positions relative to the bounds' left edge.
\param beat_grid_x Beat-rank column x positions relative to the bounds' left edge.
\param measure_grid_x Measure-rank column x positions relative to the bounds' left edge.
\param bounds Rectangle the dot columns fill vertically; also anchors the dot-row phase.
*/
void drawTempoGridDots(
    juce::Graphics& g, const std::vector<int>& subdivision_grid_x,
    const std::vector<int>& beat_grid_x, const std::vector<int>& measure_grid_x,
    juce::Rectangle<int> bounds);

} // namespace rock_hero::editor::ui
