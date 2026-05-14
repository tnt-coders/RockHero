#include "song_import_error.h"

#include <string>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Provides fallback text for call sites that have no richer runtime context.
[[nodiscard]] std::string defaultSongImportErrorMessage(SongImportErrorCode code)
{
    switch (code)
    {
        case SongImportErrorCode::MissingSource:
        {
            return "Song source does not exist.";
        }
        case SongImportErrorCode::UnsupportedSource:
        {
            return "Unsupported song source.";
        }
        case SongImportErrorCode::ExtractionFailed:
        {
            return "Could not extract song source.";
        }
        case SongImportErrorCode::InvalidImportedSong:
        {
            return "Imported song package is invalid.";
        }
        case SongImportErrorCode::AudioConversionFailed:
        {
            return "Could not convert source audio.";
        }
        case SongImportErrorCode::AudioImportFailed:
        {
            return "Could not import source audio.";
        }
        case SongImportErrorCode::NoPlayableArrangement:
        {
            return "Source did not contain a playable arrangement.";
        }
        case SongImportErrorCode::FilesystemFailure:
        {
            return "Could not write imported song files.";
        }
        case SongImportErrorCode::ExternalImportFailed:
        {
            return "Could not import song source.";
        }
    }

    return "Song import failed.";
}

} // namespace

SongImportError::SongImportError(SongImportErrorCode error_code)
    : SongImportError(error_code, defaultSongImportErrorMessage(error_code))
{}

SongImportError::SongImportError(SongImportErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::editor::core
