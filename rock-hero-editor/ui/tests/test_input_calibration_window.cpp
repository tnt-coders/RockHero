#include "input_calibration/input_calibration_window.h"
#include "shared/audio_level_meter.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/common/audio/input/pickup_types.h>
#include <rock_hero/common/audio/testing/input_device_identity_fixtures.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <rock_hero/editor/core/testing/recording_editor_controller.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>

namespace rock_hero::editor::ui
{

namespace
{

using core::testing::RecordingEditorController;
using testing::findRequiredDescendant;

[[nodiscard]] core::InputCalibrationPrompt calibrationPrompt()
{
    return core::InputCalibrationPrompt{
        .route = common::audio::testing::makeInputDeviceIdentity(),
        .stored_gain_db = -6.0,
    };
}

} // namespace

// Measure starts a measurement and becomes Stop, the gain, the pickups and Apply waiting while it
// runs; Stop ends it. The window keeps its size throughout, so its native window never resizes.
TEST_CASE("InputCalibrationWindow measures at one size", "[ui][input-calibration]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingEditorController controller;
    const core::InputCalibrationPrompt prompt = calibrationPrompt();

    InputCalibrationWindow window{controller, prompt, nullptr};

    const juce::Component* const content = window.getContentComponent();
    REQUIRE(content != nullptr);
    const juce::Rectangle<int> size = content->getLocalBounds();
    auto& measure =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_measure_button");
    const auto& apply =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_apply_button");
    const auto& slider =
        findRequiredDescendant<juce::Slider>(window, "input_calibration_manual_gain");
    const auto& pickups =
        findRequiredDescendant<juce::ComboBox>(window, "input_calibration_pickup");

    REQUIRE(measure.onClick);
    measure.onClick();
    CHECK(content->getLocalBounds() == size);
    CHECK(measure.getButtonText() == "Stop");
    CHECK_FALSE(apply.isEnabled());
    CHECK_FALSE(slider.isEnabled());
    CHECK_FALSE(pickups.isEnabled());

    measure.onClick();
    CHECK(controller.input_calibration_stop_count == 1);
    CHECK(content->getLocalBounds() == size);
    CHECK(measure.getButtonText() == "Measure");
    CHECK(apply.isEnabled());
}

// The meter marks where a hard strum on the chosen pickups lands at the right gain, and the mark
// follows the pickup type; a running measurement hides it, so nobody plays to it.
TEST_CASE("InputCalibrationWindow marks the pickups' strum target", "[ui][input-calibration]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingEditorController controller;
    const core::InputCalibrationPrompt prompt = calibrationPrompt();

    InputCalibrationWindow window{controller, prompt, nullptr};

    const auto& meter = findRequiredDescendant<AudioLevelMeter>(window, "input_calibration_meter");
    auto& pickups = findRequiredDescendant<juce::ComboBox>(window, "input_calibration_pickup");
    auto& measure =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_measure_button");

    CHECK(
        meter.targetDb() == std::optional{common::audio::inputCalibrationTargetPeakDb(
                                common::audio::PickupClass::Humbucker)});

    pickups.setSelectedItemIndex(1, juce::sendNotificationSync);
    CHECK(
        meter.targetDb() == std::optional{common::audio::inputCalibrationTargetPeakDb(
                                common::audio::PickupClass::SingleCoil)});

    REQUIRE(measure.onClick);
    measure.onClick();
    CHECK_FALSE(meter.targetDb().has_value());
}

} // namespace rock_hero::editor::ui
