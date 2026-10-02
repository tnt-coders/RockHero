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
    // A measurement still running here was ended by the gate, not by its own result; one already
    // interrupted stays so until a sample reports it.
    if (std::holds_alternative<Measurement>(m_measurement))
    {
        m_measurement = InterruptedMeasurement{};
    }
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
    return std::holds_alternative<Measurement>(m_measurement) ? LiveInputMonitoringStatus::Measuring
                                                              : m_status;
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
    PickupClass pickups, LiveInputMonitoringContext context)
{
    const std::optional<InputDeviceIdentity> route =
        m_device_configuration.currentInputDeviceIdentity();
    if (!route.has_value())
    {
        return noRouteError();
    }

    // The raw signal is measured at unity gain with processed monitoring off. Any refusal hands
    // the route straight back to the gate, which restores whatever the store says it should be.
    auto handed_over =
        m_live_input.setLiveInputMonitoringEnabled(false)
            .and_then([this] { return m_live_input.setInputGain(Gain{defaultGainDb()}); })
            .and_then([this] { return m_live_input.setCalibrationInputMonitoringEnabled(true); });
    if (!handed_over.has_value())
    {
        refresh(context);
        return backendRejectedError(std::move(handed_over.error()));
    }

    m_measurement = Measurement{
        .route = *route,
        .pickups = pickups,
        .capture = InputCalibrationCapture{inputCalibrationTargetPeakDb(pickups)},
    };
    return {};
}

LiveInputSample LiveInputMonitor::sample(LiveInputMonitoringContext context)
{
    const AudioMeterLevel raw_level = m_live_input.readRawInputMeterLevel();

    // A measurement must hear one route throughout, so another route ends it like a gate run would.
    // Read live, not from the last refresh: the driver may not have heard of the change yet. An
    // unknown route (a refresh in flight, an unplugged device) adds silence, never another
    // device's peaks, so only a different route ends it.
    if (const auto* const running = std::get_if<Measurement>(&m_measurement); running != nullptr)
    {
        const std::optional<InputDeviceIdentity> current =
            m_device_configuration.currentInputDeviceIdentity();
        if (current.has_value() && *current != running->route)
        {
            refresh(context);
        }
    }

    if (std::holds_alternative<InterruptedMeasurement>(m_measurement))
    {
        m_measurement = NoMeasurement{};
        return LiveInputSample{
            .raw_level = raw_level,
            .measurement = InputCalibrationFailure::Interrupted,
        };
    }
    auto* const measurement = std::get_if<Measurement>(&m_measurement);
    if (measurement == nullptr)
    {
        return LiveInputSample{.raw_level = raw_level, .measurement = std::nullopt};
    }

    InputCalibrationStep step = measurement->capture.pushSample(raw_level);
    if (const auto* const progress = std::get_if<InputCalibrationRunning>(&step))
    {
        return LiveInputSample{.raw_level = raw_level, .measurement = *progress};
    }

    // The measurement ends here by its own result, so it is reset before the gate takes the route
    // back; storing a result is the driver's decision, through commitCalibration().
    Measurement ended = std::move(*measurement);
    m_measurement = NoMeasurement{};
    refresh(context);
    if (const auto* const failure = std::get_if<InputCalibrationFailure>(&step))
    {
        return LiveInputSample{.raw_level = raw_level, .measurement = *failure};
    }

    const InputCalibrationResult& result = std::get<InputCalibrationResult>(step);
    // The measured facts, so runs on an interface with a known figure can re-centre the
    // assumption: volts = that interface's full-scale peak volts x 10^(ceiling_peak_db / 20).
    RH_LOG_INFO(
        "audio.live_input_monitor",
        "Input calibration measured pickups={} ceiling_peak_db={:.1f} gain_db={:+.1f}",
        pickupType(ended.pickups).name,
        result.ceiling_peak_db,
        result.calibration_gain.db);
    return LiveInputSample{
        .raw_level = raw_level,
        .measurement = InputCalibrationMeasured{
            .route = std::move(ended.route),
            .gain = result.calibration_gain,
        },
    };
}

void LiveInputMonitor::cancelMeasurement(LiveInputMonitoringContext context)
{
    // A measurement the gate already ended has nothing left to tell the driver that ended it, and
    // the gate already holds its route.
    const bool running = std::holds_alternative<Measurement>(m_measurement);
    m_measurement = NoMeasurement{};
    if (running)
    {
        refresh(context);
    }
}

// Stores the gain for the route before the gate applies it: the gain is a fact about the route,
// true even if the backend then refuses the route. Whether the gate can then arm the route is gate
// state, which status() and the Input cue report, not a failure of the calibration.
std::expected<void, LiveInputMonitorError> LiveInputMonitor::commitCalibration(
    const InputDeviceIdentity& route, Gain gain, LiveInputMonitoringContext context)
{
    auto saved = m_audio_config_store.saveInputCalibration(
        InputCalibrationState{
            .calibration_gain = gain,
            .input_device_identity = route,
        });
    refresh(context);
    if (!saved.has_value())
    {
        return std::unexpected{LiveInputMonitorError{
            LiveInputMonitorErrorCode::CalibrationStoreUnavailable, std::move(saved.error().message)
        }};
    }
    return {};
}

LiveInputMonitoringStatus LiveInputMonitor::disable(LiveInputMonitoringStatus status)
{
    logIfRefused(m_live_input.setLiveInputMonitoringEnabled(false), "gate monitoring disable");
    m_status = status;
    return m_status;
}

} // namespace rock_hero::common::audio
