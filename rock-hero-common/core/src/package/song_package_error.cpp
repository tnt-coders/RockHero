#include "package/song_package_error.h"

#include <string>
#include <utility>

namespace rock_hero::common::core
{

namespace
{

// Provides fallback text for call sites that have no richer runtime context.
[[nodiscard]] std::string defaultSongPackageErrorMessage(SongPackageErrorCode code)
{
    switch (code)
    {
        case SongPackageErrorCode::MissingPackageDirectory:
        {
            return "Song package directory does not exist.";
        }
        case SongPackageErrorCode::MissingSongDocument:
        {
            return "Song package directory does not contain song.json.";
        }
        case SongPackageErrorCode::CouldNotOpenSongDocument:
        {
            return "Could not open song.json.";
        }
        case SongPackageErrorCode::InvalidSongDocument:
        {
            return "Invalid song.json.";
        }
        case SongPackageErrorCode::InvalidAudioAsset:
        {
            return "Song package contains an invalid audio asset.";
        }
        case SongPackageErrorCode::InvalidArrangement:
        {
            return "Song package contains an invalid arrangement.";
        }
        case SongPackageErrorCode::CouldNotExtractPackage:
        {
            return "Could not extract native song package.";
        }
        case SongPackageErrorCode::CouldNotCreateSongDirectory:
        {
            return "Could not create song directory.";
        }
        case SongPackageErrorCode::CouldNotWriteSongDocument:
        {
            return "Could not write song.json.";
        }
        case SongPackageErrorCode::CouldNotWritePackage:
        {
            return "Could not write native song package.";
        }
    }

    return "Song package operation failed.";
}

} // namespace

SongPackageError::SongPackageError(SongPackageErrorCode error_code)
    : SongPackageError(error_code, defaultSongPackageErrorMessage(error_code))
{}

SongPackageError::SongPackageError(SongPackageErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::core
