#include "library/album_art_error.h"

#include <utility>

namespace rock_hero::game::core
{

namespace
{

// Default display message for each stable code.
[[nodiscard]] std::string defaultMessage(const AlbumArtErrorCode error_code)
{
    switch (error_code)
    {
        case AlbumArtErrorCode::GenerationFailed:
            return "The album-art image could not be generated";
    }
    return "Unknown album-art error";
}

} // namespace

AlbumArtError::AlbumArtError(const AlbumArtErrorCode error_code)
    : code(error_code)
    , message(defaultMessage(error_code))
{}

AlbumArtError::AlbumArtError(const AlbumArtErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::game::core
