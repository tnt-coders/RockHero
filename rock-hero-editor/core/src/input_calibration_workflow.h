/*!
\file input_calibration_workflow.h
\brief Headless workflow state for editor input calibration.
*/

#pragma once

#include <expected>
#include <optional>
#include <rock_hero/common/audio/i_live_input.h>
#include <rock_hero/common/audio/input_calibration_state.h>
#include <rock_hero/editor/core/editor_view_state.h>
#include <rock_hero/editor/core/signal_chain_view_state.h>
#include <string>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Owns editor input calibration policy without touching live-input ports.

The root controller supplies current route context and executes returned effects against settings and
audio ports. The workflow owns prompt visibility, stored calibration, route identity, backend
availability, settings-window state, and active measurement metadata.
*/
class InputCalibrationWorkflow final
{
public:
    /*! \brief Root-supplied context used to project input calibration state. */
    struct Context
    {
        /*! \brief True after arrangement audio and live rig restore have committed. */
        bool project_audio_ready{false};

        /*! \brief True when the editor session has a current arrangement. */
        bool arrangement_loaded{false};

        /*! \brief Current exact input route, if the audio backend can identify one. */
        std::optional<common::audio::InputDeviceIdentity> current_input_device_identity{};
    };

    /*! \brief Live-input side effect requested by the workflow. */
    enum class Effect
    {
        /*! \brief Persist the workflow's current calibration state. */
        PersistCalibration,

        /*! \brief Disable processed live-input monitoring through the live-input port. */
        DisableLiveInputMonitoring,

        /*! \brief Disable direct calibration monitoring through the live-input port. */
        DisableCalibrationInputMonitoring,
    };

    /*! \brief Collection of effects returned by workflow transitions. */
    using Effects = std::vector<Effect>;

    /*! \brief View-facing input calibration snapshot owned by the workflow. */
    struct Snapshot
    {
        /*! \brief Calibration status shown by the signal-chain panel. */
        InputCalibrationStatus status{InputCalibrationStatus::NoActiveInputDevice};

        /*! \brief True when the user may open the calibration prompt. */
        bool calibrate_enabled{false};

        /*! \brief True when the current route may be auditioned through the live chain. */
        bool live_input_audition_available{false};

        /*! \brief True when audio-device settings may be opened. */
        bool audio_device_settings_enabled{true};

        /*! \brief Disabled-state message shown by the signal-chain panel. */
        std::string disabled_message;

        /*! \brief Prompt request shown by the editor view, if calibration UI should be visible. */
        std::optional<InputCalibrationPrompt> prompt;
    };

    /*! \brief Measurement-session metadata captured before live-input side effects are attempted. */
    struct MeasurementSession
    {
        /*! \brief Input identity that must still be current when measurement completes. */
        common::audio::InputDeviceIdentity input_device_identity;

        /*! \brief Previous matching calibration restored if the measurement is dismissed. */
        std::optional<common::audio::InputCalibrationState> previous_calibration_state;
    };

    /*! \brief Pure commit plan produced before the controller mutates live-input ports. */
    struct CommitPlan
    {
        /*! \brief Gain to write if live-input arming succeeds. */
        common::audio::Gain calibration_gain;

        /*! \brief Input identity the calibration belongs to. */
        common::audio::InputDeviceIdentity input_device_identity;

        /*! \brief Previous state to preserve if backend arming fails. */
        std::optional<common::audio::InputCalibrationState> previous_calibration_state;
    };

    /*! \brief Measurement-restore alternatives requested when a measurement ends early. */
    struct MeasurementRestore
    {
        /*! \brief No active measurement exists. */
        struct NoRestore
        {
        };

        /*! \brief Disable live input because project audio is unavailable. */
        struct DisableLiveInput
        {
        };

        /*! \brief Clear calibration because the route no longer matches. */
        struct ClearCalibration
        {
        };

        /*! \brief Clear calibration and close the prompt because the active route changed. */
        struct ClearCalibrationAndClosePrompt
        {
        };

        /*! \brief Restore a previous calibration for the unchanged route. */
        struct RestorePreviousCalibration
        {
            /*! \brief Previous matching calibration state to restore. */
            common::audio::InputCalibrationState previous_calibration_state;
        };
    };

    /*! \brief Pure restore plan produced before the controller mutates live-input ports. */
    using MeasurementRestorePlan = std::variant<
        MeasurementRestore::NoRestore, MeasurementRestore::DisableLiveInput,
        MeasurementRestore::ClearCalibration, MeasurementRestore::ClearCalibrationAndClosePrompt,
        MeasurementRestore::RestorePreviousCalibration>;

    /*! \brief Loads the persisted calibration state, dropping invalid route identities. */
    [[nodiscard]] bool load(std::optional<common::audio::InputCalibrationState> calibration_state);

    /*! \brief Returns the calibration state that should be persisted. */
    [[nodiscard]] std::optional<common::audio::InputCalibrationState> calibrationState() const;

    /*! \brief Clears the current calibration state. */
    void clearCalibration();

    /*! \brief Updates committed route identity and returns effects required by route changes. */
    [[nodiscard]] Effects syncCommittedInputDeviceIdentity(
        std::optional<common::audio::InputDeviceIdentity> current_identity);

    /*! \brief Marks audio-device settings open and requests live route teardown. */
    [[nodiscard]] Effects openAudioDeviceSettings();

    /*! \brief Marks the audio-device settings window as closed. */
    void closeAudioDeviceSettings() noexcept;

    /*! \brief Reports whether audio-device settings are currently open. */
    [[nodiscard]] bool audioDeviceSettingsOpen() const noexcept;

    /*! \brief Opens the calibration prompt when the supplied context allows it. */
    [[nodiscard]] bool requestPrompt(const Context& context);

    /*! \brief Closes the calibration prompt without changing calibration validity. */
    void closePrompt() noexcept;

    /*! \brief Reports whether the calibration prompt is visible. */
    [[nodiscard]] bool promptVisible() const noexcept;

    /*! \brief Builds the current view-facing calibration snapshot. */
    [[nodiscard]] Snapshot snapshot(const Context& context) const;

    /*! \brief Returns true when stored calibration belongs to the supplied route. */
    [[nodiscard]] bool calibrationMatches(
        const std::optional<common::audio::InputDeviceIdentity>& current_identity) const;

    /*! \brief Prepares a raw calibration measurement without mutating workflow state. */
    [[nodiscard]] std::expected<MeasurementSession, common::audio::LiveInputError>
    prepareMeasurementStart(const Context& context) const;

    /*! \brief Stores a measurement as active after live-input setup has succeeded. */
    void activateMeasurement(MeasurementSession measurement);

    /*! \brief Reports whether an automatic measurement is active. */
    [[nodiscard]] bool hasActiveMeasurement() const noexcept;

    /*! \brief Clears active measurement metadata. */
    void clearActiveMeasurement() noexcept;

    /*! \brief Prepares a completed calibration commit without mutating workflow state. */
    [[nodiscard]] std::expected<CommitPlan, common::audio::LiveInputError> prepareCommit(
        double gain_db, const std::optional<common::audio::InputDeviceIdentity>& expected_identity,
        const Context& context) const;

    /*! \brief Prepares a commit for the currently active automatic measurement. */
    [[nodiscard]] std::expected<CommitPlan, common::audio::LiveInputError>
    prepareActiveMeasurementCommit(double gain_db, const Context& context) const;

    /*! \brief Commits a successful calibration to workflow state. */
    void commitCalibration(
        common::audio::Gain gain, common::audio::InputDeviceIdentity input_device_identity);

    /*!
    \brief Preserves the previous matching calibration after backend arming fails.
    \return Gain to re-apply to the live route when a matching previous calibration was kept;
    nullopt when no matching calibration remained.
    */
    std::optional<common::audio::Gain> preservePreviousCalibrationAfterCommitFailure(
        const std::optional<common::audio::InputCalibrationState>& previous_state,
        const std::optional<common::audio::InputDeviceIdentity>& current_identity);

    /*! \brief Prepares measurement rollback without mutating workflow state. */
    [[nodiscard]] MeasurementRestorePlan prepareMeasurementRestore(const Context& context) const;

    /*! \brief Clears calibration after measurement restoration determines the route changed. */
    void clearCalibrationAfterMeasurement();

    /*! \brief Restores a previous matching calibration and records backend availability. */
    void restorePreviousCalibration(
        common::audio::InputCalibrationState previous_state, bool backend_available);

    /*! \brief Marks the matching calibrated route as available in the live-input backend. */
    void markBackendAvailable() noexcept;

    /*! \brief Marks a matching calibrated route as unavailable in the live-input backend. */
    void markBackendUnavailable() noexcept;

private:
    [[nodiscard]] InputCalibrationStatus status(
        const std::optional<common::audio::InputDeviceIdentity>& current_identity) const;
    [[nodiscard]] std::string disabledMessage(
        const std::optional<common::audio::InputDeviceIdentity>& current_identity) const;
    [[nodiscard]] double promptGainDb(
        const std::optional<common::audio::InputDeviceIdentity>& current_identity) const;

    std::optional<common::audio::InputCalibrationState> m_calibration_state{};
    std::optional<common::audio::InputDeviceIdentity> m_committed_input_device_identity{};
    bool m_prompt_visible{false};
    bool m_audio_device_settings_open{false};
    std::optional<MeasurementSession> m_active_measurement{};
    bool m_backend_available{true};
};

} // namespace rock_hero::editor::core
