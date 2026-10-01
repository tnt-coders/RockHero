#include "input/live_input_monitor.h"

#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/core/shared/logger.h>
#include <string_view>
#include <utility>
#include <variant>

namespace rock_hero::common::audio
{

namespace
{

[[nodiscard]] std::unexpected<LiveInputMonitorError> noRouteError()
{
    return std::unexpected{LiveInputMonitorError{
        LiveInputMonitorErrorCode::InvalidRequest, "No live input route is available to calibrate."
    }};
}

[[nodiscard]] std::unexpected<LiveInputMonitorError> backendRejectedError(LiveInputError error)
{
    return std::unexpected{
        LiveInputMonitorError{LiveInputMonitorErrorCode::BackendRejected, std::move(error.message)}
    };
}

// Logs a refused live-input setter on a path that must carry on regardless.
void logIfRefused(const std::expected<void, LiveInputError>& result, std::string_view context)
{
    if (!result.has_value())
    {
        RH_LOG_WARNING(
            "audio.live_input_monitor",
            "Live input refused a setter context={:?} detail={:?}",
            context,
            result.error().message);
    }
}

} // namespace

LiveInputMonitor::LiveInputMonitor(
    ILiveInput& live_input, IAudioDeviceConfiguration& device_configuration,
    IAudioConfigStore& audio_config_store)
    : m_live_input(live_input)
    , m_device_configuration(device_configuration)
    , m_audio_config_store(audio_config_store)
{}

LiveInputMonitoringStatus LiveInputMonitor::refresh(LiveInputMonitoringContext context)
{
    m_measurement.reset();
    logIfRefused(
        m_live_input.setCalibrationInputMonitoringEnabled(false), "gate calibration disable");

    m_route = m_device_configuration.currentInputDeviceIdentity();
    m_calibration.reset();
    if (m_route.has_value())
    {
        auto stored = m_audio_config_store.inputCalibrationFor(*m_route);
        if (!stored.has_value())
        {
            RH_LOG_WARNING(
                "audio.live_input_monitor",
                "Input calibration store read failed detail={:?}",
                stored.error().message);
            return disable(LiveInputMonitoringStatus::CalibrationStoreUnavailable);
        }
        m_calibration = std::move(*stored);
    }

    if (!context.session_ready)
    {
        return disable(LiveInputMonitoringStatus::SessionNotReady);
    }
    if (!m_route.has_value())
    {
        return disable(LiveInputMonitoringStatus::NoInputDevice);
    }
    if (!m_calibration.has_value())
    {
        return disable(LiveInputMonitoringStatus::MissingCalibration);
    }

    auto gain_applied = m_live_input.setInputGain(m_calibration->calibration_gain);
    logIfRefused(gain_applied, "gate calibrated gain");
    if (!gain_applied.has_value())
    {
        return disable(LiveInputMonitoringStatus::BackendUnavailable);
    }

    auto monitoring_enabled = m_live_input.setLiveInputMonitoringEnabled(true);
    logIfRefused(monitoring_enabled, "gate monitoring enable");
    if (!monitoring_enabled.has_value())
    {
        return disable(LiveInputMonitoringStatus::BackendUnavailable);
    }

    m_status = LiveInputMonitoringStatus::Active;
    return m_status;
}

LiveInputMonitoringStatus LiveInputMonitor::status() const noexcept
{
    return m_measurement.has_value() ? LiveInputMonitoringStatus::Measuring : m_status;
}

const std::optional<InputDeviceIdentity>& LiveInputMonitor::route() const noexcept
{
    return m_route;
}

const std::optional<InputCalibrationState>& LiveInputMonitor::calibration() const noexcept
{
    return m_calibration;
}

std::expected<void, LiveInputMonitorError> LiveInputMonitor::beginMeasurement(
    LiveInputMonitoringContext context)
{
    const std::optional<InputDeviceIdentity> route =
        m_device_configuration.currentInputDeviceIdentity();
    if (!route.has_value())
    {
        return noRouteError();
    }

    // The raw signal is measured at unity gain with processed monitoring off. Any refusal hands
    // the route straight back to the gate, which restores whatever the store says it should be.
    auto monitoring_disabled = m_live_input.setLiveInputMonitoringEnabled(false);
    if (!monitoring_disabled.has_value())
    {
        refresh(context);
        return backendRejectedError(std::move(monitoring_disabled.error()));
    }
    auto gain_reset = m_live_input.setInputGain(Gain{defaultGainDb()});
    if (!gain_reset.has_value())
    {
        refresh(context);
        return backendRejectedError(std::move(gain_reset.error()));
    }
    auto calibration_monitoring = m_live_input.setCalibrationInputMonitoringEnabled(true);
    if (!calibration_monitoring.has_value())
    {
        refresh(context);
        return backendRejectedError(std::move(calibration_monitoring.error()));
    }

    m_measurement.emplace(Measurement{.route = *route, .capture = InputCalibrationCapture{}});
    return {};
}

LiveInputSample LiveInputMonitor::sample(LiveInputMonitoringContext context)
{
    const AudioMeterLevel raw_level = m_live_input.readRawInputMeterLevel();
    if (!m_measurement.has_value())
    {
        return LiveInputSample{.raw_level = raw_level, .measurement = std::nullopt};
    }

    InputCalibrationStep step = m_measurement->capture.pushSample(raw_level);
    if (const auto* const stage = std::get_if<InputCalibrationStage>(&step))
    {
        return LiveInputSample{.raw_level = raw_level, .measurement = *stage};
    }

    const InputDeviceIdentity measured_route = m_measurement->route;
    if (auto* const error = std::get_if<InputCalibrationError>(&step))
    {
        refresh(context);
        return LiveInputSample{
            .raw_level = raw_level,
            .measurement = InputCalibrationFailed{std::move(error->message)},
        };
    }

    const Gain gain = std::get<InputCalibrationResult>(step).calibration_gain;
    auto committed = commitMeasurement(measured_route, gain, context);
    if (!committed.has_value())
    {
        return LiveInputSample{
            .raw_level = raw_level,
            .measurement = InputCalibrationFailed{std::move(committed.error().message)},
        };
    }
    return LiveInputSample{
        .raw_level = raw_level,
        .measurement = InputCalibrationCommitted{.gain = gain},
    };
}

void LiveInputMonitor::cancelMeasurement(LiveInputMonitoringContext context)
{
    if (m_measurement.has_value())
    {
        refresh(context);
    }
}

std::expected<void, LiveInputMonitorError> LiveInputMonitor::commitMeasurement(
    const InputDeviceIdentity& measured_route, Gain gain, LiveInputMonitoringContext context)
{
    // Read live, not from the last refresh: a device change may still be on its way.
    if (m_device_configuration.currentInputDeviceIdentity() != measured_route)
    {
        refresh(context);
        return std::unexpected{LiveInputMonitorError{
            LiveInputMonitorErrorCode::InvalidRequest, "Input route changed during calibration."
        }};
    }

    return storeAndApply(measured_route, gain.db, context);
}

std::expected<void, LiveInputMonitorError> LiveInputMonitor::commitManualCalibration(
    double gain_db, LiveInputMonitoringContext context)
{
    const std::optional<InputDeviceIdentity> route =
        m_device_configuration.currentInputDeviceIdentity();
    if (!route.has_value())
    {
        return noRouteError();
    }

    return storeAndApply(*route, gain_db, context);
}

std::expected<void, LiveInputMonitorError> LiveInputMonitor::storeAndApply(
    const InputDeviceIdentity& route, double gain_db, LiveInputMonitoringContext context)
{
    auto saved = m_audio_config_store.saveInputCalibration(
        InputCalibrationState{
            .calibration_gain = clampGain(Gain{gain_db}),
            .input_device_identity = route,
        });
    if (!saved.has_value())
    {
        refresh(context);
        return std::unexpected{LiveInputMonitorError{
            LiveInputMonitorErrorCode::CalibrationStoreUnavailable, std::move(saved.error().message)
        }};
    }

    // Saved is committed. Whether the gate can then arm the route is gate state, which status()
    // and the Input cue report, not a failure of the calibration.
    refresh(context);
    return {};
}

LiveInputMonitoringStatus LiveInputMonitor::disable(LiveInputMonitoringStatus status)
{
    logIfRefused(m_live_input.setLiveInputMonitoringEnabled(false), "gate monitoring disable");
    m_status = status;
    return m_status;
}

} // namespace rock_hero::common::audio
