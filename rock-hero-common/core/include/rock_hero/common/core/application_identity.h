/*!
\file application_identity.h
\brief Shared application identity strings.
*/

#pragma once

#include <string_view>

namespace rock_hero::common::core
{

/*! \brief User-facing product name shared by Rock Hero applications. */
[[nodiscard]] constexpr std::string_view productName() noexcept
{
    return "Rock Hero";
}

/*! \brief Canonical per-user application data folder name used by settings, logs, and Tracktion. */
[[nodiscard]] constexpr std::string_view applicationDataFolderName() noexcept
{
    return productName();
}

/*! \brief User-facing editor application name. */
[[nodiscard]] constexpr std::string_view editorApplicationName() noexcept
{
    return "Rock Hero Editor";
}

} // namespace rock_hero::common::core
