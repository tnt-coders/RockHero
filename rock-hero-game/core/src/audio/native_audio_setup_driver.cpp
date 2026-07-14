#include "audio/native_audio_setup.h"

#include <algorithm>
#include <rock_hero/common/audio/input/audio_meter_snapshot.h>
#include <rock_hero/common/audio/settings/active_device_route.h>
#include <string>
#include <utility>

namespace rock_hero::game::core
{

NativeAudioSetup::NativeAudioSetup(
    common::audio::IAudioDeviceSettings& device_settings,
    common::audio::IAudioDeviceConfiguration& device_configuration,
    common::audio::LiveInputMonitor& live_input_monitor, common::audio::ILiveInput& live_input,
    common::audio::IAudioConfigStore& audio_config_store, IGameSettings& game_settings,
    CaptureSettings capture_settings)
    : m_device_settings(device_settings)
    , m_device_configuration(device_configuration)
    , m_live_input_monitor(live_input_monitor)
    , m_live_input(live_input)
    , m_audio_config_store(audio_config_store)
    , m_game_settings(game_settings)
    , m_capture(
          capture_settings.settle_sample_count, capture_settings.wait_sample_count,
          capture_settings.measurement_sample_count)
{}

NativeAudioSetupPhase NativeAudioSetup::phase() const noexcept
{
    return m_machine.phase();
}

std::optional<NativeAudioSetupError> NativeAudioSetup::failure() const
{
    return m_machine.failure();
}

void NativeAudioSetup::beginDeviceSelection() noexcept
{
    if (m_machine.canApplyDevice())
    {
        m_machine.beginDeviceSelection();
    }
}

common::audio::IAudioDeviceSettings& NativeAudioSetup::deviceSettings() noexcept
{
    return m_device_settings;
}

std::expected<void, NativeAudioSetupError> NativeAudioSetup::applySelectedDevice()
{
    if (!m_machine.canApplyDevice())
    {
        return std::unexpected{NativeAudioSetupError{
            NativeAudioSetupErrorCode::InvalidRequest,
            "Cannot apply a device while gain calibration is in progress.",
        }};
    }

    // Represent the staged-to-applied edge; any failure past this point is terminal and clears a
    // prior failure so a retry after a failed apply starts clean.
    m_machine.beginDeviceSelection();

    const auto applied = m_device_settings.apply();
    if (!applied.has_value())
    {
        return std::unexpected{failAndRecord(
            NativeAudioSetupError{
                NativeAudioSetupErrorCode::DeviceApplyFailed, applied.error().message
            })};
    }

    // Capture the opaque restore blob and the resolved input identity together, so the persisted
    // route and its mirrored identity come from one apply and can never drift apart.
    std::optional<std::string> serialized_state = m_device_configuration.serializedDeviceState();
    const std::optional<common::audio::InputDeviceIdentity> identity =
        m_device_configuration.currentInputDeviceIdentity();
    if (!serialized_state.has_value() || serialized_state->empty() || !identity.has_value() ||
        !common::audio::isValidInputDeviceIdentity(*identity))
    {
        return std::unexpected{failAndRecord(
            NativeAudioSetupError{NativeAudioSetupErrorCode::DeviceRouteUnresolved})};
    }

    // The game-private slot-0 player-to-route mapping. Its primary route is mirrored into the shared
    // store's ActiveDeviceRoute.identity through the plan-32 Phase 1 pure mapping.
    const GameAudioConfig game_config{
        .players = {PlayerInputConfig{.player_slot = 0, .route = *identity}}
    };
    const std::optional<common::audio::InputDeviceIdentity> primary_route =
        primaryPlayerRoute(game_config);

    common::audio::ActiveDeviceRoute route{
        .serialized_state = std::move(*serialized_state), .identity = primary_route
    };
    if (const auto stored = m_audio_config_store.setActiveDeviceRoute(std::move(route));
        !stored.has_value())
    {
        return std::unexpected{failAndRecord(
            NativeAudioSetupError{
                NativeAudioSetupErrorCode::StorePersistFailed, stored.error().message
            })};
    }

    if (const auto saved = m_game_settings.setGameAudioConfig(game_config); !saved.has_value())
    {
        return std::unexpected{failAndRecord(
            NativeAudioSetupError{
                NativeAudioSetupErrorCode::StorePersistFailed, saved.error().message
            })};
    }

    m_active_route_identity = *identity;
    m_machine.deviceApplied();
    return {};
}

std::expected<void, NativeAudioSetupError> NativeAudioSetup::beginGainCalibration()
{
    if (!m_machine.canCalibrate())
    {
        return std::unexpected{NativeAudioSetupError{
            NativeAudioSetupErrorCode::InvalidRequest,
            "Gain calibration is only available after a device is applied.",
        }};
    }

    const common::audio::LiveInputMonitoringContext context = calibrationContext();
    if (!m_live_input_monitor.requestPrompt(context))
    {
        return std::unexpected{NativeAudioSetupError{
            NativeAudioSetupErrorCode::CalibrationFailed,
            "No live input route is available to calibrate.",
        }};
    }

    if (const auto began = m_live_input_monitor.beginMeasurement(context); !began.has_value())
    {
        return std::unexpected{NativeAudioSetupError{
            NativeAudioSetupErrorCode::CalibrationFailed, began.error().message
        }};
    }

    m_capture.start();
    return {};
}

std::expected<GainCalibrationProgress, NativeAudioSetupError> NativeAudioSetup::
    sampleGainCalibration()
{
    if (!m_machine.canCalibrate() || !m_capture.active())
    {
        return std::unexpected{NativeAudioSetupError{
            NativeAudioSetupErrorCode::InvalidRequest,
            "No gain-calibration measurement is in progress.",
        }};
    }

    const common::audio::AudioMeterLevel level = m_live_input.rawInputMeterLevel();
    const common::audio::InputCalibrationCaptureUpdate update = m_capture.pushSample(level);
    switch (update.phase)
    {
        case common::audio::InputCalibrationCapturePhase::Settling:
            return GainCalibrationProgress::Settling;
        case common::audio::InputCalibrationCapturePhase::WaitingForInput:
            return GainCalibrationProgress::WaitingForStrum;
        case common::audio::InputCalibrationCapturePhase::Measuring:
            return GainCalibrationProgress::Measuring;
        case common::audio::InputCalibrationCapturePhase::Complete:
        {
            if (!update.result.has_value())
            {
                return std::unexpected{NativeAudioSetupError{
                    NativeAudioSetupErrorCode::CalibrationFailed,
                    "Calibration completed without a result.",
                }};
            }

            if (const auto committed = commitMeasuredGain(update.result->calibration_gain.db);
                !committed.has_value())
            {
                return std::unexpected{committed.error()};
            }
            return GainCalibrationProgress::Committed;
        }
        case common::audio::InputCalibrationCapturePhase::Failed:
        {
            // A signal-quality failure is recoverable: restore the route and stay in CalibratingGain
            // so the player can strum again. The applied device and its persisted route are intact.
            static_cast<void>(m_live_input_monitor.cancelMeasurement());
            std::string message = update.error.has_value()
                                      ? update.error->message
                                      : std::string{"Gain calibration failed."};
            return std::unexpected{NativeAudioSetupError{
                NativeAudioSetupErrorCode::CalibrationFailed, std::move(message)
            }};
        }
        case common::audio::InputCalibrationCapturePhase::Idle:
            break;
    }

    return std::unexpected{NativeAudioSetupError{
        NativeAudioSetupErrorCode::InvalidRequest,
        "No gain-calibration measurement is in progress.",
    }};
}

void NativeAudioSetup::cancelGainCalibration()
{
    if (!m_machine.canCalibrate())
    {
        return;
    }

    static_cast<void>(m_live_input_monitor.cancelMeasurement());
    m_capture.reset();
}

common::audio::LiveInputMonitoringContext NativeAudioSetup::calibrationContext() const noexcept
{
    // Calibration only needs the live input path up, which an applied device provides. The setup
    // menu has no arrangement, so arrangement_loaded stays false honestly -- it gates active
    // processed monitoring, not the raw measurement calibration performs here.
    return common::audio::LiveInputMonitoringContext{.live_input_ready = true};
}

std::expected<void, NativeAudioSetupError> NativeAudioSetup::commitMeasuredGain(double gain_db)
{
    // commitCalibration persists the InputCalibrationState for the active route through the store
    // the monitor was composed with (the game's own store).
    const auto committed = m_live_input_monitor.commitCalibration(gain_db, m_active_route_identity);
    m_capture.reset();
    if (!committed.has_value())
    {
        return std::unexpected{NativeAudioSetupError{
            NativeAudioSetupErrorCode::CalibrationFailed, committed.error().message
        }};
    }

    m_machine.calibrationCommitted();
    return {};
}

NativeAudioSetupError NativeAudioSetup::failAndRecord(NativeAudioSetupError error)
{
    m_machine.fail(error);
    return error;
}

} // namespace rock_hero::game::core
