#include "highway_theme/highway_theme_registry.h"

#include "highway_theme/highway_theme_file.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <format>
#include <optional>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/core/shared/user_data_folders.h>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

// The built-in themes folder's name under the resources tree.
constexpr std::string_view g_themes_folder_name = "themes";

// The theme directories inside one folder, in name order so loading is deterministic. A folder
// that is absent or cannot be listed holds none.
[[nodiscard]] std::vector<std::filesystem::path> themeDirectoriesIn(
    const std::filesystem::path& folder)
{
    std::vector<std::filesystem::path> directories;
    std::error_code error;
    for (std::filesystem::directory_iterator entry{folder, error}, end; !error && entry != end;
         entry.increment(error))
    {
        std::error_code type_error;
        if (entry->is_directory(type_error))
        {
            directories.push_back(entry->path());
        }
    }
    std::ranges::sort(directories);
    return directories;
}

// A theme is a palette when its own file states its strings, which is when the palette it carries
// names the theme itself.
[[nodiscard]] bool statesOwnPalette(const LoadedHighwayTheme& theme)
{
    return theme.palette.source_id == theme.id;
}

} // namespace

HighwayThemeError::HighwayThemeError(HighwayThemeErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

// The built-in folder sits in the product's deployed resources; the user folder is per-user.
HighwayThemeFolders highwayThemeFolders(const std::filesystem::path& resources_root)
{
    return HighwayThemeFolders{
        .built_in = resources_root / g_themes_folder_name,
        .user = common::core::themesFolder(),
    };
}

// The default theme first, inheriting nothing; then the other built-ins, each free to inherit it;
// then the user's own, each free to inherit a built-in as shipped, and only then may a user theme
// take a built-in's place. Only the default theme is required: every other theme inherits it and
// every unknown pick falls back to it, so without it nothing could draw, where any other theme
// that fails is dropped with its error and its pick falls back.
std::expected<HighwayThemeRegistry, HighwayThemeError> HighwayThemeRegistry::load(
    const HighwayThemeFolders& folders)
{
    std::error_code probe_error;
    if (!std::filesystem::is_directory(folders.built_in, probe_error))
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::MissingThemesFolder,
            std::format(
                "The built-in highway themes folder {} does not exist; the install is broken",
                common::core::utf8FromPath(folders.built_in)),
        }};
    }
    const std::filesystem::path default_directory = folders.built_in / g_default_highway_theme_id;
    if (!std::filesystem::is_directory(default_directory, probe_error))
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::MissingDefaultTheme,
            std::format(
                "The built-in highway themes folder {} has no \"{}\" theme; the install is broken",
                common::core::utf8FromPath(folders.built_in),
                g_default_highway_theme_id),
        }};
    }
    std::expected<LoadedHighwayTheme, HighwayThemeError> default_theme =
        loadHighwayTheme(default_directory, {});
    if (!default_theme.has_value())
    {
        return std::unexpected{std::move(default_theme.error())};
    }

    std::vector<LoadedHighwayTheme> themes;
    for (const std::filesystem::path& directory : themeDirectoriesIn(folders.built_in))
    {
        if (common::core::utf8FromPath(directory.filename()) == g_default_highway_theme_id)
        {
            continue;
        }
        std::expected<LoadedHighwayTheme, HighwayThemeError> theme =
            loadHighwayTheme(directory, std::span{&*default_theme, 1});
        if (!theme.has_value())
        {
            RH_LOG_ERROR("ui.theme", "Dropped a built-in theme: {}", theme.error().message);
            continue;
        }
        themes.push_back(std::move(*theme));
    }
    themes.push_back(std::move(*default_theme));
    std::ranges::sort(themes, {}, &LoadedHighwayTheme::id);
    // A user theme either takes a built-in's slot or appends, so the default keeps this index.
    const auto default_index = static_cast<std::size_t>(
        std::ranges::find(themes, g_default_highway_theme_id, &LoadedHighwayTheme::id) -
        themes.begin());

    std::vector<LoadedHighwayTheme> user_themes;
    for (const std::filesystem::path& directory : themeDirectoriesIn(folders.user))
    {
        std::expected<LoadedHighwayTheme, HighwayThemeError> theme =
            loadHighwayTheme(directory, themes);
        if (!theme.has_value())
        {
            RH_LOG_WARNING("ui.theme", "Dropped a user theme: {}", theme.error().message);
            continue;
        }
        user_themes.push_back(std::move(*theme));
    }
    // Folder names are unique, so a user theme's id can only repeat a built-in's.
    for (LoadedHighwayTheme& user_theme : user_themes)
    {
        const auto built_in = std::ranges::find(themes, user_theme.id, &LoadedHighwayTheme::id);
        if (built_in == themes.end())
        {
            themes.push_back(std::move(user_theme));
            continue;
        }
        RH_LOG_INFO(
            "ui.theme",
            "The user theme \"{}\" shadows the built-in theme of that id",
            user_theme.id);
        *built_in = std::move(user_theme);
    }

    return HighwayThemeRegistry{std::move(themes), default_index};
}

// Every loaded theme, read-only.
std::span<const LoadedHighwayTheme> HighwayThemeRegistry::themes() const noexcept
{
    return m_themes;
}

// Every theme whose own file states its strings, in theme order.
std::vector<std::reference_wrapper<const HighwayThemePalette>> HighwayThemeRegistry::palettes()
    const
{
    std::vector<std::reference_wrapper<const HighwayThemePalette>> palettes;
    for (const LoadedHighwayTheme& theme : m_themes)
    {
        if (statesOwnPalette(theme))
        {
            palettes.emplace_back(theme.palette);
        }
    }
    return palettes;
}

// The one place an unknown or absent pick falls back, so both products draw the same theme for it.
const LoadedHighwayTheme& HighwayThemeRegistry::themeFor(
    const common::core::HighwayPlayerSettings& settings) const
{
    const std::optional<std::string>& theme_id = settings.theme_id;
    if (!theme_id.has_value())
    {
        return m_themes.at(m_default_index);
    }
    const auto picked = std::ranges::find(m_themes, *theme_id, &LoadedHighwayTheme::id);
    return picked != m_themes.end() ? *picked : m_themes.at(m_default_index);
}

// A pin draws only while it names a palette; any other pin is the theme's choice.
const HighwayThemePalette* HighwayThemeRegistry::pinnedPaletteFor(
    const common::core::HighwayPlayerSettings& settings) const
{
    const std::optional<std::string>& pinned_id = settings.pinned_palette_id;
    if (!pinned_id.has_value())
    {
        return nullptr;
    }
    // Ids are unique, so the theme with the pin's id is the only palette the pin can name.
    const auto theme = std::ranges::find(m_themes, *pinned_id, &LoadedHighwayTheme::id);
    return theme != m_themes.end() && statesOwnPalette(*theme) ? &theme->palette : nullptr;
}

// The pin wins over every theme; without one, the theme's own choice draws.
const HighwayThemePalette& HighwayThemeRegistry::paletteFor(
    const common::core::HighwayPlayerSettings& settings) const
{
    const HighwayThemePalette* const pinned = pinnedPaletteFor(settings);
    return pinned != nullptr ? *pinned : themeFor(settings).palette;
}

HighwayThemeRegistry::HighwayThemeRegistry(
    std::vector<LoadedHighwayTheme> themes, const std::size_t default_index)
    : m_themes(std::move(themes))
    , m_default_index(default_index)
{
    assert(m_themes.at(m_default_index).id == g_default_highway_theme_id);
}

} // namespace rock_hero::common::ui
