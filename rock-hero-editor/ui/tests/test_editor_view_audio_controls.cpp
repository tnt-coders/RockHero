#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>

namespace rock_hero::editor::ui
{

// Verifies that pressing the input calibration button emits a controller intent.
TEST_CASE("Input calibration button emits controller intent", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setState(
        core::EditorViewState{
            .signal_chain = core::SignalChainViewState{
                .input_calibrate_enabled = true,
            },
        });

    auto& calibrate_button =
        findRequiredDescendant<juce::TextButton>(view, "input_calibrate_button");
    calibrate_button.onClick();

    CHECK(controller.input_calibration_request_count == 1);
}

// Verifies the calibration popup starts with target, status, and documentation controls.
TEST_CASE("Calibration prompt starts with target and status", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    view.setBounds(0, 0, 1280, 800);

    core::EditorViewState state;
    state.input_calibration_prompt = core::InputCalibrationPrompt{
        .message = "Live input disabled: input calibration required.",
        .input_gain_db = 2.0,
    };
    view.setState(state);

    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    REQUIRE(window.getContentComponent() != nullptr);
    auto& target_label = findRequiredDescendant<juce::Label>(window, "input_calibration_target");
    auto& help_button =
        findRequiredDescendant<juce::DrawableButton>(window, "input_calibration_help_button");
    auto& status = findRequiredDescendant<juce::Label>(window, "input_calibration_status");
    auto& meter = findRequiredDescendant<juce::Component>(window, "input_calibration_meter");
    auto& manual_label =
        findRequiredDescendant<juce::Label>(window, "input_calibration_manual_label");
    auto& slider = findRequiredDescendant<juce::Slider>(window, "input_calibration_manual_gain");
    auto& start_button =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_start_button");
    auto& master_meter = findRequiredDescendant<AudioLevelMeter>(view, "master_output_meter");

    CHECK(target_label.getText() == "Target: -12 dBFS average, -6 dBFS peak");
    CHECK(
        status.getText() ==
        "Click \"Calibrate\" to run automatic calibration, or adjust gain manually and click "
        "\"Apply\".");
    CHECK(status.isVisible());
    CHECK(status.isColourSpecified(juce::Label::backgroundColourId));
    CHECK(status.getMinimumHorizontalScale() == 1.0f);
    CHECK_FALSE(status.getText().startsWith("Info:"));
    REQUIRE(help_button.onClick);
    CHECK(help_button.getTooltip() == "Open input calibration guide");
    CHECK(start_button.getButtonText() == "Calibrate");
    CHECK(manual_label.getText() == "Gain:");
    // The popup meter keeps the master meter's preferred 384px width. The live master meter can
    // flex narrower than that, because the window-centered playback transport has layout
    // priority over the meter's preferred width.
    CHECK(meter.getWidth() == 384);
    CHECK(master_meter.getWidth() <= meter.getWidth());
    CHECK(window.getContentComponent()->getWidth() < 520);
    CHECK(findDescendant(window, "input_calibration_gain") == nullptr);
    CHECK(findDescendant(window, "input_calibration_recommendation") == nullptr);
    CHECK(findDescendant(window, "input_calibration_docs_link") == nullptr);
    CHECK(target_label.getBounds().getRight() <= help_button.getBounds().getX());
    CHECK(help_button.getBounds().getCentreY() == target_label.getBounds().getCentreY());
    CHECK(target_label.getBounds().getBottom() <= status.getBounds().getY());
    CHECK(status.getBounds().getBottom() <= meter.getBounds().getY());
    CHECK(slider.getBounds().getY() >= meter.getBounds().getBottom());
    CHECK(manual_label.getBounds().getY() == slider.getBounds().getY());
    CHECK(manual_label.getBounds().getRight() <= slider.getBounds().getX());
    CHECK(start_button.getBounds().getY() >= slider.getBounds().getBottom());
    CHECK(window.getContentComponent()->getHeight() < 235);
}

// Verifies calibration gain controls do not expose negative zero after one-decimal rounding.
TEST_CASE("Calibration gain control hides negative rounded zero", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    core::EditorViewState state;
    state.input_calibration_prompt = core::InputCalibrationPrompt{
        .message = "Live input disabled: input calibration required.",
        .input_gain_db = -0.04,
    };
    view.setState(state);

    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    auto& slider = findRequiredDescendant<juce::Slider>(window, "input_calibration_manual_gain");

    CHECK(slider.getValue() == Catch::Approx(0.0));
    CHECK_FALSE(slider.getTextFromValue(slider.getValue()).startsWith("-0.0"));
    CHECK(findDescendant(window, "input_calibration_gain") == nullptr);
}

// Verifies manual gain remains adjustable after a manual calibration save.
TEST_CASE("Manual calibration stays editable after saving", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    core::EditorViewState state;
    state.input_calibration_prompt = core::InputCalibrationPrompt{
        .message = "Live input disabled: input calibration required.",
        .input_gain_db = 2.0,
    };
    view.setState(state);

    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    auto& slider = findRequiredDescendant<juce::Slider>(window, "input_calibration_manual_gain");
    auto& apply_button =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_manual_apply_button");
    auto& status = findRequiredDescendant<juce::Label>(window, "input_calibration_status");

    slider.setValue(3.5, juce::sendNotificationSync);
    REQUIRE(apply_button.onClick);
    apply_button.onClick();

    CHECK(controller.input_calibration_manual_set_count == 1);
    CHECK(controller.last_input_calibration_gain_db == std::optional{3.5});
    CHECK(slider.isEnabled());
    CHECK(apply_button.isEnabled());
    CHECK(status.getText() == "Manual calibration saved. Gain set to 3.5 dB.");
}

// Verifies that moving the output gain slider emits a controller intent.
TEST_CASE("Output gain slider emits controller intent", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setState(
        core::EditorViewState{
            .signal_chain = core::SignalChainViewState{
                .output_gain_controls_enabled = true,
            },
        });

    auto& output_slider = findRequiredDescendant<juce::Slider>(view, "output_gain_slider");
    output_slider.setValue(-6.0, juce::sendNotificationSync);

    CHECK(controller.output_gain_change_count == 1);
    CHECK(controller.output_gain_preview_change_count == 0);
    CHECK(controller.last_output_gain_db == std::optional{-6.0});
}

// Verifies that output gain drag changes preview continuously and commits once on release.
TEST_CASE("Output gain drag previews then commits once", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setState(
        core::EditorViewState{
            .signal_chain = core::SignalChainViewState{
                .output_gain_controls_enabled = true,
            },
        });

    auto& output_slider = findRequiredDescendant<juce::Slider>(view, "output_gain_slider");
    REQUIRE(static_cast<bool>(output_slider.onDragStart));
    REQUIRE(static_cast<bool>(output_slider.onDragEnd));

    output_slider.onDragStart();
    output_slider.setValue(-3.0, juce::sendNotificationSync);
    output_slider.setValue(-6.0, juce::sendNotificationSync);

    CHECK(controller.output_gain_preview_change_count == 2);
    CHECK(controller.output_gain_change_count == 0);
    CHECK(controller.last_output_gain_preview_db == std::optional{-6.0});

    output_slider.onDragEnd();

    CHECK(controller.output_gain_preview_change_count == 2);
    CHECK(controller.output_gain_change_count == 1);
    CHECK(controller.last_output_gain_db == std::optional{-6.0});
}

} // namespace rock_hero::editor::ui
