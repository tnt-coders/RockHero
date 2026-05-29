#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>

namespace rock_hero::editor::ui
{

// Verifies that input calibration and output gain controls exist and are disabled by default.
TEST_CASE("Signal-chain controls present and disabled by default", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    auto& calibrate_button =
        findRequiredDescendant<juce::TextButton>(view, "input_calibrate_button");
    auto& output_slider = findRequiredDescendant<juce::Slider>(view, "output_gain_slider");

    CHECK_FALSE(calibrate_button.isEnabled());
    CHECK_FALSE(output_slider.isEnabled());
    CHECK(output_slider.isDoubleClickReturnEnabled());
    CHECK(output_slider.getTextBoxPosition() == juce::Slider::TextBoxBelow);
    CHECK(output_slider.isTextBoxEditable());
    CHECK(output_slider.getTextBoxWidth() == 72);
    CHECK(output_slider.getTextBoxHeight() == 20);
    CHECK(output_slider.getMinimum() == common::audio::minimumGainDb());
    CHECK(output_slider.getMaximum() == common::audio::maximumGainDb());
    CHECK(output_slider.getDoubleClickReturnValue() == common::audio::defaultGainDb());
}

// Verifies the global and live-rig meter widgets are present in the composed editor view.
TEST_CASE("EditorView creates audio meter components", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    auto& master_meter = findRequiredDescendant<AudioLevelMeter>(view, "master_output_meter");
    auto& input_meter = findRequiredDescendant<AudioLevelMeter>(view, "input_meter");
    auto& output_meter = findRequiredDescendant<AudioLevelMeter>(view, "output_gain_meter");

    CHECK(master_meter.level() == common::audio::AudioMeterLevel{});
    CHECK(input_meter.level() == common::audio::AudioMeterLevel{});
    CHECK(output_meter.level() == common::audio::AudioMeterLevel{});
}

// Verifies the signal-chain meters use the intended input and output control layout.
TEST_CASE("Signal chain meters sit with their controls", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);

    auto& calibrate_button =
        findRequiredDescendant<juce::TextButton>(view, "input_calibrate_button");
    auto& output_slider = findRequiredDescendant<juce::Slider>(view, "output_gain_slider");
    auto& input_meter = findRequiredDescendant<AudioLevelMeter>(view, "input_meter");
    auto& output_meter = findRequiredDescendant<AudioLevelMeter>(view, "output_gain_meter");

    CHECK(input_meter.getBottom() <= calibrate_button.getY());
    CHECK(output_meter.getHeight() == input_meter.getHeight());
    CHECK(output_meter.getY() == input_meter.getY());
    CHECK(output_slider.getBottom() == calibrate_button.getBottom());
    CHECK(output_meter.getX() > output_slider.getX());
    CHECK(output_meter.getRight() <= output_slider.getRight());
    CHECK(output_meter.getX() - output_slider.getX() <= (output_slider.getWidth() / 2) + 4);
}

// Verifies EditorView samples the optional meter port and forwards the values to meter widgets.
TEST_CASE("EditorView samples audio meter source", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    FakeAudioMeterSource meter_source;
    meter_source.snapshot = common::audio::AudioMeterSnapshot{
        .live_rig_input = common::audio::AudioMeterLevel{.peak_db = -18.0},
        .live_rig_output = common::audio::AudioMeterLevel{.peak_db = -2.0, .clipping = true},
        .master_output = common::audio::AudioMeterLevel{.peak_db = -6.0},
    };
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory, meter_source)};

    view.setState(core::EditorViewState{});

    auto& master_meter = findRequiredDescendant<AudioLevelMeter>(view, "master_output_meter");
    auto& input_meter = findRequiredDescendant<AudioLevelMeter>(view, "input_meter");
    auto& output_meter = findRequiredDescendant<AudioLevelMeter>(view, "output_gain_meter");

    CHECK(meter_source.snapshot_read_count >= 1);
    CHECK(master_meter.level() == meter_source.snapshot.master_output);
    CHECK(input_meter.level() == meter_source.snapshot.live_rig_input);
    CHECK(output_meter.level() == meter_source.snapshot.live_rig_output);
}

// Verifies that signal-chain controls follow their independent view-state gates.
TEST_CASE("Signal-chain controls follow view-state gates", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    auto& calibrate_button =
        findRequiredDescendant<juce::TextButton>(view, "input_calibrate_button");
    auto& output_slider = findRequiredDescendant<juce::Slider>(view, "output_gain_slider");

    view.setState(
        core::EditorViewState{
            .signal_chain = core::SignalChainViewState{
                .input_calibrate_enabled = true,
                .output_gain_controls_enabled = true,
                .output_gain_db = -24.0,
            },
        });

    CHECK(calibrate_button.isEnabled());
    CHECK(output_slider.isEnabled());
    CHECK(output_slider.getValue() == -24.0);
}

} // namespace rock_hero::editor::ui
