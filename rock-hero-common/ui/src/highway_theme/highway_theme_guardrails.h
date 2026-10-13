/*!
\file highway_theme_guardrails.h
\brief The legibility checks a theme's colors must pass before any surface draws with them.
*/

#pragma once

#include <expected>
#include <rock_hero/common/ui/highway_theme/highway_theme.h>
#include <rock_hero/common/ui/highway_theme/highway_theme_registry.h>
#include <rock_hero/common/ui/highway_theme/tab_shape_inks.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>

namespace rock_hero::common::ui
{

/*!
\brief Refuses colors that collapse a distinction the notation reads by color alone.

Four distinctions are carried by color: the kind of span and the hand that holds it (on the
highway's boxes and rails, chord against arpeggio, each against its picking-hand counterpart, the
two picking-hand colors against each other; in the tab lane, its chord ink against its arpeggio
ink); the three states of a fret number (fret_number_lit, the dim number derived from
lane_line_lit, fret_number_target, pairwise); and the two hands' strike pops (strike_fretting,
strike_picking). Each pair must lie a minimum CIELAB distance apart, and so must every two string
colors. The lit fret line must also be lighter than the unlit one by a margin, since the hand window
is read from it, and each tab ink dark enough to carry the white label its span chip prints.

Colors are compared as drawn, a derived color included, but without their alpha: a role's alpha is
an opacity the renderer scales further, while the distinction lives in the color.

\param colors The theme's color roles.
\param tab_inks The tab lane's span inks.
\param strings The theme's string colors.
\return Nothing, or a GuardrailViolated error naming the first failed check and its measure.
*/
[[nodiscard]] std::expected<void, HighwayThemeError> checkHighwayThemeGuardrails(
    const HighwayTheme& colors, const TabShapeInks& tab_inks, const StringColorPalette& strings);

} // namespace rock_hero::common::ui
