#include <optional>
#include <rock_hero/common/audio/testing/in_memory_audio_config_store.h>
#include <rock_hero/common/audio/testing/input_device_identity_fixtures.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>

namespace rock_hero::editor::core
{

namespace
{

// Marks the fake device open, as the engine reports a running hardware device.
void openDevice(ConfigurableAudioDeviceConfiguration& audio_devices)
{
    audio_devices.current_status.open = true;
    audio_devices.current_status.backend_name = "ASIO";
    audio_devices.current_status.device_name = "Interface";
    audio_devices.current_status.unavailable_reason.clear();
}

// Drops the fake device the way the engine reports a disconnect, and tells the listeners.
void loseDevice(ConfigurableAudioDeviceConfiguration& audio_devices)
{
    audio_devices.current_status.open = false;
    audio_devices.current_status.unavailable_reason = "Disconnected";
    audio_devices.current_input_identity.reset();
    audio_devices.notifyChanged();
}

} // namespace

// A device lost mid-session raises the notice with the engine's reason; Close dismisses it and
// leaves the settings window closed.
TEST_CASE("Losing the audio device raises the device-lost notice", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    openDevice(audio_devices);
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        controllerServices(nullEditorSettings(), store),
        noopExitFunction(),
    };
    controller.attachView(view);
    const auto* const running_state = stateOrNull(view.last_state);
    REQUIRE(running_state != nullptr);
    CHECK_FALSE(running_state->audio_device_lost_prompt.has_value());

    loseDevice(audio_devices);

    const auto* const lost_state = stateOrNull(view.last_state);
    REQUIRE(lost_state != nullptr);
    CHECK(
        lost_state->audio_device_lost_prompt ==
        std::optional{AudioDeviceLostPrompt{.reason = "Disconnected"}});

    controller.onAudioDeviceLostDecision(AudioDeviceLostDecision::Close);
    const auto* const closed_state = stateOrNull(view.last_state);
    REQUIRE(closed_state != nullptr);
    CHECK_FALSE(closed_state->audio_device_lost_prompt.has_value());
    CHECK_FALSE(closed_state->audio_device_settings_open);
}

// The notice's Audio Settings button opens the settings window.
TEST_CASE("Device-lost notice opens the audio settings", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    openDevice(audio_devices);
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        controllerServices(nullEditorSettings(), store),
        noopExitFunction(),
    };
    controller.attachView(view);
    loseDevice(audio_devices);

    controller.onAudioDeviceLostDecision(AudioDeviceLostDecision::OpenSettings);

    const auto* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->audio_device_lost_prompt.has_value());
    CHECK(state->audio_device_settings_open);
}

// Only a running device can be lost: a change while no device runs, or one the settings window
// makes while the user is choosing a device, raises nothing.
TEST_CASE("Device-lost notice needs a running device outside settings", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        controllerServices(nullEditorSettings(), store),
        noopExitFunction(),
    };
    controller.attachView(view);

    loseDevice(audio_devices);
    const auto* const never_running_state = stateOrNull(view.last_state);
    REQUIRE(never_running_state != nullptr);
    CHECK_FALSE(never_running_state->audio_device_lost_prompt.has_value());

    openDevice(audio_devices);
    audio_devices.notifyChanged();
    controller.onAudioDeviceSettingsOpenRequested();
    loseDevice(audio_devices);
    const auto* const settings_state = stateOrNull(view.last_state);
    REQUIRE(settings_state != nullptr);
    CHECK_FALSE(settings_state->audio_device_lost_prompt.has_value());
}

// A saved device that does not open at startup raises the notice with the backend's reason.
TEST_CASE("Unavailable saved device raises the device-lost notice", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.restore_serialized_device_state_outcome =
        common::audio::DeviceRestoreOutcome::DeviceUnavailable;
    audio_devices.current_status.unavailable_reason = "Device in use by another application.";
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        controllerServices(nullEditorSettings(), store),
        noopExitFunction(),
    };
    controller.attachView(view);

    const auto* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(
        state->audio_device_lost_prompt ==
        std::optional{AudioDeviceLostPrompt{.reason = "Device in use by another application."}});
    CHECK_FALSE(state->audio_device_settings_open);
}

// Losing the device takes its input route, so the calibration prompt open for that route closes
// and the notice takes its place.
TEST_CASE("Device-lost notice replaces the calibration prompt", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    openDevice(audio_devices);
    audio_devices.current_input_identity = common::audio::testing::makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
    };
    controller.attachView(view);
    const auto* const offered_state = stateOrNull(view.last_state);
    REQUIRE(offered_state != nullptr);
    REQUIRE(offered_state->input_calibration_prompt.has_value());

    loseDevice(audio_devices);

    const auto* const lost_state = stateOrNull(view.last_state);
    REQUIRE(lost_state != nullptr);
    CHECK_FALSE(lost_state->input_calibration_prompt.has_value());
    CHECK(lost_state->audio_device_lost_prompt.has_value());
    CHECK(lost_state->audio_device_settings_enabled);
}

} // namespace rock_hero::editor::core
