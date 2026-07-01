/*!
\file editor_colours.h
\brief Editor UI colours shared by more than one editor component.
*/

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace rock_hero::editor::ui
{

/*! \brief Track viewport background, reused for the seam beneath the timeline ruler. */
inline const juce::Colour g_track_viewport_colour{juce::Colours::darkgrey.darker(0.34f)};

/*! \brief Off-beat tempo grid dots. */
inline const juce::Colour g_beat_grid_colour{46, 46, 46};

/*!
\brief Downbeat tempo grid dots and the ruler's measure ticks.

Shared so the timeline grid and the ruler render measure boundaries in the same colour.
*/
inline const juce::Colour g_measure_grid_colour{108, 108, 108};

} // namespace rock_hero::editor::ui
