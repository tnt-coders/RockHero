#include "highway_theme/highway_theme_registry.h"

#include "highway_theme/highway_theme_file.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <format>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/core/shared/user_data_folders.h>
#include <span>
#include <system_error>
#include <utility>

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

// Whether a loaded theme already has the id.
[[nodiscard]] bool hasThemeId(
    const std::span<const LoadedHighwayTheme> themes, const std::string_view id)
{
    return std::ranges::find(themes, id, &LoadedHighwayTheme::id) != themes.end();
}

// Loads one user theme through the one loader, refusing an id a loaded theme already has.
[[nodiscard]] std::expected<LoadedHighwayTheme, HighwayThemeError> loadUserTheme(
    const std::filesystem::path& theme_directory, const std::span<const LoadedHighwayTheme> loaded)
{
    return loadHighwayTheme(theme_directory)
        .and_then(
            [&theme_directory, loaded](
                LoadedHighwayTheme theme) -> std::expected<LoadedHighwayTheme, HighwayThemeError> {
                if (hasThemeId(loaded, theme.id))
                {
                    return std::unexpected{HighwayThemeError{
                        HighwayThemeErrorCode::DuplicateId,
                        std::format(
                            "Highway theme {}: a loaded theme already has the id \"{}\"",
                            common::core::utf8FromPath(theme_directory),
                            theme.id),
                    }};
                }
                return theme;
            });
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

// Built-ins first, so a user theme can never take a built-in's id, then the user's own.
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

    std::vector<LoadedHighwayTheme> themes;
    for (const std::filesystem::path& directory : themeDirectoriesIn(folders.built_in))
    {
        std::expected<LoadedHighwayTheme, HighwayThemeError> theme = loadHighwayTheme(directory);
        if (!theme.has_value())
        {
            return std::unexpected{std::move(theme.error())};
        }
        themes.push_back(std::move(*theme));
    }
    const auto default_theme =
        std::ranges::find(themes, g_default_highway_theme_id, &LoadedHighwayTheme::id);
    if (default_theme == themes.end())
    {
        return std::unexpected{HighwayThemeError{
            HighwayThemeErrorCode::MissingDefaultTheme,
            std::format(
                "The built-in highway themes folder {} has no \"{}\" theme; the install is broken",
                common::core::utf8FromPath(folders.built_in),
                g_default_highway_theme_id),
        }};
    }
    // User themes only append, so the default keeps this index.
    const auto default_index = static_cast<std::size_t>(default_theme - themes.begin());

    for (const std::filesystem::path& directory : themeDirectoriesIn(folders.user))
    {
        std::expected<LoadedHighwayTheme, HighwayThemeError> theme =
            loadUserTheme(directory, themes);
        if (!theme.has_value())
        {
            RH_LOG_WARNING("ui.theme", "Dropped a user theme: {}", theme.error().message);
            continue;
        }
        themes.push_back(std::move(*theme));
    }

    return HighwayThemeRegistry{std::move(themes), default_index};
}

// The index was found when the built-ins loaded, so no lookup happens here.
const LoadedHighwayTheme& HighwayThemeRegistry::defaultTheme() const
{
    return m_themes.at(m_default_index);
}

// The one place an unknown or absent pick falls back, so both products draw the same theme for it.
const LoadedHighwayTheme& HighwayThemeRegistry::themeOrDefault(
    const std::optional<std::string>& theme_id) const
{
    if (!theme_id.has_value())
    {
        return defaultTheme();
    }
    const auto picked = std::ranges::find(m_themes, *theme_id, &LoadedHighwayTheme::id);
    return picked != m_themes.end() ? *picked : defaultTheme();
}

// Every loaded theme, read-only.
std::span<const LoadedHighwayTheme> HighwayThemeRegistry::themes() const noexcept
{
    return m_themes;
}

HighwayThemeRegistry::HighwayThemeRegistry(
    std::vector<LoadedHighwayTheme> themes, const std::size_t default_index)
    : m_themes(std::move(themes))
    , m_default_index(default_index)
{
    assert(m_themes.at(m_default_index).id == g_default_highway_theme_id);
}

} // namespace rock_hero::common::ui
