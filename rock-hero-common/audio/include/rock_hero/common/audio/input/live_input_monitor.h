/*!
\file live_input_monitor.h
\brief Calibrate-first live-input monitoring service shared by the editor and the game.
*/

#pragma once

#include <expected>
#include <optional>
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <rock_hero/common/audio/input/i_live_input.h>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/input/input_calibration_workflow.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/input/live_input_monitor_error.h>
#include <rock_hero/common/audio/input/live_input_monitoring_status.h>
#include <rock_hero/common/audio/settings/i_audio_config_store.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <string_view>

namespace rock_hero::common::audio
{

/*!
\brief Owns the calibrate-first live-input monitoring gate and calibration orchestration.

A plain adapter (no listener/observer): every state change is caused by a driver-invoked method,
so each product drives the gate from its own lifecycle handler and repaints after the call returns.
Holds the pure InputCalibrationWorkflow by value and performs its plans against the injected ports.
The IAudioConfigStore is a single swappable dependency: each composition root injects the app's own
store (or, for the editor, plan 48's effective-source facade) with no edit here.
*/
class LiveInputMonitor final
{
public:
    /*!
    \brief Builds the service over the live-input port, device configuration, and calibration store.
    \param live_input Live-input port the gate drives.
    \param device_configuration Device-configuration port sampled for the current input route.
    \param audio_config_store Calibration store this app owns; swappable per composition root.
    */
    LiveInputMonitor(
        ILiveInput& live_input, IAudioDeviceConfiguration& device_configuration,
        IAudioConfigStore& audio_config_store);

    /*! \brief Copying is disabled because the service holds injected port references. */
    LiveInputMonitor(const LiveInputMonitor&) = delete;

    /*!
    \brief Copy assignment is disabled because the service holds injected port references.
    \return Reference to this service.
    */
    LiveInputMonitor& operator=(const LiveInputMonitor&) = delete;

    /*! \brief Moving is disabled so the service keeps a stable address for its driver. */
    LiveInputMonitor(LiveInputMonitor&&) = delete;

    /*!
    \brief Move assignment is disabled so the service keeps a stable address for its driver.
    \return Reference to this service.
    */
    LiveInputMonitor& operator=(LiveInputMonitor&&) = delete;

    /*! \brief Destroys the service. */
    ~LiveInputMonitor() = default;

    /*!
    \brief Re-reads calibration for the current route, then re-runs the ordered gate.
    \param context Session facts the gate evaluates.
    \return Monitoring status after the gate ran.
    */
    [[nodiscard]] LiveInputMonitoringStatus refresh(LiveInputMonitoringContext context);

    /*!
    \brief Re-runs the ordered gate over the already-selected calibration.
    \param context Session facts the gate evaluates.
    \return Monitoring status after the gate ran.
    */
    [[nodiscard]] LiveInputMonitoringStatus applyGate(LiveInputMonitoringContext context);

    /*! \brief Tears down both processed and calibration monitoring on a best-effort basis. */
    void disableMonitoring();

    /*!
    \brief Returns the status cached by the last gate run.
    \return Last monitoring status.
    */
    [[nodiscard]] LiveInputMonitoringStatus status() const noexcept;

    /*!
    \brief Prepares the current input route for a raw calibration measurement.
    \param context Session facts used to validate the measurement start.
    \return Empty success, or a coarse monitoring failure.
    */
    [[nodiscard]] std::expected<void, LiveInputMonitorError> beginMeasurement(
        LiveInputMonitoringContext context);

    /*!
    \brief Stops an active measurement and restores the prior route state.
    \return Empty success, or a coarse monitoring failure.
    */
    [[nodiscard]] std::expected<void, LiveInputMonitorError> cancelMeasurement();

    /*!
    \brief Commits a measured calibration gain and enables processed monitoring.
    \param gain_db Measured gain in decibels.
    \param expected_identity Input identity captured when the measurement started, if any.
    \return Empty success, or a coarse monitoring failure.
    */
    [[nodiscard]] std::expected<void, LiveInputMonitorError> commitCalibration(
        double gain_db, const std::optional<InputDeviceIdentity>& expected_identity);

    /*!
    \brief Commits a manually entered calibration gain and enables processed monitoring.
    \param gain_db Gain in decibels.
    \return Empty success, or a coarse monitoring failure.
    */
    [[nodiscard]] std::expected<void, LiveInputMonitorError> setManualCalibration(double gain_db);

    /*!
    \brief Opens the calibration prompt when the supplied context allows it.
    \param context Session facts used to gate prompt visibility.
    \return True when the prompt became or remained visible.
    */
    [[nodiscard]] bool requestPrompt(LiveInputMonitoringContext context);

    /*! \brief Closes the calibration prompt without changing calibration validity. */
    void closePrompt() noexcept;

    /*! \brief Marks audio-device settings open and releases the calibrated route. */
    void openAudioDeviceSettings();

    /*!
    \brief Re-selects calibration and re-runs the gate after the settings window closes.
    \param context Session facts the gate evaluates.
    \return Monitoring status after the gate ran.
    */
    [[nodiscard]] LiveInputMonitoringStatus closeAudioDeviceSettings(
        LiveInputMonitoringContext context);

    /*!
    \brief Returns the active calibration state for the current route.
    \return Stored calibration state, or empty when no route is calibrated.
    */
    [[nodiscard]] std::optional<InputCalibrationState> activeCalibrationState() const;

    /*!
    \brief Returns the current physical input route identity, if the backend can supply one.
    \return Current input identity, or empty.
    */
    [[nodiscard]] std::optional<InputDeviceIdentity> currentInputDeviceIdentity() const;

    /*!
    \brief Reports whether stored calibration matches the current route.
    \return True when stored calibration belongs to the current input route.
    */
    [[nodiscard]] bool calibrationMatchesCurrentRoute() const;

    /*!
    \brief Reports whether the calibration prompt is visible.
    \return True when the view should present the calibration prompt.
    */
    [[nodiscard]] bool promptVisible() const noexcept;

    /*!
    \brief Reports whether audio-device settings are currently open.
    \return True while the audio-device settings window is open.
    */
    [[nodiscard]] bool audioDeviceSettingsOpen() const noexcept;

    /*!
    \brief Reports whether the matching calibrated route is available in the backend.
    \return True when the last live-input arming attempt for the matching route succeeded.
    */
    [[nodiscard]] bool backendAvailable() const noexcept;

private:
    // Backend routing snapshot used to roll back a failed calibration setup (was the editor's
    // InputCalibrationRouteState -- a live-input port concern, not workflow state).
    struct RouteState
    {
        Gain input_gain;
        bool live_input_monitoring_enabled{false};
        bool calibration_input_monitoring_enabled{false};
    };

    // Samples currentInputDeviceIdentity() ONCE and builds the workflow context for one operation.
    [[nodiscard]] InputCalibrationWorkflow::Context workflowContext(
        LiveInputMonitoringContext context) const;
    [[nodiscard]] LiveInputMonitoringStatus applyGateInternal(
        const InputCalibrationWorkflow::Context& context);
    [[nodiscard]] std::expected<void, LiveInputMonitorError> reselectCalibration(
        const InputCalibrationWorkflow::Context& context);
    [[nodiscard]] std::expected<void, LiveInputMonitorError> commitCalibrationInternal(
        double gain_db, const std::optional<InputDeviceIdentity>& expected_identity);
    [[nodiscard]] std::expected<void, LiveInputMonitorError> restoreMeasurementState(
        const InputCalibrationWorkflow::Context& context);
    void saveActiveInputCalibration();
    [[nodiscard]] RouteState currentRouteState() const;
    void restoreRouteStateBestEffort(const RouteState& route_state);
    void executeInputCalibrationEffects(const InputCalibrationWorkflow::Effects& effects);
    bool setLiveInputMonitoringBestEffort(bool enabled, std::string_view reason);
    bool setCalibrationInputMonitoringBestEffort(bool enabled, std::string_view reason);
    bool setInputGainBestEffort(Gain gain, std::string_view reason);

    InputCalibrationWorkflow m_workflow;
    ILiveInput& m_live_input;
    IAudioDeviceConfiguration& m_device_configuration;
    IAudioConfigStore& m_audio_config_store; // swappable: app's own store, or plan 48's facade
    LiveInputMonitoringStatus m_status{};

    // Latest session facts supplied by a context-taking driver call. commitCalibration() and
    // cancelMeasurement() take no context of their own -- they run inside an active prompt the
    // driver established with refresh()/requestPrompt()/beginMeasurement() -- so they reuse the
    // session flags captured here while re-sampling the input identity fresh per operation.
    LiveInputMonitoringContext m_context{};
};

} // namespace rock_hero::common::audio
