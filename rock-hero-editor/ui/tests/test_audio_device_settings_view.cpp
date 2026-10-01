#include "audio_device/audio_device_settings_view.h"

#include <catch2/catch_test_macros.hpp>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <string>

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
        .sample_rates = {{.id = 1, .label = "44.1kHz"}, {.id = 2, .label = "48kHz"}},
        .selected_sample_rate_id = 2,
        .buffer_sizes = {{.id = 1, .label = "128 samples"}},
        .selected_buffer_size_id = 1,
        .control_panel_supported = true,
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

// A supported control panel whose device driver failed to initialize renders visible but disabled;
// the error label doubles as the standing unavailable notice once no more-specific operation error
// is active, so a re-open that lands on a disconnected device never finishes silently and the
// disabled button needs no tooltip of its own.
TEST_CASE("AudioDeviceSettingsView presents an unavailable device", "[ui][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceSettingsController controller;
    AudioDeviceSettingsView view{controller};

    auto state = splitDeviceState();
    state.staged_device_error = std::string{"Can't detect asio channels"};
    view.setState(state);

    auto& control_panel =
        findRequiredDirectChild<juce::TextButton>(view, "audio_settings_control_panel_button");
    const auto& error_label = findRequiredDirectChild<juce::Label>(view, "audio_settings_error");

    CHECK(control_panel.isVisible());
    CHECK_FALSE(control_panel.isEnabled());
    // No tooltip: the standing notice in the error label already explains the disable.
    CHECK(control_panel.getTooltip().isEmpty());
    // A transient operation error is the more specific diagnostic and wins the label.
    CHECK(error_label.getText() == "Could not open Output B");

    state.error_message.clear();
    view.setState(state);

    // With no operation error active, the label carries the standing unavailable notice: the
    // backend's own error text, verbatim.
    CHECK(error_label.getText() == "Can't detect asio channels");
}

// The view never second-guesses the controller's OK availability: a pushed ok_enabled of false
// grays OK out, because the controller alone decides whether the staged route can be applied.
TEST_CASE(
    "AudioDeviceSettingsView renders OK availability from the controller",
    "[ui][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceSettingsController controller;
    AudioDeviceSettingsView view{controller};

    auto state = splitDeviceState();
    state.ok_enabled = false;
    view.setState(state);

    const auto& ok_button =
        findRequiredDirectChild<juce::TextButton>(view, "audio_settings_ok_button");
    CHECK_FALSE(ok_button.isEnabled());
}

} // namespace rock_hero::editor::ui
