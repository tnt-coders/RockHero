/*!
\file highway_theme_file.h
\brief The one loader that reads a theme directory in the theme file format.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/common/ui/highway_theme/highway_theme_registry.h>
#include <rock_hero/common/ui/highway_theme/loaded_highway_theme.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>
#include <span>
#include <string_view>

namespace rock_hero::common::ui
{

/*!
\brief Parses a CSS hex color, `#RRGGBB` (opaque) or `#RRGGBBAA`, in either letter case.

Alpha comes last, as CSS writes it, so a color pasted from any CSS tool reads as authored; the
packed result moves it to the front.

\param text The color as the theme file writes it.
\return The packed 0xAARRGGBB color, or empty when the text is neither form.
*/
[[nodiscard]] std::optional<ArgbColor> parseCssHexColor(std::string_view text);

/*!
\brief Loads one theme directory, resolves it against its base, and validates every part of it.

The directory's name is the theme's id and must be `[a-z0-9-]+`. Its `theme.json` holds a
non-blank `name` and a `colors` object with every HighwayTheme role, keyed by its member name, plus
`strings`: exactly one opaque `#RRGGBB` per string the chart format allows, highest string first.
Beside it lies the art, named by common::core::HighwayTexture.

A file whose `inherits` names one of \p bases may leave out any of that: each role it omits is the
base's, and `strings` and each art file it omits are the base's whole, so it registers no palette
of its own. `name` is never inherited. The file is strict either way: an unknown key or role, a
bad color, a wrong-length or translucent `strings`, and an `inherits` naming no base are refused.

The resolved colors must pass checkHighwayThemeGuardrails and the resolved art the texture
contract. Any other file in the directory, such as a license notice, is ignored.

An optional `look` chooses how the finished frame is presented: `{"style": "plain"}`,
`{"style": "pixel", "scale": 2-8, "scanlines": 0-1}` with `scanlines` defaulting to 0,
`{"style": "dither", "scale": 1-8}` with `scale` defaulting to 1, or
`{"style": "halftone", "cell": 4-16}` with `cell` defaulting to 8; every size is a whole number,
and a style takes only its own keys. A theme that leaves `look` out takes its base's, or the plain
look; one that states it replaces the base's whole.

\param theme_directory The theme's directory.
\param bases The themes this one may inherit: the built-ins for a user theme, none for a built-in.
\return The loaded theme, or the first failure, its message prefixed with the directory.
*/
[[nodiscard]] std::expected<LoadedHighwayTheme, HighwayThemeError> loadHighwayTheme(
    const std::filesystem::path& theme_directory, std::span<const LoadedHighwayTheme> bases);

} // namespace rock_hero::common::ui
