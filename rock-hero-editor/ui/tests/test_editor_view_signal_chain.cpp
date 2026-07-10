#include <compare>
#include <cstddef>
#include <optional>
#include <rock_hero/common/audio/plugin/plugin_chain_limits.h>
#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

constexpr const char* g_plugin_drag_prefix{"rockhero.signal-chain.plugin:"};

[[nodiscard]] juce::String insertPluginButtonId(std::size_t index)
{
    return juce::String{"insert_plugin_button_"} + juce::String{std::to_string(index)};
}

[[nodiscard]] juce::String insertSlotId(std::size_t index)
{
    return juce::String{"insert_slot_"} + juce::String{std::to_string(index)};
}

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

// Builds one plugin tile with the visual block assigned by editor core.
[[nodiscard]] core::PluginViewState makePluginAtBlock(
    std::string instance_id, std::size_t chain_index, std::size_t block_index)
{
    core::PluginViewState plugin = makePlugin(std::move(instance_id), chain_index);
    plugin.block_index = block_index;
    return plugin;
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

// Minimal signal-chain view listener used by direct panel layout tests.
class RecordingSignalChainViewListener final : public SignalChainView::Listener
{
public:
    void onInsertPluginPressed(std::size_t chain_index, std::size_t block_index) override
    {
        last_insert_index = chain_index;
        last_insert_block = block_index;
        insert_call_count += 1;
    }

    void onRemovePluginPressed(std::string /*instance_id*/) override
    {}

    void onMovePluginPressed(
        std::string instance_id, std::size_t destination_index,
        std::vector<core::PluginBlockAssignment> placement) override
    {
        last_moved_instance_id = std::move(instance_id);
        last_move_destination_index = destination_index;
        last_move_placement = std::move(placement);
        move_call_count += 1;
    }

    void onSignalChainPlacementChanged(std::vector<core::PluginBlockAssignment> placement) override
    {
        last_signal_chain_placement = std::move(placement);
        placement_change_count += 1;
    }

    void onPluginDisplayTypeOverrideChanged(
        std::string /*instance_id*/,
        std::optional<core::PluginDisplayType> /*display_type*/) override
    {}

    void onOpenPluginPressed(std::string /*instance_id*/) override
    {}

    void onInputCalibrationPressed() override
    {}

    void onOutputGainPreviewChanged(double /*gain_db*/) override
    {}

    void onOutputGainChanged(double /*gain_db*/) override
    {}

    std::optional<std::size_t> last_insert_index{};
    std::optional<std::size_t> last_insert_block{};
    std::optional<std::string> last_moved_instance_id{};
    std::optional<std::size_t> last_move_destination_index{};
    std::vector<core::PluginBlockAssignment> last_move_placement{};
    std::vector<core::PluginBlockAssignment> last_signal_chain_placement{};
    int insert_call_count{0};
    int move_call_count{0};
    int placement_change_count{0};
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

// Drops a plugin-tile payload directly onto a component's JUCE drag target.
void dropPluginOnTarget(
    juce::Component& target, juce::Component& source_tile, std::size_t source_index,
    const std::string& instance_id, juce::Point<int> local_position)
{
    auto* const drop_target = dynamic_cast<juce::DragAndDropTarget*>(&target);
    REQUIRE(drop_target != nullptr);

    const juce::DragAndDropTarget::SourceDetails details{
        juce::var{pluginDragPayload(source_index, instance_id)},
        &source_tile,
        local_position,
    };
    CHECK(drop_target->isInterestedInDragSource(details));
    drop_target->itemDropped(details);
}

// Returns the final bounds a component is moving toward during animated preview tests.
[[nodiscard]] juce::Rectangle<int> componentTargetBounds(juce::Component& component)
{
    return juce::Desktop::getInstance().getAnimator().getComponentDestination(&component);
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
    CHECK(std::is_eq(output_slider.getMinimum() <=> common::audio::minimumGainDb()));
    CHECK(std::is_eq(output_slider.getMaximum() <=> common::audio::maximumGainDb()));
    CHECK(std::is_eq(output_slider.getDoubleClickReturnValue() <=> common::audio::defaultGainDb()));
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
    CHECK(std::is_eq(output_slider.getValue() <=> -24.0));
}

// Verifies signal-chain buttons cannot steal focus from editor-level keyboard shortcuts.
TEST_CASE("Signal-chain action buttons do not take keyboard focus", "[ui][editor-view]")
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
                .remove_plugins_enabled = true,
                .plugins = {makePlugin("amp", 0), makePlugin("cab", 1)},
                .input_calibrate_enabled = true,
            },
        });

    auto& calibrate_button =
        findRequiredDescendant<juce::TextButton>(view, "input_calibrate_button");
    auto& insert_append = findRequiredDescendant<juce::TextButton>(view, "insert_plugin_button_2");
    auto& remove_amp = findRequiredDescendant<juce::TextButton>(view, "remove_plugin_button_amp");

    CHECK_FALSE(calibrate_button.getWantsKeyboardFocus());
    CHECK_FALSE(calibrate_button.getMouseClickGrabsKeyboardFocus());
    CHECK_FALSE(insert_append.getWantsKeyboardFocus());
    CHECK_FALSE(insert_append.getMouseClickGrabsKeyboardFocus());
    CHECK_FALSE(remove_amp.getWantsKeyboardFocus());
    CHECK_FALSE(remove_amp.getMouseClickGrabsKeyboardFocus());
}

// Verifies fixed block placeholders emit the next legal append insertion index.
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
    auto& insert_append = findRequiredDescendant<juce::TextButton>(view, "insert_plugin_button_2");
    auto& insert_later = findRequiredDescendant<juce::TextButton>(
        view, insertPluginButtonId(common::audio::g_max_signal_chain_plugins - 1));

    CHECK_FALSE(insert_first.isEnabled());
    CHECK(insert_append.isEnabled());
    CHECK(insert_later.isEnabled());
    testing::clickButton(insert_first);
    testing::clickButton(insert_append);
    testing::clickButton(insert_later);

    CHECK(controller.plugin_insert_slot_selection_count == 2);
    CHECK(controller.last_plugin_insert_slot == std::optional<std::size_t>{2});
}

// Verifies an empty chain still presents the configured fixed block locations.
TEST_CASE("Signal-chain empty chain exposes fixed placeholders", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainViewListener listener;
    SignalChainPanel panel{listener};
    panel.setBounds(0, 0, 720, 220);
    panel.setState(
        core::SignalChainViewState{
            .insert_plugin_enabled = true,
        });

    for (std::size_t index = 0; index < common::audio::g_max_signal_chain_plugins; ++index)
    {
        auto& slot = findRequiredDescendant<juce::Component>(panel, insertSlotId(index));
        auto& button = findRequiredDescendant<juce::TextButton>(panel, insertPluginButtonId(index));

        CHECK(slot.isVisible());
        CHECK(button.isVisible());
        CHECK(button.isEnabled());
    }
    CHECK(
        findDescendant(panel, insertPluginButtonId(common::audio::g_max_signal_chain_plugins)) ==
        nullptr);

    auto& insert_first = findRequiredDescendant<juce::TextButton>(panel, "insert_plugin_button_0");
    auto& insert_last = findRequiredDescendant<juce::TextButton>(
        panel, insertPluginButtonId(common::audio::g_max_signal_chain_plugins - 1));
    testing::clickButton(insert_first);
    testing::clickButton(insert_last);

    CHECK(listener.insert_call_count == 2);
    CHECK(listener.last_insert_index == std::optional<std::size_t>{0});
    CHECK(
        listener.last_insert_block ==
        std::optional<std::size_t>{common::audio::g_max_signal_chain_plugins - 1});
}

// Verifies inserted plugin state from core can occupy the originally clicked fixed block.
TEST_CASE("Signal-chain inserts keep selected empty block", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainViewListener listener;
    SignalChainPanel panel{listener};
    panel.setBounds(0, 0, 720, 220);
    panel.setState(
        core::SignalChainViewState{
            .insert_plugin_enabled = true,
            .plugins = {
                makePlugin("amp", 0),
                makePlugin("cab", 1),
            },
        });

    auto& selected_slot = findRequiredDescendant<juce::Component>(
        panel, insertSlotId(common::audio::g_max_signal_chain_plugins - 1));
    auto& insert_later = findRequiredDescendant<juce::TextButton>(
        panel, insertPluginButtonId(common::audio::g_max_signal_chain_plugins - 1));
    const int selected_block_center_x = selected_slot.getBounds().getCentreX();
    testing::clickButton(insert_later);
    CHECK(listener.insert_call_count == 1);
    CHECK(listener.last_insert_index == std::optional<std::size_t>{2});
    CHECK(
        listener.last_insert_block ==
        std::optional<std::size_t>{common::audio::g_max_signal_chain_plugins - 1});

    panel.setState(
        core::SignalChainViewState{
            .insert_plugin_enabled = true,
            .plugins = {
                makePluginAtBlock("amp", 0, 0),
                makePluginAtBlock("cab", 1, 1),
                makePluginAtBlock("lead", 2, common::audio::g_max_signal_chain_plugins - 1),
            },
        });

    auto& inserted_tile = findRequiredDescendant<juce::Component>(panel, "plugin_tile_lead");
    auto& gap_insert = findRequiredDescendant<juce::TextButton>(panel, "insert_plugin_button_2");
    auto& occupied_insert = findRequiredDescendant<juce::TextButton>(
        panel, insertPluginButtonId(common::audio::g_max_signal_chain_plugins - 1));

    CHECK(inserted_tile.getBounds().getCentreX() == selected_block_center_x);
    CHECK(gap_insert.isVisible());
    CHECK(gap_insert.isEnabled());
    CHECK_FALSE(occupied_insert.isVisible());
}

// Verifies fixed block placeholders expose only the configured capacity.
TEST_CASE("Signal-chain insert controls stop at the plugin limit", "[ui][editor-view]")
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
                .plugins = makePlugins(common::audio::g_max_signal_chain_plugins),
            },
        });

    auto& insert_first = findRequiredDescendant<juce::TextButton>(view, "insert_plugin_button_0");
    auto& insert_last = findRequiredDescendant<juce::TextButton>(
        view, insertPluginButtonId(common::audio::g_max_signal_chain_plugins - 1));

    CHECK_FALSE(insert_first.isEnabled());
    CHECK_FALSE(insert_last.isEnabled());
    CHECK(
        findDescendant(view, insertPluginButtonId(common::audio::g_max_signal_chain_plugins)) ==
        nullptr);
    testing::clickButton(insert_last);

    CHECK(controller.plugin_insert_slot_selection_count == 0);
}

// Verifies tile drag/drop emits final destinations from occupied block targets.
TEST_CASE("Signal-chain drag drops move tiles to occupied blocks", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    {
        RecordingSignalChainViewListener listener;
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

        auto& tile_cab = findRequiredDescendant<juce::Component>(panel, "plugin_tile_cab");
        auto& slot_amp = findRequiredDescendant<juce::Component>(panel, "insert_slot_0");

        dropPluginOnTarget(slot_amp, tile_cab, 2, "cab", juce::Point<int>{2, 8});
        CHECK(listener.move_call_count == 1);
        CHECK(listener.last_moved_instance_id == std::optional<std::string>{"cab"});
        CHECK(listener.last_move_destination_index == std::optional<std::size_t>{0});
        CHECK(
            listener.last_move_placement ==
            std::vector<core::PluginBlockAssignment>{
                core::PluginBlockAssignment{.instance_id = "amp", .block_index = 1},
                core::PluginBlockAssignment{.instance_id = "drive", .block_index = 2},
                core::PluginBlockAssignment{.instance_id = "cab", .block_index = 0},
            });
    }

    {
        RecordingSignalChainViewListener listener;
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
        auto& slot_drive = findRequiredDescendant<juce::Component>(panel, "insert_slot_1");

        dropPluginOnTarget(
            slot_drive, tile_amp, 0, "amp", juce::Point<int>{slot_drive.getWidth() - 2, 8});
        CHECK(listener.move_call_count == 1);
        CHECK(listener.last_moved_instance_id == std::optional<std::string>{"amp"});
        CHECK(listener.last_move_destination_index == std::optional<std::size_t>{1});
    }

    {
        RecordingSignalChainViewListener listener;
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
        auto& slot_cab = findRequiredDescendant<juce::Component>(panel, "insert_slot_2");

        dropPluginOnTarget(
            slot_cab, tile_amp, 0, "amp", juce::Point<int>{slot_cab.getWidth() - 2, 8});
        CHECK(listener.move_call_count == 1);
        CHECK(listener.last_moved_instance_id == std::optional<std::string>{"amp"});
        CHECK(listener.last_move_destination_index == std::optional<std::size_t>{2});
    }

    {
        RecordingSignalChainViewListener listener;
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

        auto& tile_drive = findRequiredDescendant<juce::Component>(panel, "plugin_tile_drive");
        auto& slot_drive = findRequiredDescendant<juce::Component>(panel, "insert_slot_1");
        auto* const no_op_drop_target = dynamic_cast<juce::DragAndDropTarget*>(&slot_drive);
        REQUIRE(no_op_drop_target != nullptr);
        const juce::DragAndDropTarget::SourceDetails no_op_details{
            juce::var{pluginDragPayload(1, "drive")},
            &tile_drive,
            juce::Point<int>{2, 8},
        };

        CHECK(no_op_drop_target->isInterestedInDragSource(no_op_details));
        no_op_drop_target->itemDropped(no_op_details);
        CHECK(listener.move_call_count == 0);
    }
}

// Verifies empty fixed cells accept dragged plugins as end-of-chain moves.
TEST_CASE("Signal-chain drag drops move tiles to empty cells", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainViewListener listener;
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
    auto& empty_slot = findRequiredDescendant<juce::Component>(panel, "insert_slot_6");
    const juce::Rectangle<int> amp_bounds = tile_amp.getBounds();
    const juce::Rectangle<int> drive_bounds = tile_drive.getBounds();
    const juce::Rectangle<int> cab_bounds = tile_cab.getBounds();
    auto* const drop_target = dynamic_cast<juce::DragAndDropTarget*>(&empty_slot);
    REQUIRE(drop_target != nullptr);

    const juce::DragAndDropTarget::SourceDetails details{
        juce::var{pluginDragPayload(0, "amp")},
        &tile_amp,
        juce::Point<int>{8, 8},
    };

    CHECK(drop_target->isInterestedInDragSource(details));
    drop_target->itemDragEnter(details);
    CHECK(componentTargetBounds(tile_drive) == drive_bounds);
    CHECK(componentTargetBounds(tile_cab) == cab_bounds);
    CHECK(componentTargetBounds(tile_amp).getX() > cab_bounds.getRight());

    drop_target->itemDragExit(details);
    CHECK(componentTargetBounds(tile_drive) == drive_bounds);
    CHECK(componentTargetBounds(tile_cab) == cab_bounds);
    CHECK(componentTargetBounds(tile_amp).getX() > cab_bounds.getRight());

    drop_target->itemDragEnter(details);
    drop_target->itemDropped(details);
    tile_amp.mouseUp(testing::makeMouseDownEvent(tile_amp, 8.0f, 8.0f));

    CHECK(listener.move_call_count == 1);
    CHECK(listener.last_moved_instance_id == std::optional<std::string>{"amp"});
    CHECK(listener.last_move_destination_index == std::optional<std::size_t>{2});
    CHECK(componentTargetBounds(tile_drive) == drive_bounds);
    CHECK(componentTargetBounds(tile_cab) == cab_bounds);
    CHECK(componentTargetBounds(tile_amp).getX() > cab_bounds.getRight());

    panel.setState(
        core::SignalChainViewState{
            .move_plugins_enabled = true,
            .plugins = {
                makePlugin("amp", 0),
                makePlugin("drive", 1),
                makePlugin("cab", 2),
            },
        });

    auto& stale_amp = findRequiredDescendant<juce::Component>(panel, "plugin_tile_amp");
    auto& stale_drive = findRequiredDescendant<juce::Component>(panel, "plugin_tile_drive");
    auto& stale_cab = findRequiredDescendant<juce::Component>(panel, "plugin_tile_cab");

    CHECK(stale_amp.getBounds() == amp_bounds);
    CHECK(stale_drive.getBounds() == drive_bounds);
    CHECK(stale_cab.getBounds() == cab_bounds);

    panel.setState(
        core::SignalChainViewState{
            .move_plugins_enabled = true,
            .plugins = {
                makePluginAtBlock("drive", 0, 1),
                makePluginAtBlock("cab", 1, 2),
                makePluginAtBlock("amp", 2, 6),
            },
        });

    auto& moved_amp = findRequiredDescendant<juce::Component>(panel, "plugin_tile_amp");
    auto& moved_drive = findRequiredDescendant<juce::Component>(panel, "plugin_tile_drive");
    auto& moved_cab = findRequiredDescendant<juce::Component>(panel, "plugin_tile_cab");

    CHECK(moved_drive.getBounds() == drive_bounds);
    CHECK(moved_cab.getBounds() == cab_bounds);
    CHECK(moved_amp.getX() > cab_bounds.getRight());
}

// Verifies dropping the last plugin into a later empty slot leaves a visible gap.
TEST_CASE("Signal-chain empty drops can keep gaps", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainViewListener listener;
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
    auto& empty_slot = findRequiredDescendant<juce::Component>(panel, "insert_slot_6");
    const juce::Rectangle<int> amp_bounds = tile_amp.getBounds();
    const juce::Rectangle<int> drive_bounds = tile_drive.getBounds();
    const juce::Rectangle<int> cab_bounds = tile_cab.getBounds();
    auto* const drop_target = dynamic_cast<juce::DragAndDropTarget*>(&empty_slot);
    REQUIRE(drop_target != nullptr);

    const juce::DragAndDropTarget::SourceDetails details{
        juce::var{pluginDragPayload(2, "cab")},
        &tile_cab,
        juce::Point<int>{8, 8},
    };

    CHECK(drop_target->isInterestedInDragSource(details));
    drop_target->itemDragEnter(details);
    CHECK(componentTargetBounds(tile_amp) == amp_bounds);
    CHECK(componentTargetBounds(tile_drive) == drive_bounds);
    CHECK(componentTargetBounds(tile_cab).getX() > cab_bounds.getRight());

    drop_target->itemDropped(details);
    tile_cab.mouseUp(testing::makeMouseDownEvent(tile_cab, 8.0f, 8.0f));

    CHECK(listener.move_call_count == 0);
    CHECK(listener.placement_change_count == 1);
    CHECK(
        listener.last_signal_chain_placement ==
        std::vector<core::PluginBlockAssignment>{
            core::PluginBlockAssignment{.instance_id = "amp", .block_index = 0},
            core::PluginBlockAssignment{.instance_id = "drive", .block_index = 1},
            core::PluginBlockAssignment{.instance_id = "cab", .block_index = 6},
        });
    CHECK(componentTargetBounds(tile_amp) == amp_bounds);
    CHECK(componentTargetBounds(tile_drive) == drive_bounds);
    CHECK(componentTargetBounds(tile_cab).getX() > cab_bounds.getRight());

    panel.setState(
        core::SignalChainViewState{
            .move_plugins_enabled = true,
            .plugins = {
                makePluginAtBlock("amp", 0, 0),
                makePluginAtBlock("drive", 1, 1),
                makePluginAtBlock("cab", 2, 6),
            },
        });

    auto& refreshed_amp = findRequiredDescendant<juce::Component>(panel, "plugin_tile_amp");
    auto& refreshed_drive = findRequiredDescendant<juce::Component>(panel, "plugin_tile_drive");
    auto& refreshed_cab = findRequiredDescendant<juce::Component>(panel, "plugin_tile_cab");

    CHECK(refreshed_amp.getBounds() == amp_bounds);
    CHECK(refreshed_drive.getBounds() == drive_bounds);
    CHECK(refreshed_cab.getX() > cab_bounds.getRight());
}

// Verifies an occupied hover replaces a prior gap preview with the matching move preview.
TEST_CASE("Signal-chain drag preview replaces gap preview", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainViewListener listener;
    SignalChainPanel panel{listener};
    panel.setBounds(0, 0, 720, 220);
    panel.setState(
        core::SignalChainViewState{
            .insert_plugin_enabled = true,
            .move_plugins_enabled = true,
            .plugins = {
                makePlugin("amp", 0),
                makePlugin("drive", 1),
                makePlugin("cab", 2),
            },
        });

    auto& initial_cab = findRequiredDescendant<juce::Component>(panel, "plugin_tile_cab");
    auto& fourth_slot = findRequiredDescendant<juce::Component>(panel, "insert_slot_3");
    const juce::Rectangle<int> third_block_bounds = initial_cab.getBounds();
    auto* const fourth_target = dynamic_cast<juce::DragAndDropTarget*>(&fourth_slot);
    REQUIRE(fourth_target != nullptr);

    const juce::DragAndDropTarget::SourceDetails fourth_details{
        juce::var{pluginDragPayload(2, "cab")},
        &initial_cab,
        juce::Point<int>{8, 8},
    };

    CHECK(fourth_target->isInterestedInDragSource(fourth_details));
    fourth_target->itemDropped(fourth_details);
    CHECK(listener.move_call_count == 0);
    CHECK(listener.placement_change_count == 1);

    panel.setState(
        core::SignalChainViewState{
            .insert_plugin_enabled = true,
            .move_plugins_enabled = true,
            .plugins = {
                makePluginAtBlock("amp", 0, 0),
                makePluginAtBlock("drive", 1, 1),
                makePluginAtBlock("cab", 2, 3),
            },
        });

    auto& tile_amp = findRequiredDescendant<juce::Component>(panel, "plugin_tile_amp");
    auto& tile_drive = findRequiredDescendant<juce::Component>(panel, "plugin_tile_drive");
    auto& tile_cab = findRequiredDescendant<juce::Component>(panel, "plugin_tile_cab");
    auto& third_slot = findRequiredDescendant<juce::Component>(panel, "insert_slot_2");
    auto& third_insert = findRequiredDescendant<juce::TextButton>(panel, "insert_plugin_button_2");
    auto& drive_slot = findRequiredDescendant<juce::Component>(panel, "insert_slot_1");
    const juce::Rectangle<int> amp_bounds = tile_amp.getBounds();
    const juce::Rectangle<int> drive_bounds = tile_drive.getBounds();
    CHECK(tile_cab.getBounds().getX() > third_block_bounds.getRight());
    CHECK(third_insert.isVisible());
    CHECK(third_insert.isEnabled());

    auto* const third_target = dynamic_cast<juce::DragAndDropTarget*>(&third_slot);
    auto* const drive_target = dynamic_cast<juce::DragAndDropTarget*>(&drive_slot);
    REQUIRE(third_target != nullptr);
    REQUIRE(drive_target != nullptr);

    const juce::DragAndDropTarget::SourceDetails third_details{
        juce::var{pluginDragPayload(2, "cab")},
        &tile_cab,
        juce::Point<int>{8, 8},
    };

    CHECK(third_target->isInterestedInDragSource(third_details));
    third_target->itemDragEnter(third_details);
    CHECK(componentTargetBounds(tile_amp) == amp_bounds);
    CHECK(componentTargetBounds(tile_drive) == drive_bounds);
    CHECK(componentTargetBounds(tile_cab) == third_block_bounds);

    const juce::DragAndDropTarget::SourceDetails drive_details{
        juce::var{pluginDragPayload(2, "cab")},
        &tile_cab,
        juce::Point<int>{drive_slot.getWidth() - 2, 8},
    };

    CHECK(drive_target->isInterestedInDragSource(drive_details));
    drive_target->itemDragEnter(drive_details);
    CHECK(componentTargetBounds(tile_amp) == amp_bounds);
    CHECK(componentTargetBounds(tile_drive) == third_block_bounds);
    CHECK(componentTargetBounds(tile_cab) == drive_bounds);

    drive_target->itemDropped(drive_details);
    CHECK(listener.move_call_count == 1);
    CHECK(listener.last_moved_instance_id == std::optional<std::string>{"cab"});
    CHECK(listener.last_move_destination_index == std::optional<std::size_t>{1});
    CHECK(componentTargetBounds(tile_amp) == amp_bounds);
    CHECK(componentTargetBounds(tile_drive) == third_block_bounds);
    CHECK(componentTargetBounds(tile_cab) == drive_bounds);
    CHECK_FALSE(third_insert.isVisible());
    CHECK_FALSE(third_insert.isEnabled());
}

// Verifies drag hover relayouts tiles into their transient reorder preview positions.
TEST_CASE("Signal-chain drag preview shifts occupied blocks", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainViewListener listener;
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
    auto& slot_amp = findRequiredDescendant<juce::Component>(panel, "insert_slot_0");
    const juce::Rectangle<int> amp_bounds = tile_amp.getBounds();
    const juce::Rectangle<int> drive_bounds = tile_drive.getBounds();
    const juce::Rectangle<int> cab_bounds = tile_cab.getBounds();
    auto* const drop_target = dynamic_cast<juce::DragAndDropTarget*>(&slot_amp);
    REQUIRE(drop_target != nullptr);

    const juce::DragAndDropTarget::SourceDetails details{
        juce::var{pluginDragPayload(2, "cab")},
        &tile_cab,
        juce::Point<int>{2, 8},
    };

    CHECK(drop_target->isInterestedInDragSource(details));
    drop_target->itemDragEnter(details);
    CHECK(tile_cab.getBounds() == cab_bounds);
    CHECK(componentTargetBounds(tile_cab) == amp_bounds);
    CHECK(componentTargetBounds(tile_amp) == drive_bounds);
    CHECK(componentTargetBounds(tile_drive) == cab_bounds);

    drop_target->itemDragExit(details);
    CHECK(componentTargetBounds(tile_cab) == amp_bounds);
    CHECK(componentTargetBounds(tile_amp) == drive_bounds);
    CHECK(componentTargetBounds(tile_drive) == cab_bounds);
    CHECK(listener.move_call_count == 0);
}

// Verifies the hovered side of an occupied block does not alter the swap preview.
TEST_CASE("Signal-chain drag preview ignores occupied side", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainViewListener listener;
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
    auto& slot_drive = findRequiredDescendant<juce::Component>(panel, "insert_slot_1");
    const juce::Rectangle<int> amp_bounds = tile_amp.getBounds();
    const juce::Rectangle<int> drive_bounds = tile_drive.getBounds();
    const juce::Rectangle<int> cab_bounds = tile_cab.getBounds();
    auto* const drop_target = dynamic_cast<juce::DragAndDropTarget*>(&slot_drive);
    REQUIRE(drop_target != nullptr);

    const juce::DragAndDropTarget::SourceDetails enter_details{
        juce::var{pluginDragPayload(0, "amp")},
        &tile_amp,
        juce::Point<int>{slot_drive.getWidth() - 2, 8},
    };

    CHECK(drop_target->isInterestedInDragSource(enter_details));
    drop_target->itemDragEnter(enter_details);
    CHECK(componentTargetBounds(tile_drive) == amp_bounds);
    CHECK(componentTargetBounds(tile_amp) == drive_bounds);
    CHECK(componentTargetBounds(tile_cab) == cab_bounds);

    const juce::DragAndDropTarget::SourceDetails move_details{
        juce::var{pluginDragPayload(0, "amp")},
        &tile_amp,
        juce::Point<int>{2, 8},
    };

    drop_target->itemDragMove(move_details);
    CHECK(componentTargetBounds(tile_drive) == amp_bounds);
    CHECK(componentTargetBounds(tile_amp) == drive_bounds);
    CHECK(componentTargetBounds(tile_cab) == cab_bounds);

    drop_target->itemDropped(move_details);
    CHECK(listener.move_call_count == 1);
    CHECK(listener.last_moved_instance_id == std::optional<std::string>{"amp"});
    CHECK(listener.last_move_destination_index == std::optional<std::size_t>{1});
}

// Verifies the dragged block can return to its source slot after another preview.
TEST_CASE("Signal-chain drag preview accepts source slot", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainViewListener listener;
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
    auto& slot_amp = findRequiredDescendant<juce::Component>(panel, "insert_slot_0");
    auto& slot_cab = findRequiredDescendant<juce::Component>(panel, "insert_slot_2");
    const juce::Rectangle<int> amp_bounds = tile_amp.getBounds();
    const juce::Rectangle<int> drive_bounds = tile_drive.getBounds();
    const juce::Rectangle<int> cab_bounds = tile_cab.getBounds();
    auto* const amp_target = dynamic_cast<juce::DragAndDropTarget*>(&slot_amp);
    auto* const source_target = dynamic_cast<juce::DragAndDropTarget*>(&slot_cab);
    REQUIRE(amp_target != nullptr);
    REQUIRE(source_target != nullptr);

    const juce::DragAndDropTarget::SourceDetails amp_details{
        juce::var{pluginDragPayload(2, "cab")},
        &tile_cab,
        juce::Point<int>{2, 8},
    };

    CHECK(amp_target->isInterestedInDragSource(amp_details));
    amp_target->itemDragEnter(amp_details);
    CHECK(componentTargetBounds(tile_cab) == amp_bounds);
    CHECK(componentTargetBounds(tile_amp) == drive_bounds);
    CHECK(componentTargetBounds(tile_drive) == cab_bounds);

    const juce::DragAndDropTarget::SourceDetails source_details{
        juce::var{pluginDragPayload(2, "cab")},
        &tile_cab,
        juce::Point<int>{8, 8},
    };

    CHECK(source_target->isInterestedInDragSource(source_details));
    source_target->itemDragEnter(source_details);
    CHECK(componentTargetBounds(tile_amp) == amp_bounds);
    CHECK(componentTargetBounds(tile_drive) == drive_bounds);
    CHECK(componentTargetBounds(tile_cab) == cab_bounds);

    source_target->itemDropped(source_details);
    CHECK(listener.move_call_count == 0);
    CHECK(componentTargetBounds(tile_amp) == amp_bounds);
    CHECK(componentTargetBounds(tile_drive) == drive_bounds);
    CHECK(componentTargetBounds(tile_cab) == cab_bounds);
}

// Verifies a valid drop does not snap the preview back before controller state arrives.
TEST_CASE("Signal-chain valid drops hold preview until state", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainViewListener listener;
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
    auto& slot_amp = findRequiredDescendant<juce::Component>(panel, "insert_slot_0");
    const juce::Rectangle<int> amp_bounds = tile_amp.getBounds();
    const juce::Rectangle<int> drive_bounds = tile_drive.getBounds();
    const juce::Rectangle<int> cab_bounds = tile_cab.getBounds();
    auto* const drop_target = dynamic_cast<juce::DragAndDropTarget*>(&slot_amp);
    REQUIRE(drop_target != nullptr);

    const juce::DragAndDropTarget::SourceDetails details{
        juce::var{pluginDragPayload(2, "cab")},
        &tile_cab,
        juce::Point<int>{2, 8},
    };

    CHECK(drop_target->isInterestedInDragSource(details));
    drop_target->itemDragEnter(details);
    CHECK(componentTargetBounds(tile_cab) == amp_bounds);
    CHECK(componentTargetBounds(tile_amp) == drive_bounds);
    CHECK(componentTargetBounds(tile_drive) == cab_bounds);

    drop_target->itemDropped(details);
    tile_cab.mouseUp(testing::makeMouseDownEvent(tile_cab, 8.0f, 8.0f));

    CHECK(listener.move_call_count == 1);
    CHECK(listener.last_moved_instance_id == std::optional<std::string>{"cab"});
    CHECK(listener.last_move_destination_index == std::optional<std::size_t>{0});
    CHECK(componentTargetBounds(tile_cab) == amp_bounds);
    CHECK(componentTargetBounds(tile_amp) == drive_bounds);
    CHECK(componentTargetBounds(tile_drive) == cab_bounds);

    panel.setState(
        core::SignalChainViewState{
            .move_plugins_enabled = true,
            .plugins = {
                makePlugin("cab", 0),
                makePlugin("amp", 1),
                makePlugin("drive", 2),
            },
        });

    auto& moved_cab = findRequiredDescendant<juce::Component>(panel, "plugin_tile_cab");
    auto& moved_amp = findRequiredDescendant<juce::Component>(panel, "plugin_tile_amp");
    auto& moved_drive = findRequiredDescendant<juce::Component>(panel, "plugin_tile_drive");

    CHECK(moved_cab.getBounds() == amp_bounds);
    CHECK(moved_amp.getBounds() == drive_bounds);
    CHECK(moved_drive.getBounds() == cab_bounds);
}

// Verifies source mouse-up does not clear a preview before JUCE delivers the drop callback.
TEST_CASE("Signal-chain release keeps preview before drop callback", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainViewListener listener;
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
    auto& slot_amp = findRequiredDescendant<juce::Component>(panel, "insert_slot_0");
    const juce::Rectangle<int> amp_bounds = tile_amp.getBounds();
    const juce::Rectangle<int> drive_bounds = tile_drive.getBounds();
    const juce::Rectangle<int> cab_bounds = tile_cab.getBounds();
    auto* const drop_target = dynamic_cast<juce::DragAndDropTarget*>(&slot_amp);
    REQUIRE(drop_target != nullptr);

    const juce::DragAndDropTarget::SourceDetails details{
        juce::var{pluginDragPayload(2, "cab")},
        &tile_cab,
        juce::Point<int>{2, 8},
    };

    CHECK(drop_target->isInterestedInDragSource(details));
    drop_target->itemDragEnter(details);
    CHECK(componentTargetBounds(tile_cab) == amp_bounds);
    CHECK(componentTargetBounds(tile_amp) == drive_bounds);
    CHECK(componentTargetBounds(tile_drive) == cab_bounds);

    tile_cab.mouseUp(testing::makeMouseDownEvent(tile_cab, 8.0f, 8.0f));
    CHECK(componentTargetBounds(tile_cab) == amp_bounds);
    CHECK(componentTargetBounds(tile_amp) == drive_bounds);
    CHECK(componentTargetBounds(tile_drive) == cab_bounds);

    drop_target->itemDropped(details);
    CHECK(listener.move_call_count == 1);
    CHECK(listener.last_moved_instance_id == std::optional<std::string>{"cab"});
    CHECK(listener.last_move_destination_index == std::optional<std::size_t>{0});
}

// Verifies the occupied-target preview is independent of pointer side.
TEST_CASE("Signal-chain drag preview ignores target side", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainViewListener listener;
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
    auto& slot_amp = findRequiredDescendant<juce::Component>(panel, "insert_slot_0");
    const juce::Rectangle<int> amp_bounds = tile_amp.getBounds();
    const juce::Rectangle<int> drive_bounds = tile_drive.getBounds();
    const juce::Rectangle<int> cab_bounds = tile_cab.getBounds();
    auto* const drop_target = dynamic_cast<juce::DragAndDropTarget*>(&slot_amp);
    REQUIRE(drop_target != nullptr);

    const juce::DragAndDropTarget::SourceDetails details{
        juce::var{pluginDragPayload(2, "cab")},
        &tile_cab,
        juce::Point<int>{slot_amp.getWidth() - 2, 8},
    };

    CHECK(drop_target->isInterestedInDragSource(details));
    drop_target->itemDragEnter(details);
    CHECK(componentTargetBounds(tile_cab) == amp_bounds);
    CHECK(componentTargetBounds(tile_amp) == drive_bounds);
    CHECK(componentTargetBounds(tile_drive) == cab_bounds);

    drop_target->itemDropped(details);
    CHECK(listener.move_call_count == 1);
    CHECK(listener.last_moved_instance_id == std::optional<std::string>{"cab"});
    CHECK(listener.last_move_destination_index == std::optional<std::size_t>{0});
}

// Verifies drop targets are quiet while move editing is disabled.
TEST_CASE("Signal-chain drag drops respect move gate", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainViewListener listener;
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
    auto& slot_amp = findRequiredDescendant<juce::Component>(panel, "insert_slot_0");
    auto* const drop_target = dynamic_cast<juce::DragAndDropTarget*>(&slot_amp);
    REQUIRE(drop_target != nullptr);

    const juce::DragAndDropTarget::SourceDetails details{
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

    CHECK(controller.plugin_insert_slot_selection_count == 0);
    CHECK(controller.remove_plugin_request_count == 0);
}

// Verifies cramped panels keep every tile reachable through the horizontal signal-chain viewport.
TEST_CASE("Signal-chain cramped panel scrolls overflowing tiles", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingSignalChainViewListener listener;
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
    auto& final_slot = findRequiredDescendant<juce::Component>(
        panel, insertSlotId(common::audio::g_max_signal_chain_plugins - 1));

    CHECK(content.getWidth() > viewport.getWidth());
    CHECK(slot_first.isVisible());
    CHECK(tile_first.isVisible());
    CHECK(slot_second.isVisible());
    CHECK(tile_second.isVisible());
    CHECK(append_slot.isVisible());
    CHECK(final_slot.isVisible());
    CHECK(append_slot.getX() >= viewport.getWidth());
    CHECK(final_slot.getX() >= viewport.getWidth());
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
    const juce::MouseEvent event = testing::makeMouseDownEvent(tile, 8.0f, 8.0f);
    tile.mouseDown(event);
    tile.mouseUp(event);

    CHECK(controller.open_plugin_request_count == 1);
    CHECK(controller.last_opened_plugin_instance_id == std::optional<std::string>{"amp"});
}

} // namespace rock_hero::editor::ui
