#include "highway_theme/highway_theme.h"

namespace rock_hero::common::ui
{

namespace
{

// The look both products draw. The board chrome, the fret numbers, the chord-box family and the
// arpeggio purple are Charter's values.
constexpr HighwayTheme g_default_highway_theme{
    .backdrop = 0xFF000000,
    .beat_bar = 0xFF0F3B5E,
    .attack_line = 0xFF00D2D5,
    .lit_lane = 0x402590E8,
    .lit_lane_dotted = 0x40185C94,
    .lane_border = 0xFF07928F,
    .fret_line = 0xFF202020,
    .fret_line_lit = 0xFFC0C0C0,
    .fret_number_lit = 0xFF87DDF6,
    .fret_number_dim = 0x8007928F,
    .fret_number_target = 0xFFFFA821,
    // A hot orange whose blue-channel lift lets a peak white out over already-lit content.
    .strike_fretting = 0xFFFFB040,
    .strike_picking = 0xFFFFFFFF,
    .chord_box = 0xFF00D2D5,
    .chord_box_dark = 0xFF003C3D,
    // A white as bright as the teal box beside it, matched in PERCEIVED lightness (CIELAB L* with
    // the Fairchild-Pirrotta Helmholtz-Kohlrausch correction), never in luma: a saturated color
    // looks brighter than its luminance, so a luma-matched neutral looks darker and reads grey.
    // The 8th string's achromatic grey, cooled a touch (a faint blue cast reads white where a
    // neutral reads dirty on the cool board) and lifted to the teal box's L* of 81.
    .pick_furniture = 0xFFC0C7D8,
    .full_mute_mark = 0xFF52798A,
    .arpeggio = 0xFFC040FF,
    .capo_shade = 0xB4000000,
    .capo_bar = 0xFFB9BEC6,
    .capo_bar_rim = 0xFF14161A,
    .inlay_tint = 0xFFFFFFFF,
    .label_text = 0xFFFFFFFF,
};

} // namespace

// Returns the compiled default theme, the one statement of its values.
const HighwayTheme& defaultHighwayTheme()
{
    return g_default_highway_theme;
}

} // namespace rock_hero::common::ui
