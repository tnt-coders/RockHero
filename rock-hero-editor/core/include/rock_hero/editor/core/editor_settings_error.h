/*!
\file editor_settings_error.h
\brief Typed errors returned by app-local editor settings persistence.
*/

#pragma once

#include <string>

namespace rock_hero::editor::core
{

/*! \brief Stable failure reasons for app-local editor settings operations. */
enum class EditorSettingsErrorCode
{
    /*! \brief A settings value was not valid for persistence or lookup. */
    InvalidSettingValue,

    /*! \brief Persisted input calibration history could not be parsed. */
    InvalidInputCalibrationHistory,

    /*! \brief The settings file could not be saved. */
    CouldNotSave,
};

/*! \brief Recoverable editor settings failure with a stable code and displayable detail. */
struct [[nodiscard]] EditorSettingsError
{
    /*! \brief Stable error code used by callers for branching. */
    EditorSettingsErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*! \brief Creates an error with the default message for its code. */
    explicit EditorSettingsError(EditorSettingsErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    EditorSettingsError(EditorSettingsErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::editor::core
