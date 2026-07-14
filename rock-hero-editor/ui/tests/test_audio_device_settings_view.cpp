#include "audio_device/audio_device_settings_view.h"

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <optional>
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

    void onCommitRequested() override
    {
        ++commit_call_count;
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
    int commit_call_count{};
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

// A supported control panel whose device driver failed to initialize renders visible but disabled,
// with a tooltip explaining the unavailability instead of an enabled button that silently no-ops.
// The error label doubles as the standing unavailable notice once no more-specific operation error
// is active, so a re-open that lands on a disconnected device never finishes silently.
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
    CHECK(
        control_panel.getTooltip() ==
        "The selected audio device is unavailable: Can't detect asio channels");
    // A transient operation error is the more specific diagnostic and wins the label.
    CHECK(error_label.getText() == "Could not open Output B");

    state.error_message.clear();
    view.setState(state);

    // With no operation error active, the label carries the standing unavailable notice, composed
    // from the base text plus the backend's own detail.
    CHECK(
        error_label.getText() ==
        "The selected audio device is unavailable: Can't detect asio channels");

    // A backend that supplied no usable detail presents the base message alone.
    state.staged_device_error = std::string{};
    view.setState(state);
    CHECK(error_label.getText() == "The selected audio device is unavailable");
}

// Toggle ON with an available game config renders the device fields read-only, disabled, and
// tagged with the derived-from-game tooltip.
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

    auto& input_device =
        findRequiredDirectChild<juce::ComboBox>(view, "audio_settings_input_device");
    auto& sample_rate = findRequiredDirectChild<juce::ComboBox>(view, "audio_settings_sample_rate");
    auto& ok_button = findRequiredDirectChild<juce::TextButton>(view, "audio_settings_ok_button");
    auto& control_panel =
        findRequiredDirectChild<juce::TextButton>(view, "audio_settings_control_panel_button");
    const auto& toggle =
        findRequiredDirectChild<juce::ToggleButton>(view, "audio_settings_use_game_toggle");

    CHECK(toggle.getToggleState());
    CHECK_FALSE(input_device.isEnabled());
    CHECK_FALSE(sample_rate.isEnabled());
    // The locked fields carry the derived-from-game tooltip explaining why they cannot be edited.
    CHECK(input_device.getTooltip() == "Derived from game settings");
    CHECK(sample_rate.getTooltip() == "Derived from game settings");
    // The control panel button stays enabled while the game source is active: it opens the audio
    // driver's own external window, independent of Rock Hero's route lock, so it is not one of the
    // grayed controls and carries no derived-from-game tooltip.
    CHECK(control_panel.isVisible());
    CHECK(control_panel.isEnabled());
    CHECK(control_panel.getTooltip().isEmpty());
    // OK is not grayed out while locked: with the game source active it keeps the live route, so it
    // stays enabled and routes to the commit intent (not apply, not cancel/restore).
    CHECK(ok_button.isEnabled());

    REQUIRE(ok_button.onClick);
    ok_button.onClick();
    CHECK(controller.ok_call_count == 0);
    CHECK(controller.cancel_call_count == 0);
    CHECK(controller.commit_call_count == 1);
}

// The row is a checkbox-only toggle plus a separate non-interactive caption label, so only the box
// square flips the setting; clicks on the caption text do not toggle it.
TEST_CASE(
    "AudioDeviceSettingsView splits the toggle box from its caption label",
    "[ui][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceSettingsController controller;
    AudioDeviceSettingsView view{controller};
    view.setState(splitDeviceState());

    const auto& toggle =
        findRequiredDirectChild<juce::ToggleButton>(view, "audio_settings_use_game_toggle");
    const auto& label = findRequiredDirectChild<juce::Label>(view, "audio_settings_use_game_label");

    // The toggle carries no text of its own; the caption is the separate label.
    CHECK(toggle.getButtonText().isEmpty());
    CHECK(label.getText() == "Use game audio settings");
    // The label must not intercept clicks, so pressing the text never flips the toggle.
    bool clicks_this{true};
    bool clicks_children{true};
    label.getInterceptsMouseClicks(clicks_this, clicks_children);
    CHECK_FALSE(clicks_this);
}

// Cancel restores the toggle to its checked open-time value after the user unchecks it, and
// re-fires the change callback so the host reopens the original source.
TEST_CASE(
    "AudioDeviceSettingsView restores the checked toggle on cancel", "[ui][audio-device-settings]")
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

    std::optional<bool> requested;
    view.setGameAudioSettingsChangedCallback(
        [&](bool enabled, std::function<void(bool)>) { requested = enabled; });

    auto& toggle =
        findRequiredDirectChild<juce::ToggleButton>(view, "audio_settings_use_game_toggle");
    // User unchecks the toggle (drops to the editor-own flow).
    toggle.setToggleState(false, juce::dontSendNotification);
    REQUIRE(toggle.onClick);
    toggle.onClick();
    REQUIRE(requested.has_value());
    CHECK_FALSE(requested.value());

    // Cancel restores the open-time checked toggle first, then routes the cancel intent.
    clickTextButton(view, "audio_settings_cancel_button");

    CHECK(controller.cancel_call_count == 1);
    CHECK(toggle.getToggleState());
    REQUIRE(requested.has_value());
    CHECK(requested.value());
}

// Cancel restores the toggle to its unchecked open-time value after the user checks it, landing
// back on the exact pre-window state in the other direction.
TEST_CASE(
    "AudioDeviceSettingsView restores the unchecked toggle on cancel",
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

    std::optional<bool> requested;
    view.setGameAudioSettingsChangedCallback(
        [&](bool enabled, std::function<void(bool)>) { requested = enabled; });

    auto& toggle =
        findRequiredDirectChild<juce::ToggleButton>(view, "audio_settings_use_game_toggle");
    // User checks the toggle (adopts the game source live).
    toggle.setToggleState(true, juce::dontSendNotification);
    REQUIRE(toggle.onClick);
    toggle.onClick();
    REQUIRE(requested.has_value());
    CHECK(requested.value());

    // Cancel restores the open-time unchecked toggle and re-fires the callback with the off value.
    clickTextButton(view, "audio_settings_cancel_button");

    CHECK(controller.cancel_call_count == 1);
    CHECK_FALSE(toggle.getToggleState());
    REQUIRE(requested.has_value());
    CHECK_FALSE(requested.value());
}

// Cancel without a toggle change restores nothing and does not fire the change callback, so a plain
// device edit followed by Cancel routes only the cancel intent.
TEST_CASE(
    "AudioDeviceSettingsView cancel leaves an untouched toggle alone",
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

    std::optional<bool> requested;
    view.setGameAudioSettingsChangedCallback(
        [&](bool enabled, std::function<void(bool)>) { requested = enabled; });

    clickTextButton(view, "audio_settings_cancel_button");

    CHECK(controller.cancel_call_count == 1);
    CHECK_FALSE(requested.has_value());
}

// Toggle ON with an unconfigured game still locks the fields; unchecking the toggle is the one way
// back to the editor's own audio and re-enables the fields and clears the tooltip.
TEST_CASE(
    "AudioDeviceSettingsView locks fields with an unconfigured game", "[ui][audio-device-settings]")
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

    auto& input_device =
        findRequiredDirectChild<juce::ComboBox>(view, "audio_settings_input_device");
    auto& toggle =
        findRequiredDirectChild<juce::ToggleButton>(view, "audio_settings_use_game_toggle");

    CHECK_FALSE(input_device.isEnabled());
    CHECK(input_device.getTooltip() == "Derived from game settings");
    // The toggle stays usable so the user can uncheck it -- the only opt-out, no separate button.
    CHECK(toggle.isEnabled());

    std::optional<bool> requested;
    view.setGameAudioSettingsChangedCallback(
        [&](bool enabled, std::function<void(bool)>) { requested = enabled; });

    // Unchecking the toggle drops locally into the editable device flow before the controller
    // round-trip and asks the host to restore the editor's own audio.
    toggle.setToggleState(false, juce::dontSendNotification);
    REQUIRE(toggle.onClick);
    toggle.onClick();

    REQUIRE(requested.has_value());
    CHECK_FALSE(requested.value());
    CHECK(input_device.isEnabled());
    CHECK(input_device.getTooltip().isEmpty());
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
    view.setGameAudioSettingsChangedCallback(
        [&](bool enabled, std::function<void(bool)>) { requested = enabled; });

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
