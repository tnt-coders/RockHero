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
\brief Loads one theme directory and validates every part of it.

The directory's name is the theme's id and must be `[a-z0-9-]+`. Its `theme.json` holds a
non-blank `name` and a `colors` object with every HighwayTheme role, keyed by its member name, plus
`strings`: exactly one opaque `#RRGGBB` per string the chart format allows, highest string first.
The file is strict: an unknown key or role, a bad color, a wrong-length or translucent `strings`,
and an `inherits` key are all refused. The colors must pass checkHighwayThemeGuardrails and the art
beside the file, named by common::core::HighwayTexture, the texture contract. Any other file in the
directory, such as a license notice, is ignored.

\param theme_directory The theme's directory.
\return The loaded theme, or the first failure, its message prefixed with the directory.
*/
[[nodiscard]] std::expected<LoadedHighwayTheme, HighwayThemeError> loadHighwayTheme(
    const std::filesystem::path& theme_directory);

} // namespace rock_hero::common::ui
