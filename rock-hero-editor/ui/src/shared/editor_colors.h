/*!
\file editor_colors.h
\brief Editor UI colors shared by more than one editor component.
*/

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace rock_hero::editor::ui
{

// 0xff555555 is juce::Colours::darkgrey spelled as a literal. These inline globals initialize
// dynamically in whichever translation unit the linker selects, and juce::Colours entries are
// per-TU dynamically initialized globals, so reading them here races static initialization
// order across TUs: incremental builds intermittently produced a transparent (zero) color and
// the window's #323e44 default-LookAndFeel background showed through.

/*! \brief Editor window background, also the tempo band above the timeline ruler's body. */
inline const juce::Colour g_editor_background_color{0xff555555};

/*! \brief Track viewport background, reused for the seam beneath the timeline ruler. */
inline const juce::Colour g_track_viewport_color{juce::Colour{0xff555555}.darker(0.34f)};

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
