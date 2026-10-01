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
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/input/live_input_monitor_error.h>
#include <rock_hero/common/audio/input/live_input_monitoring_status.h>
#include <rock_hero/common/audio/settings/i_audio_config_store.h>

namespace rock_hero::common::audio
{

/*!
\brief Owns the calibrate-first live-input monitoring gate and calibration measurement.

The invariant: processed live monitoring is on only while a one-channel input route is current, the
audio-config store holds a calibration for that route, and the backend accepted its gain and the
route. The store is the only authority for calibration. The monitor keeps no copy of it beyond what
the last gate run found, so a calibration the other product saved is picked up at the next refresh,
and every failure recovers by re-running the gate rather than rolling state back by hand.

A plain adapter (no listener/observer): every state change is caused by a driver-invoked method, so
each product drives the gate from its own lifecycle handler and repaints after the call returns.
*/
class LiveInputMonitor final
{
public:
    /*!
    \brief Builds the service over the live-input port, device configuration, and calibration store.
    \param live_input Live-input port the gate drives.
    \param device_configuration Device-configuration port sampled for the current input route.
    \param audio_config_store Calibration store both products share.
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
    \brief Reads the current route and its calibration from the store, then runs the ordered gate.

    Ends any measurement in progress: the gate owns the route from here. A session that does not
    allow monitoring (including one being torn down) turns both monitoring paths off.
    \param context Session facts the gate evaluates.
    \return Monitoring status after the gate ran.
    */
    LiveInputMonitoringStatus refresh(LiveInputMonitoringContext context);

    /*!
    \brief Returns what the live input is doing now.
    \return Measuring while a measurement holds the route, else the last gate run's status.
    */
    [[nodiscard]] LiveInputMonitoringStatus status() const noexcept;

    /*!
    \brief Returns the input route the last refresh saw.

    Read together with calibration() and status(), so every fact a view shows comes from one gate
    run even while a device change is still on its way.
    \return Input route identity, or empty when there was none.
    */
    [[nodiscard]] const std::optional<InputDeviceIdentity>& route() const noexcept;

    /*!
    \brief Returns the stored calibration the last refresh found for route().
    \return Calibration state, or empty when the route had none or there was no route.
    */
    [[nodiscard]] const std::optional<InputCalibrationState>& calibration() const noexcept;

    /*!
    \brief Hands the current input route to a raw calibration measurement at unity gain.
    \param context Session facts the gate re-runs with if the backend refuses the measurement.
    \return Empty success, or a coarse monitoring failure.
    */
    [[nodiscard]] std::expected<void, LiveInputMonitorError> beginMeasurement(
        LiveInputMonitoringContext context);

    /*!
    \brief Ends a measurement without a result and gives the route back to the gate.
    \param context Session facts the gate evaluates.
    */
    void cancelMeasurement(LiveInputMonitoringContext context);

    /*!
    \brief Stores a measured gain for the route the measurement started on, then runs the gate.

    Refused unless a measurement is in progress and its route is still the current one, so a
    measurement can never calibrate a route it did not measure.
    \param gain_db Measured calibration gain in decibels; clamped to the supported range.
    \param context Session facts the gate evaluates.
    \return Empty success, or a coarse monitoring failure.
    */
    [[nodiscard]] std::expected<void, LiveInputMonitorError> commitMeasurement(
        double gain_db, LiveInputMonitoringContext context);

    /*!
    \brief Stores a typed gain for the current route, then runs the gate.

    The manual path for an interface whose documented gain sets the level exactly.
    \param gain_db Calibration gain in decibels; clamped to the supported range.
    \param context Session facts the gate evaluates.
    \return Empty success, or a coarse monitoring failure.
    */
    [[nodiscard]] std::expected<void, LiveInputMonitorError> commitManualCalibration(
        double gain_db, LiveInputMonitoringContext context);

private:
    // Stores the gain for the route before the gate applies it: the gain is a fact about the
    // route, true even if the backend then refuses the route.
    [[nodiscard]] std::expected<void, LiveInputMonitorError> storeAndApply(
        const InputDeviceIdentity& route, double gain_db, LiveInputMonitoringContext context);
    LiveInputMonitoringStatus disable(LiveInputMonitoringStatus status);

    ILiveInput& m_live_input;
    IAudioDeviceConfiguration& m_device_configuration;
    IAudioConfigStore& m_audio_config_store;

    // What the last gate run saw and decided.
    LiveInputMonitoringStatus m_status{LiveInputMonitoringStatus::SessionNotReady};
    std::optional<InputDeviceIdentity> m_route{};
    std::optional<InputCalibrationState> m_calibration{};

    // The route a measurement in progress started on; empty while the gate owns the route.
    std::optional<InputDeviceIdentity> m_measuring_route{};
};

} // namespace rock_hero::common::audio
