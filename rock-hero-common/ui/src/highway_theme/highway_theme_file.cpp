#include "highway_theme/highway_theme_file.h"

#include "highway/highway_texture_contract.h"
#include "highway_theme/highway_theme_guardrails.h"
#include "highway_theme/highway_theme_roles.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <juce_core/juce_core.h>
#include <optional>
#include <rock_hero/common/core/highway/highway_resources.h>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/ui/highway_theme/highway_render_look.h>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace rock_hero::common::ui
{

namespace
{

using common::core::Json;

constexpr std::string_view g_theme_file_name = "theme.json";
constexpr std::string_view g_name_key = "name";
constexpr std::string_view g_colors_key = "colors";
constexpr std::string_view g_inherits_key = "inherits";
constexpr std::string_view g_strings_key = "strings";
constexpr std::string_view g_look_key = "look";
constexpr std::string_view g_style_key = "style";
constexpr std::string_view g_scale_key = "scale";
constexpr std::string_view g_scanlines_key = "scanlines";
constexpr std::string_view g_line_height_key = "line_height";
constexpr std::string_view g_cell_key = "cell";

// The keys the top level of a theme file may hold.
constexpr std::array g_theme_keys{g_name_key, g_colors_key, g_inherits_key, g_look_key};

// The keys each style's `look` may hold: its style, then its own parameters.
constexpr std::array g_plain_look_keys{g_style_key};
constexpr std::array g_pixel_look_keys{g_style_key, g_scale_key, g_scanlines_key};
constexpr std::array g_crt_look_keys{g_style_key, g_line_height_key, g_scanlines_key};
constexpr std::array g_dither_look_keys{g_style_key, g_scale_key};
constexpr std::array g_halftone_look_keys{g_style_key, g_cell_key};

// The keys `colors` may hold: every role, read from the one role table, then the string colors.
constexpr auto g_color_keys = [] {
    std::array<std::string_view, g_highway_theme_roles.size() + 1> keys{};
    std::ranges::transform(g_highway_theme_roles, keys.begin(), &HighwayThemeRole::name);
    keys.back() = g_strings_key;
    return keys;
}();

// The fully opaque alpha byte of a packed color.
constexpr ArgbColor g_opaque_alpha = 0xFF000000U;

// A theme id is a folder name every platform spells the same: lowercase letters, digits, hyphens.
[[nodiscard]] bool isThemeId(const std::string_view id)
{
    return !id.empty() && std::ranges::all_of(id, [](const char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '-';
    });
}

// Reads a whole file; empty when it is absent, unreadable or empty, which a theme treats alike.
[[nodiscard]] std::optional<juce::MemoryBlock> readFile(const std::filesystem::path& path)
{
    const juce::File file = common::core::juceFileFromPath(path);
    juce::MemoryBlock block;
    if (!file.existsAsFile() || !file.loadFileAsData(block) || block.getSize() == 0)
    {
        return std::nullopt;
    }
    return block;
}

// Refuses the first key of `object` that `known` does not list; `where` names the object.
[[nodiscard]] std::expected<void, HighwayThemeError> refuseUnknownKeys(
    const juce::DynamicObject& object, const std::span<const std::string_view> known,
    const std::string_view where)
{
    for (const juce::NamedValueSet::NamedValue& property : object.getProperties())
    {
        const std::string key = property.name.toString().toStdString();
        if (std::ranges::find(known, key) == known.end())
        {
            return std::unexpected{HighwayThemeError{
                HighwayThemeErrorCode::UnknownKey,
                std::format("{} holds the unknown key \"{}\"", where, key),
            }};
        }
    }
    return {};
}

// Finds the theme `inherits` names among the bases; null when the file inherits nothing.
[[nodiscard]] std::expected<const LoadedHighwayTheme*, HighwayThemeError> readBase(
    const juce::var& document, const std::span<const LoadedHighwayTheme> bases)
{
    const juce::var& inherits = Json::value(document, g_inherits_key);
    if (inherits.isVoid())
    {
        return nullptr;
    }
    // A value that is not a string names no theme, like an id no base has.
    const std::string base_id = inherits.isString() ? inherits.toString().toStdString() : "";
    const auto base = std::ranges::find(bases, base_id, &LoadedHighwayTheme::id);
    if (base == bases.end())
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::UnknownBase,
            std::format(
                "\"inherits\" is {}, which names no theme this one may inherit: a user theme "
                "inherits a built-in, and a built-in inherits nothing",
                juce::JSON::toString(inherits, true).toStdString()),
        }};
    }
    return &*base;
}

// Reads every color role from `colors`, through the one role table; a role the file leaves out
// is the base's.
[[nodiscard]] std::expected<HighwayTheme, HighwayThemeError> readRoles(
    const juce::var& colors, const LoadedHighwayTheme* const base)
{
    HighwayTheme theme = base != nullptr ? base->colors : HighwayTheme{};
    for (const HighwayThemeRole& role : g_highway_theme_roles)
    {
        const juce::var& value = Json::value(colors, role.name);
        if (value.isVoid() && base != nullptr)
        {
            continue;
        }
        if (value.isVoid())
        {
            return std::unexpected{HighwayThemeError{
                HighwayThemeErrorCode::MissingKey,
                std::format("colors lacks the role \"{}\"", role.name),
            }};
        }
        const std::optional<ArgbColor> color =
            value.isString() ? parseCssHexColor(value.toString().toStdString()) : std::nullopt;
        if (!color.has_value())
        {
            return std::unexpected{HighwayThemeError{
                HighwayThemeErrorCode::InvalidColor,
                std::format(
                    "colors.{} is {}, not a #RRGGBB or #RRGGBBAA color",
                    role.name,
                    juce::JSON::toString(value, true).toStdString()),
            }};
        }
        theme.*role.member = *color;
    }
    return theme;
}

// Reads `strings`: one opaque color per string the format allows, highest string first.
[[nodiscard]] std::expected<StringColorPalette, HighwayThemeError> readStrings(
    const juce::var& strings)
{
    StringColorPalette palette;
    const juce::Array<juce::var>* const entries = strings.getArray();
    if (entries == nullptr || std::cmp_not_equal(entries->size(), palette.colors.size()))
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::InvalidStringColors,
            std::format(
                "colors.strings must list exactly {} colors, highest string first",
                palette.colors.size()),
        }};
    }
    for (std::size_t index = 0; index < palette.colors.size(); ++index)
    {
        const juce::var& entry = entries->getReference(static_cast<int>(index));
        const std::optional<ArgbColor> color =
            entry.isString() ? parseCssHexColor(entry.toString().toStdString()) : std::nullopt;
        if (!color.has_value())
        {
            return std::unexpected{HighwayThemeError{
                HighwayThemeErrorCode::InvalidColor,
                std::format(
                    "colors.strings entry {} is {}, not a #RRGGBB color",
                    index + 1,
                    juce::JSON::toString(entry, true).toStdString()),
            }};
        }
        // A string color is drawn at full strength on every surface, so it carries no alpha.
        if ((*color & g_opaque_alpha) != g_opaque_alpha)
        {
            return std::unexpected{HighwayThemeError{
                HighwayThemeErrorCode::InvalidStringColors,
                std::format(
                    "colors.strings entry {} is {}; a string color must be opaque #RRGGBB",
                    index + 1,
                    entry.toString().toStdString()),
            }};
        }
        palette.colors.at(index) = *color;
    }
    return palette;
}

// Reads the theme's palette: its own `strings`, named by the theme, or else its base's whole.
[[nodiscard]] std::expected<HighwayThemePalette, HighwayThemeError> readPalette(
    const juce::var& colors, const std::string& id, const std::string& name,
    const LoadedHighwayTheme* const base)
{
    const juce::var& strings = Json::value(colors, g_strings_key);
    if (strings.isVoid() && base != nullptr)
    {
        return base->palette;
    }
    if (strings.isVoid())
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::MissingKey,
            "colors lacks \"strings\"",
        }};
    }
    return readStrings(strings).transform([&id, &name](const StringColorPalette& palette) {
        return HighwayThemePalette{.source_id = id, .name = name, .colors = palette};
    });
}

// Reads look.<key> as a whole number from `low` to `high`; an absent key is `fallback`, or refused
// when the style requires it.
[[nodiscard]] std::expected<int, HighwayThemeError> readLookWholeNumber(
    const juce::var& look, const std::string_view key, const int low, const int high,
    const std::optional<int> fallback)
{
    const juce::var& value = Json::value(look, key);
    if (value.isVoid())
    {
        if (fallback.has_value())
        {
            return *fallback;
        }
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::MissingKey,
            std::format("look lacks \"{}\"", key),
        }};
    }
    const std::optional<std::int64_t> number = Json::tryReadInt64(look, key);
    if (!number.has_value() || *number < low || *number > high)
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::InvalidLook,
            std::format(
                "look.{} is {}, not a whole number from {} to {}",
                key,
                juce::JSON::toString(value, true).toStdString(),
                low,
                high),
        }};
    }
    return static_cast<int>(*number);
}

// Reads look.<key> as a number from 0 to 1; an absent key is `fallback`.
[[nodiscard]] std::expected<double, HighwayThemeError> readLookFraction(
    const juce::var& look, const std::string_view key, const double fallback)
{
    const juce::var& value = Json::value(look, key);
    if (value.isVoid())
    {
        return fallback;
    }
    const std::optional<double> number = Json::tryReadDouble(look, key);
    // Written as "inside" rather than "outside" so a NaN, were JSON ever to carry one, is refused.
    if (!number.has_value() || !(*number >= 0.0 && *number <= 1.0))
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::InvalidLook,
            std::format(
                "look.{} is {}, not a number from 0 to 1",
                key,
                juce::JSON::toString(value, true).toStdString()),
        }};
    }
    return *number;
}

// Reads `look`: absent is the base's look, or the plain look without a base; a stated block
// replaces the base's whole and is strict, its keys checked against its own style's.
[[nodiscard]] std::expected<HighwayRenderLook, HighwayThemeError> readLook(
    const juce::var& document, const LoadedHighwayTheme* const base)
{
    const juce::var& look = Json::value(document, g_look_key);
    if (look.isVoid())
    {
        return base != nullptr ? base->look : HighwayRenderLook{HighwayPlainLook{}};
    }
    const juce::DynamicObject* const object = look.getDynamicObject();
    if (object == nullptr)
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::MalformedJson,
            "\"look\" must be a JSON object",
        }};
    }
    const juce::var& style = Json::value(look, g_style_key);
    if (style.isVoid())
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::MissingKey,
            "look lacks \"style\"",
        }};
    }
    const std::string style_name = style.isString() ? style.toString().toStdString() : "";

    if (style_name == "plain")
    {
        if (std::expected<void, HighwayThemeError> keys =
                refuseUnknownKeys(*object, g_plain_look_keys, g_look_key);
            !keys.has_value())
        {
            return std::unexpected{std::move(keys.error())};
        }
        return HighwayPlainLook{};
    }
    if (style_name == "pixel")
    {
        if (std::expected<void, HighwayThemeError> keys =
                refuseUnknownKeys(*object, g_pixel_look_keys, g_look_key);
            !keys.has_value())
        {
            return std::unexpected{std::move(keys.error())};
        }
        const std::expected<int, HighwayThemeError> scale = readLookWholeNumber(
            look, g_scale_key, g_pixel_look_min_scale, g_pixel_look_max_scale, std::nullopt);
        if (!scale.has_value())
        {
            return std::unexpected{scale.error()};
        }
        const std::expected<double, HighwayThemeError> scanlines =
            readLookFraction(look, g_scanlines_key, 0.0);
        if (!scanlines.has_value())
        {
            return std::unexpected{scanlines.error()};
        }
        return HighwayPixelLook{.scale = *scale, .scanlines = *scanlines};
    }
    if (style_name == "crt")
    {
        if (std::expected<void, HighwayThemeError> keys =
                refuseUnknownKeys(*object, g_crt_look_keys, g_look_key);
            !keys.has_value())
        {
            return std::unexpected{std::move(keys.error())};
        }
        const std::expected<int, HighwayThemeError> line_height = readLookWholeNumber(
            look,
            g_line_height_key,
            g_crt_look_min_line_height,
            g_crt_look_max_line_height,
            g_crt_look_default_line_height);
        if (!line_height.has_value())
        {
            return std::unexpected{line_height.error()};
        }
        const std::expected<double, HighwayThemeError> scanlines =
            readLookFraction(look, g_scanlines_key, g_crt_look_default_scanlines);
        if (!scanlines.has_value())
        {
            return std::unexpected{scanlines.error()};
        }
        return HighwayCrtLook{.line_height = *line_height, .scanlines = *scanlines};
    }
    if (style_name == "dither")
    {
        if (std::expected<void, HighwayThemeError> keys =
                refuseUnknownKeys(*object, g_dither_look_keys, g_look_key);
            !keys.has_value())
        {
            return std::unexpected{std::move(keys.error())};
        }
        const std::expected<int, HighwayThemeError> scale = readLookWholeNumber(
            look,
            g_scale_key,
            g_dither_look_min_scale,
            g_dither_look_max_scale,
            g_dither_look_default_scale);
        if (!scale.has_value())
        {
            return std::unexpected{scale.error()};
        }
        return HighwayDitherLook{.scale = *scale};
    }
    if (style_name == "halftone")
    {
        if (std::expected<void, HighwayThemeError> keys =
                refuseUnknownKeys(*object, g_halftone_look_keys, g_look_key);
            !keys.has_value())
        {
            return std::unexpected{std::move(keys.error())};
        }
        const std::expected<int, HighwayThemeError> cell = readLookWholeNumber(
            look,
            g_cell_key,
            g_halftone_look_min_cell,
            g_halftone_look_max_cell,
            g_halftone_look_default_cell);
        if (!cell.has_value())
        {
            return std::unexpected{cell.error()};
        }
        return HighwayHalftoneLook{.cell = *cell};
    }
    return std::unexpected{HighwayThemeError{
        HighwayThemeErrorCode::InvalidLook,
        std::format(
            "look.style is {}, which names no style: \"plain\", \"pixel\", \"crt\", \"dither\" or "
            "\"halftone\"",
            juce::JSON::toString(style, true).toStdString()),
    }};
}

// Reads every art file beside theme.json, taking each one the directory lacks from the base, and
// holds the resolved set to the texture contract.
[[nodiscard]] std::expected<HighwayTextureSet, HighwayThemeError> readTextures(
    const std::filesystem::path& theme_directory, const LoadedHighwayTheme* const base)
{
    HighwayTextureSet textures = base != nullptr ? base->textures : HighwayTextureSet{};
    for (const common::core::HighwayTexture texture : common::core::g_highway_textures)
    {
        const std::string_view file_name = common::core::highwayTextureFileName(texture);
        const std::filesystem::path path = theme_directory / file_name;
        if (base != nullptr && !common::core::juceFileFromPath(path).exists())
        {
            continue;
        }
        const std::optional<juce::MemoryBlock> block = readFile(path);
        if (!block.has_value())
        {
            return std::unexpected{HighwayThemeError{
                HighwayThemeErrorCode::MissingFile,
                std::format("{} is missing, unreadable or empty", file_name),
            }};
        }
        const std::span<const std::byte> bytes{
            static_cast<const std::byte*>(block->getData()), block->getSize()
        };
        textures.at(common::core::indexOf(texture)).assign(bytes.begin(), bytes.end());
    }

    const std::expected<HighwayTextureMeasurements, HighwayTextureContractError> measured =
        measureHighwayTextures(textures);
    if (!measured.has_value())
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::AssetContractViolated,
            measured.error().message,
        }};
    }
    return textures;
}

// Loads the directory in the order a reader would check it: identity, the file's shape, its base,
// its colors, their legibility, then the art. Messages name what failed but not the directory,
// which loadHighwayTheme adds once.
[[nodiscard]] std::expected<LoadedHighwayTheme, HighwayThemeError> readThemeDirectory(
    const std::filesystem::path& theme_directory, const std::span<const LoadedHighwayTheme> bases)
{
    const std::string id = common::core::utf8FromPath(theme_directory.filename());
    if (!isThemeId(id))
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::InvalidFolderName,
            "the directory's name is the theme's id, which must be lowercase letters, digits and "
            "hyphens",
        }};
    }

    const std::optional<juce::MemoryBlock> file = readFile(theme_directory / g_theme_file_name);
    if (!file.has_value())
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::MissingFile,
            std::format("{} is missing, unreadable or empty", g_theme_file_name),
        }};
    }
    const std::expected<juce::var, Json::Error> document = Json::parseUtf8Document(
        std::string_view{static_cast<const char*>(file->getData()), file->getSize()});
    if (!document.has_value())
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::MalformedJson,
            std::format("{} is not JSON: {}", g_theme_file_name, document.error().message),
        }};
    }
    const juce::DynamicObject* const root = document->getDynamicObject();
    if (root == nullptr)
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::MalformedJson,
            std::format("{} must hold one JSON object", g_theme_file_name),
        }};
    }
    if (const std::expected<void, HighwayThemeError> keys =
            refuseUnknownKeys(*root, g_theme_keys, g_theme_file_name);
        !keys.has_value())
    {
        return std::unexpected{keys.error()};
    }
    const std::expected<const LoadedHighwayTheme*, HighwayThemeError> base =
        readBase(*document, bases);
    if (!base.has_value())
    {
        return std::unexpected{base.error()};
    }

    const juce::var& name = Json::value(*document, g_name_key);
    if (name.isVoid())
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::MissingKey,
            std::format("{} lacks \"name\"", g_theme_file_name),
        }};
    }
    if (!name.isString() || name.toString().trim().isEmpty())
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::InvalidName,
            "\"name\" must be a non-blank string",
        }};
    }

    // An inheriting theme may leave out `colors` whole; whatever it does hold is still strict.
    const juce::var& colors = Json::value(*document, g_colors_key);
    if (colors.isVoid() && *base == nullptr)
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::MissingKey,
            std::format("{} lacks \"colors\"", g_theme_file_name),
        }};
    }
    if (!colors.isVoid())
    {
        const juce::DynamicObject* const colors_object = colors.getDynamicObject();
        if (colors_object == nullptr)
        {
            return std::unexpected{HighwayThemeError{
                HighwayThemeErrorCode::MalformedJson,
                "\"colors\" must be a JSON object",
            }};
        }
        if (const std::expected<void, HighwayThemeError> keys =
                refuseUnknownKeys(*colors_object, g_color_keys, g_colors_key);
            !keys.has_value())
        {
            return std::unexpected{keys.error()};
        }
    }
    std::expected<HighwayTheme, HighwayThemeError> roles = readRoles(colors, *base);
    if (!roles.has_value())
    {
        return std::unexpected{std::move(roles.error())};
    }
    const std::string name_text = name.toString().toStdString();
    std::expected<HighwayThemePalette, HighwayThemeError> palette =
        readPalette(colors, id, name_text, *base);
    if (!palette.has_value())
    {
        return std::unexpected{std::move(palette.error())};
    }

    // The guardrails hold the colors the theme draws with, whichever file stated each one.
    if (std::expected<void, HighwayThemeError> legible =
            checkHighwayThemeGuardrails(*roles, palette->colors);
        !legible.has_value())
    {
        return std::unexpected{std::move(legible.error())};
    }

    std::expected<HighwayRenderLook, HighwayThemeError> look = readLook(*document, *base);
    if (!look.has_value())
    {
        return std::unexpected{std::move(look.error())};
    }

    std::expected<HighwayTextureSet, HighwayThemeError> textures =
        readTextures(theme_directory, *base);
    if (!textures.has_value())
    {
        return std::unexpected{std::move(textures.error())};
    }

    return LoadedHighwayTheme{
        .id = id,
        .name = name_text,
        .colors = *roles,
        .palette = std::move(*palette),
        .textures = std::move(*textures),
        .look = *look,
    };
}

} // namespace

// Reads the six or eight hex digits after the '#' as one number; the eight-digit form carries
// alpha last, so a rotation moves it to the packed color's top byte.
std::optional<ArgbColor> parseCssHexColor(const std::string_view text)
{
    constexpr std::size_t opaque_length = 7; // "#RRGGBB"
    constexpr std::size_t alpha_length = 9;  // "#RRGGBBAA"
    if (!text.starts_with('#') || (text.size() != opaque_length && text.size() != alpha_length))
    {
        return std::nullopt;
    }

    const std::string_view digits = text.substr(1);
    ArgbColor value = 0;
    const std::from_chars_result parsed =
        std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size())
    {
        return std::nullopt;
    }
    return text.size() == opaque_length ? (g_opaque_alpha | value) : std::rotr(value, 8);
}

// Loads the directory and names it in any failure, so a logged error points at the theme.
std::expected<LoadedHighwayTheme, HighwayThemeError> loadHighwayTheme(
    const std::filesystem::path& theme_directory, const std::span<const LoadedHighwayTheme> bases)
{
    return readThemeDirectory(theme_directory, bases)
        .transform_error([&theme_directory](const HighwayThemeError& error) {
            return HighwayThemeError{
                error.code,
                std::format(
                    "Highway theme {}: {}",
                    common::core::utf8FromPath(theme_directory),
                    error.message),
            };
        });
}

} // namespace rock_hero::common::ui
