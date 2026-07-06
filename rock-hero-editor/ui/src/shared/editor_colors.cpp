#include "shared/editor_colors.h"

namespace rock_hero::editor::ui
{

// Definitions live in this single translation unit so the juce::Colours constants included above
// are dynamically initialized before these globals; see the header comment for the incremental-
// link ordering failure that inline definitions produced.

const juce::Colour g_editor_background_color{juce::Colours::darkgrey};

const juce::Colour g_track_viewport_color{juce::Colours::darkgrey.darker(0.34f)};

const juce::Colour g_subdivision_grid_color{38, 38, 38};

const juce::Colour g_beat_grid_color{46, 46, 46};

const juce::Colour g_measure_grid_color{108, 108, 108};

} // namespace rock_hero::editor::ui
