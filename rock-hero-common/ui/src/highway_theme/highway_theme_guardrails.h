/*!
\file highway_theme_guardrails.h
\brief The legibility checks a theme's colors must pass before any surface draws with them.
*/

#pragma once

#include <expected>
#include <rock_hero/common/ui/highway_theme/highway_theme.h>
#include <rock_hero/common/ui/highway_theme/highway_theme_registry.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>

namespace rock_hero::common::ui
{

/*!
\brief Refuses colors that collapse a distinction the notation reads by color alone.

Four distinctions are carried by color: the fretting hand's chord box against a tapped chord's
(chord_box, pick_furniture), an arpeggio's rails against a held shape's (arpeggio, lane_border),
the three states of a fret number (fret_number_lit, fret_number_dim, fret_number_target, pairwise),
and the two hands' strike pops (strike_fretting, strike_picking). Each pair must lie a minimum
CIELAB distance apart, and so must every two string colors. The lit fret line must also be
lighter than the unlit one by a margin, since the hand window is read from it.

Colors are compared as authored, without their alpha: a role's alpha is an opacity the renderer
scales further, while the distinction lives in the color.

\param colors The theme's color roles.
\param strings The theme's string colors.
\return Nothing, or a GuardrailViolated error naming the first failed check and its measure.
*/
[[nodiscard]] std::expected<void, HighwayThemeError> checkHighwayThemeGuardrails(
    const HighwayTheme& colors, const StringColorPalette& strings);

} // namespace rock_hero::common::ui
