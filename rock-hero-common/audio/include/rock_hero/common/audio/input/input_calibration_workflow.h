/*!
\file input_calibration_workflow.h
\brief Headless workflow state for live-input calibration.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <rock_hero/common/audio/input/i_live_input.h>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/input/live_input_monitoring_status.h>
#include <variant>
#include <vector>

namespace rock_hero::common::audio
{

/*!
\brief Owns live-input calibration policy without touching live-input ports.

The owning controller supplies current route context and executes returned effects against settings
and audio ports. The workflow owns prompt visibility, stored calibration, route identity, backend
availability, settings-window state, and active measurement metadata.
*/
class InputCalibrationWorkflow final
{
public:
    /*! \brief Owner-supplied context used to project input calibration state. */
    struct Context
    {
        /*! \brief True when the live input path is up so a raw signal can be measured or monitored. */
        bool live_input_ready{false};

        /*! \brief True when the editor session has a current arrangement. */
        bool arrangement_loaded{false};

        /*! \brief Current physical input route, if the audio backend can identify one. */
        std::optional<InputDeviceIdentity> current_input_device_identity{};
    };

    /*! \brief Live-input side effect requested by the workflow. */
    enum class Effect : std::uint8_t
    {
        /*! \brief Disable processed live-input monitoring through the live-input port. */
        DisableLiveInputMonitoring,

        /*! \brief Disable direct calibration monitoring through the live-input port. */
        DisableCalibrationInputMonitoring,
    };

    /*! \brief Collection of effects returned by workflow transitions. */
    using Effects = std::vector<Effect>;

    /*!
    \brief Measurement-session metadata captured before live-input side effects are attempted.
    */
    struct MeasurementSession
    {
        /*! \brief Input identity that must still be current when measurement completes. */
        InputDeviceIdentity input_device_identity;

        /*! \brief Previous matching calibration restored if the measurement is dismissed. */
        std::optional<InputCalibrationState> previous_calibration_state;
    };

    /*! \brief Pure commit plan produced before the controller mutates live-input ports. */
    struct CommitPlan
    {
        /*! \brief Gain to write if live-input arming succeeds. */
        Gain calibration_gain;

        /*! \brief Input identity the calibration belongs to. */
        InputDeviceIdentity input_device_identity;

        /*! \brief Previous state to preserve if backend arming fails. */
        std::optional<InputCalibrationState> previous_calibration_state;
    };

    /*! \brief Measurement-restore alternatives requested when a measurement ends early. */
    struct MeasurementRestore
    {
        /*! \brief No active measurement exists. */
        struct NoRestore
        {
        };

        /*! \brief Disable live input because the live input path is unavailable. */
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
            InputCalibrationState previous_calibration_state;
        };
    };

    /*! \brief Pure restore plan produced before the controller mutates live-input ports. */
    using MeasurementRestorePlan = std::variant<
        MeasurementRestore::NoRestore, MeasurementRestore::DisableLiveInput,
        MeasurementRestore::ClearCalibration, MeasurementRestore::ClearCalibrationAndClosePrompt,
        MeasurementRestore::RestorePreviousCalibration>;

    /*!
    \brief Returns the active calibration state for the current route.
    \return Stored calibration state, or empty when no route is calibrated.
    */
    [[nodiscard]] std::optional<InputCalibrationState> activeCalibrationState() const;

    /*! \brief Clears the current calibration state. */
    void clearCalibration();

    /*!
    \brief Updates committed route identity and returns effects required by route changes.
    \param current_identity Current physical input route, if one is active.
    \param saved_calibration Calibration loaded from settings for the current route, if any.
    \return Effects the controller should apply after the route-state transition.
    */
    [[nodiscard]] Effects syncCommittedInputDeviceIdentity(
        std::optional<InputDeviceIdentity> current_identity,
        std::optional<InputCalibrationState> saved_calibration);

    /*!
    \brief Marks audio-device settings open and requests live route teardown.
    \return Effects the controller should apply before opening settings.
    */
    [[nodiscard]] Effects openAudioDeviceSettings();

    /*! \brief Marks the audio-device settings window as closed. */
    void closeAudioDeviceSettings() noexcept;

    /*!
    \brief Reports whether audio-device settings are currently open.
    \return True while the audio-device settings window is open.
    */
    [[nodiscard]] bool audioDeviceSettingsOpen() const noexcept;

    /*!
    \brief Opens the calibration prompt when the supplied context allows it.
    \param context Current controller facts used to gate prompt visibility.
    \return True when the prompt became or remained visible.
    */
    [[nodiscard]] bool requestPrompt(const Context& context);

    /*! \brief Closes the calibration prompt without changing calibration validity. */
    void closePrompt() noexcept;

    /*!
    \brief Reports whether the calibration prompt is visible.
    \return True when the view should present the calibration prompt.
    */
    [[nodiscard]] bool promptVisible() const noexcept;

    /*!
    \brief Reports whether the matching calibrated route is available in the live-input backend.
    \return True when the last live-input arming attempt for the matching route succeeded.
    */
    [[nodiscard]] bool backendAvailable() const noexcept;

    /*!
    \brief Evaluates whether processed live-input monitoring should be active.

    Mirrors the controller's ordered live-input gate branches: it early-outs on an open settings
    window and an unready session before it inspects the route and stored calibration. It reports
    only the decisions the workflow can make from its own state; the backend-availability and
    calibration-store outcomes are post-I/O facts the downstream service adds.
    \param context Current controller facts used to gate monitoring.
    \return Monitoring state paired with the ordered reason it is disabled.
    */
    [[nodiscard]] LiveInputMonitoringStatus evaluateMonitoring(const Context& context) const
    {
        if (m_audio_device_settings_open)
        {
            return {
                LiveInputMonitoringState::Disabled,
                LiveInputMonitoringDisabledReason::AudioDeviceSettingsOpen,
            };
        }

        if (!context.live_input_ready || !context.arrangement_loaded)
        {
            return {
                LiveInputMonitoringState::Disabled,
                LiveInputMonitoringDisabledReason::SessionNotReady
            };
        }

        if (!context.current_input_device_identity.has_value())
        {
            return {
                LiveInputMonitoringState::Disabled, LiveInputMonitoringDisabledReason::NoInputDevice
            };
        }

        if (!m_calibration_state.has_value())
        {
            return {
                LiveInputMonitoringState::Disabled,
                LiveInputMonitoringDisabledReason::MissingCalibration
            };
        }

        if (!calibrationMatches(context.current_input_device_identity))
        {
            return {
                LiveInputMonitoringState::Disabled,
                LiveInputMonitoringDisabledReason::CalibrationRouteMismatch,
            };
        }

        return {LiveInputMonitoringState::Active, LiveInputMonitoringDisabledReason::None};
    }

    /*!
    \brief Reports whether stored calibration belongs to the supplied route.
    \param current_identity Current physical input route, if one is active.
    \return True when stored calibration matches the current route.
    */
    [[nodiscard]] bool calibrationMatches(
        const std::optional<InputDeviceIdentity>& current_identity) const;

    /*!
    \brief Prepares a raw calibration measurement without mutating workflow state.
    \param context Current controller facts used to validate measurement start.
    \return Measurement metadata, or a typed live-input failure.
    */
    [[nodiscard]] std::expected<MeasurementSession, LiveInputError> prepareMeasurementStart(
        const Context& context) const;

    /*!
    \brief Stores a measurement as active after live-input setup has succeeded.
    \param measurement Measurement metadata captured before live-input side effects.
    */
    void activateMeasurement(MeasurementSession measurement);

    /*!
    \brief Reports whether an automatic measurement is active.
    \return True when measurement metadata is currently stored.
    */
    [[nodiscard]] bool hasActiveMeasurement() const noexcept;

    /*! \brief Clears active measurement metadata. */
    void clearActiveMeasurement() noexcept;

    /*!
    \brief Prepares a completed calibration commit without mutating workflow state.
    \param gain_db Measured gain value in decibels.
    \param expected_identity Input identity captured when the measurement started.
    \param context Current controller facts used to validate the commit route.
    \return Commit plan, or a typed live-input failure.
    */
    [[nodiscard]] std::expected<CommitPlan, LiveInputError> prepareCommit(
        double gain_db, const std::optional<InputDeviceIdentity>& expected_identity,
        const Context& context) const;

    /*!
    \brief Prepares a commit for the currently active automatic measurement.
    \param gain_db Measured gain value in decibels.
    \param context Current controller facts used to validate the commit route.
    \return Commit plan, or a typed live-input failure.
    */
    [[nodiscard]] std::expected<CommitPlan, LiveInputError> prepareActiveMeasurementCommit(
        double gain_db, const Context& context) const;

    /*!
    \brief Commits a successful calibration to workflow state.
    \param gain Gain to store for the input route.
    \param input_device_identity Input route the calibration belongs to.
    */
    void commitCalibration(Gain gain, InputDeviceIdentity input_device_identity);

    /*!
    \brief Preserves the previous matching calibration after backend arming fails.
    \param previous_state Previous calibration captured before the failed commit.
    \param current_identity Current physical input route, if one is active.
    \return Gain to re-apply to the live route when a matching previous calibration was kept;
    nullopt when no matching calibration remained.
    */
    std::optional<Gain> preservePreviousCalibrationAfterCommitFailure(
        const std::optional<InputCalibrationState>& previous_state,
        const std::optional<InputDeviceIdentity>& current_identity);

    /*!
    \brief Prepares measurement rollback without mutating workflow state.
    \param context Current controller facts used to choose rollback effects.
    \return Restore plan for the controller to apply.
    */
    [[nodiscard]] MeasurementRestorePlan prepareMeasurementRestore(const Context& context) const;

    /*! \brief Clears calibration after measurement restoration determines the route changed. */
    void clearCalibrationAfterMeasurement();

    /*!
    \brief Restores a previous matching calibration and records backend availability.
    \param previous_state Previous matching calibration state to restore.
    \param backend_available True when the live-input backend accepted the restore.
    */
    void restorePreviousCalibration(InputCalibrationState previous_state, bool backend_available);

    /*! \brief Marks the matching calibrated route as available in the live-input backend. */
    void markBackendAvailable() noexcept;

    /*! \brief Marks a matching calibrated route as unavailable in the live-input backend. */
    void markBackendUnavailable() noexcept;

private:
    void selectActiveCalibration(
        const InputDeviceIdentity& current_identity,
        std::optional<InputCalibrationState> saved_calibration);

    std::optional<InputCalibrationState> m_calibration_state{};
    std::optional<InputDeviceIdentity> m_committed_input_device_identity{};
    bool m_prompt_visible{false};
    bool m_audio_device_settings_open{false};
    std::optional<MeasurementSession> m_active_measurement{};
    bool m_backend_available{true};
};

} // namespace rock_hero::common::audio
