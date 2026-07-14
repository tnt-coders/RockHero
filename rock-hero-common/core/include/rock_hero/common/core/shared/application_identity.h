/*!
\file application_identity.h
\brief Shared application identity strings.
*/

#pragma once

#include <string_view>

namespace rock_hero::common::core
{

/*!
\brief User-facing product name shared by Rock Hero applications.
\return Stable product name text.
*/
[[nodiscard]] constexpr std::string_view productName() noexcept
{
    return "Rock Hero";
}

/*!
\brief Canonical per-user application data folder name used by settings, logs, and Tracktion.
\return Stable data folder name text.
*/
[[nodiscard]] constexpr std::string_view applicationDataFolderName() noexcept
{
    return productName();
}

/*!
\brief User-facing editor application name.
\return Stable editor application name text.
*/
[[nodiscard]] constexpr std::string_view editorApplicationName() noexcept
{
    return "Rock Hero Editor";
}

/*!
\brief User-facing game application name; names the game's per-user settings file.
\return Stable game application name text.
*/
[[nodiscard]] constexpr std::string_view gameApplicationName() noexcept
{
    return productName();
}

} // namespace rock_hero::common::core
