/*!
\file audio_device_error.h
\brief Typed errors reported by live audio-device selection and monitoring.
*/

#pragma once

#include <string>

namespace rock_hero::common::audio
{

/*! \brief Stable live audio-device failure reasons. */
enum class AudioDeviceErrorCode
{
    /*! \brief ASIO support is not available in this build or on this platform. */
    AsioUnavailable,

    /*! \brief The selected ASIO device was not present in the current device list. */
    AsioDeviceNotFound,

    /*! \brief The selected ASIO device did not expose the requested input channel. */
    AsioInputChannelUnavailable,

    /*! \brief The selected audio device could not be opened for live monitoring. */
    AudioDeviceOpenFailed,

    /*! \brief The selected input could not be routed into the live monitoring path. */
    LiveInputRoutingFailed,
};

/*! \brief Audio-device error with a stable code and diagnostic message. */
struct [[nodiscard]] AudioDeviceError
{
    /*! \brief Stable reason for the audio-device failure. */
    AudioDeviceErrorCode code{};

    /*! \brief Human-readable diagnostic text for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an audio-device error with the default message for the code.
    \param error_code Stable audio-device failure code.
    */
    explicit AudioDeviceError(AudioDeviceErrorCode error_code);

    /*!
    \brief Creates an audio-device error with explicit diagnostic text.
    \param error_code Stable audio-device failure code.
    \param message_text Human-readable diagnostic text.
    */
    AudioDeviceError(AudioDeviceErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::common::audio
