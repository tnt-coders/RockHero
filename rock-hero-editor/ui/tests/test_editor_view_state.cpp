#include "keybinds/editor_command_registry.h"

#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>

namespace rock_hero::editor::ui
{

namespace
{

using testing::getPlayPauseButton;
using testing::getStopButton;

// Drives a key press through the command manager's mapping set exactly the way the window
// shell's key-listener attachment does: chord matching, enablement, then perform.
[[nodiscard]] bool pressCommandKey(EditorView& view, const juce::KeyPress& key)
{
    juce::KeyListener* const mappings = view.commandManager().getKeyMappings();
    return mappings->keyPressed(key, &view);
}

} // namespace

// The marker verbs read the core's published VERB and nothing else — not the drawn selection, not
// the project gate they used to keep. Phase 3 retired the grammar's rule 2 (a chord SELECTS the
// marker at the cursor) and moved the restate off the selection's flags, so a chord over an
// outlined chip authors nothing, and with every verb published as nothing all four presses are
// inert: no select, no author, no prompt raised. Inert rather than declined, which is what keeps
// JUCE from sounding the system alert for a chord its own mapping set matched.
//
// The branches that DO act open modal prompts or popup menus, which a headless view test cannot
// dismiss; what each verb names is pinned in the core's own suites instead.
TEST_CASE("EditorView marker verbs read only the published target", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;

    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    constexpr common::core::GridPosition chorus{.measure = 3, .beat = 1};
    core::EditorViewState state{};
    state.project_loaded = true;
    // A chip and a region drawn SELECTED, with every published verb left at its default nothing:
    // under the old rule the section chord would have restated the chip and Enter its name.
    state.sections = {
        core::SongSectionViewState{
            .seconds = 4.0,
            .position = chorus,
            .name = "Chorus",
            .selected = true,
        },
    };
    state.tone_track.regions = {
        core::ToneRegionViewState{
            .id = "solo-region",
            .name = "Solo",
            .tone_document_ref = "tones/solo.rht",
            .grid_start = chorus,
            .grid_end = common::core::GridPosition{.measure = 5, .beat = 1},
            .time_range = {},
            .active = false,
            .selected = true,
        },
    };
    view.setState(state);
    const int windows_before = juce::TopLevelWindow::getNumTopLevelWindows();

    const auto press = [&view](EditorCommandId command) {
        const juce::ApplicationCommandTarget::InvocationInfo info{static_cast<juce::CommandID>(
            command)};
        return view.perform(info);
    };
    CHECK(press(EditorCommandId::InsertSongSection));
    CHECK(press(EditorCommandId::InsertToneChange));
    CHECK(press(EditorCommandId::RestateSelection));
    CHECK(press(EditorCommandId::RenameSelection));

    CHECK(controller.song_section_select_count == 0);
    CHECK(controller.last_selected_tone_region_id.empty());
    CHECK(controller.last_inserted_song_section_name.empty());
    CHECK(controller.last_created_tone_region_id.empty());
    CHECK(controller.last_renamed_song_section_name.empty());
    CHECK(controller.last_renamed_tone_document_ref.empty());
    CHECK(juce::TopLevelWindow::getNumTopLevelWindows() == windows_before);
}

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
            .export_enabled = false,
            .suggested_export_file = std::filesystem::path{},
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

    const auto& menu_bar = findRequiredDescendant<juce::MenuBarComponent>(view, "file_menu_bar");
    auto& controls = findRequiredDescendant<TransportControls>(view, "transport_controls");
    const auto& track_viewport = findRequiredDescendant<juce::Component>(view, "track_viewport");
    const auto& track_content =
        findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    const auto& arrangement_view =
        findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    const auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    const auto& signal_chain_panel =
        findRequiredDescendant<SignalChainPanel>(view, "signal_chain_panel");
    const auto& signal_chain_view =
        findRequiredDescendant<SignalChainView>(view, "signal_chain_view");
    const int save_command = toJuceCommandId(EditorCommandId::SaveProject);
    const int close_command = toJuceCommandId(EditorCommandId::CloseProject);
    const int exit_command = toJuceCommandId(EditorCommandId::ExitEditor);
    const int export_command = toJuceCommandId(EditorCommandId::ExportSong);
    const int undo_command = toJuceCommandId(EditorCommandId::Undo);
    const int redo_command = toJuceCommandId(EditorCommandId::Redo);
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
    // Direct invocation of disabled commands stays a no-op: perform mirrors the enablement
    // guards, so tests and the preview-window filter cannot bypass them.
    view.commandManager().invokeDirectly(save_command, false);
    view.commandManager().invokeDirectly(undo_command, false);
    view.commandManager().invokeDirectly(redo_command, false);
    CHECK(controller.save_request_count == 0);
    CHECK(controller.undo_request_count == 0);
    CHECK(controller.redo_request_count == 0);
    // The mapping set refuses disabled commands at the key level too.
    CHECK_FALSE(pressCommandKey(view, undo_key));
    CHECK_FALSE(pressCommandKey(view, redo_key));
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
            .export_enabled = true,
            .undo_enabled = true,
            .undo_label = std::string{"Move Plugin"},
            .redo_enabled = true,
            .redo_label = std::string{"Restore Plugin"},
            .suggested_export_file = std::filesystem::path{"song.rock"},
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
    const auto export_item = requiredMenuItem(view.getMenuForIndex(0, "File"), export_command);
    CHECK(export_item.isEnabled);
    CHECK(export_item.text == "Export Song...");
    CHECK(requiredMenuItem(view.getMenuForIndex(0, "File"), close_command).isEnabled);
    const auto undo_item = requiredMenuItem(view.getMenuForIndex(1, "Edit"), undo_command);
    CHECK(undo_item.isEnabled);
    CHECK(undo_item.text == "Undo Move Plugin");
    const auto redo_item = requiredMenuItem(view.getMenuForIndex(1, "Edit"), redo_command);
    CHECK(redo_item.isEnabled);
    CHECK(redo_item.text == "Redo Restore Plugin");
    view.commandManager().invokeDirectly(save_command, false);
    view.commandManager().invokeDirectly(close_command, false);
    view.commandManager().invokeDirectly(exit_command, false);
    view.commandManager().invokeDirectly(undo_command, false);
    view.commandManager().invokeDirectly(redo_command, false);
    CHECK(controller.save_request_count == 1);
    CHECK(controller.close_request_count == 1);
    CHECK(controller.exit_request_count == 1);
    CHECK(controller.undo_request_count == 1);
    CHECK(controller.redo_request_count == 1);
    CHECK(pressCommandKey(view, undo_key));
    CHECK(pressCommandKey(view, redo_key));
    CHECK(controller.undo_request_count == 2);
    CHECK(controller.redo_request_count == 2);
    // The DAW-convention redo alternative dispatches the same intent.
    CHECK(pressCommandKey(
        view,
        juce::KeyPress{
            'z',
            juce::ModifierKeys{
                juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier
            },
            0
        }));
    CHECK(controller.redo_request_count == 3);
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

// Locks the registry's command ids and default chords: persistence keys off the hex id forever
// and the shipped defaults are a committed part of the keymap, so any renumbering, reuse, or
// accidental default change must fail loudly here.
TEST_CASE("Editor command registry locks ids and default chords", "[ui][editor-view][keybinds]")
{
    constexpr int command = juce::ModifierKeys::commandModifier;
    constexpr int shift = juce::ModifierKeys::shiftModifier;
    constexpr int alt = juce::ModifierKeys::altModifier;
    const auto chord = [](int key_code, int modifier_flags = 0) {
        return juce::KeyPress{key_code, juce::ModifierKeys{modifier_flags}, 0};
    };

    struct ExpectedCommand final
    {
        EditorCommandId id;
        int value{};
        std::vector<juce::KeyPress> chords{};
    };

    const std::vector<ExpectedCommand> expected{
        {.id = EditorCommandId::OpenProject, .value = 0x1001, .chords = {chord('o', command)}},
        {.id = EditorCommandId::ImportSong, .value = 0x1002, .chords = {chord('i', command)}},
        {.id = EditorCommandId::SaveProject, .value = 0x1003, .chords = {chord('s', command)}},
        {.id = EditorCommandId::SaveProjectAs,
         .value = 0x1004,
         .chords = {chord('s', command | shift)}},
        {.id = EditorCommandId::ExportSong, .value = 0x1005, .chords = {chord('e', command)}},
        {.id = EditorCommandId::CloseProject, .value = 0x1006, .chords = {chord('w', command)}},
        {.id = EditorCommandId::ExitEditor, .value = 0x1007, .chords = {chord('q', command)}},
        {.id = EditorCommandId::ImportTone,
         .value = 0x1008,
         .chords = {chord('i', command | shift)}},
        {.id = EditorCommandId::ExportTone,
         .value = 0x1009,
         .chords = {chord('e', command | shift)}},
        {.id = EditorCommandId::Undo, .value = 0x1101, .chords = {chord('z', command)}},
        {.id = EditorCommandId::Redo,
         .value = 0x1102,
         .chords = {chord('y', command), chord('z', command | shift)}},
        {.id = EditorCommandId::ShowActions, .value = 0x1103, .chords = {chord('/', shift)}},
        {.id = EditorCommandId::PlayPause,
         .value = 0x1201,
         .chords = {chord(juce::KeyPress::spaceKey)}},
        {.id = EditorCommandId::ToggleWaveform,
         .value = 0x1301,
         .chords = {chord(juce::KeyPress::F5Key)}},
        {.id = EditorCommandId::ToggleUndoHistory,
         .value = 0x1302,
         .chords = {chord(juce::KeyPress::F8Key)}},
        {.id = EditorCommandId::TogglePreview3D,
         .value = 0x1303,
         .chords = {chord(juce::KeyPress::F3Key)}},
        {.id = EditorCommandId::InsertToneChange, .value = 0x1401, .chords = {chord('t', command)}},
        {.id = EditorCommandId::InsertSongSection,
         .value = 0x1402,
         .chords = {chord('m', command)}},
        {.id = EditorCommandId::RestateSelection,
         .value = 0x1404,
         .chords = {chord(juce::KeyPress::returnKey)}},
        {.id = EditorCommandId::RenameSelection, .value = 0x1405, .chords = {chord('r', command)}},
        {.id = EditorCommandId::CaretStepLeft,
         .value = 0x1501,
         .chords = {chord(juce::KeyPress::leftKey)}},
        {.id = EditorCommandId::CaretStepRight,
         .value = 0x1502,
         .chords = {chord(juce::KeyPress::rightKey)}},
        {.id = EditorCommandId::CaretStepUp,
         .value = 0x1503,
         .chords = {chord(juce::KeyPress::upKey)}},
        {.id = EditorCommandId::CaretStepDown,
         .value = 0x1504,
         .chords = {chord(juce::KeyPress::downKey)}},
        {.id = EditorCommandId::CaretJumpSurfaceAbove,
         .value = 0x150B,
         .chords = {chord(juce::KeyPress::upKey, command)}},
        {.id = EditorCommandId::CaretJumpSurfaceBelow,
         .value = 0x150C,
         .chords = {chord(juce::KeyPress::downKey, command)}},
        {.id = EditorCommandId::CaretMeasureJumpLeft,
         .value = 0x1505,
         .chords = {chord(juce::KeyPress::leftKey, command)}},
        {.id = EditorCommandId::CaretMeasureJumpRight,
         .value = 0x1506,
         .chords = {chord(juce::KeyPress::rightKey, command)}},
        {.id = EditorCommandId::CaretJumpChartStart,
         .value = 0x1507,
         .chords = {chord(juce::KeyPress::homeKey), chord(juce::KeyPress::homeKey, command)}},
        {.id = EditorCommandId::CaretJumpChartEnd,
         .value = 0x1508,
         .chords = {chord(juce::KeyPress::endKey), chord(juce::KeyPress::endKey, command)}},
        {.id = EditorCommandId::CaretJumpPreviousSection,
         .value = 0x1509,
         .chords = {chord(juce::KeyPress::pageUpKey), chord(juce::KeyPress::pageUpKey, command)}},
        {.id = EditorCommandId::CaretJumpNextSection,
         .value = 0x150A,
         .chords =
             {chord(juce::KeyPress::pageDownKey), chord(juce::KeyPress::pageDownKey, command)}},
        {.id = EditorCommandId::CaretStepNextObject,
         .value = 0x150D,
         .chords = {chord(juce::KeyPress::tabKey)}},
        {.id = EditorCommandId::CaretStepPreviousObject,
         .value = 0x150E,
         .chords = {chord(juce::KeyPress::tabKey, shift)}},
        // The physical Ctrl key, not commandModifier: Cmd+Tab is the macOS application switcher.
        {.id = EditorCommandId::CaretStepNextNote,
         .value = 0x150F,
         .chords = {chord(juce::KeyPress::tabKey, juce::ModifierKeys::ctrlModifier)}},
        {.id = EditorCommandId::CaretStepPreviousNote,
         .value = 0x1510,
         .chords = {chord(juce::KeyPress::tabKey, juce::ModifierKeys::ctrlModifier | shift)}},
        // The row jumps: each marker kind's letter with Ctrl+Shift, the select half of the pair
        // whose author half is Ctrl+letter.
        {.id = EditorCommandId::CaretJumpSectionRow,
         .value = 0x1511,
         .chords = {chord('m', command | shift)}},
        {.id = EditorCommandId::CaretJumpTempoRow,
         .value = 0x1512,
         .chords = {chord('b', command | shift)}},
        {.id = EditorCommandId::CaretJumpTimeSignatureRow,
         .value = 0x1513,
         .chords = {chord('/', command | shift)}},
        {.id = EditorCommandId::CaretJumpToneRow,
         .value = 0x1514,
         .chords = {chord('t', command | shift)}},
        {.id = EditorCommandId::CaretJumpAddLaneRow,
         .value = 0x1515,
         .chords = {chord('a', command | shift)}},
        {.id = EditorCommandId::TimeSelectionExtendLeft,
         .value = 0x1601,
         .chords = {chord(juce::KeyPress::leftKey, shift)}},
        {.id = EditorCommandId::TimeSelectionExtendRight,
         .value = 0x1602,
         .chords = {chord(juce::KeyPress::rightKey, shift)}},
        {.id = EditorCommandId::TimeSelectionExtendMeasureLeft,
         .value = 0x1603,
         .chords = {chord(juce::KeyPress::leftKey, command | shift)}},
        {.id = EditorCommandId::TimeSelectionExtendMeasureRight,
         .value = 0x1604,
         .chords = {chord(juce::KeyPress::rightKey, command | shift)}},
        {.id = EditorCommandId::TimeSelectionExtendPreviousSection,
         .value = 0x1605,
         .chords = {chord(juce::KeyPress::pageUpKey, shift)}},
        {.id = EditorCommandId::TimeSelectionExtendNextSection,
         .value = 0x1606,
         .chords = {chord(juce::KeyPress::pageDownKey, shift)}},
        {.id = EditorCommandId::TimeSelectionExtendChartStart,
         .value = 0x1607,
         .chords = {chord(juce::KeyPress::homeKey, shift)}},
        {.id = EditorCommandId::TimeSelectionExtendChartEnd,
         .value = 0x1608,
         .chords = {chord(juce::KeyPress::endKey, shift)}},
        {.id = EditorCommandId::SelectionMoveLeft,
         .value = 0x1609,
         .chords = {chord(juce::KeyPress::leftKey, alt)}},
        {.id = EditorCommandId::SelectionMoveRight,
         .value = 0x160A,
         .chords = {chord(juce::KeyPress::rightKey, alt)}},
        {.id = EditorCommandId::SelectionMoveUp,
         .value = 0x160B,
         .chords = {chord(juce::KeyPress::upKey, alt)}},
        {.id = EditorCommandId::SelectionMoveDown,
         .value = 0x160C,
         .chords = {chord(juce::KeyPress::downKey, alt)}},
        {.id = EditorCommandId::SelectionDelete,
         .value = 0x1611,
         .chords = {chord(juce::KeyPress::deleteKey)}},
        {.id = EditorCommandId::CancelDismiss,
         .value = 0x1708,
         .chords = {chord(juce::KeyPress::escapeKey)}},
        {.id = EditorCommandId::SustainLengthen,
         .value = 0x1701,
         .chords = {chord(juce::KeyPress::rightKey, alt | shift)}},
        {.id = EditorCommandId::SustainShorten,
         .value = 0x1702,
         .chords = {chord(juce::KeyPress::leftKey, alt | shift)}},
        {.id = EditorCommandId::FretShiftUp,
         .value = 0x1705,
         .chords = {chord(juce::KeyPress::upKey, alt | shift)}},
        {.id = EditorCommandId::FretShiftDown,
         .value = 0x1706,
         .chords = {chord(juce::KeyPress::downKey, alt | shift)}},
        {.id = EditorCommandId::InsertLanePoint,
         .value = 0x1707,
         .chords = {chord(juce::KeyPress::insertKey)}},
        {.id = EditorCommandId::ChartPickSlideToggle,
         .value = 0x1709,
         .chords = {chord('x', shift)}},
        {.id = EditorCommandId::ChartLegatoToggle, .value = 0x170A, .chords = {chord('l')}},
        {.id = EditorCommandId::ChartJunctionToggle,
         .value = 0x1713,
         .chords = {chord('l', shift)}},
        {.id = EditorCommandId::ChartLeftTap, .value = 0x170B, .chords = {chord('t', shift)}},
        {.id = EditorCommandId::ChartTapToggle, .value = 0x1715, .chords = {chord('t')}},
        {.id = EditorCommandId::ChartSlapToggle, .value = 0x1716, .chords = {chord('s')}},
        {.id = EditorCommandId::ChartPopToggle, .value = 0x1717, .chords = {chord('p')}},
        {.id = EditorCommandId::ChartPalmMuteToggle, .value = 0x170C, .chords = {chord('m')}},
        {.id = EditorCommandId::ChartDeadNoteToggle, .value = 0x170D, .chords = {chord('x')}},
        {.id = EditorCommandId::ChartAccentToggle, .value = 0x170E, .chords = {chord('a')}},
        {.id = EditorCommandId::ChartGhostToggle, .value = 0x170F, .chords = {chord('g')}},
        {.id = EditorCommandId::ChartHarmonic, .value = 0x1718, .chords = {chord('h')}},
        {.id = EditorCommandId::ChartPinchHarmonicToggle,
         .value = 0x1719,
         .chords = {chord('h', shift)}},
        {.id = EditorCommandId::ChartVibratoToggle, .value = 0x1711, .chords = {chord('v')}},
        {.id = EditorCommandId::ChartWideVibratoToggle,
         .value = 0x1714,
         .chords = {chord('v', shift)}},
        {.id = EditorCommandId::ChartTremoloToggle, .value = 0x1710, .chords = {chord('r')}},
        {.id = EditorCommandId::TypeDigit0,
         .value = 0x1801,
         .chords = {chord('0'), chord(juce::KeyPress::numberPad0)}},
        {.id = EditorCommandId::TypePathDigit0,
         .value = 0x180B,
         .chords = {chord('0', alt), chord(juce::KeyPress::numberPad0, alt)}},
        {.id = EditorCommandId::TypeDigit1,
         .value = 0x1802,
         .chords = {chord('1'), chord(juce::KeyPress::numberPad1)}},
        {.id = EditorCommandId::TypePathDigit1,
         .value = 0x180C,
         .chords = {chord('1', alt), chord(juce::KeyPress::numberPad1, alt)}},
        {.id = EditorCommandId::TypeDigit2,
         .value = 0x1803,
         .chords = {chord('2'), chord(juce::KeyPress::numberPad2)}},
        {.id = EditorCommandId::TypePathDigit2,
         .value = 0x180D,
         .chords = {chord('2', alt), chord(juce::KeyPress::numberPad2, alt)}},
        {.id = EditorCommandId::TypeDigit3,
         .value = 0x1804,
         .chords = {chord('3'), chord(juce::KeyPress::numberPad3)}},
        {.id = EditorCommandId::TypePathDigit3,
         .value = 0x180E,
         .chords = {chord('3', alt), chord(juce::KeyPress::numberPad3, alt)}},
        {.id = EditorCommandId::TypeDigit4,
         .value = 0x1805,
         .chords = {chord('4'), chord(juce::KeyPress::numberPad4)}},
        {.id = EditorCommandId::TypePathDigit4,
         .value = 0x180F,
         .chords = {chord('4', alt), chord(juce::KeyPress::numberPad4, alt)}},
        {.id = EditorCommandId::TypeDigit5,
         .value = 0x1806,
         .chords = {chord('5'), chord(juce::KeyPress::numberPad5)}},
        {.id = EditorCommandId::TypePathDigit5,
         .value = 0x1810,
         .chords = {chord('5', alt), chord(juce::KeyPress::numberPad5, alt)}},
        {.id = EditorCommandId::TypeDigit6,
         .value = 0x1807,
         .chords = {chord('6'), chord(juce::KeyPress::numberPad6)}},
        {.id = EditorCommandId::TypePathDigit6,
         .value = 0x1811,
         .chords = {chord('6', alt), chord(juce::KeyPress::numberPad6, alt)}},
        {.id = EditorCommandId::TypeDigit7,
         .value = 0x1808,
         .chords = {chord('7'), chord(juce::KeyPress::numberPad7)}},
        {.id = EditorCommandId::TypePathDigit7,
         .value = 0x1812,
         .chords = {chord('7', alt), chord(juce::KeyPress::numberPad7, alt)}},
        {.id = EditorCommandId::TypeDigit8,
         .value = 0x1809,
         .chords = {chord('8'), chord(juce::KeyPress::numberPad8)}},
        {.id = EditorCommandId::TypePathDigit8,
         .value = 0x1813,
         .chords = {chord('8', alt), chord(juce::KeyPress::numberPad8, alt)}},
        {.id = EditorCommandId::TypeDigit9,
         .value = 0x180A,
         .chords = {chord('9'), chord(juce::KeyPress::numberPad9)}},
        {.id = EditorCommandId::TypePathDigit9,
         .value = 0x1814,
         .chords = {chord('9', alt), chord(juce::KeyPress::numberPad9, alt)}},
        {.id = EditorCommandId::GridFiner,
         .value = 0x1901,
         .chords = {chord('=', shift), chord('+'), chord('=')}},
        {.id = EditorCommandId::GridCoarser,
         .value = 0x1902,
         .chords = {chord('-'), chord('-', shift)}},
        {.id = EditorCommandId::ZoomIn,
         .value = 0x1903,
         .chords = {chord('=', command | shift), chord('+', command), chord('=', command)}},
        {.id = EditorCommandId::ZoomOut,
         .value = 0x1904,
         .chords = {chord('-', command), chord('-', command | shift)}},
        {.id = EditorCommandId::ToggleGridSnap, .value = 0x1905, .chords = {chord('g', command)}},
        {.id = EditorCommandId::OpenFileMenu, .value = 0x1B01, .chords = {chord('f', alt)}},
        {.id = EditorCommandId::OpenEditMenu, .value = 0x1B02, .chords = {chord('e', alt)}},
        {.id = EditorCommandId::OpenViewMenu, .value = 0x1B03, .chords = {chord('v', alt)}},
    };

    const std::vector<EditorCommandSpec>& registry = editorCommandRegistry();
    REQUIRE(registry.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        INFO("registry index " << index);
        CHECK(registry[index].id == expected[index].id);
        CHECK(toJuceCommandId(registry[index].id) == expected[index].value);
        REQUIRE(registry[index].default_keypresses.size() == expected[index].chords.size());
        for (std::size_t chord_index = 0; chord_index < expected[index].chords.size();
             ++chord_index)
        {
            CHECK(
                registry[index].default_keypresses[chord_index] ==
                expected[index].chords[chord_index]);
        }
    }
}

// Verifies the mapping set installs the registry defaults: each default chord resolves to its
// command, and exact modifier matching keeps guarded neighbors (Ctrl+Alt+T) unbound.
TEST_CASE("Editor command mappings resolve default chords", "[ui][editor-view][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    const juce::KeyPressMappingSet* const mappings = view.commandManager().getKeyMappings();
    REQUIRE(mappings != nullptr);
    for (const EditorCommandSpec& spec : editorCommandRegistry())
    {
        for (const juce::KeyPress& key : spec.default_keypresses)
        {
            INFO(spec.name);
            CHECK(mappings->findCommandForKeyPress(key) == toJuceCommandId(spec.id));
        }
    }

    // Exact matching refuses the Alt-composed neighbor of Ctrl+T (the fine-tier authoring
    // namespace) and the Shift-composed neighbor of Ctrl+Z (which belongs to Redo).
    const juce::KeyPress ctrl_alt_t{
        't',
        juce::ModifierKeys{juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier},
        0
    };
    CHECK(mappings->findCommandForKeyPress(ctrl_alt_t) == 0);
    const juce::KeyPress ctrl_shift_z{
        'z',
        juce::ModifierKeys{juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier},
        0
    };
    CHECK(mappings->findCommandForKeyPress(ctrl_shift_z) == toJuceCommandId(EditorCommandId::Redo));
}

// The two tone commands are the keyboard twins of the signal-chain header's tone buttons: each
// chord is the Shift tier of the song's own Ctrl+I / Ctrl+E, and each command's enablement IS the
// flag its button follows, so command and button can never disagree about when the operation is
// legal. Performing one while enabled raises a native file chooser, which no headless test can
// dismiss, so the press is driven only in the disabled state; the chord's owner is read out of the
// mapping set instead.
TEST_CASE(
    "Editor tone commands follow the signal-chain tone buttons", "[ui][editor-view][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    constexpr int command_shift =
        juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    const juce::KeyPress import_tone_key{'i', juce::ModifierKeys{command_shift}, 0};
    const juce::KeyPress export_tone_key{'e', juce::ModifierKeys{command_shift}, 0};

    const juce::KeyPressMappingSet* const mappings = view.commandManager().getKeyMappings();
    REQUIRE(mappings != nullptr);
    CHECK(
        mappings->findCommandForKeyPress(import_tone_key) ==
        toJuceCommandId(EditorCommandId::ImportTone));
    CHECK(
        mappings->findCommandForKeyPress(export_tone_key) ==
        toJuceCommandId(EditorCommandId::ExportTone));

    const auto command_enabled = [&view](const EditorCommandId id) {
        juce::ApplicationCommandInfo info{toJuceCommandId(id)};
        view.getCommandInfo(toJuceCommandId(id), info);
        return (info.flags & juce::ApplicationCommandInfo::isDisabled) == 0;
    };

    // Both buttons hidden: neither command is available, and the mapping set refuses the chord at
    // the key level rather than raising a chooser.
    core::EditorViewState state = makeLoadedEditorState(20.0);
    state.signal_chain.tone_import_enabled = false;
    state.signal_chain.tone_export_enabled = false;
    view.setState(state);
    CHECK_FALSE(command_enabled(EditorCommandId::ImportTone));
    CHECK_FALSE(command_enabled(EditorCommandId::ExportTone));
    CHECK_FALSE(pressCommandKey(view, import_tone_key));
    CHECK_FALSE(pressCommandKey(view, export_tone_key));

    // One flag at a time, so each command is pinned to its OWN button rather than to the pair.
    state.signal_chain.tone_import_enabled = true;
    view.setState(state);
    CHECK(command_enabled(EditorCommandId::ImportTone));
    CHECK_FALSE(command_enabled(EditorCommandId::ExportTone));

    state.signal_chain.tone_export_enabled = true;
    view.setState(state);
    CHECK(command_enabled(EditorCommandId::ImportTone));
    CHECK(command_enabled(EditorCommandId::ExportTone));
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

    const auto& remove_button =
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
    const auto& audio_button = findRequiredDescendant<MenuBarButton>(view, "audio_device_button");

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

// Verifies the transport-strip selection-count chip appears from two selected chart notes up,
// reporting the selection size, and hides below that threshold: typing acts on the whole
// selection, so its size must stay visible even when the highlighted notes scroll off-screen.
TEST_CASE("EditorView projects the selection-count chip", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    const auto& selection_count_chip =
        findRequiredDescendant<juce::Label>(view, "chart_selection_count_display");

    // The default state carries no chart selection, so the chip stays hidden.
    view.setState(core::EditorViewState{});
    CHECK_FALSE(selection_count_chip.isVisible());

    // A single selected note is still below the two-note threshold.
    core::EditorViewState state = makeLoadedEditorState(20.0);
    state.chart_edit.selected_notes = {0};
    view.setState(state);
    CHECK_FALSE(selection_count_chip.isVisible());

    // Two selected notes reveal the chip with the "N notes" count.
    state.chart_edit.selected_notes = {0, 1};
    view.setState(state);
    CHECK(selection_count_chip.isVisible());
    CHECK(selection_count_chip.getText() == "2 notes");

    // The count is derived from the selection size, not a fixed label.
    state.chart_edit.selected_notes = {0, 1, 2};
    view.setState(state);
    CHECK(selection_count_chip.isVisible());
    CHECK(selection_count_chip.getText() == "3 notes");

    // Dropping back below the threshold hides the chip again.
    state.chart_edit.selected_notes = {0};
    view.setState(state);
    CHECK_FALSE(selection_count_chip.isVisible());
}

// The selection-centring rule follows the shortcuts-dialog categories — the verbs that act on the
// selection or at the caret, not navigation (selecting never scrolls; a moved position is followed
// on its own), and not the file, history, view, grid, menu or author-chord commands.
TEST_CASE("Command registry classifies which commands act on the selection", "[ui][editor-view]")
{
    const auto acts = [](const EditorCommandId id) {
        const EditorCommandSpec* const spec = findEditorCommandSpec(toJuceCommandId(id));
        REQUIRE(spec != nullptr);
        return editorCommandActsOnSelection(*spec);
    };
    CHECK(acts(EditorCommandId::SelectionDelete));
    CHECK(acts(EditorCommandId::SelectionMoveLeft));
    CHECK(acts(EditorCommandId::TypeDigit1));
    CHECK(acts(EditorCommandId::ChartLegatoToggle));
    CHECK(acts(EditorCommandId::RenameSelection));

    CHECK_FALSE(acts(EditorCommandId::CaretStepLeft));
    CHECK_FALSE(acts(EditorCommandId::CaretJumpSectionRow));
    CHECK_FALSE(acts(EditorCommandId::InsertSongSection));
    CHECK_FALSE(acts(EditorCommandId::InsertToneChange));
    CHECK_FALSE(acts(EditorCommandId::SaveProject));
    CHECK_FALSE(acts(EditorCommandId::Undo));
    CHECK_FALSE(acts(EditorCommandId::TogglePreview3D));
    CHECK_FALSE(acts(EditorCommandId::ToggleGridSnap));
    CHECK_FALSE(acts(EditorCommandId::PlayPause));
    CHECK_FALSE(acts(EditorCommandId::ImportTone));
    CHECK_FALSE(acts(EditorCommandId::OpenFileMenu));
    CHECK_FALSE(acts(EditorCommandId::CancelDismiss));
}

} // namespace rock_hero::editor::ui
