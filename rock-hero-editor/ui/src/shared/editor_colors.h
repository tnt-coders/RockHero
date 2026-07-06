/*!
\file editor_colors.h
\brief Editor UI colors shared by more than one editor component.
*/

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace rock_hero::editor::ui
{

// These are defined in editor_colors.cpp rather than inline here so they can be spelled with the
// named juce::Colours constants. Those constants are per-translation-unit dynamically initialized
// globals, and an inline variable's selected initializer is not reliably ordered after them under
// MSVC incremental linking: incremental builds intermittently left these transparent (zero) and
// the window's #323e44 default-LookAndFeel background showed through. Defining them in one
// translation unit restores the C++ same-TU initialization-order guarantee.

/*! \brief Editor window background, also the tempo band above the timeline ruler's body. */
extern const juce::Colour g_editor_background_color;

/*! \brief Track viewport background, reused for the seam beneath the timeline ruler. */
extern const juce::Colour g_track_viewport_color;

/*! \brief Sub-beat subdivision tempo grid dots, dimmer than beats against the waveform row. */
extern const juce::Colour g_subdivision_grid_color;

/*! \brief Off-beat tempo grid dots. */
extern const juce::Colour g_beat_grid_color;

/*!
\brief Downbeat tempo grid dots and the ruler's measure ticks.

Shared so the timeline grid and the ruler render measure boundaries in the same color.
*/
extern const juce::Colour g_measure_grid_color;

} // namespace rock_hero::editor::ui
