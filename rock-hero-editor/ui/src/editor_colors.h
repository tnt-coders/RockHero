/*!
\file editor_colors.h
\brief Editor UI colors shared by more than one editor component.
*/

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace rock_hero::editor::ui
{

/*! \brief Track viewport background, reused for the seam beneath the timeline ruler. */
inline const juce::Colour g_track_viewport_color{juce::Colours::darkgrey.darker(0.34f)};

/*! \brief Sub-beat subdivision tempo grid dots, dimmer than beats against the waveform row. */
inline const juce::Colour g_subdivision_grid_color{38, 38, 38};

/*! \brief Off-beat tempo grid dots. */
inline const juce::Colour g_beat_grid_color{46, 46, 46};

/*!
\brief Downbeat tempo grid dots and the ruler's measure ticks.

Shared so the timeline grid and the ruler render measure boundaries in the same color.
*/
inline const juce::Colour g_measure_grid_color{108, 108, 108};

} // namespace rock_hero::editor::ui
