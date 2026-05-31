#include "song_audio_error.h"

#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Centralises default song audio messages so public errors stay consistent.
[[nodiscard]] std::string defaultSongAudioErrorMessage(SongAudioErrorCode code)
{
    switch (code)
    {
        case SongAudioErrorCode::MissingAudioAssetPath:
        {
            return "Arrangement is missing a backing audio asset.";
        }
        case SongAudioErrorCode::UnreadableAudioFile:
        {
            return "Backing audio file could not be read.";
        }
        case SongAudioErrorCode::InvalidAudioDuration:
        {
            return "Backing audio file has no playable duration.";
        }
        case SongAudioErrorCode::MissingBackingTrack:
        {
            return "Backing audio track is not available.";
        }
        case SongAudioErrorCode::BackendClipInsertionFailed:
        {
            return "Could not make backing audio playable.";
        }
        case SongAudioErrorCode::MonitoringRouteFailed:
        {
            return "Could not restore live input monitoring after changing backing audio.";
        }
    }

    return "Song audio operation failed.";
}

} // namespace

SongAudioError::SongAudioError(SongAudioErrorCode error_code)
    : SongAudioError(error_code, defaultSongAudioErrorMessage(error_code))
{}

SongAudioError::SongAudioError(SongAudioErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::audio
