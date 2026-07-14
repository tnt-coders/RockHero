#include "audio/native_audio_setup.h"

#include <utility>

namespace rock_hero::game::core
{

namespace
{

// Default detail text for each native audio-setup failure code, used when a caller does not supply
// operation-specific detail.
[[nodiscard]] std::string defaultMessage(NativeAudioSetupErrorCode code)
{
    switch (code)
    {
        case NativeAudioSetupErrorCode::InvalidRequest:
            return "The requested audio-setup operation is not valid in the current phase.";
        case NativeAudioSetupErrorCode::DeviceApplyFailed:
            return "Applying the selected audio device failed.";
        case NativeAudioSetupErrorCode::DeviceRouteUnresolved:
            return "The applied device did not resolve a usable mono input route.";
        case NativeAudioSetupErrorCode::StorePersistFailed:
            return "Persisting the audio configuration failed.";
        case NativeAudioSetupErrorCode::CalibrationFailed:
            return "Gain calibration failed.";
    }

    return "Native audio setup failed.";
}

} // namespace

NativeAudioSetupError::NativeAudioSetupError(NativeAudioSetupErrorCode error_code)
    : code(error_code)
    , message(defaultMessage(error_code))
{}

NativeAudioSetupError::NativeAudioSetupError(
    NativeAudioSetupErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

NativeAudioSetupPhase NativeAudioSetupMachine::phase() const noexcept
{
    return m_phase;
}

const std::optional<NativeAudioSetupError>& NativeAudioSetupMachine::failure() const noexcept
{
    return m_failure;
}

bool NativeAudioSetupMachine::canApplyDevice() const noexcept
{
    return m_phase != NativeAudioSetupPhase::CalibratingGain;
}

bool NativeAudioSetupMachine::canCalibrate() const noexcept
{
    return m_phase == NativeAudioSetupPhase::CalibratingGain;
}

void NativeAudioSetupMachine::beginDeviceSelection() noexcept
{
    m_failure.reset();
    m_phase = NativeAudioSetupPhase::SelectingDevice;
}

void NativeAudioSetupMachine::deviceApplied() noexcept
{
    m_phase = NativeAudioSetupPhase::CalibratingGain;
}

void NativeAudioSetupMachine::calibrationCommitted() noexcept
{
    m_phase = NativeAudioSetupPhase::Ready;
}

void NativeAudioSetupMachine::fail(NativeAudioSetupError error) noexcept
{
    m_failure = std::move(error);
    m_phase = NativeAudioSetupPhase::Failed;
}

} // namespace rock_hero::game::core
