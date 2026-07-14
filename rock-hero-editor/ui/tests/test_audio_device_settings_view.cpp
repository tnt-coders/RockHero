#include "audio_device/audio_device_settings_view.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>

namespace rock_hero::editor::ui
{

namespace
{

using testing::findRequiredDirectChild;

// Captures controller intents emitted by the passive settings view.
class FakeAudioDeviceSettingsController final : public core::IAudioDeviceSettingsController
{
public:
    void onAudioSystemSelected(int choice_id) override
    {
        selected_audio_system_id = choice_id;
    }

    void onDeviceSelected(int choice_id) override
    {
        selected_device_id = choice_id;
    }

    void onInputDeviceSelected(int choice_id) override
    {
        selected_input_device_id = choice_id;
    }

    void onOutputDeviceSelected(int choice_id) override
    {
        selected_output_device_id = choice_id;
    }

    void onInputChannelSelected(int choice_id) override
    {
        selected_input_channel_id = choice_id;
    }

    void onStereoOutputPairSelected(int choice_id) override
    {
        selected_stereo_output_pair_id = choice_id;
    }

    void onSampleRateSelected(int choice_id) override
    {
        selected_sample_rate_id = choice_id;
    }

    void onBufferSizeSelected(int choice_id) override
    {
        selected_buffer_size_id = choice_id;
    }

    void onControlPanelRequested() override
    {
        ++control_panel_call_count;
    }

    void onOkRequested() override
    {
        ++ok_call_count;
    }

    void onCancelRequested() override
    {
        ++cancel_call_count;
    }

    int selected_audio_system_id{};
    int selected_device_id{};
    int selected_input_device_id{};
    int selected_output_device_id{};
    int selected_input_channel_id{};
    int selected_stereo_output_pair_id{};
    int selected_sample_rate_id{};
    int selected_buffer_size_id{};
    int control_panel_call_count{};
    int ok_call_count{};
    int cancel_call_count{};
};

[[nodiscard]] core::AudioDeviceSettingsViewState splitDeviceState()
{
    return core::AudioDeviceSettingsViewState{
        .audio_systems = {{.id = 1, .label = "ASIO"}},
        .selected_audio_system_id = 1,
        .uses_separate_input_output_devices = true,
        .input_devices = {{.id = 1, .label = "Input A"}, {.id = 2, .label = "Input B"}},
        .selected_input_device_id = 1,
        .output_devices = {{.id = 1, .label = "Output A"}, {.id = 2, .label = "Output B"}},
        .selected_output_device_id = 1,
        .input_channels = {{.id = 1, .label = "Input 1"}},
        .selected_input_channel_id = 1,
        .stereo_output_pairs = {{.id = 1, .label = "Output 1 + Output 2"}},
        .selected_stereo_output_pair_id = 1,
        .sample_rates = {{.id = 1, .label = "44100 Hz"}, {.id = 2, .label = "48000 Hz"}},
        .selected_sample_rate_id = 2,
        .buffer_sizes = {{.id = 1, .label = "128 samples"}},
        .selected_buffer_size_id = 1,
        .control_panel_enabled = true,
        .ok_enabled = true,
        .error_message = "Could not open Output B",
    };
}

void clickTextButton(AudioDeviceSettingsView& view, const juce::String& component_id)
{
    auto& button = findRequiredDirectChild<juce::TextButton>(view, component_id);
    REQUIRE(button.onClick);
    button.onClick();
}

} // namespace

// setState renders choices, selected IDs, visibility, enablement, and error text.
TEST_CASE("AudioDeviceSettingsView renders controller state", "[ui][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceSettingsController controller;
    AudioDeviceSettingsView view{controller};

    view.setState(splitDeviceState());

    const auto& input_device =
        findRequiredDirectChild<juce::ComboBox>(view, "audio_settings_input_device");
    const auto& output_device =
        findRequiredDirectChild<juce::ComboBox>(view, "audio_settings_output_device");
    const auto& combined_device =
        findRequiredDirectChild<juce::ComboBox>(view, "audio_settings_device");
    const auto& error_label = findRequiredDirectChild<juce::Label>(view, "audio_settings_error");
    const auto& ok_button =
        findRequiredDirectChild<juce::TextButton>(view, "audio_settings_ok_button");

    CHECK(input_device.isVisible());
    CHECK(output_device.isVisible());
    CHECK_FALSE(combined_device.isVisible());
    CHECK(output_device.getNumItems() == 2);
    CHECK(output_device.getSelectedId() == 1);
    CHECK(error_label.getText() == "Could not open Output B");
    CHECK(ok_button.isEnabled());
}

// ComboBox changes emit stable one-based IDs to the controller.
TEST_CASE("AudioDeviceSettingsView emits selected IDs", "[ui][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceSettingsController controller;
    AudioDeviceSettingsView view{controller};
    view.setState(splitDeviceState());

    auto& output_device =
        findRequiredDirectChild<juce::ComboBox>(view, "audio_settings_output_device");
    auto& sample_rate = findRequiredDirectChild<juce::ComboBox>(view, "audio_settings_sample_rate");

    output_device.setSelectedId(2, juce::sendNotificationSync);
    sample_rate.setSelectedId(1, juce::sendNotificationSync);

    CHECK(controller.selected_output_device_id == 2);
    CHECK(controller.selected_sample_rate_id == 1);
}

// Buttons emit the corresponding settings-controller intents.
TEST_CASE("AudioDeviceSettingsView emits button intents", "[ui][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceSettingsController controller;
    AudioDeviceSettingsView view{controller};
    view.setState(splitDeviceState());

    clickTextButton(view, "audio_settings_control_panel_button");
    clickTextButton(view, "audio_settings_ok_button");
    clickTextButton(view, "audio_settings_cancel_button");

    CHECK(controller.control_panel_call_count == 1);
    CHECK(controller.ok_call_count == 1);
    CHECK(controller.cancel_call_count == 1);
}

// Toggle ON with an available game config renders the device fields read-only with the notice and
// no opt-out.
TEST_CASE(
    "AudioDeviceSettingsView locks fields read-only when sourcing the game",
    "[ui][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceSettingsController controller;
    AudioDeviceSettingsView view{controller};
    view.setState(splitDeviceState());

    view.setGameAudioSettings(
        AudioDeviceSettingsView::GameAudioSettingsState{
            .use_game_settings = true,
            .game_source_available = true,
        });

    const auto& input_device =
        findRequiredDirectChild<juce::ComboBox>(view, "audio_settings_input_device");
    const auto& sample_rate =
        findRequiredDirectChild<juce::ComboBox>(view, "audio_settings_sample_rate");
    const auto& ok_button =
        findRequiredDirectChild<juce::TextButton>(view, "audio_settings_ok_button");
    const auto& toggle =
        findRequiredDirectChild<juce::ToggleButton>(view, "audio_settings_use_game_toggle");
    const auto& notice = findRequiredDirectChild<juce::Label>(view, "audio_settings_game_notice");
    const auto& opt_out =
        findRequiredDirectChild<juce::TextButton>(view, "audio_settings_use_editor_button");

    CHECK(toggle.getToggleState());
    CHECK_FALSE(input_device.isEnabled());
    CHECK_FALSE(sample_rate.isEnabled());
    CHECK_FALSE(ok_button.isEnabled());
    CHECK(notice.isVisible());
    CHECK(notice.getText().contains("Game audio settings"));
    CHECK_FALSE(opt_out.isVisible());
}

// Toggle ON with an unconfigured game surfaces the disabled reason and the one-action opt-out
// instead of an inert switch.
TEST_CASE(
    "AudioDeviceSettingsView surfaces the opt-out when the game is unconfigured",
    "[ui][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceSettingsController controller;
    AudioDeviceSettingsView view{controller};
    view.setState(splitDeviceState());

    view.setGameAudioSettings(
        AudioDeviceSettingsView::GameAudioSettingsState{
            .use_game_settings = true,
            .game_source_available = false,
        });

    const auto& input_device =
        findRequiredDirectChild<juce::ComboBox>(view, "audio_settings_input_device");
    const auto& notice = findRequiredDirectChild<juce::Label>(view, "audio_settings_game_notice");
    auto& opt_out =
        findRequiredDirectChild<juce::TextButton>(view, "audio_settings_use_editor_button");

    CHECK_FALSE(input_device.isEnabled());
    CHECK(notice.isVisible());
    CHECK(opt_out.isVisible());
    CHECK(opt_out.isEnabled());

    bool last_requested_on{true};
    int change_count{0};
    view.setGameAudioSettingsChangedCallback([&](bool enabled) {
        last_requested_on = enabled;
        change_count += 1;
    });

    REQUIRE(opt_out.onClick);
    opt_out.onClick();

    CHECK(change_count == 1);
    CHECK_FALSE(last_requested_on);
    // The opt-out drops locally into the editable device flow before the controller round-trip.
    CHECK(input_device.isEnabled());
    CHECK_FALSE(notice.isVisible());
}

// Toggle OFF keeps the full editable device flow and emits the toggle change to the host.
TEST_CASE(
    "AudioDeviceSettingsView emits the use-game-settings toggle change",
    "[ui][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceSettingsController controller;
    AudioDeviceSettingsView view{controller};
    view.setState(splitDeviceState());
    view.setGameAudioSettings(
        AudioDeviceSettingsView::GameAudioSettingsState{
            .use_game_settings = false,
            .game_source_available = true,
        });

    const auto& input_device =
        findRequiredDirectChild<juce::ComboBox>(view, "audio_settings_input_device");
    CHECK(input_device.isEnabled());

    std::optional<bool> requested;
    view.setGameAudioSettingsChangedCallback([&](bool enabled) { requested = enabled; });

    // Drive the toggle deterministically: set the state, then invoke its handler as a real click
    // would, matching the file's direct-onClick pattern for buttons.
    auto& toggle =
        findRequiredDirectChild<juce::ToggleButton>(view, "audio_settings_use_game_toggle");
    toggle.setToggleState(true, juce::dontSendNotification);
    REQUIRE(toggle.onClick);
    toggle.onClick();

    REQUIRE(requested.has_value());
    CHECK(requested.value());
    CHECK_FALSE(input_device.isEnabled());
}

} // namespace rock_hero::editor::ui
