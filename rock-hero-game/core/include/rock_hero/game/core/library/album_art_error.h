/*!
\file album_art_error.h
\brief Typed error returned by album-art image generation.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::game::core
{

/*! \brief Stable failure reasons for album-art image generation. */
enum class AlbumArtErrorCode : std::uint8_t
{
    /*! \brief Album art was present but could not be decoded or cached. */
    GenerationFailed,
};

/*! \brief Recoverable album-art image failure with a stable code and displayable detail. */
struct [[nodiscard]] AlbumArtError
{
    /*! \brief Stable error code used by callers for branching. */
    AlbumArtErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for a library warning or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit AlbumArtError(AlbumArtErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for a library warning or logs.
    */
    AlbumArtError(AlbumArtErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::game::core
