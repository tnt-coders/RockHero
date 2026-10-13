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

// The keys the top level of a theme file may hold.
constexpr std::array g_theme_keys{g_name_key, g_colors_key, g_inherits_key};

// The keys `colors` may hold: every role, read from the one role table, then the string colors.
constexpr auto g_color_keys = [] {
    std::array<std::string_view, g_highway_theme_roles.size() + 1> keys{};
    std::ranges::transform(g_highway_theme_roles, keys.begin(), &HighwayThemeRole::name);
    keys.back() = g_strings_key;
    return keys;
}();

// The halves of a role split by surface, in the order a split value pairs them.
constexpr std::array<std::string_view, 2> g_surface_keys{"highway", "tab"};

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
                "inherits a built-in, a built-in inherits the default theme, and the default "
                "theme inherits nothing",
                juce::JSON::toString(inherits, true).toStdString()),
        }};
    }
    return &*base;
}

// Every color the roles resolve to: the highway's roles, and the tab lane's span inks.
struct ResolvedRoles
{
    HighwayTheme highway;
    TabShapeInks tab;
};

// Reads one color value; `where` names it after "colors.".
[[nodiscard]] std::expected<ArgbColor, HighwayThemeError> readColor(
    const juce::var& value, const std::string_view where)
{
    const std::optional<ArgbColor> color =
        value.isString() ? parseCssHexColor(value.toString().toStdString()) : std::nullopt;
    if (!color.has_value())
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::InvalidColor,
            std::format(
                "colors.{} is {}, not a #RRGGBB or #RRGGBBAA color",
                where,
                juce::JSON::toString(value, true).toStdString()),
        }};
    }
    return *color;
}

// Reads a span role's two halves: one color for both surfaces, or a {"highway", "tab"} pair.
[[nodiscard]] std::expected<std::pair<ArgbColor, ArgbColor>, HighwayThemeError> readSurfacePair(
    const juce::var& value, const std::string_view role)
{
    const juce::DynamicObject* const halves = value.getDynamicObject();
    if (halves == nullptr)
    {
        return readColor(value, role).transform([](const ArgbColor color) {
            return std::pair{color, color};
        });
    }

    const std::string where = std::format("colors.{}", role);
    if (const std::expected<void, HighwayThemeError> keys =
            refuseUnknownKeys(*halves, g_surface_keys, where);
        !keys.has_value())
    {
        return std::unexpected{keys.error()};
    }
    std::array<ArgbColor, g_surface_keys.size()> colors{};
    for (std::size_t index = 0; index < g_surface_keys.size(); ++index)
    {
        const std::string_view surface = g_surface_keys.at(index);
        const juce::var& half = Json::value(value, surface);
        if (half.isVoid())
        {
            return std::unexpected{HighwayThemeError{
                HighwayThemeErrorCode::MissingKey,
                std::format(
                    "{} lacks \"{}\"; a role split by surface states both halves", where, surface),
            }};
        }
        const std::expected<ArgbColor, HighwayThemeError> color =
            readColor(half, std::format("{}.{}", role, surface));
        if (!color.has_value())
        {
            return std::unexpected{color.error()};
        }
        colors.at(index) = *color;
    }
    return std::pair{colors.at(0), colors.at(1)};
}

// Reads every color role from `colors`, through the one role table; a role the file leaves out
// is the base's, on both surfaces.
[[nodiscard]] std::expected<ResolvedRoles, HighwayThemeError> readRoles(
    const juce::var& colors, const LoadedHighwayTheme* const base)
{
    ResolvedRoles roles{};
    if (base != nullptr)
    {
        roles = ResolvedRoles{.highway = base->colors, .tab = base->tab_inks};
    }
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
        if (role.tab == nullptr)
        {
            const std::expected<ArgbColor, HighwayThemeError> color = readColor(value, role.name);
            if (!color.has_value())
            {
                return std::unexpected{color.error()};
            }
            roles.highway.*role.member = *color;
            continue;
        }

        const std::expected<std::pair<ArgbColor, ArgbColor>, HighwayThemeError> pair =
            readSurfacePair(value, role.name);
        if (!pair.has_value())
        {
            return std::unexpected{pair.error()};
        }
        const auto [highway, tab] = *pair;
        // The chip a tab ink fills prints its label on it, so the ink carries no alpha.
        if ((tab & g_opaque_alpha) != g_opaque_alpha)
        {
            return std::unexpected{HighwayThemeError{
                HighwayThemeErrorCode::InvalidColor,
                std::format(
                    "colors.{} gives the tab lane #{:08X}; a tab ink must be opaque, since a "
                    "chip's label prints on it",
                    role.name,
                    std::rotl(tab, 8)),
            }};
        }
        roles.highway.*role.member = highway;
        roles.tab.*role.tab = tab;
    }
    return roles;
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
    std::expected<ResolvedRoles, HighwayThemeError> roles = readRoles(colors, *base);
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
            checkHighwayThemeGuardrails(roles->highway, roles->tab, palette->colors);
        !legible.has_value())
    {
        return std::unexpected{std::move(legible.error())};
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
        .colors = roles->highway,
        .palette = std::move(*palette),
        .tab_inks = roles->tab,
        .textures = std::move(*textures),
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
