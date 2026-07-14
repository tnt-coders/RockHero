/*!
\file null_editor_settings.h
\brief No-op editor settings implementation for tests that do not observe persistence.
*/

#pragma once

#include <filesystem>
#include <optional>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/editor/core/settings/i_editor_settings.h>
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
    \brief Reports that no waveform visibility preference is stored.
    \return Always empty.
    */
    [[nodiscard]] std::optional<bool> waveformVisible() const override
    {
        return std::nullopt;
    }

    /*!
    \brief Ignores waveform visibility writes.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setWaveformVisible(bool) override
    {
        return {};
    }

    /*!
    \brief Reports that no use-game-audio-settings choice is stored.
    \return Always empty.
    */
    [[nodiscard]] std::optional<bool> useGameAudioSettings() const override
    {
        return std::nullopt;
    }

    /*!
    \brief Ignores use-game-audio-settings writes.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setUseGameAudioSettings(bool) override
    {
        return {};
    }

    /*!
    \brief Reports that no recommendation-suppression choice is stored.
    \return Always empty.
    */
    [[nodiscard]] std::optional<bool> suppressGameAudioRecommendation() const override
    {
        return std::nullopt;
    }

    /*!
    \brief Ignores recommendation-suppression writes.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setSuppressGameAudioRecommendation(
        bool) override
    {
        return {};
    }

    /*!
    \brief Reports that no tablature string display minimum is stored.
    \return Always empty.
    */
    [[nodiscard]] std::optional<int> tabMinimumDisplayedStrings() const override
    {
        return std::nullopt;
    }

    /*!
    \brief Ignores tablature string display minimum writes.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setTabMinimumDisplayedStrings(
        int) override
    {
        return {};
    }

    /*!
    \brief Reports that no app-local project cursor is stored.
    \return Always empty optional success.
    */
    [[nodiscard]] std::optional<common::core::TimePosition> projectCursorPositionFor(
        const std::filesystem::path&) const override
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
    \brief Reports that no app-local project grid note value is stored.
    \return Always empty optional success.
    */
    [[nodiscard]] std::optional<common::core::Fraction> projectGridNoteValueFor(
        const std::filesystem::path&) const override
    {
        return std::nullopt;
    }

    /*!
    \brief Ignores app-local project grid note value writes.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> saveProjectGridNoteValue(
        const std::filesystem::path&, common::core::Fraction) override
    {
        return {};
    }

    /*!
    \brief Reports that no app-local project timeline zoom is stored.
    \return Always empty optional success.
    */
    [[nodiscard]] std::optional<double> projectTimelineZoomFor(
        const std::filesystem::path&) const override
    {
        return std::nullopt;
    }

    /*!
    \brief Ignores app-local project timeline zoom writes.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> saveProjectTimelineZoom(
        const std::filesystem::path&, double) override
    {
        return {};
    }

    /*!
    \brief Reports that no app-local selected arrangement is stored.
    \return Always empty optional success.
    */
    [[nodiscard]] std::optional<std::string> projectSelectedArrangementFor(
        const std::filesystem::path&) const override
    {
        return std::nullopt;
    }

    /*!
    \brief Ignores app-local selected arrangement writes.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> saveProjectSelectedArrangement(
        const std::filesystem::path&, std::string) override
    {
        return {};
    }
};

} // namespace rock_hero::editor::core::testing
