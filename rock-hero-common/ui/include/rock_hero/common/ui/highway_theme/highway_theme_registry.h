/*!
\file highway_theme_registry.h
\brief Loads every highway theme directory into the registry both products draw from.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <rock_hero/common/ui/highway_theme/loaded_highway_theme.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rock_hero::common::ui
{

/*! \brief The id of the built-in theme both products draw with. */
inline constexpr std::string_view g_default_highway_theme_id = "ice";

/*! \brief Stable reasons a theme directory, or the built-in set, fails to load. */
enum class HighwayThemeErrorCode : std::uint8_t
{
    /*! \brief The built-in themes folder does not exist: a broken install. */
    MissingThemesFolder,

    /*! \brief No built-in theme has the default id: a broken install. */
    MissingDefaultTheme,

    /*! \brief The directory's name, which is the theme's id, is not `[a-z0-9-]+`. */
    InvalidFolderName,

    /*! \brief `theme.json` or one of the art files is absent, unreadable or empty. */
    MissingFile,

    /*! \brief `theme.json` is not JSON, or does not have the theme file's object shape. */
    MalformedJson,

    /*! \brief The file carries a key the format does not define, top level or color role. */
    UnknownKey,

    /*! \brief The file leaves out `name`, `colors`, a color role or `strings`. */
    MissingKey,

    /*! \brief `name` is not a non-blank string. */
    InvalidName,

    /*! \brief A color is not CSS hex, `#RRGGBB` or `#RRGGBBAA`. */
    InvalidColor,

    /*! \brief `strings` is not exactly one opaque color per string the chart format allows. */
    InvalidStringColors,

    /*! \brief The file names a base theme in `inherits`; every theme states all of its keys. */
    InheritanceUnsupported,

    /*! \brief An art file breaks the texture contract its renderer use requires. */
    AssetContractViolated,

    /*! \brief The colors collapse a distinction the notation reads by color alone. */
    GuardrailViolated,

    /*! \brief A user theme's id is already taken by a loaded theme. */
    DuplicateId,
};

/*! \brief Typed failure of one theme directory or of the built-in set, naming what failed. */
struct [[nodiscard]] HighwayThemeError
{
    /*! \brief Stable failure reason for program branching. */
    HighwayThemeErrorCode code{};

    /*! \brief Diagnostic naming the directory and the violated expectation, for logs and UI. */
    std::string message;

    /*!
    \brief Creates an error with its diagnostic.
    \param error_code Stable failure reason.
    \param message_text Diagnostic naming the directory and the violated expectation.
    */
    HighwayThemeError(HighwayThemeErrorCode error_code, std::string message_text);
};

/*! \brief The two folders themes are scanned from, each holding one directory per theme. */
struct HighwayThemeFolders
{
    /*! \brief The themes the product ships; any that fails to load is a broken install. */
    std::filesystem::path built_in;

    /*! \brief The player's own themes; one that fails to load is dropped and logged. */
    std::filesystem::path user;
};

/*!
\brief Returns where a product finds its themes.
\param resources_root The resources tree the product deploys beside its executable.
\return The built-in folder under that tree, and the per-user folder in the shared application
        data folder.
*/
[[nodiscard]] HighwayThemeFolders highwayThemeFolders(const std::filesystem::path& resources_root);

/*!
\brief Every theme that loaded, built-ins first, holding the default theme by construction.

Every theme states its own string colors, so the registry is also the palette registry: each
theme's strings are one palette, named by the theme.
*/
class HighwayThemeRegistry
{
public:
    /*!
    \brief Loads every theme directory in both folders through the one theme loader.

    Built-ins fail loudly: any that does not load, a missing built-in folder, or no theme with the
    default id fails the whole load. User themes are untrusted input: one that does not load, or
    whose id a loaded theme already has, is logged with its typed error and dropped, and a missing
    user folder simply contributes none.

    \param folders The built-in and user theme folders.
    \return The registry, or the error of the first built-in failure.
    */
    [[nodiscard]] static std::expected<HighwayThemeRegistry, HighwayThemeError> load(
        const HighwayThemeFolders& folders);

    /*!
    \brief Returns the built-in theme both products draw with.
    \return The theme with the default id; valid for the registry's lifetime.
    */
    [[nodiscard]] const LoadedHighwayTheme& defaultTheme() const;

    /*!
    \brief Returns every theme that loaded.
    \return Built-ins then user themes, each group in id order; ids are unique.
    */
    [[nodiscard]] std::span<const LoadedHighwayTheme> themes() const noexcept;

private:
    // Only load() builds a registry, after finding the default theme among the themes.
    HighwayThemeRegistry(std::vector<LoadedHighwayTheme> themes, std::size_t default_index);

    // Built-ins then user themes, each group in id order; ids are unique.
    std::vector<LoadedHighwayTheme> m_themes;

    // Where the default theme sits in m_themes.
    std::size_t m_default_index;
};

} // namespace rock_hero::common::ui
