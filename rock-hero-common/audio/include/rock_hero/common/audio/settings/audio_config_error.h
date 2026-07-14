/*!
\file audio_config_error.h
\brief Typed errors returned by the per-app audio-config store.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::common::audio
{

/*! \brief Stable failure reasons for per-app audio-config store operations. */
enum class AudioConfigErrorCode : std::uint8_t
{
    /*! \brief A config value was not valid for persistence or lookup. */
    InvalidSettingValue,

    /*! \brief Persisted input calibration history could not be parsed. */
    InvalidInputCalibrationHistory,

    /*! \brief The store could not be saved, including a write into a read-only store. */
    CouldNotSave,
};

/*! \brief Recoverable audio-config store failure with a stable code and displayable detail. */
struct [[nodiscard]] AudioConfigError
{
    /*! \brief Stable error code used by callers for branching. */
    AudioConfigErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit AudioConfigError(AudioConfigErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    AudioConfigError(AudioConfigErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::common::audio
