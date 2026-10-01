#include "controller/editor_controller_impl.h"
#include "shared/editor_controller_logging.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

// Wraps the supplied audio-device open work in the editor's busy overlay paint fence so the
// blocking presentation paints once before juce::AudioDeviceManager occupies the message thread.
// The settings dialog launcher provides work already aware of dialog success/failure handling, so
// this method owns only the busy lifecycle.
void EditorController::Impl::onAudioDeviceChangeRequested(
    std::function<void()> change_audio_device, std::function<void()> after_busy_cleared)
{
    if (!change_audio_device)
    {
        if (after_busy_cleared)
        {
            after_busy_cleared();
        }
        return;
    }

    m_busy.runMessageThreadBusyOperation(
        BusyOperation::OpeningAudioDevice,
        std::move(change_audio_device),
        std::move(after_busy_cleared));
}

// Re-runs the live-input gate and re-derives view state after a configuration change, a
// mid-session disconnect included: the status text and Play's availability follow the device. A
// device lost outside the settings window raises the notice; inside it, the user is already
// choosing one.
void EditorController::Impl::onAudioDeviceConfigurationChanged()
{
    const common::audio::AudioDeviceStatus status = m_audio_devices.currentDeviceStatus();
    if (m_audio_device_open && !status.open && !m_audio_device_settings_open)
    {
        openAudioDeviceLostPrompt(status);
    }
    m_audio_device_open = status.open;
    refreshLiveInput();
    updateView();
}

// Raises the notice that the user's audio device is not running, carrying the reason observed
// with the closure it reports.
void EditorController::Impl::openAudioDeviceLostPrompt(
    const common::audio::AudioDeviceStatus& status)
{
    m_audio_device_lost_prompt = AudioDeviceLostPrompt{.reason = status.unavailable_reason};
}

// Closes the device-lost notice; its Audio Settings button opens settings as the menu button does.
void EditorController::Impl::onAudioDeviceLostDecision(AudioDeviceLostDecision decision)
{
    m_audio_device_lost_prompt.reset();
    if (decision == AudioDeviceLostDecision::OpenSettings)
    {
        onAudioDeviceSettingsOpenRequested();
        return;
    }
    updateView();
}

// Opens the audio settings window on user request. Refused while the calibration prompt is up so
// the two modal flows never overlap.
void EditorController::Impl::onAudioDeviceSettingsOpenRequested()
{
    if (!inputCalibrationProjection().audio_device_settings_enabled)
    {
        return;
    }

    openAudioDeviceSettings();
    updateView();
}

// Marks the audio settings window open, which the view presents from state. The window holds the
// route so its transitions commit as one change: playback pauses and the live-input gate re-runs.
void EditorController::Impl::openAudioDeviceSettings()
{
    if (m_transport.state().playing)
    {
        m_transport.pause();
    }

    m_audio_device_settings_open = true;
    refreshLiveInput();
}

// Re-applies the route gate after settings closes or restores its previous route.
void EditorController::Impl::onAudioDeviceSettingsClosed()
{
    m_audio_device_settings_open = false;
    refreshLiveInput();
    updateView();
}

// Applies the active device route stored by a previous session of either product. Returns the
// restore's outcome, or nullopt when no usable route is saved: none at all, or one the backend
// rejects, which is cleared as corrupt.
std::optional<common::audio::DeviceRestoreOutcome> EditorController::Impl::restoreAudioDeviceState()
{
    const std::optional<std::string> route = m_audio_config_store.activeDeviceRoute();
    if (!route.has_value())
    {
        return std::nullopt;
    }

    const auto restored = m_audio_devices.restoreSerializedDeviceState(*route);
    if (!restored.has_value())
    {
        logEditorControllerBestEffortFailure(
            "restore serialized audio-device state", restored.error().message);
        recordAudioConfigResultBestEffort(
            m_audio_config_store.setActiveDeviceRoute(std::nullopt),
            "clear invalid serialized audio-device state");
        return std::nullopt;
    }

    if (*restored == common::audio::DeviceRestoreOutcome::DeviceUnavailable)
    {
        // A designed outcome, not a failure: the saved device is absent or in use, so the route was
        // applied closed and the saved choice retained. The status text surfaces the recorded
        // reason; logged so a headless run is explainable too.
        logEditorControllerBestEffortFailure(
            "open saved audio device",
            "saved device unavailable; the audio device stays closed and the saved choice is kept");
    }
    return *restored;
}

} // namespace rock_hero::editor::core
