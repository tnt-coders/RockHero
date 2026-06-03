#include <cstddef>
#include <optional>
#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

constexpr const char* g_plugin_drag_prefix{"rockhero.signal-chain.plugin:"};

// Builds one plugin tile for signal-chain UI intent tests.
[[nodiscard]] core::PluginViewState makePlugin(std::string instance_id, std::size_t chain_index)
{
    return core::PluginViewState{
        .instance_id = std::move(instance_id),
        .plugin_id = "plugin-" + std::to_string(chain_index),
        .name = "Plugin " + std::to_string(chain_index),
        .manufacturer = "Tests",
        .format_name = "VST3",
        .chain_index = chain_index,
    };
}

// Builds a contiguous chain for capacity and overflow tests.
[[nodiscard]] std::vector<core::PluginViewState> makePlugins(std::size_t count)
{
    std::vector<core::PluginViewState> plugins;
    plugins.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        plugins.push_back(makePlugin("block-" + std::to_string(index), index));
    }
    return plugins;
}

// Minimal panel listener used by direct SignalChainPanel layout tests.
class RecordingSignalChainPanelListener final : public SignalChainPanel::Listener
{
public:
    void onInsertPluginPressed(std::size_t chain_index) override
    {
        last_insert_index = chain_index;
        insert_call_count += 1;
    }

    void onRemovePluginPressed(std::string /*instance_id*/) override
    {}

    void onMovePluginPressed(std::string instance_id, std::size_t destination_index) override
    {
        last_moved_instance_id = std::move(instance_id);
        last_move_destination_index = destination_index;
        move_call_count += 1;
    }

    void onOpenPluginPressed(std::string /*instance_id*/) override
    {}

    void onInputCalibrationPressed() override
    {}

    void onOutputGainChanged(double /*gain_db*/) override
    {}

    std::optional<std::size_t> last_insert_index{};
    std::optional<std::string> last_moved_instance_id{};
    std::optional<std::size_t> last_move_destination_index{};
    int insert_call_count{0};
    int move_call_count{0};
};

// Builds the tile drag payload used by SignalChainPanel's internal JUCE drag targets.
[[nodiscard]] juce::String pluginDragPayload(
    std::size_t source_index, const std::string& instance_id)
{
    juce::String payload{g_plugin_drag_prefix};
    payload += juce::String{std::to_string(source_index)};
    payload += ":";
    payload += juce::String{instance_id};
    return payload;
}

// Drops a plugin-tile payload directly onto a slot's JUCE drag target.
void dropPluginOnSlot(
    juce::Component& slot, juce::Component& source_tile, std::size_t source_index,
    const std::string& instance_id)
{
    auto* const drop_target = dynamic_cast<juce::DragAndDropTarget*>(&slot);
    REQUIRE(drop_target != nullptr);

    juce::DragAndDropTarget::SourceDetails details{
        juce::var{pluginDragPayload(source_index, instance_id)},
        &source_tile,
        juce::Point<int>{4, 4},
    };
    CHECK(drop_target->isInterestedInDragSource(details));
    drop_target->itemDropped(details);
}

} // namespace

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

// Verifies insert controls emit their rendered chain insertion index.
TEST_CASE("Signal-chain insert controls emit indices", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    view.setBounds(0, 0, 1280, 800);
    view.setState(
        core::EditorViewState{
            .signal_chain = core::SignalChainViewState{
                .insert_plugin_enabled = true,
                .plugins = {makePlugin("amp", 0), makePlugin("cab", 1)},
            },
        });

    auto& insert_first = findRequiredDescendant<juce::TextButton>(view, "insert_plugin_button_0");
    auto& insert_middle = findRequiredDescendant<juce::TextButton>(view, "insert_plugin_button_1");
    auto& insert_append = findRequiredDescendant<juce::TextButton>(view, "insert_plugin_button_2");

    testing::clickButton(insert_first);
    testing::clickButton(insert_middle);
    testing::clickButton(insert_append);

    CHECK(controller.insert_plugin_request_count == 3);
    CHECK(controller.last_insert_plugin_index == std::optional<std::size_t>{2});
}

// Verifies the Quad Cortex-style path caps add controls at eight visible blocks.
TEST_CASE("Signal-chain insert controls stop at eight blocks", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    view.setBounds(0, 0, 1280, 800);
    view.setState(
        core::EditorViewState{
            .signal_chain = core::SignalChainViewState{
                .insert_plugin_enabled = true,
                .plugins = makePlugins(8),
            },
        });

    auto& insert_first = findRequiredDescendant<juce::TextButton>(view, "insert_plugin_button_0");
    auto& insert_append = findRequiredDescendant<juce::TextButton>(view, "insert_plugin_button_8");

    CHECK_FALSE(insert_first.isEnabled());
    CHECK_FALSE(insert_append.isEnabled());
    testing::clickButton(insert_append);

    CHECK(controller.insert_plugin_request_count == 0);
}

// Verifies tile drag/drop emits final destinations and rejects adjacent no-op slots.
TEST_CASE("Signal-chain drag drops move tiles to insertion slots", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainPanelListener listener;
    SignalChainPanel panel{listener};
    panel.setBounds(0, 0, 720, 220);
    panel.setState(
        core::SignalChainViewState{
            .move_plugins_enabled = true,
            .plugins = {
                makePlugin("amp", 0),
                makePlugin("drive", 1),
                makePlugin("cab", 2),
            },
        });

    auto& tile_amp = findRequiredDescendant<juce::Component>(panel, "plugin_tile_amp");
    auto& tile_drive = findRequiredDescendant<juce::Component>(panel, "plugin_tile_drive");
    auto& tile_cab = findRequiredDescendant<juce::Component>(panel, "plugin_tile_cab");
    auto& slot_first = findRequiredDescendant<juce::Component>(panel, "insert_slot_0");
    auto& slot_after_amp = findRequiredDescendant<juce::Component>(panel, "insert_slot_1");
    auto& slot_after_drive = findRequiredDescendant<juce::Component>(panel, "insert_slot_2");
    auto& slot_append = findRequiredDescendant<juce::Component>(panel, "insert_slot_3");

    dropPluginOnSlot(slot_first, tile_cab, 2, "cab");
    CHECK(listener.move_call_count == 1);
    CHECK(listener.last_moved_instance_id == std::optional<std::string>{"cab"});
    CHECK(listener.last_move_destination_index == std::optional<std::size_t>{0});

    dropPluginOnSlot(slot_after_drive, tile_amp, 0, "amp");
    CHECK(listener.move_call_count == 2);
    CHECK(listener.last_moved_instance_id == std::optional<std::string>{"amp"});
    CHECK(listener.last_move_destination_index == std::optional<std::size_t>{1});

    dropPluginOnSlot(slot_append, tile_amp, 0, "amp");
    CHECK(listener.move_call_count == 3);
    CHECK(listener.last_moved_instance_id == std::optional<std::string>{"amp"});
    CHECK(listener.last_move_destination_index == std::optional<std::size_t>{2});

    auto* const no_op_drop_target = dynamic_cast<juce::DragAndDropTarget*>(&slot_after_amp);
    REQUIRE(no_op_drop_target != nullptr);
    const juce::DragAndDropTarget::SourceDetails no_op_details{
        juce::var{pluginDragPayload(1, "drive")},
        &tile_drive,
        juce::Point<int>{4, 4},
    };

    CHECK_FALSE(no_op_drop_target->isInterestedInDragSource(no_op_details));
    no_op_drop_target->itemDropped(no_op_details);
    CHECK(listener.move_call_count == 3);
}

// Verifies drop targets are quiet while move editing is disabled.
TEST_CASE("Signal-chain drag drops respect move gate", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainPanelListener listener;
    SignalChainPanel panel{listener};
    panel.setBounds(0, 0, 720, 220);
    panel.setState(
        core::SignalChainViewState{
            .plugins = {
                makePlugin("amp", 0),
                makePlugin("cab", 1),
            },
        });

    auto& tile_cab = findRequiredDescendant<juce::Component>(panel, "plugin_tile_cab");
    auto& slot_first = findRequiredDescendant<juce::Component>(panel, "insert_slot_0");
    auto* const drop_target = dynamic_cast<juce::DragAndDropTarget*>(&slot_first);
    REQUIRE(drop_target != nullptr);

    juce::DragAndDropTarget::SourceDetails details{
        juce::var{pluginDragPayload(1, "cab")},
        &tile_cab,
        juce::Point<int>{4, 4},
    };

    CHECK_FALSE(drop_target->isInterestedInDragSource(details));
    drop_target->itemDropped(details);

    CHECK(listener.move_call_count == 0);
}

// Verifies disabled edit controls do not emit insert, move, or remove intents.
TEST_CASE("Signal-chain disabled edit controls stay quiet", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    view.setBounds(0, 0, 1280, 800);
    view.setState(
        core::EditorViewState{
            .signal_chain = core::SignalChainViewState{
                .plugins = {makePlugin("amp", 0), makePlugin("cab", 1)},
            },
        });

    auto& insert_first = findRequiredDescendant<juce::TextButton>(view, "insert_plugin_button_0");
    auto& remove_amp = findRequiredDescendant<juce::TextButton>(view, "remove_plugin_button_amp");

    CHECK(remove_amp.getButtonText() == "X");
    CHECK(remove_amp.getWidth() == 20);
    CHECK(remove_amp.getHeight() == 20);
    CHECK_FALSE(insert_first.isEnabled());
    CHECK_FALSE(remove_amp.isEnabled());

    testing::clickButton(insert_first);
    testing::clickButton(remove_amp);

    CHECK(controller.insert_plugin_request_count == 0);
    CHECK(controller.remove_plugin_request_count == 0);
}

// Verifies cramped panels keep every tile reachable through the horizontal signal-chain viewport.
TEST_CASE("Signal-chain cramped panel scrolls overflowing tiles", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainPanelListener listener;
    SignalChainPanel panel{listener};
    // The center strip width is roughly panel_width - 176 (two side columns plus insets). 320px
    // leaves ~144px of strip, narrower than the append slot and the full content width, so the
    // rail/tile strip must scroll horizontally to stay reachable.
    panel.setBounds(0, 0, 320, 160);
    panel.setState(
        core::SignalChainViewState{
            .insert_plugin_enabled = true,
            .plugins = {makePlugin("amp", 0), makePlugin("cab", 1)},
        });

    auto& viewport = findRequiredDescendant<juce::Viewport>(panel, "signal_chain_viewport");
    auto& content = findRequiredDescendant<juce::Component>(panel, "signal_chain_content");
    auto& slot_first = findRequiredDescendant<juce::Component>(panel, "insert_slot_0");
    auto& tile_first = findRequiredDescendant<juce::Component>(panel, "plugin_tile_amp");
    auto& slot_second = findRequiredDescendant<juce::Component>(panel, "insert_slot_1");
    auto& tile_second = findRequiredDescendant<juce::Component>(panel, "plugin_tile_cab");
    auto& append_slot = findRequiredDescendant<juce::Component>(panel, "insert_slot_2");

    CHECK(content.getWidth() > viewport.getWidth());
    CHECK(slot_first.isVisible());
    CHECK(tile_first.isVisible());
    CHECK(slot_second.isVisible());
    CHECK(tile_second.isVisible());
    CHECK(append_slot.isVisible());
    CHECK(append_slot.getX() >= viewport.getWidth());
    CHECK(listener.insert_call_count == 0);
}

// Verifies tile clicks still open plugin windows independently of tile edit buttons.
TEST_CASE("Signal-chain tile click still opens plugin", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    view.setBounds(0, 0, 1280, 800);
    view.setState(
        core::EditorViewState{
            .signal_chain = core::SignalChainViewState{
                .plugins = {makePlugin("amp", 0)},
            },
        });

    auto& tile = findRequiredDescendant<juce::Component>(view, "plugin_tile_amp");
    juce::MouseEvent event = testing::makeMouseDownEvent(tile, 8.0f, 8.0f);
    tile.mouseDown(event);
    tile.mouseUp(event);

    CHECK(controller.open_plugin_request_count == 1);
    CHECK(controller.last_opened_plugin_instance_id == std::optional<std::string>{"amp"});
}

} // namespace rock_hero::editor::ui
