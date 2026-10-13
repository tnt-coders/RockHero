/*!
\file highway_theme_registry.h
\brief Loads every highway theme directory into the registry both products draw from.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <rock_hero/common/core/highway/highway_player_settings.h>
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

    /*!
    \brief `theme.json` is absent, a file present is unreadable or empty, or a theme that inherits
    nothing lacks an art file.
    */
    MissingFile,

    /*! \brief `theme.json` is not JSON, or does not have the theme file's object shape. */
    MalformedJson,

    /*! \brief The file carries a key the format does not define, top level or color role. */
    UnknownKey,

    /*!
    \brief The file leaves out `name`, or a theme that inherits nothing leaves out `colors`, a color
    role or `strings`.
    */
    MissingKey,

    /*! \brief `name` is not a non-blank string. */
    InvalidName,

    /*! \brief A color is not CSS hex, `#RRGGBB` or `#RRGGBBAA`. */
    InvalidColor,

    /*! \brief `strings` is not exactly one opaque color per string the chart format allows. */
    InvalidStringColors,

    /*!
    \brief `inherits` names no theme this one may inherit: a user theme inherits only a built-in,
    a built-in only the default theme, and the default theme nothing.
    */
    UnknownBase,

    /*! \brief An art file breaks the texture contract its renderer use requires. */
    AssetContractViolated,

    /*! \brief The colors collapse a distinction the notation reads by color alone. */
    GuardrailViolated,
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
\brief Every theme that loaded, holding the default theme by construction, and the one place the
player's settings resolve to what the highway draws with.

The registry is also the palette registry: every theme that states its own strings is one palette,
named by the theme.
*/
class HighwayThemeRegistry
{
public:
    /*!
    \brief Loads every theme directory in both folders through the one theme loader.

    Built-ins fail loudly: any that does not load, a missing built-in folder, or no theme with the
    default id fails the whole load. The default theme is complete; every other built-in may
    inherit it, which is how the shipped color themes share its art. User themes are untrusted
    input and may inherit any built-in: one that does not load is logged with its typed error and
    dropped, one whose id a built-in has takes that built-in's place (logged), and a missing user
    folder simply contributes none. A theme inheriting a built-in reads the built-in itself, never
    a user theme in its place.

    \param folders The built-in and user theme folders.
    \return The registry, or the error of the first built-in failure.
    */
    [[nodiscard]] static std::expected<HighwayThemeRegistry, HighwayThemeError> load(
        const HighwayThemeFolders& folders);

    /*!
    \brief Returns every theme that loaded.
    \return The built-ins, each replaced by any user theme with its id, then the other user themes;
            each group in id order, and ids are unique.
    */
    [[nodiscard]] std::span<const LoadedHighwayTheme> themes() const noexcept;

    /*!
    \brief Returns every palette the player can pin: the strings of each theme that states its own.
    \return The palettes in the order of themes(), each valid for the registry's lifetime.
    */
    [[nodiscard]] std::vector<std::reference_wrapper<const HighwayThemePalette>> palettes() const;

    /*!
    \brief Returns the theme the player's settings draw with.
    \param settings The player's highway settings.
    \return The loaded theme the settings pick; the default theme when they pick none or one no
            loaded theme has (a user theme since removed). Valid for the registry's lifetime.
    */
    [[nodiscard]] const LoadedHighwayTheme& themeFor(
        const common::core::HighwayPlayerSettings& settings) const;

    /*!
    \brief Returns the palette the player's settings pin, when one draws.
    \param settings The player's highway settings.
    \return The pinned palette, or null for "Theme's choice": no pin, or a pin no loaded theme
            states (one since removed, or one that now inherits its strings).
    */
    [[nodiscard]] const HighwayThemePalette* pinnedPaletteFor(
        const common::core::HighwayPlayerSettings& settings) const;

    /*!
    \brief Returns the string colors the player's settings draw with.
    \param settings The player's highway settings.
    \return The pinned palette when one draws, otherwise the palette of the theme they draw with.
    */
    [[nodiscard]] const HighwayThemePalette& paletteFor(
        const common::core::HighwayPlayerSettings& settings) const;

private:
    // Only load() builds a registry, after finding the default theme among the themes.
    HighwayThemeRegistry(std::vector<LoadedHighwayTheme> themes, std::size_t default_index);

    // As themes() returns them.
    std::vector<LoadedHighwayTheme> m_themes;

    // Where the default theme sits in m_themes.
    std::size_t m_default_index;
};

} // namespace rock_hero::common::ui
