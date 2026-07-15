#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>

namespace rock_hero::editor::ui
{

// Verifies the arrangement thumbnail is created and later pointed at pushed audio.
TEST_CASE("EditorView applies arrangement audio to the thumbnail", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;

    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    CHECK(thumbnail_factory.create_call_count == 1);
    REQUIRE(thumbnail_factory.last_owner != nullptr);
    CHECK(thumbnail_factory.last_owner->getComponentID() == "arrangement_view");
    REQUIRE(thumbnail_factory.last_thumbnail != nullptr);
    CHECK(thumbnail_factory.last_thumbnail->set_source_call_count == 0);

    view.setState(
        core::EditorViewState{
            .open_enabled = true,
            .import_enabled = true,
            .save_enabled = false,
            .save_as_enabled = false,
            .publish_enabled = false,
            .suggested_publish_file = std::filesystem::path{},
            .close_enabled = false,
            .project_loaded = true,
            .save_requires_destination = false,
            .transport =
                core::TransportViewState{
                    .play_pause_enabled = true,
                    .stop_enabled = false,
                    .play_pause_shows_pause_icon = false,
                },
            .visible_timeline =
                common::core::TimeRange{
                    .start = common::core::TimePosition{},
                    .end = common::core::TimePosition{4.0},
                },
            .arrangement = makeArrangementState(std::filesystem::path{"full_mix.wav"}),
            .signal_chain =
                core::SignalChainViewState{
                    .plugins = {},
                },
            .unsaved_changes_prompt = std::nullopt,
            .save_as_prompt = std::nullopt,
            .busy = std::nullopt,
        });

    CHECK(thumbnail_factory.create_call_count == 1);
    CHECK(thumbnail_factory.last_thumbnail->set_source_call_count == 1);
}

// Verifies setState projects state while the transport readouts sample position independently of
// the one-shot load focus.
TEST_CASE("EditorView setState projects controls with load focus", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    auto& menu_bar = findRequiredDescendant<juce::MenuBarComponent>(view, "file_menu_bar");
    auto& controls = findRequiredDescendant<TransportControls>(view, "transport_controls");
    auto& track_viewport = findRequiredDescendant<juce::Component>(view, "track_viewport");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    auto& signal_chain_panel = findRequiredDescendant<SignalChainPanel>(view, "signal_chain_panel");
    auto& signal_chain_view = findRequiredDescendant<SignalChainView>(view, "signal_chain_view");
    constexpr int save_command{3};
    constexpr int close_command{5};
    constexpr int exit_command{6};
    constexpr int publish_command{7};
    constexpr int undo_command{101};
    constexpr int redo_command{102};
    const juce::KeyPress undo_key{'z', juce::ModifierKeys::commandModifier, 0};
    const juce::KeyPress redo_key{'y', juce::ModifierKeys::commandModifier, 0};

    view.setState(core::EditorViewState{});

    CHECK(menu_bar.isVisible());
    const juce::StringArray menu_names = view.getMenuBarNames();
    REQUIRE(menu_names.size() == 3);
    CHECK(menu_names[0] == "File");
    CHECK(menu_names[1] == "Edit");
    CHECK(menu_names[2] == "View");
    CHECK_FALSE(requiredMenuItem(view.getMenuForIndex(0, "File"), save_command).isEnabled);
    CHECK_FALSE(requiredMenuItem(view.getMenuForIndex(1, "Edit"), undo_command).isEnabled);
    CHECK_FALSE(requiredMenuItem(view.getMenuForIndex(1, "Edit"), redo_command).isEnabled);
    view.menuItemSelected(save_command, 0);
    view.menuItemSelected(undo_command, 1);
    view.menuItemSelected(redo_command, 1);
    CHECK(controller.save_request_count == 0);
    CHECK(controller.undo_request_count == 0);
    CHECK(controller.redo_request_count == 0);
    CHECK_FALSE(view.keyPressed(undo_key));
    CHECK_FALSE(view.keyPressed(redo_key));
    CHECK(controller.undo_request_count == 0);
    CHECK(controller.redo_request_count == 0);
    CHECK_FALSE(getPlayPauseButton(controls).isEnabled());
    CHECK_FALSE(getStopButton(controls).isEnabled());
    CHECK(track_viewport.isVisible());
    CHECK(track_content.isVisible());
    CHECK_FALSE(arrangement_view.isVisible());
    CHECK_FALSE(cursor_overlay.isVisible());
    CHECK(signal_chain_panel.isVisible());
    CHECK(signal_chain_view.isVisible());
    CHECK(findDescendant(view, "add_plugin_button") == nullptr);
    CHECK(transport.position_read_count == 2);

    view.setState(
        core::EditorViewState{
            .open_enabled = true,
            .import_enabled = true,
            .save_enabled = true,
            .save_as_enabled = true,
            .publish_enabled = true,
            .undo_enabled = true,
            .undo_label = std::string{"Move Plugin"},
            .redo_enabled = true,
            .redo_label = std::string{"Restore Plugin"},
            .suggested_publish_file = std::filesystem::path{"song.rock"},
            .close_enabled = true,
            .project_loaded = true,
            .save_requires_destination = false,
            .transport =
                core::TransportViewState{
                    .play_pause_enabled = true,
                    .stop_enabled = true,
                    .play_pause_shows_pause_icon = true,
                },
            .visible_timeline =
                common::core::TimeRange{
                    .start = common::core::TimePosition{},
                    .end = common::core::TimePosition{8.0},
                },
            .arrangement = makeArrangementState(std::filesystem::path{"mix.wav"}),
            .signal_chain =
                core::SignalChainViewState{
                    .remove_plugins_enabled = true,
                    .plugins =
                        {
                            core::PluginViewState{
                                .instance_id = "instance",
                                .plugin_id = "plugin",
                                .name = "Amp Sim",
                                .manufacturer = "Example Audio",
                                .format_name = "VST3",
                                .chain_index = 0,
                            },
                        },
                },
            .unsaved_changes_prompt = std::nullopt,
            .save_as_prompt = std::nullopt,
            .busy = std::nullopt,
        });

    CHECK(requiredMenuItem(view.getMenuForIndex(0, "File"), save_command).isEnabled);
    const auto publish_item = requiredMenuItem(view.getMenuForIndex(0, "File"), publish_command);
    CHECK(publish_item.isEnabled);
    CHECK(publish_item.text == "Publish...");
    CHECK(requiredMenuItem(view.getMenuForIndex(0, "File"), close_command).isEnabled);
    const auto undo_item = requiredMenuItem(view.getMenuForIndex(1, "Edit"), undo_command);
    CHECK(undo_item.isEnabled);
    CHECK(undo_item.text == "Undo Move Plugin");
    const auto redo_item = requiredMenuItem(view.getMenuForIndex(1, "Edit"), redo_command);
    CHECK(redo_item.isEnabled);
    CHECK(redo_item.text == "Redo Restore Plugin");
    view.menuItemSelected(save_command, 0);
    view.menuItemSelected(close_command, 0);
    view.menuItemSelected(exit_command, 0);
    view.menuItemSelected(undo_command, 1);
    view.menuItemSelected(redo_command, 1);
    CHECK(controller.save_request_count == 1);
    CHECK(controller.close_request_count == 1);
    CHECK(controller.exit_request_count == 1);
    CHECK(controller.undo_request_count == 1);
    CHECK(controller.redo_request_count == 1);
    CHECK(view.keyPressed(undo_key));
    CHECK(view.keyPressed(redo_key));
    CHECK(controller.undo_request_count == 2);
    CHECK(controller.redo_request_count == 2);
    CHECK(getPlayPauseButton(controls).isEnabled());
    CHECK(getStopButton(controls).isEnabled());
    CHECK(
        findRequiredDescendant<juce::TextButton>(view, "remove_plugin_button_instance")
            .isEnabled());
    CHECK(arrangement_view.isVisible());
    CHECK(cursor_overlay.isVisible());
    CHECK_FALSE(getPlayPauseButton(controls).getToggleState());
    CHECK(transport.position_read_count == 4);
}

// Verifies plugin tile remove controls reflect state and emit the selected instance ID.
TEST_CASE("EditorView emits plugin remove intents", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    core::EditorViewState state;
    state.signal_chain = core::SignalChainViewState{
        .remove_plugins_enabled = false,
        .plugins = {
            core::PluginViewState{
                .instance_id = "instance",
                .plugin_id = "plugin",
                .name = "Amp Sim",
                .manufacturer = "Example Audio",
                .format_name = "VST3",
                .chain_index = 0,
            },
        },
    };
    view.setState(state);

    CHECK_FALSE(
        findRequiredDescendant<juce::TextButton>(view, "remove_plugin_button_instance")
            .isEnabled());

    state.signal_chain.remove_plugins_enabled = true;
    view.setState(state);

    auto& remove_button =
        findRequiredDescendant<juce::TextButton>(view, "remove_plugin_button_instance");
    CHECK(remove_button.isEnabled());
    REQUIRE(remove_button.onClick);
    remove_button.onClick();
    CHECK(controller.remove_plugin_request_count == 1);
    CHECK(controller.last_removed_plugin_instance_id == std::optional<std::string>{"instance"});
}

// Verifies plugin tile clicks request opening the selected plugin editor window.
TEST_CASE("EditorView emits plugin open intents", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    core::EditorViewState state;
    state.signal_chain = core::SignalChainViewState{
        .remove_plugins_enabled = true,
        .plugins = {
            core::PluginViewState{
                .instance_id = "instance",
                .plugin_id = "plugin",
                .name = "Amp Sim",
                .manufacturer = "Example Audio",
                .format_name = "VST3",
                .chain_index = 0,
            },
        },
    };
    view.setState(state);

    auto& plugin_tile = findRequiredDescendant<juce::Component>(view, "plugin_tile_instance");
    plugin_tile.mouseUp(makeMouseDownEvent(plugin_tile, 4.0f, 4.0f));

    CHECK(controller.open_plugin_request_count == 1);
    CHECK(controller.last_opened_plugin_instance_id == std::optional<std::string>{"instance"});
}

// Verifies the menu-bar button reflects the current audio-device status text and the settings
// enablement gate.
TEST_CASE("EditorView projects audio device menu button state", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    auto& audio_button = findRequiredDescendant<MenuBarButton>(view, "audio_device_button");

    view.setState(core::EditorViewState{});

    CHECK(audio_button.isEnabled());
    CHECK(audio_button.getText() == "[audio device closed]");

    core::EditorViewState state;
    state.audio_device_settings_enabled = false;
    view.setState(state);

    CHECK_FALSE(audio_button.isEnabled());
    CHECK(audio_button.getText() == "[audio device closed]");

    state.audio_device_settings_enabled = true;
    state.audio_device_status_text = "[48kHz 24bit: 2/2ch 128spls ~4.5/7.5ms ASIO]";
    view.setState(state);

    CHECK(audio_button.isEnabled());
    CHECK(audio_button.getText() == "[48kHz 24bit: 2/2ch 128spls ~4.5/7.5ms ASIO]");
}

} // namespace rock_hero::editor::ui
