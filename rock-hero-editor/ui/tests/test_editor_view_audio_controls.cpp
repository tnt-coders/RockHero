#include <algorithm>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <rock_hero/common/audio/input/known_interfaces.h>
#include <rock_hero/common/audio/testing/input_device_identity_fixtures.h>
#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>
#include <string_view>

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
    showOnScreen(view);

    core::EditorViewState state;
    state.input_calibration_prompt = core::InputCalibrationPrompt{
        .route = common::audio::testing::makeInputDeviceIdentity(),
        .stored_gain_db = 2.0,
    };
    view.setState(state);

    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    REQUIRE(window.getContentComponent() != nullptr);
    auto& help_button =
        findRequiredDescendant<juce::DrawableButton>(window, "input_calibration_help_button");
    auto& status = findRequiredDescendant<juce::Label>(window, "input_calibration_status");
    auto& meter = findRequiredDescendant<juce::Component>(window, "input_calibration_meter");
    auto& manual_label =
        findRequiredDescendant<juce::Label>(window, "input_calibration_manual_label");
    auto& slider = findRequiredDescendant<juce::Slider>(window, "input_calibration_manual_gain");
    auto& disclosure =
        findRequiredDescendant<juce::Button>(window, "input_calibration_measure_disclosure");
    auto& cancel_button =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_cancel_button");
    auto& master_meter = findRequiredDescendant<AudioLevelMeter>(view, "master_output_meter");

    // The reference reaches the player only as the formula behind the gain slider.
    CHECK(findDescendant(window, "input_calibration_target") == nullptr);
    CHECK(slider.getTooltip() == "Gain = your audio device's dBu at 0 dBFS, minus 12.");
    CHECK(
        status.getText() ==
        "Calibrated. Choose an audio device or change the gain to recalibrate.");
    CHECK(status.isVisible());
    // A message, not a field: the status draws no box.
    CHECK_FALSE(status.isColourSpecified(juce::Label::backgroundColourId));
    CHECK_THAT(status.getMinimumHorizontalScale(), Catch::Matchers::WithinULP(1.0f, 0));
    CHECK_FALSE(status.getText().startsWith("Info:"));
    REQUIRE(help_button.onClick);
    CHECK(help_button.getTooltip() == "Open the known audio devices table");
    CHECK(manual_label.getText() == "Gain");
    // The popup meter keeps the master meter's preferred 384px width. The live master meter can
    // flex narrower than that, because the window-centered playback transport has layout
    // priority over the meter's preferred width.
    CHECK(meter.getWidth() == 384);
    CHECK(master_meter.getWidth() <= meter.getWidth());
    CHECK(window.getContentComponent()->getWidth() < 520);
    CHECK(findDescendant(window, "input_calibration_gain") == nullptr);
    CHECK(findDescendant(window, "input_calibration_recommendation") == nullptr);
    CHECK(findDescendant(window, "input_calibration_docs_link") == nullptr);
    // Top to bottom: the chooser with the guide at its end, the gain, the status, the meter, the
    // closed measuring header, then the dismiss button at the trailing edge.
    auto& chooser = findRequiredDescendant<juce::ComboBox>(window, "input_calibration_interface");
    CHECK(chooser.getBounds().getRight() <= help_button.getBounds().getX());
    CHECK(chooser.getBounds().getBottom() <= slider.getBounds().getY());
    CHECK(manual_label.getBounds().getY() == slider.getBounds().getY());
    CHECK(manual_label.getBounds().getRight() <= slider.getBounds().getX());
    CHECK(slider.getBounds().getBottom() <= status.getBounds().getY());
    CHECK(status.getBounds().getBottom() <= meter.getBounds().getY());
    CHECK(meter.getBounds().getBottom() <= disclosure.getBounds().getY());
    CHECK(disclosure.getBounds().getX() == manual_label.getBounds().getX());
    CHECK(disclosure.getBounds().getBottom() <= cancel_button.getBounds().getY());
    CHECK(cancel_button.getBounds().getX() > window.getContentComponent()->getWidth() / 2);
    CHECK(window.getContentComponent()->getHeight() < 260);
}

// Verifies calibration gain controls do not expose negative zero after one-decimal rounding.
TEST_CASE("Calibration gain control hides negative rounded zero", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    showOnScreen(view);

    core::EditorViewState state;
    state.input_calibration_prompt = core::InputCalibrationPrompt{
        .route = common::audio::testing::makeInputDeviceIdentity(),
        .stored_gain_db = -0.04,
    };
    view.setState(state);

    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    auto& slider = findRequiredDescendant<juce::Slider>(window, "input_calibration_manual_gain");

    // Gain sliders snap to a 0.1 dB interval, and the snap arithmetic (start + interval * steps)
    // contracts to a fused multiply-add on arm64, landing grid points a few ULP off the nominal
    // value (CI macs deliver 1.3e-15 where x64 delivers exactly 0.0). Slider values therefore
    // get a tolerance far below the grid step instead of exact comparison; the 1-decimal display
    // rounding these tests also assert is unaffected either way.
    CHECK_THAT(slider.getValue(), Catch::Matchers::WithinAbs(0.0, 1e-9));
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
    showOnScreen(view);

    core::EditorViewState state;
    state.input_calibration_prompt = core::InputCalibrationPrompt{
        .route = common::audio::testing::makeInputDeviceIdentity(),
        .stored_gain_db = 2.0,
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
    // Tolerance instead of exact equality: arm64 FMA contraction in the interval-snap math (see
    // the rounded-zero test above).
    REQUIRE(controller.last_input_calibration_gain_db.has_value());
    if (controller.last_input_calibration_gain_db.has_value())
    {
        CHECK_THAT(
            *controller.last_input_calibration_gain_db, Catch::Matchers::WithinAbs(3.5, 1e-9));
    }
    CHECK(slider.isEnabled());
    CHECK(apply_button.isEnabled());
    CHECK(status.getText() == "Saved: +3.5 dB.");
    CHECK(slider.getTextFromValue(slider.getValue()) == "+3.5 dB");
}

// The chooser lists the known interfaces and opens on its placeholder; choosing one emits the
// row's index, and the window shows the chosen row and the controller's sentence for it.
TEST_CASE("Calibration chooser selects a known interface", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    showOnScreen(view);

    core::EditorViewState state;
    state.input_calibration_prompt = core::InputCalibrationPrompt{
        .route = common::audio::testing::makeInputDeviceIdentity(),
        .stored_gain_db = std::nullopt,
    };
    view.setState(state);

    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    auto& chooser = findRequiredDescendant<juce::ComboBox>(window, "input_calibration_interface");
    auto& slider = findRequiredDescendant<juce::Slider>(window, "input_calibration_manual_gain");
    auto& status = findRequiredDescendant<juce::Label>(window, "input_calibration_status");
    const auto rows = common::audio::knownInterfaces();

    CHECK(chooser.getNumItems() == static_cast<int>(rows.size()));
    CHECK(chooser.getSelectedId() == 0);
    CHECK(chooser.getTextWhenNothingSelected() == "Choose your audio device");

    const auto quad_cortex = std::ranges::find(
        rows, std::string_view{"Neural DSP Quad Cortex"}, &common::audio::KnownInterface::model);
    REQUIRE(quad_cortex != rows.end());
    const int quad_cortex_id = static_cast<int>(quad_cortex - rows.begin()) + 1;
    chooser.setSelectedId(quad_cortex_id, juce::sendNotificationSync);

    CHECK(chooser.getSelectedId() == quad_cortex_id);
    CHECK(slider.getTextFromValue(slider.getValue()) == "+2.3 dB");
    CHECK(
        status.getText() ==
        "Estimated figure. Set the audio device to the instrument input, 1 MOhm, "
        "at 0.0 dB input level, then click Apply.");
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
    // Tolerance instead of exact equality: arm64 FMA contraction in the interval-snap math (see
    // the rounded-zero test above).
    REQUIRE(controller.last_output_gain_db.has_value());
    if (controller.last_output_gain_db.has_value())
    {
        CHECK_THAT(*controller.last_output_gain_db, Catch::Matchers::WithinAbs(-6.0, 1e-9));
    }
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
    // Tolerance instead of exact equality: arm64 FMA contraction in the interval-snap math (see
    // the rounded-zero test above).
    REQUIRE(controller.last_output_gain_preview_db.has_value());
    if (controller.last_output_gain_preview_db.has_value())
    {
        CHECK_THAT(*controller.last_output_gain_preview_db, Catch::Matchers::WithinAbs(-6.0, 1e-9));
    }

    output_slider.onDragEnd();

    CHECK(controller.output_gain_preview_change_count == 2);
    CHECK(controller.output_gain_change_count == 1);
    REQUIRE(controller.last_output_gain_db.has_value());
    if (controller.last_output_gain_db.has_value())
    {
        CHECK_THAT(*controller.last_output_gain_db, Catch::Matchers::WithinAbs(-6.0, 1e-9));
    }
}

// A prompt for another route retires the open window, so the new route gets a fresh popup seeded
// from its own gain; the replacement is presented once the old window is gone.
TEST_CASE("Calibration prompt for another route retires the open window", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    view.setBounds(0, 0, 1280, 800);
    showOnScreen(view);

    core::EditorViewState state;
    state.input_calibration_prompt = core::InputCalibrationPrompt{
        .route = common::audio::testing::makeInputDeviceIdentity(),
        .stored_gain_db = 2.0,
    };
    view.setState(state);
    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    REQUIRE(window.isVisible());

    state.input_calibration_prompt = core::InputCalibrationPrompt{
        .route = common::audio::testing::makeInputDeviceIdentity("ASIO", "Interface B"),
        .stored_gain_db = std::nullopt,
    };
    view.setState(state);

    CHECK_FALSE(window.isVisible());
}

// The audio settings button only asks the controller; the window opens from state, and only once
// the view is on screen to own it.
TEST_CASE("Audio settings window opens from state once on screen", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    view.setBounds(0, 0, 1280, 800);

    auto& audio_button = findRequiredDescendant<MenuBarButton>(view, "audio_device_button");
    REQUIRE(audio_button.onClick);
    audio_button.onClick();
    CHECK(controller.audio_device_settings_open_count == 1);

    core::EditorViewState state;
    state.audio_device_settings_open = true;
    view.setState(state);
    CHECK_THROWS(
        findRequiredTopLevelComponent<juce::DocumentWindow>("audio_device_settings_window"));

    showOnScreen(view);
    CHECK(
        findRequiredTopLevelComponent<juce::DocumentWindow>("audio_device_settings_window")
            .isVisible());
}

// Behind the measuring header, the pickup chooser lists the five kinds and opens on humbuckers;
// "Start Calibration" then measures the chosen pickups.
TEST_CASE("Calibration pickup chooser reaches the measurement", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    showOnScreen(view);

    core::EditorViewState state;
    state.input_calibration_prompt = core::InputCalibrationPrompt{
        .route = common::audio::testing::makeInputDeviceIdentity(),
        .stored_gain_db = std::nullopt,
    };
    view.setState(state);

    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    auto& disclosure =
        findRequiredDescendant<juce::Button>(window, "input_calibration_measure_disclosure");
    auto& chooser = findRequiredDescendant<juce::ComboBox>(window, "input_calibration_pickup");
    auto& start_button =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_start_button");

    REQUIRE(disclosure.onClick);
    disclosure.onClick();
    CHECK(start_button.isShowing());
    CHECK(chooser.getNumItems() == 5);
    CHECK(chooser.getText() == "Humbucker");

    chooser.setSelectedItemIndex(1, juce::sendNotificationSync);
    CHECK(chooser.getText() == "Single-coil");

    REQUIRE(start_button.onClick);
    start_button.onClick();
    CHECK(
        controller.last_input_calibration_pickups ==
        std::optional{common::audio::PickupClass::SingleCoil});
}

} // namespace rock_hero::editor::ui
