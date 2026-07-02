#include "editor_settings_error.h"

#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Centralises default settings messages so public errors stay consistent.
[[nodiscard]] std::string defaultEditorSettingsErrorMessage(EditorSettingsErrorCode code)
{
    switch (code)
    {
        case EditorSettingsErrorCode::InvalidSettingValue:
        {
            return "Editor settings value is not valid.";
        }
        case EditorSettingsErrorCode::InvalidInputCalibrationHistory:
        {
            return "Saved input calibration settings are invalid.";
        }
        case EditorSettingsErrorCode::InvalidProjectCursorHistory:
        {
            return "Saved project cursor settings are invalid.";
        }
        case EditorSettingsErrorCode::InvalidProjectGridSpacingHistory:
        {
            return "Saved project grid spacing settings are invalid.";
        }
        case EditorSettingsErrorCode::CouldNotSave:
        {
            return "Could not save editor settings.";
        }
    }

    return "Editor settings operation failed.";
}

} // namespace

EditorSettingsError::EditorSettingsError(EditorSettingsErrorCode error_code)
    : EditorSettingsError(error_code, defaultEditorSettingsErrorMessage(error_code))
{}

EditorSettingsError::EditorSettingsError(
    EditorSettingsErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::editor::core
