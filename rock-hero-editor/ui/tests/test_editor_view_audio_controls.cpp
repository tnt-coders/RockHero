#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/common/audio/input/pickup_types.h>
#include <rock_hero/common/audio/testing/input_device_identity_fixtures.h>
#include <rock_hero/editor/core/input_calibration/input_calibration_text.h>
#include <rock_hero/editor/core/testing/input_calibration_fixtures.h>
#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>
#include <string>
#include <utility>

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

// The calibration popup is one fixed screen: the message with the guide, the meter, the pickup type
// with Measure, the gain, then Apply and Cancel.
TEST_CASE("Calibration window lays out one screen", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    view.setBounds(0, 0, 1280, 800);
    showOnScreen(view);

    core::EditorViewState state;
    state.input_calibration_prompt = core::testing::makeInputCalibrationPrompt(2.0);
    view.setState(state);

    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    REQUIRE(window.getContentComponent() != nullptr);
    auto& help_button =
        findRequiredDescendant<juce::DrawableButton>(window, "input_calibration_help_button");
    auto& message = findRequiredDescendant<juce::Label>(window, "input_calibration_message");
    auto& meter = findRequiredDescendant<juce::Component>(window, "input_calibration_meter");
    auto& pickups = findRequiredDescendant<juce::ComboBox>(window, "input_calibration_pickup");
    auto& calibrate =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_measure_button");
    auto& slider = findRequiredDescendant<juce::Slider>(window, "input_calibration_manual_gain");
    auto& apply_button =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_apply_button");
    auto& cancel_button =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_cancel_button");
    auto& master_meter = findRequiredDescendant<AudioLevelMeter>(view, "master_output_meter");

    // The reference reaches the player only as the formula behind the gain slider.
    CHECK(slider.getTooltip() == juce::String{common::audio::inputCalibrationGainFormulaText()});
    REQUIRE(help_button.onClick);
    CHECK(help_button.getTooltip() == "Open the input calibration guide");
    CHECK(message.getText() == juce::String{core::idleText()});
    CHECK(calibrate.getButtonText() == "Measure");
    CHECK(apply_button.getButtonText() == "Apply");
    CHECK(cancel_button.getButtonText() == "Cancel");
    // A message, not a field: it draws no box and is never squeezed.
    CHECK_FALSE(message.isColourSpecified(juce::Label::backgroundColourId));
    CHECK_THAT(message.getMinimumHorizontalScale(), Catch::Matchers::WithinULP(1.0f, 0));
    // The popup meter keeps the master meter's preferred 384px width. The live master meter can
    // flex narrower than that, because the window-centered playback transport has layout
    // priority over the meter's preferred width.
    CHECK(meter.getWidth() == 384);
    CHECK(master_meter.getWidth() <= meter.getWidth());
    CHECK(window.getContentComponent()->getWidth() < 520);
    // Top to bottom: the message with the guide at its corner, the meter, the pickup type with
    // Measure, the gain the measurement fills, then Apply before Cancel at the trailing edge.
    CHECK(message.getBounds().getRight() <= help_button.getBounds().getX());
    CHECK(message.getBounds().getBottom() <= meter.getBounds().getY());
    CHECK(meter.getBounds().getBottom() <= pickups.getBounds().getY());
    CHECK(pickups.getBounds().getRight() <= calibrate.getBounds().getX());
    CHECK(calibrate.getBounds().getBottom() <= slider.getBounds().getY());
    CHECK(slider.getBounds().getBottom() <= apply_button.getBounds().getY());
    CHECK(apply_button.getBounds().getRight() <= cancel_button.getBounds().getX());
    CHECK(cancel_button.getBounds().getRight() == meter.getBounds().getRight());
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
    state.input_calibration_prompt = core::testing::makeInputCalibrationPrompt(-0.04);
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
}

// Apply stores the shown gain; the editor then ends the prompt.
TEST_CASE("Calibration Apply sends the shown gain", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    showOnScreen(view);

    core::EditorViewState state;
    state.input_calibration_prompt = core::testing::makeInputCalibrationPrompt(2.0);
    view.setState(state);

    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    auto& slider = findRequiredDescendant<juce::Slider>(window, "input_calibration_manual_gain");
    auto& apply_button =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_apply_button");
    slider.setValue(3.5, juce::sendNotificationSync);
    REQUIRE(apply_button.onClick);
    apply_button.onClick();

    CHECK(controller.input_calibration_apply_count == 1);
    // Tolerance instead of exact equality: arm64 FMA contraction in the interval-snap math (see
    // the rounded-zero test above).
    REQUIRE(controller.last_input_calibration_gain_db.has_value());
    if (controller.last_input_calibration_gain_db.has_value())
    {
        CHECK_THAT(
            *controller.last_input_calibration_gain_db, Catch::Matchers::WithinAbs(3.5, 1e-9));
    }
    CHECK(slider.getTextFromValue(slider.getValue()) == "+3.5 dB");
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
    state.input_calibration_prompt = core::testing::makeInputCalibrationPrompt(2.0);
    view.setState(state);
    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    REQUIRE(window.isVisible());

    state.input_calibration_prompt = core::testing::makeInputCalibrationPrompt(
        std::nullopt, common::audio::testing::makeInputDeviceIdentity("ASIO", "Interface B"));
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

// The pickup chooser lists the five kinds, opens on humbuckers and offers the chosen kind's
// description on hover; "Measure" then measures the chosen pickups.
TEST_CASE("Calibration pickup chooser reaches the measurement", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    showOnScreen(view);

    core::EditorViewState state;
    state.input_calibration_prompt = core::testing::makeInputCalibrationPrompt(std::nullopt);
    view.setState(state);

    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    auto& chooser = findRequiredDescendant<juce::ComboBox>(window, "input_calibration_pickup");
    auto& calibrate =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_measure_button");

    CHECK(static_cast<std::size_t>(chooser.getNumItems()) == common::audio::pickupTypes().size());
    CHECK(chooser.getText() == "Humbucker");
    CHECK(
        chooser.getTooltip() ==
        juce::String{
            std::string{common::audio::pickupType(common::audio::PickupClass::Humbucker).covers}
        });

    chooser.setSelectedItemIndex(
        static_cast<int>(std::to_underlying(common::audio::PickupClass::SingleCoil)),
        juce::sendNotificationSync);
    CHECK(chooser.getText() == "Single-coil");
    CHECK(
        chooser.getTooltip() ==
        juce::String{
            std::string{common::audio::pickupType(common::audio::PickupClass::SingleCoil).covers}
        });

    REQUIRE(calibrate.onClick);
    calibrate.onClick();
    CHECK(
        controller.last_input_calibration_pickups ==
        std::optional{common::audio::PickupClass::SingleCoil});
}

} // namespace rock_hero::editor::ui
