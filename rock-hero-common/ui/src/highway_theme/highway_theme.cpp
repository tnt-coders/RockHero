#include "highway_theme/highway_theme.h"

namespace rock_hero::common::ui
{

namespace
{

// The look both products draw: the vector construction in its ice-blue accent. One accent hue
// drives the chrome roles by a fixed lightness and chroma per role, on a black ground whose
// board stays nearly neutral away from the hand window.
constexpr HighwayTheme g_default_highway_theme{
    .backdrop = 0xFF000000,
    .beat_bar = 0xFF243480,
    .attack_line = 0xFFF1F5FF,
    .lit_lane = 0x495978FF,
    .lit_lane_dotted = 0x492F3F86,
    .lane_border = 0xFF6C7EB4,
    .fret_line = 0xFF1D1E22,
    .fret_line_lit = 0xFFF1F5FF,
    .fret_number_lit = 0xFFADBFF6,
    .fret_number_dim = 0x806C7EB4,
    .fret_number_target = 0xFFFFFFFF,
    .strike_fretting = 0xFFF1F5FF,
    .strike_picking = 0xFF9CA4BB,
    .chord_box = 0xFF9CA4BB,
    .chord_box_dark = 0xFF0C1331,
    .pick_furniture = 0xFFF1F5FF,
    .full_mute_mark = 0xFFF1F5FF,
    .arpeggio = 0xFFE068D8,
    .capo_shade = 0xB4000000,
    .capo_bar = 0xFFC6CDE2,
    .capo_bar_rim = 0xFF000000,
    .inlay_tint = 0xE68691B2,
    .label_text = 0xFFF1F5FF,
};

} // namespace

// Returns the compiled default theme, the one statement of its values.
const HighwayTheme& defaultHighwayTheme()
{
    return g_default_highway_theme;
}

} // namespace rock_hero::common::ui
