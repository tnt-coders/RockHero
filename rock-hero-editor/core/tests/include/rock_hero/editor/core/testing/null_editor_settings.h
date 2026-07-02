/*!
\file null_editor_settings.h
\brief No-op editor settings implementation for tests that do not observe persistence.
*/

#pragma once

#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/input_calibration_state.h>
#include <rock_hero/common/core/fraction.h>
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
    /*!
    \brief Reports that no last-open project is stored.
    \return Always empty.
    */
    [[nodiscard]] std::optional<std::filesystem::path> lastOpenProject() const override
    {
        return std::nullopt;
    }

    /*!
    \brief Ignores last-open project writes.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setLastOpenProject(
        std::optional<std::filesystem::path>) override
    {
        return {};
    }

    /*!
    \brief Reports that no interrupted restore project is stored.
    \return Always empty.
    */
    [[nodiscard]] std::optional<std::filesystem::path> interruptedRestoreProject() const override
    {
        return std::nullopt;
    }

    /*!
    \brief Ignores interrupted restore project writes.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setInterruptedRestoreProject(
        std::optional<std::filesystem::path>) override
    {
        return {};
    }

    /*!
    \brief Reports that no serialized audio-device state is stored.
    \return Always empty.
    */
    [[nodiscard]] std::optional<std::string> audioDeviceState() const override
    {
        return std::nullopt;
    }

    /*!
    \brief Ignores serialized audio-device state writes.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setAudioDeviceState(
        std::optional<std::string>) override
    {
        return {};
    }

    /*!
    \brief Reports that no app-local project cursor is stored.
    \return Always empty optional success.
    */
    [[nodiscard]] std::expected<std::optional<common::core::TimePosition>, EditorSettingsError>
    projectCursorPositionFor(const std::filesystem::path&) const override
    {
        return std::nullopt;
    }

    /*!
    \brief Ignores app-local project cursor writes.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> saveProjectCursorPosition(
        const std::filesystem::path&, common::core::TimePosition) override
    {
        return {};
    }

    /*!
    \brief Reports that no app-local project grid spacing is stored.
    \return Always empty optional success.
    */
    [[nodiscard]] std::expected<std::optional<common::core::Fraction>, EditorSettingsError>
    projectGridSpacingFor(const std::filesystem::path&) const override
    {
        return std::nullopt;
    }

    /*!
    \brief Ignores app-local project grid spacing writes.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> saveProjectGridSpacing(
        const std::filesystem::path&, common::core::Fraction) override
    {
        return {};
    }

    /*!
    \brief Reports that no calibration is stored for the supplied input route.
    \return Always empty optional success.
    */
    [[nodiscard]] std::expected<
        std::optional<common::audio::InputCalibrationState>, EditorSettingsError>
    inputCalibrationFor(const common::audio::InputDeviceIdentity&) const override
    {
        return std::nullopt;
    }

    /*!
    \brief Ignores input calibration writes.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> saveInputCalibration(
        common::audio::InputCalibrationState) override
    {
        return {};
    }

    /*!
    \brief Ignores input calibration removal requests.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> removeInputCalibration(
        const common::audio::InputDeviceIdentity&) override
    {
        return {};
    }
};

} // namespace rock_hero::editor::core::testing
