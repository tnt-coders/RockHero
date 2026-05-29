/*!
\file null_editor_settings.h
\brief No-op editor settings implementation for tests that do not observe persistence.
*/

#pragma once

#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/input_calibration_state.h>
#include <rock_hero/editor/core/i_editor_settings.h>
#include <string>

namespace rock_hero::editor::core::testing
{

/*!
\brief IEditorSettings implementation that returns no stored values and ignores writes.

Use this when a test must construct an editor controller or editor composition wrapper but does not
observe settings persistence. Tests that need to assert reads or writes should use a purpose-built
recording fake instead.
*/
class NullEditorSettings final : public IEditorSettings
{
public:
    /*! \brief Reports that no last-open project is stored. */
    [[nodiscard]] std::optional<std::filesystem::path> lastOpenProject() const override
    {
        return std::nullopt;
    }

    /*! \brief Ignores last-open project writes. */
    void setLastOpenProject(std::optional<std::filesystem::path>) override
    {}

    /*! \brief Reports that no interrupted restore project is stored. */
    [[nodiscard]] std::optional<std::filesystem::path> interruptedRestoreProject() const override
    {
        return std::nullopt;
    }

    /*! \brief Ignores interrupted restore project writes. */
    void setInterruptedRestoreProject(std::optional<std::filesystem::path>) override
    {}

    /*! \brief Reports that no serialized audio-device state is stored. */
    [[nodiscard]] std::optional<std::string> audioDeviceState() const override
    {
        return std::nullopt;
    }

    /*! \brief Ignores serialized audio-device state writes. */
    void setAudioDeviceState(std::optional<std::string>) override
    {}

    /*! \brief Reports that no input calibration state is stored. */
    [[nodiscard]] std::optional<common::audio::InputCalibrationState> inputCalibrationState()
        const override
    {
        return std::nullopt;
    }

    /*! \brief Ignores input calibration state writes. */
    void setInputCalibrationState(std::optional<common::audio::InputCalibrationState>) override
    {}
};

} // namespace rock_hero::editor::core::testing
