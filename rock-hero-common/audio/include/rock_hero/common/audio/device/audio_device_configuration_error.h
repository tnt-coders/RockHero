/*!
\file audio_device_configuration_error.h
\brief Typed errors returned by the audio-device configuration boundary.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::common::audio
{

/*! \brief Stable failure reasons for audio-device configuration operations. */
enum class AudioDeviceConfigurationErrorCode : std::uint8_t
{
    /*! \brief An audio-device operation was invoked from the wrong thread. */
    MessageThreadRequired,

    /*! \brief Serialized device state could not be parsed as XML. */
    InvalidSerializedState,

    /*! \brief The audio backend rejected the serialized device state. */
    RestoreFailed,
};

/*! \brief Recoverable audio-device configuration failure with stable code and detail. */
struct [[nodiscard]] AudioDeviceConfigurationError
{
    /*! \brief Stable error code used by callers for branching. */
    AudioDeviceConfigurationErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit AudioDeviceConfigurationError(AudioDeviceConfigurationErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    AudioDeviceConfigurationError(
        AudioDeviceConfigurationErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::common::audio
