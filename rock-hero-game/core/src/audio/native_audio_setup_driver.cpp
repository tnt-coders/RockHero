#include "audio/native_audio_setup.h"

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace rock_hero::game::core
{

namespace
{

// The setup menu has no song, so it never allows processed monitoring; calibration needs only the
// input route the applied device provides.
constexpr common::audio::LiveInputMonitoringContext g_setup_menu_context{.session_ready = false};

} // namespace

NativeAudioSetup::NativeAudioSetup(
    common::audio::IAudioDeviceSettings& device_settings,
    common::audio::IAudioDeviceConfiguration& device_configuration,
    common::audio::LiveInputMonitor& live_input_monitor, IGameSettings& game_settings)
    : m_device_settings(device_settings)
    , m_device_configuration(device_configuration)
    , m_live_input_monitor(live_input_monitor)
    , m_game_settings(game_settings)
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
            "Cannot apply a device while input calibration is in progress.",
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

    // The apply saved the route; the player maps onto the input route it resolved to.
    const std::optional<common::audio::InputDeviceIdentity> identity =
        m_device_configuration.currentInputDeviceIdentity();
    if (!identity.has_value() || !common::audio::isValidInputDeviceIdentity(*identity))
    {
        return std::unexpected{failAndRecord(
            NativeAudioSetupError{NativeAudioSetupErrorCode::DeviceRouteUnresolved})};
    }

    // The game-private slot-0 player-to-route mapping.
    const GameAudioConfig game_config{
        .players = {PlayerInputConfig{.player_slot = 0, .route = *identity}}
    };
    if (const auto saved = m_game_settings.setGameAudioConfig(game_config); !saved.has_value())
    {
        return std::unexpected{failAndRecord(
            NativeAudioSetupError{
                NativeAudioSetupErrorCode::StorePersistFailed, saved.error().message
            })};
    }

    m_machine.deviceApplied();
    return {};
}

std::expected<void, NativeAudioSetupError> NativeAudioSetup::beginGainCalibration()
{
    if (!m_machine.canCalibrate())
    {
        return std::unexpected{NativeAudioSetupError{
            NativeAudioSetupErrorCode::InvalidRequest,
            "Input calibration is only available after a device is applied.",
        }};
    }

    if (const auto began = m_live_input_monitor.beginMeasurement(g_setup_menu_context);
        !began.has_value())
    {
        return std::unexpected{NativeAudioSetupError{
            NativeAudioSetupErrorCode::CalibrationFailed, began.error().message
        }};
    }
    return {};
}

std::expected<common::audio::InputCalibrationProgress, NativeAudioSetupError> NativeAudioSetup::
    sampleGainCalibration()
{
    std::optional<common::audio::InputCalibrationProgress> progress =
        m_machine.canCalibrate() ? m_live_input_monitor.sample(g_setup_menu_context).measurement
                                 : std::nullopt;
    if (!progress.has_value())
    {
        return std::unexpected{NativeAudioSetupError{
            NativeAudioSetupErrorCode::InvalidRequest,
            "No input calibration measurement is in progress.",
        }};
    }

    if (std::holds_alternative<common::audio::InputCalibrationCommitted>(*progress))
    {
        m_machine.calibrationCommitted();
    }
    return std::move(*progress);
}

void NativeAudioSetup::cancelGainCalibration()
{
    if (m_machine.canCalibrate())
    {
        m_live_input_monitor.cancelMeasurement(g_setup_menu_context);
    }
}

NativeAudioSetupError NativeAudioSetup::failAndRecord(NativeAudioSetupError error)
{
    m_machine.fail(error);
    return error;
}

} // namespace rock_hero::game::core
