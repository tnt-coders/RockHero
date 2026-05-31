#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rock_hero/editor/core/audio_device_settings_controller.h>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Fake settings backend that exposes simple mutable state through the public settings port.
class FakeAudioDeviceSettings final : public common::audio::IAudioDeviceSettings
{
public:
    [[nodiscard]] common::audio::AudioDeviceSettingsState state() const override
    {
        return current_state;
    }

    void selectAudioSystem(int choice_id) override
    {
        selected_audio_system_id = choice_id;
        current_state.selected_audio_system_id = choice_id;
    }

    void selectDevice(int choice_id) override
    {
        selected_device_id = choice_id;
        current_state.selected_device_id = choice_id;
    }

    void selectInputDevice(int choice_id) override
    {
        selected_input_device_id = choice_id;
        current_state.selected_input_device_id = choice_id;
    }

    void selectOutputDevice(int choice_id) override
    {
        selected_output_device_id = choice_id;
        current_state.selected_output_device_id = choice_id;
    }

    void selectInputChannel(int choice_id) override
    {
        selected_input_channel_id = choice_id;
        current_state.selected_input_channel_id = choice_id;
    }

    void selectStereoOutputPair(int choice_id) override
    {
        selected_stereo_output_pair_id = choice_id;
        current_state.selected_stereo_output_pair_id = choice_id;
    }

    void selectSampleRate(int choice_id) override
    {
        selected_sample_rate_id = choice_id;
        current_state.selected_sample_rate_id = choice_id;
    }

    void selectBufferSize(int choice_id) override
    {
        selected_buffer_size_id = choice_id;
        current_state.selected_buffer_size_id = choice_id;
    }

    [[nodiscard]] std::expected<void, common::audio::AudioDeviceSettingsError> apply() override
    {
        ++apply_call_count;
        if (next_apply_error.has_value())
        {
            current_state.error_message = next_apply_error->message;
            return std::unexpected{*next_apply_error};
        }

        return {};
    }

    [[nodiscard]] std::expected<void, common::audio::AudioDeviceSettingsError> cancel() override
    {
        ++cancel_call_count;
        if (next_cancel_error.has_value())
        {
            current_state.error_message = next_cancel_error->message;
            return std::unexpected{*next_cancel_error};
        }

        return {};
    }

    [[nodiscard]] std::expected<void, common::audio::AudioDeviceSettingsError> openControlPanel()
        override
    {
        ++control_panel_call_count;
        return {};
    }

    void addListener(Listener& listener) override
    {
        listeners.push_back(&listener);
    }

    void removeListener(Listener& listener) override
    {
        std::erase(listeners, &listener);
    }

    void notifyChanged()
    {
        for (auto* listener : listeners)
        {
            listener->onAudioDeviceSettingsChanged();
        }
    }

    common::audio::AudioDeviceSettingsState current_state{
        .audio_systems = {"ASIO"},
        .selected_audio_system_id = 1,
        .uses_separate_input_output_devices = true,
        .devices = {},
        .input_devices = {"Input A", "Input B"},
        .selected_input_device_id = 1,
        .output_devices = {"Output A", "Output B"},
        .selected_output_device_id = 1,
        .input_channels = {"Input 1", "Input 2"},
        .selected_input_channel_id = 1,
        .stereo_output_pairs = {common::audio::StereoOutputPair{
            .left_channel = 0,
            .right_channel = 1,
            .label = "Output 1 + Output 2",
        }},
        .selected_stereo_output_pair_id = 1,
        .sample_rates = {44100.0, 48000.0},
        .selected_sample_rate_id = 2,
        .buffer_sizes = {128, 256},
        .selected_buffer_size_id = 1,
        .control_panel_enabled = true,
        .error_message = {},
    };
    std::optional<common::audio::AudioDeviceSettingsError> next_apply_error{};
    std::optional<common::audio::AudioDeviceSettingsError> next_cancel_error{};
    std::vector<Listener*> listeners{};
    int cancel_call_count{};
    int apply_call_count{};
    int control_panel_call_count{};
    int selected_audio_system_id{};
    int selected_device_id{};
    int selected_input_device_id{};
    int selected_output_device_id{};
    int selected_input_channel_id{};
    int selected_stereo_output_pair_id{};
    int selected_sample_rate_id{};
    int selected_buffer_size_id{};
};

// Captures state pushes and close requests from the controller under test.
class FakeAudioDeviceSettingsView final : public IAudioDeviceSettingsView
{
public:
    void setState(const AudioDeviceSettingsViewState& state) override
    {
        last_state = state;
        ++set_state_call_count;
    }

    void requestClose() override
    {
        ++close_call_count;
    }

    void setApplying(bool applying) override
    {
        last_applying = applying;
        applying_transitions.push_back(applying);
    }

    AudioDeviceSettingsViewState last_state{};
    int set_state_call_count{};
    int close_call_count{};
    bool last_applying{false};
    std::vector<bool> applying_transitions{};
};

} // namespace

// Attaching a view pushes editor-specific state derived from the shared settings state.
TEST_CASE("AudioDeviceSettingsController maps settings state", "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    AudioDeviceSettingsController controller{settings};
    FakeAudioDeviceSettingsView view;

    controller.attachView(view);

    REQUIRE(view.set_state_call_count == 1);
    CHECK(view.last_state.audio_systems[0].label == "ASIO");
    CHECK(view.last_state.output_devices[1].label == "Output B");
    CHECK(view.last_state.sample_rates[1].label == "48000 Hz");
    CHECK(view.last_state.buffer_sizes[0].label == "128 samples");
    CHECK(view.last_state.ok_enabled);
}

// Selection intents are forwarded as stable choice IDs and followed by a fresh state push.
TEST_CASE("AudioDeviceSettingsController forwards selected IDs", "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    AudioDeviceSettingsController controller{settings};
    FakeAudioDeviceSettingsView view;
    controller.attachView(view);

    controller.onOutputDeviceSelected(2);
    controller.onSampleRateSelected(1);

    CHECK(settings.selected_output_device_id == 2);
    CHECK(settings.selected_sample_rate_id == 1);
    CHECK(view.last_state.selected_output_device_id == 2);
    CHECK(view.last_state.selected_sample_rate_id == 1);
}

// Successful OK applies the settings and closes the view.
TEST_CASE(
    "AudioDeviceSettingsController closes after successful OK", "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    AudioDeviceSettingsController controller{settings};
    FakeAudioDeviceSettingsView view;
    controller.attachView(view);

    controller.onOkRequested();

    CHECK(settings.apply_call_count == 1);
    CHECK(view.close_call_count == 1);
}

// Failed OK leaves the window open and renders the backend error from public settings state.
TEST_CASE("AudioDeviceSettingsController keeps failed OK open", "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    settings.next_apply_error = common::audio::AudioDeviceSettingsError{
        common::audio::AudioDeviceSettingsErrorCode::ApplyFailed,
        "Could not open Output B",
    };
    AudioDeviceSettingsController controller{settings};
    FakeAudioDeviceSettingsView view;
    controller.attachView(view);

    controller.onOkRequested();

    CHECK(settings.apply_call_count == 1);
    CHECK(view.close_call_count == 0);
    CHECK(view.last_state.error_message == "Could not open Output B");
}

// Cancel abandons the staged edit and closes the view.
TEST_CASE("AudioDeviceSettingsController cancels and closes", "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    AudioDeviceSettingsController controller{settings};
    FakeAudioDeviceSettingsView view;
    controller.attachView(view);

    controller.onCancelRequested();

    CHECK(settings.cancel_call_count == 1);
    CHECK(view.close_call_count == 1);
}

// Failed Cancel keeps the settings window open and renders the route-restore diagnostic.
TEST_CASE("AudioDeviceSettingsController keeps failed cancel open", "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    settings.next_cancel_error = common::audio::AudioDeviceSettingsError{
        common::audio::AudioDeviceSettingsErrorCode::RestoreFailed,
        "Could not restore Output A",
    };
    AudioDeviceSettingsController controller{settings};
    FakeAudioDeviceSettingsView view;
    controller.attachView(view);

    controller.onCancelRequested();

    CHECK(settings.cancel_call_count == 1);
    CHECK(view.close_call_count == 0);
    CHECK(view.last_state.error_message == "Could not restore Output A");
}

// Native window close destroys the controller without a Cancel intent, so the destructor cancels.
TEST_CASE("AudioDeviceSettingsController cancels on native close", "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    {
        AudioDeviceSettingsController controller{settings};
        FakeAudioDeviceSettingsView view;
        controller.attachView(view);
    }

    CHECK(settings.cancel_call_count == 1);
}

// Backend notifications re-derive and push view state without a user intent.
TEST_CASE(
    "AudioDeviceSettingsController refreshes on backend notification",
    "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    AudioDeviceSettingsController controller{settings};
    FakeAudioDeviceSettingsView view;
    controller.attachView(view);
    settings.current_state.error_message = "Device changed";

    settings.notifyChanged();

    CHECK(view.last_state.error_message == "Device changed");
    CHECK(view.set_state_call_count == 2);
}

// A staged edit without an audio system selection cannot apply, so OK must be disabled.
TEST_CASE(
    "AudioDeviceSettingsController disables OK without system", "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    settings.current_state.selected_audio_system_id = 0;
    AudioDeviceSettingsController controller{settings};
    FakeAudioDeviceSettingsView view;
    controller.attachView(view);

    CHECK_FALSE(view.last_state.ok_enabled);
}

// Split-device backends require both input and output selections before OK can apply.
TEST_CASE(
    "AudioDeviceSettingsController disables OK without split pair", "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    settings.current_state.selected_output_device_id = 0;
    AudioDeviceSettingsController controller{settings};
    FakeAudioDeviceSettingsView view;
    controller.attachView(view);

    CHECK_FALSE(view.last_state.ok_enabled);
}

// Combined-device backends require a single device selection before OK can apply.
TEST_CASE(
    "AudioDeviceSettingsController disables OK without device", "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    settings.current_state.uses_separate_input_output_devices = false;
    settings.current_state.selected_device_id = 0;
    AudioDeviceSettingsController controller{settings};
    FakeAudioDeviceSettingsView view;
    controller.attachView(view);

    CHECK_FALSE(view.last_state.ok_enabled);
}

// With a dispatcher supplied, OK marks the view applying first and defers apply through the
// dispatcher rather than blocking on apply inside onOkRequested().
TEST_CASE(
    "AudioDeviceSettingsController defers OK apply through dispatcher",
    "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    std::function<void()> captured_apply;
    AudioDeviceSettingsController controller{
        settings, [&captured_apply](std::function<void()> operation) {
            captured_apply = std::move(operation);
        }
    };
    FakeAudioDeviceSettingsView view;
    controller.attachView(view);

    controller.onOkRequested();

    CHECK(view.applying_transitions == std::vector<bool>{true});
    CHECK(settings.apply_call_count == 0);
    REQUIRE(captured_apply);

    captured_apply();

    CHECK(settings.apply_call_count == 1);
    CHECK(view.close_call_count == 1);
}

// Async OK failure restores editing and pushes the backend error into view state so the existing
// in-dialog error label can display the diagnostic.
TEST_CASE(
    "AudioDeviceSettingsController restores editing on async OK failure",
    "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    settings.next_apply_error = common::audio::AudioDeviceSettingsError{
        common::audio::AudioDeviceSettingsErrorCode::ApplyFailed,
        "Could not open Output B",
    };
    std::function<void()> captured_apply;
    AudioDeviceSettingsController controller{
        settings, [&captured_apply](std::function<void()> operation) {
            captured_apply = std::move(operation);
        }
    };
    FakeAudioDeviceSettingsView view;
    controller.attachView(view);

    controller.onOkRequested();
    REQUIRE(captured_apply);
    captured_apply();

    CHECK(view.applying_transitions == std::vector<bool>{true, false});
    CHECK(view.close_call_count == 0);
    CHECK(view.last_state.error_message == "Could not open Output B");
}

// With a dispatcher supplied, Cancel marks the view applying first and defers cancel through the
// dispatcher rather than blocking on the reopen path. This is what gives Cancel the same
// dismiss-immediately, busy-overlay feel that OK has.
TEST_CASE(
    "AudioDeviceSettingsController defers cancel through dispatcher",
    "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    std::function<void()> captured_cancel;
    AudioDeviceSettingsController controller{
        settings, [&captured_cancel](std::function<void()> operation) {
            captured_cancel = std::move(operation);
        }
    };
    FakeAudioDeviceSettingsView view;
    controller.attachView(view);

    controller.onCancelRequested();

    CHECK(view.applying_transitions == std::vector<bool>{true});
    CHECK(settings.cancel_call_count == 0);
    REQUIRE(captured_cancel);

    captured_cancel();

    CHECK(settings.cancel_call_count == 1);
    CHECK(view.close_call_count == 1);
}

// If the host closes the settings window before the paint-fenced apply runs, the deferred
// continuation must not touch the destroyed controller or its settings backend.
TEST_CASE(
    "AudioDeviceSettingsController drops deferred OK apply after destruction",
    "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    std::function<void()> captured_apply;
    FakeAudioDeviceSettingsView view;
    {
        AudioDeviceSettingsController controller{
            settings, [&captured_apply](std::function<void()> operation) {
                captured_apply = std::move(operation);
            }
        };
        controller.attachView(view);

        controller.onOkRequested();

        CHECK(view.applying_transitions == std::vector<bool>{true});
        CHECK(settings.apply_call_count == 0);
    }

    REQUIRE(captured_apply);
    captured_apply();

    CHECK(settings.apply_call_count == 0);
    CHECK(settings.cancel_call_count == 1);
}

// Control panel requests are gated by derived state.
TEST_CASE(
    "AudioDeviceSettingsController gates control panel action", "[core][audio-device-settings]")
{
    FakeAudioDeviceSettings settings;
    settings.current_state.control_panel_enabled = false;
    AudioDeviceSettingsController controller{settings};
    FakeAudioDeviceSettingsView view;
    controller.attachView(view);

    controller.onControlPanelRequested();

    CHECK(settings.control_panel_call_count == 0);
}

} // namespace rock_hero::editor::core
