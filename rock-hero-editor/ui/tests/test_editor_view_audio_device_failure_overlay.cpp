#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>

namespace rock_hero::editor::ui
{

// The failure overlay follows EditorViewState::audio_device_failure_prompt directly: it appears
// while a prompt is staged, live-updates its text, and retracts when the prompt clears. The
// overlay is identified by its componentID so the test does not depend on the concrete type.
TEST_CASE(
    "EditorView shows the audio-device failure overlay while a prompt is staged",
    "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;

    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    const juce::Component* const overlay = view.findChildWithID("audio_device_failure_overlay");
    REQUIRE(overlay != nullptr);
    CHECK_FALSE(overlay->isVisible());

    core::EditorViewState state;
    state.visible_timeline = common::core::TimeRange{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{},
    };
    state.arrangement = makeArrangementState(std::filesystem::path{});
    state.signal_chain = core::SignalChainViewState{};
    state.audio_device_failure_prompt = core::AudioDeviceFailurePrompt{
        .message = "Driver failed to initialize",
    };
    view.setState(state);

    CHECK(overlay->isVisible());
    CHECK(
        findRequiredDescendant<juce::TextButton>(view, "audio_device_failure_retry_button")
            .isVisible());
    CHECK(
        findRequiredDescendant<juce::TextButton>(view, "audio_device_failure_open_settings_button")
            .isVisible());

    // A staged prompt clears when a device opens; the overlay retracts.
    state.audio_device_failure_prompt.reset();
    view.setState(state);

    CHECK_FALSE(overlay->isVisible());
}

// The Retry and Open Audio Settings buttons emit the matching controller decision.
TEST_CASE(
    "EditorView audio-device failure overlay buttons report their decisions", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;

    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    core::EditorViewState state;
    state.visible_timeline = common::core::TimeRange{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{},
    };
    state.arrangement = makeArrangementState(std::filesystem::path{});
    state.signal_chain = core::SignalChainViewState{};
    state.audio_device_failure_prompt = core::AudioDeviceFailurePrompt{
        .message = "Disconnected",
    };
    view.setState(state);

    auto& retry_button =
        findRequiredDescendant<juce::TextButton>(view, "audio_device_failure_retry_button");
    REQUIRE(retry_button.onClick);
    retry_button.onClick();
    CHECK(
        controller.last_audio_device_failure_decision ==
        std::optional{core::AudioDeviceFailureDecision::Retry});

    auto& open_settings_button =
        findRequiredDescendant<juce::TextButton>(view, "audio_device_failure_open_settings_button");
    REQUIRE(open_settings_button.onClick);
    open_settings_button.onClick();
    CHECK(
        controller.last_audio_device_failure_decision ==
        std::optional{core::AudioDeviceFailureDecision::OpenSettings});
    CHECK(controller.audio_device_failure_decision_count == 2);
}

} // namespace rock_hero::editor::ui
