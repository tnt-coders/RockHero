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
    if (!change_audio_device || m_live_input_monitor.promptVisible())
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

// Persists the new device manager state and re-derives view state after a configuration change,
// a mid-session disconnect included: the status text and Play's availability follow the device.
void EditorController::Impl::onAudioDeviceConfigurationChanged()
{
    persistAudioDeviceState();
    static_cast<void>(m_live_input_monitor.refresh(monitoringContext()));
    updateView();
}

// Marks the audio settings window active so route transitions can be committed as one change.
// Refuses while the calibration prompt is up so the two modal flows never overlap.
bool EditorController::Impl::onAudioDeviceSettingsOpenRequested()
{
    if (m_live_input_monitor.promptVisible())
    {
        return false;
    }

    if (m_transport.state().playing)
    {
        m_transport.pause();
    }

    m_live_input_monitor.openAudioDeviceSettings();
    updateView();
    return true;
}

// Re-applies the route gate after settings closes or restores its previous route.
void EditorController::Impl::onAudioDeviceSettingsClosed()
{
    static_cast<void>(m_live_input_monitor.closeAudioDeviceSettings(monitoringContext()));
    updateView();
}

// Applies the active device route stored by a previous editor session, if any. The route's resolved
// identity is not needed to reopen the device; only the opaque blob feeds the device manager.
void EditorController::Impl::restoreAudioDeviceState()
{
    const std::optional<common::audio::ActiveDeviceRoute> route =
        m_audio_config_store.activeDeviceRoute();
    if (!route.has_value() || route->serialized_state.empty())
    {
        return;
    }

    const auto restored = m_audio_devices.restoreSerializedDeviceState(route->serialized_state);
    if (!restored.has_value())
    {
        logEditorControllerBestEffortFailure(
            "restore serialized audio-device state", restored.error().message);
        recordAudioConfigResultBestEffort(
            m_audio_config_store.setActiveDeviceRoute(std::nullopt),
            "clear invalid serialized audio-device state");
        return;
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
}

// Stores the current device blob paired with its resolved input identity so the next launch can
// restore the user's selection and answer availability questions offline.
void EditorController::Impl::persistAudioDeviceState()
{
    std::optional<std::string> serialized_state = m_audio_devices.serializedDeviceState();
    if (serialized_state.has_value() && !serialized_state->empty())
    {
        recordAudioConfigResultBestEffort(
            m_audio_config_store.setActiveDeviceRoute(
                common::audio::ActiveDeviceRoute{
                    .serialized_state = std::move(*serialized_state),
                    .identity = m_audio_devices.currentInputDeviceIdentity(),
                }),
            "persist serialized audio-device state");
    }
    else
    {
        recordAudioConfigResultBestEffort(
            m_audio_config_store.setActiveDeviceRoute(std::nullopt),
            "clear serialized audio-device state");
    }
}

} // namespace rock_hero::editor::core
