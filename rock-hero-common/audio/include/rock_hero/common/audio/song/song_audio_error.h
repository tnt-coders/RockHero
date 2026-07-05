/*!
\file song_audio_error.h
\brief Typed errors returned by the song audio boundary.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::common::audio
{

/*! \brief Stable failure reasons for preparing or activating song audio. */
enum class SongAudioErrorCode : std::uint8_t
{
    /*! \brief An arrangement did not provide a backing audio asset path. */
    MissingAudioAssetPath,

    /*! \brief A backing audio asset could not be opened as a readable file. */
    UnreadableAudioFile,

    /*! \brief A backing audio asset reported no positive playable duration. */
    InvalidAudioDuration,

    /*! \brief The backend backing track was not available. */
    MissingBackingTrack,

    /*! \brief The backend could not insert the backing clip for playback. */
    BackendClipInsertionFailed,

    /*! \brief The backend could not restore live input routing after graph mutation. */
    MonitoringRouteFailed,
};

/*! \brief Recoverable song audio failure with a stable code and displayable detail. */
struct [[nodiscard]] SongAudioError
{
    /*! \brief Stable error code used by callers for branching. */
    SongAudioErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit SongAudioError(SongAudioErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    SongAudioError(SongAudioErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::common::audio
