/*!
\file application_identity.h
\brief Shared application identity strings.
*/

#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

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
\brief Name of the per-user folder every Rock Hero location sits in (user_data_folders.h).
\return Stable data folder name text.
*/
[[nodiscard]] constexpr std::string_view applicationDataFolderName() noexcept
{
    return productName();
}

/*! \brief The two applications that keep per-application files in the shared folder. */
enum class Product : std::uint8_t
{
    /*! \brief The game. */
    Game,

    /*! \brief The editor. */
    Editor,
};

/*!
\brief Names one application's settings file and log file.

One token per application names both of its files, so the pair cannot drift, and no display name
reaches a saved path: renaming a product never renames a file it has already written.

\param product The application.
\return The application's file token, one lowercase word.
*/
[[nodiscard]] constexpr std::string_view productToken(Product product) noexcept
{
    switch (product)
    {
        case Product::Game:
        {
            return "game";
        }
        case Product::Editor:
        {
            return "editor";
        }
    }
    std::unreachable();
}

} // namespace rock_hero::common::core
