#include "keybinds/editor_command_registry.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <rock_hero/common/ui/highway_theme/highway_theme_registry.h>
#include <rock_hero/common/ui/highway_theme/loaded_highway_theme.h>
#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

using testing::getPlayPauseButton;
using testing::getStopButton;

// Drives a key press through the command manager's mapping set exactly the way the window
// shell's keyPressed dispatch does: chord matching, enablement, then perform.
[[nodiscard]] bool pressCommandKey(EditorView& view, const juce::KeyPress& key)
{
    juce::KeyListener* const mappings = view.commandManager().getKeyMappings();
    return mappings->keyPressed(key, &view);
}

// The anchor the tempo prompt tests publish a SetTempoTarget for.
constexpr common::core::GridPosition g_tempo_anchor{.measure = 3, .beat = 1};

// What a prompt test expects the prompt to show: its title and the published starting text.
struct ShownPrompt
{
    const char* title;
    const char* text;
};

// The tempo prompt the tempo tests publish a span tempo of 137 for.
constexpr ShownPrompt g_tempo_prompt{.title = "Set Tempo", .text = "137"};

// Performs a command that should open a text prompt, checks the prompt shows the expected title and
// starting text, types into it and accepts. The prompt's result arrives asynchronously, so one real
// dispatch loop delivers it, stopped by a message posted after the result's: one call per test,
// because JUCE's quit flag latches per MessageManager.
void acceptPrompt(
    EditorView& view, const EditorCommandId command, const ShownPrompt& shown,
    const juce::String& typed)
{
    const juce::ApplicationCommandTarget::InvocationInfo info{toJuceCommandId(command)};
    CHECK(view.perform(info));
    auto* const prompt = dynamic_cast<juce::AlertWindow*>(
        juce::ModalComponentManager::getInstance()->getModalComponent(0));
    REQUIRE(prompt != nullptr);
    CHECK(prompt->getName() == shown.title);
    CHECK(prompt->getTextEditorContents("value") == shown.text);
    juce::TextEditor* const field = prompt->getTextEditor("value");
    REQUIRE(field != nullptr);
    field->setText(typed);
    prompt->exitModalState(1);
    const bool stop_posted = juce::MessageManager::callAsync(
        [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    REQUIRE(stop_posted);
    juce::MessageManager::getInstance()->runDispatchLoop();
}

} // namespace

// The marker verbs read the core's published VERB and nothing else — not the drawn selection, not
// a project gate of their own. A chord never SELECTS the marker at the cursor and the restate does
// not read the selection's flags, so a chord over an outlined chip authors nothing, and with every
// verb published as nothing all four presses are inert: no select, no author, no prompt raised.
// Inert rather than declined, which is what keeps JUCE from sounding the system alert for a chord
// its own mapping set matched.
//
// The branches that DO act open modal prompts or popup menus, which a headless view test cannot
// dismiss; what each verb names is pinned in the core's own suites instead.
TEST_CASE("EditorView marker verbs read only the published target", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;

    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };

    constexpr common::core::GridPosition chorus{.measure = 3, .beat = 1};
    core::EditorViewState state{};
    state.project_loaded = true;
    // A chip and a region drawn SELECTED, with every published verb left at its default nothing:
    // a view that read the selection would have the section chord restate the chip and Enter
    // rename it.
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
    CHECK(press(EditorCommandId::InsertTempoAnchor));
    CHECK(press(EditorCommandId::InsertTimeSignature));
    CHECK(press(EditorCommandId::InsertToneChange));
    CHECK(press(EditorCommandId::RestateSelection));
    CHECK(press(EditorCommandId::RenameSelection));

    CHECK(controller.song_section_select_count == 0);
    CHECK(controller.last_selected_tone_region_id.empty());
    CHECK(controller.last_inserted_song_section_name.empty());
    CHECK(controller.last_created_tone_region_id.empty());
    CHECK(controller.last_renamed_song_section_name.empty());
    CHECK(controller.last_renamed_tone_document_ref.empty());
    CHECK_FALSE(controller.last_inserted_tempo_anchor_position.has_value());
    CHECK_FALSE(controller.last_time_signature_set_position.has_value());
    CHECK(juce::TopLevelWindow::getNumTopLevelWindows() == windows_before);
}

// The tempo chord's insert has nothing to prompt for: with an insert published, Ctrl+B forwards the
// published beat. Ctrl+Shift+B, the same letter's select half, still jumps to the tempo row.
TEST_CASE("EditorView forwards the tempo chord's published insert", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };

    constexpr common::core::GridPosition beat{.measure = 2, .beat = 3};
    core::EditorViewState state{};
    state.project_loaded = true;
    state.tempo_chord_target = core::InsertTempoAnchorTarget{.position = beat};
    view.setState(state);

    const juce::KeyPress ctrl_b{'b', juce::ModifierKeys{juce::ModifierKeys::commandModifier}, 0};
    CHECK(pressCommandKey(view, ctrl_b));
    CHECK(controller.last_inserted_tempo_anchor_position == std::optional{beat});

    const juce::KeyPressMappingSet* const mappings = view.commandManager().getKeyMappings();
    REQUIRE(mappings != nullptr);
    const juce::KeyPress ctrl_shift_b{
        'b',
        juce::ModifierKeys{juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier},
        0
    };
    CHECK(
        mappings->findCommandForKeyPress(ctrl_shift_b) ==
        toJuceCommandId(EditorCommandId::CaretJumpTempoRow));
}

// The tempo chord over an anchor opens the tempo prompt, prefilled with the span tempo the core
// published; accepting it hands the raw text back for the core to parse.
TEST_CASE("EditorView opens the tempo prompt the tempo chord publishes", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };

    core::EditorViewState state{};
    state.project_loaded = true;
    state.tempo_chord_target = core::SetTempoTarget{.position = g_tempo_anchor, .bpm_text = "137"};
    view.setState(state);

    acceptPrompt(view, EditorCommandId::InsertTempoAnchor, g_tempo_prompt, "140");
    CHECK(controller.last_tempo_span_bpm_position == std::optional{g_tempo_anchor});
    CHECK(controller.last_tempo_span_bpm_text == "140");
    CHECK_FALSE(controller.last_inserted_tempo_anchor_position.has_value());
}

// Enter on a selected tempo chip opens the same prompt through the published restate verb.
TEST_CASE("EditorView opens the tempo prompt Enter's restate publishes", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };

    core::EditorViewState state{};
    state.project_loaded = true;
    state.restate_target = core::SetTempoTarget{.position = g_tempo_anchor, .bpm_text = "137"};
    view.setState(state);

    acceptPrompt(view, EditorCommandId::RestateSelection, g_tempo_prompt, "136.5");
    CHECK(controller.last_tempo_span_bpm_position == std::optional{g_tempo_anchor});
    CHECK(controller.last_tempo_span_bpm_text == "136.5");
}

// The meter chord's insert opens the meter prompt titled as its undo entry, prefilled with the
// meter governing the measure the core published; accepting it hands the raw text back for that
// measure, for the core to parse.
TEST_CASE(
    "EditorView opens the meter prompt the meter chord's insert publishes", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };

    core::EditorViewState state{};
    state.project_loaded = true;
    constexpr common::core::GridPosition beat{.measure = 5, .beat = 3};
    state.time_signature_chord_target =
        core::InsertTimeSignatureTarget{.position = beat, .meter_text = "4/4"};
    view.setState(state);

    acceptPrompt(
        view,
        EditorCommandId::InsertTimeSignature,
        ShownPrompt{.title = "Add Time Signature", .text = "4/4"},
        "7/8");
    CHECK(controller.last_time_signature_set_position == std::optional{beat});
    CHECK(controller.last_time_signature_set_text == "7/8");
}

// Over a measure whose downbeat carries a time signature, the meter chord restates it: the same
// prompt, titled as the restate's undo entry and prefilled with the meter standing there.
TEST_CASE(
    "EditorView opens the meter prompt the meter chord's restate publishes", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };

    core::EditorViewState state{};
    state.project_loaded = true;
    constexpr common::core::GridPosition start{.measure = 3, .beat = 1};
    state.time_signature_chord_target =
        core::SetTimeSignatureTarget{.position = start, .meter_text = "3/4"};
    view.setState(state);

    acceptPrompt(
        view,
        EditorCommandId::InsertTimeSignature,
        ShownPrompt{.title = "Set Time Signature", .text = "3/4"},
        "6/8");
    CHECK(controller.last_time_signature_set_position == std::optional{start});
    CHECK(controller.last_time_signature_set_text == "6/8");
}

// Enter on a selected time-signature chip opens the restate prompt through the published restate
// verb; a chip double-click asks for that same verb (the ruler's own suite pins that half).
TEST_CASE("EditorView opens the meter prompt Enter's restate publishes", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };

    core::EditorViewState state{};
    state.project_loaded = true;
    constexpr common::core::GridPosition start{.measure = 7, .beat = 1};
    state.restate_target = core::SetTimeSignatureTarget{.position = start, .meter_text = "5/4"};
    view.setState(state);

    acceptPrompt(
        view,
        EditorCommandId::RestateSelection,
        ShownPrompt{.title = "Set Time Signature", .text = "5/4"},
        " 7 / 8 ");
    CHECK(controller.last_time_signature_set_position == std::optional{start});
    CHECK(controller.last_time_signature_set_text == " 7 / 8 ");
}

// Scale Tempo is an Edit-menu verb, greyed while the marker plane is closed (no song, or the
// transport playing), when performing it opens nothing. Open, it raises the factor prompt, empty so
// an accidental Enter scales nothing, and hands the typed text back raw for the core to parse. The
// tempo row's "Scale Tempo..." row runs this same command (the ruler suite pins the row). The
// prompt is titled as the entry it will record: with the Note Lock off, the stretch.
TEST_CASE("EditorView Scale Tempo opens the factor prompt", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };
    const int scale_command = toJuceCommandId(EditorCommandId::ScaleTempo);

    view.setState(core::EditorViewState{});
    const juce::PopupMenu::Item closed_item =
        requiredMenuItem(view.getMenuForIndex(1, "Edit"), scale_command);
    CHECK(closed_item.text == "Scale Tempo...");
    CHECK_FALSE(closed_item.isEnabled);
    const juce::ApplicationCommandTarget::InvocationInfo info{scale_command};
    CHECK(view.perform(info));
    CHECK(juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0);

    core::EditorViewState state = makeLoadedEditorState(20.0);
    state.marker_edits_enabled = true;
    view.setState(state);
    CHECK(requiredMenuItem(view.getMenuForIndex(1, "Edit"), scale_command).isEnabled);

    REQUIRE_FALSE(state.note_lock);
    acceptPrompt(
        view,
        EditorCommandId::ScaleTempo,
        ShownPrompt{.title = "Scale Tempo (notes follow the beat)", .text = ""},
        "3/2");
    CHECK(controller.last_scale_tempo_text == std::optional<std::string>{"3/2"});
}

// With the Note Lock on the same command opens the same prompt titled as the relabel, so the
// charter reads which form the factor will take before typing it.
TEST_CASE("EditorView titles the Scale Tempo prompt by the Note Lock", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };

    core::EditorViewState state = makeLoadedEditorState(20.0);
    state.marker_edits_enabled = true;
    state.note_lock = true;
    view.setState(state);

    acceptPrompt(
        view,
        EditorCommandId::ScaleTempo,
        ShownPrompt{.title = "Scale Tempo (notes keep their time)", .text = ""},
        "2");
    CHECK(controller.last_scale_tempo_text == std::optional<std::string>{"2"});
}

// The fret-hand position chord has nothing to prompt for, so no verb is published for it: the view
// forwards the press as it stands and the core decides what it does.
TEST_CASE("EditorView forwards the fret-hand position chord to the core", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;

    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };
    const juce::ApplicationCommandTarget::InvocationInfo info{static_cast<juce::CommandID>(
        EditorCommandId::InsertFretHandPosition)};
    CHECK(view.perform(info));
    CHECK(controller.fret_hand_position_chord_count == 1);
    CHECK_FALSE(controller.last_selected_fret_hand_position.has_value());
}

// Alt+Delete forwards the end-fret clear only while something is selected, as Delete does; the core
// decides whether the selection is a placement with an authored end to clear.
TEST_CASE("EditorView forwards the hand end clear while a selection stands", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;

    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };
    const juce::ApplicationCommandTarget::InvocationInfo info{static_cast<juce::CommandID>(
        EditorCommandId::ClearHandPositionEndFret)};
    CHECK(view.perform(info));
    CHECK(controller.fret_hand_position_end_clear_count == 0);

    core::EditorViewState state;
    state.selection_present = true;
    view.setState(state);
    CHECK(view.perform(info));
    CHECK(controller.fret_hand_position_end_clear_count == 1);
}

// Prune Hand Positions is an Edit-menu verb with Scale Tempo's gate: greyed, and forwarding
// nothing, while the marker plane is closed; open, it forwards the prune for the core to decide.
TEST_CASE("EditorView Prune Hand Positions forwards the prune", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };
    const int prune_command = toJuceCommandId(EditorCommandId::PruneHandPositions);
    const juce::ApplicationCommandTarget::InvocationInfo info{prune_command};

    view.setState(core::EditorViewState{});
    const juce::PopupMenu::Item closed_item =
        requiredMenuItem(view.getMenuForIndex(1, "Edit"), prune_command);
    CHECK(closed_item.text == "Prune Hand Positions");
    CHECK_FALSE(closed_item.isEnabled);
    CHECK(view.perform(info));
    CHECK(controller.fret_hand_positions_prune_count == 0);

    core::EditorViewState state = makeLoadedEditorState(20.0);
    state.marker_edits_enabled = true;
    view.setState(state);
    CHECK(requiredMenuItem(view.getMenuForIndex(1, "Edit"), prune_command).isEnabled);
    CHECK(view.perform(info));
    CHECK(controller.fret_hand_positions_prune_count == 1);
}

// Verifies the arrangement thumbnail is created and later pointed at pushed audio.
TEST_CASE("EditorView applies arrangement audio to the thumbnail", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;

    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };

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
                    .unavailable_reason = std::nullopt,
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
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };

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
                    .unavailable_reason = std::nullopt,
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
        {.id = EditorCommandId::InsertTempoAnchor,
         .value = 0x1407,
         .chords = {chord('b', command)}},
        {.id = EditorCommandId::InsertTimeSignature,
         .value = 0x1408,
         .chords = {chord('/', command)}},
        {.id = EditorCommandId::ScaleTempo, .value = 0x1409, .chords = {}},
        {.id = EditorCommandId::InsertFretHandPosition,
         .value = 0x1406,
         .chords = {chord('p', command)}},
        {.id = EditorCommandId::PruneHandPositions, .value = 0x140A, .chords = {}},
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
        {.id = EditorCommandId::CaretJumpFretHandPositionRow,
         .value = 0x1516,
         .chords = {chord('p', command | shift)}},
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
        {.id = EditorCommandId::ClearHandPositionEndFret,
         .value = 0x1612,
         .chords = {chord(juce::KeyPress::deleteKey, alt)}},
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
        {.id = EditorCommandId::InsertAtCaret,
         .value = 0x1707,
         .chords = {chord(juce::KeyPress::insertKey)}},
        {.id = EditorCommandId::InsertRingPoint,
         .value = 0x171C,
         .chords = {chord(juce::KeyPress::insertKey, alt)}},
        {.id = EditorCommandId::ChartPickSlideToggle,
         .value = 0x1709,
         .chords = {chord('x', shift)}},
        {.id = EditorCommandId::ChartLegatoToggle, .value = 0x170A, .chords = {chord('l')}},
        {.id = EditorCommandId::ChartJunctionToggle,
         .value = 0x1713,
         .chords = {chord('l', shift)}},
        {.id = EditorCommandId::ChartLeftTapToggle, .value = 0x170B, .chords = {chord('t', shift)}},
        {.id = EditorCommandId::ChartTapToggle, .value = 0x1715, .chords = {chord('t')}},
        {.id = EditorCommandId::ChartSlapToggle, .value = 0x1716, .chords = {chord('s')}},
        {.id = EditorCommandId::ChartPopToggle, .value = 0x1717, .chords = {chord('p')}},
        {.id = EditorCommandId::ChartPalmMuteToggle, .value = 0x170C, .chords = {chord('m')}},
        {.id = EditorCommandId::ChartDeadNoteToggle, .value = 0x170D, .chords = {chord('x')}},
        {.id = EditorCommandId::ChartAccentToggle, .value = 0x170E, .chords = {chord('a')}},
        {.id = EditorCommandId::ChartGhostToggle, .value = 0x170F, .chords = {chord('g')}},
        {.id = EditorCommandId::ChartHarmonic, .value = 0x1718, .chords = {chord('h')}},
        {.id = EditorCommandId::ChartBend, .value = 0x171D, .chords = {chord('b')}},
        {.id = EditorCommandId::ChartRingBend, .value = 0x171E, .chords = {chord('b', alt)}},
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
        {.id = EditorCommandId::TypeRingDigit0, .value = 0x1815, .chords = {chord('0', alt)}},
        {.id = EditorCommandId::TypeDigit1,
         .value = 0x1802,
         .chords = {chord('1'), chord(juce::KeyPress::numberPad1)}},
        {.id = EditorCommandId::TypeRingDigit1, .value = 0x1816, .chords = {chord('1', alt)}},
        {.id = EditorCommandId::TypeDigit2,
         .value = 0x1803,
         .chords = {chord('2'), chord(juce::KeyPress::numberPad2)}},
        {.id = EditorCommandId::TypeRingDigit2, .value = 0x1817, .chords = {chord('2', alt)}},
        {.id = EditorCommandId::TypeDigit3,
         .value = 0x1804,
         .chords = {chord('3'), chord(juce::KeyPress::numberPad3)}},
        {.id = EditorCommandId::TypeRingDigit3, .value = 0x1818, .chords = {chord('3', alt)}},
        {.id = EditorCommandId::TypeDigit4,
         .value = 0x1805,
         .chords = {chord('4'), chord(juce::KeyPress::numberPad4)}},
        {.id = EditorCommandId::TypeRingDigit4, .value = 0x1819, .chords = {chord('4', alt)}},
        {.id = EditorCommandId::TypeDigit5,
         .value = 0x1806,
         .chords = {chord('5'), chord(juce::KeyPress::numberPad5)}},
        {.id = EditorCommandId::TypeRingDigit5, .value = 0x181A, .chords = {chord('5', alt)}},
        {.id = EditorCommandId::TypeDigit6,
         .value = 0x1807,
         .chords = {chord('6'), chord(juce::KeyPress::numberPad6)}},
        {.id = EditorCommandId::TypeRingDigit6, .value = 0x181B, .chords = {chord('6', alt)}},
        {.id = EditorCommandId::TypeDigit7,
         .value = 0x1808,
         .chords = {chord('7'), chord(juce::KeyPress::numberPad7)}},
        {.id = EditorCommandId::TypeRingDigit7, .value = 0x181C, .chords = {chord('7', alt)}},
        {.id = EditorCommandId::TypeDigit8,
         .value = 0x1809,
         .chords = {chord('8'), chord(juce::KeyPress::numberPad8)}},
        {.id = EditorCommandId::TypeRingDigit8, .value = 0x181D, .chords = {chord('8', alt)}},
        {.id = EditorCommandId::TypeDigit9,
         .value = 0x180A,
         .chords = {chord('9'), chord(juce::KeyPress::numberPad9)}},
        {.id = EditorCommandId::TypeRingDigit9, .value = 0x181E, .chords = {chord('9', alt)}},
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
        {.id = EditorCommandId::ToggleTempoLock, .value = 0x1906, .chords = {}},
        {.id = EditorCommandId::ToggleNoteLock, .value = 0x1907, .chords = {}},
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
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };

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

    // Exact matching refuses the Alt-composed neighbor of Ctrl+T and the Shift-composed neighbor
    // of Ctrl+Z (which belongs to Redo).
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
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };

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
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };

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
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };

    core::EditorViewState state;
    state.signal_chain = core::SignalChainViewState{
        .remove_plugins_enabled = true,
        .open_plugins_enabled = true,
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
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };
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
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };
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
// on its own), and not the file, history, view, grid, menu or author-chord commands, nor Delete,
// which destroys what it acted on and is kept in sight after it lands, as undo is.
TEST_CASE("Command registry classifies which commands act on the selection", "[ui][editor-view]")
{
    const auto acts = [](const EditorCommandId id) {
        const EditorCommandSpec* const spec = findEditorCommandSpec(toJuceCommandId(id));
        REQUIRE(spec != nullptr);
        return editorCommandActsOnSelection(*spec);
    };
    CHECK(acts(EditorCommandId::SelectionMoveLeft));
    CHECK(acts(EditorCommandId::TypeDigit1));
    CHECK(acts(EditorCommandId::ChartLegatoToggle));
    CHECK(acts(EditorCommandId::RenameSelection));

    CHECK_FALSE(acts(EditorCommandId::CaretStepLeft));
    CHECK_FALSE(acts(EditorCommandId::CaretJumpSectionRow));
    CHECK_FALSE(acts(EditorCommandId::InsertSongSection));
    CHECK_FALSE(acts(EditorCommandId::InsertTempoAnchor));
    CHECK_FALSE(acts(EditorCommandId::InsertTimeSignature));
    CHECK_FALSE(acts(EditorCommandId::ScaleTempo));
    CHECK_FALSE(acts(EditorCommandId::PruneHandPositions));
    CHECK_FALSE(acts(EditorCommandId::InsertToneChange));
    CHECK_FALSE(acts(EditorCommandId::InsertFretHandPosition));
    CHECK_FALSE(acts(EditorCommandId::SaveProject));
    CHECK_FALSE(acts(EditorCommandId::Undo));
    CHECK_FALSE(acts(EditorCommandId::TogglePreview3D));
    CHECK_FALSE(acts(EditorCommandId::ToggleGridSnap));
    CHECK_FALSE(acts(EditorCommandId::PlayPause));
    CHECK_FALSE(acts(EditorCommandId::ImportTone));
    CHECK_FALSE(acts(EditorCommandId::OpenFileMenu));
    CHECK_FALSE(acts(EditorCommandId::CancelDismiss));
    CHECK_FALSE(acts(EditorCommandId::SelectionDelete));
}

// The tempo lock button renders the published lock — ringed while on, dimmed with no song — and
// a click runs the Tempo Lock command, the same one the menu row ticks.
TEST_CASE(
    "EditorView tempo lock button reflects the lock and toggles through its command",
    "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };
    view.setBounds(0, 0, 1280, 800);

    auto& button = findRequiredDescendant<ModeToggleButton>(view, "tempo_lock_button");
    const auto command_ticked = [&view] {
        const juce::CommandID id = toJuceCommandId(EditorCommandId::ToggleTempoLock);
        juce::ApplicationCommandInfo info{id};
        view.getCommandInfo(id, info);
        return (info.flags & juce::ApplicationCommandInfo::isTicked) != 0;
    };

    view.setState(core::EditorViewState{});
    CHECK_FALSE(button.isEnabled());

    core::EditorViewState state = makeLoadedEditorState(20.0);
    REQUIRE(state.tempo_lock);
    view.setState(state);
    CHECK(button.isEnabled());
    CHECK(button.getToggleState());
    CHECK(command_ticked());

    state.tempo_lock = false;
    view.setState(state);
    CHECK_FALSE(button.getToggleState());
    CHECK_FALSE(command_ticked());

    // The click asks; it never flips the button itself, which waits for the published state.
    testing::clickButton(button);
    CHECK(controller.tempo_lock_toggle_count == 1);
    CHECK_FALSE(button.getToggleState());
}

// The Note Lock button renders the published switch — off at every open, ringed while
// on, dimmed with no song — and a click runs its command, the one the menu row ticks.
TEST_CASE(
    "EditorView Note Lock button reflects the switch and toggles through its command",
    "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };
    view.setBounds(0, 0, 1280, 800);

    auto& button = findRequiredDescendant<ModeToggleButton>(view, "note_lock_button");
    const auto command_ticked = [&view] {
        const juce::CommandID id = toJuceCommandId(EditorCommandId::ToggleNoteLock);
        juce::ApplicationCommandInfo info{id};
        view.getCommandInfo(id, info);
        return (info.flags & juce::ApplicationCommandInfo::isTicked) != 0;
    };

    view.setState(core::EditorViewState{});
    CHECK_FALSE(button.isEnabled());

    core::EditorViewState state = makeLoadedEditorState(20.0);
    REQUIRE_FALSE(state.note_lock);
    view.setState(state);
    CHECK(button.isEnabled());
    CHECK_FALSE(button.getToggleState());
    CHECK_FALSE(command_ticked());

    state.note_lock = true;
    view.setState(state);
    CHECK(button.getToggleState());
    CHECK(command_ticked());

    // The click asks; it never flips the button itself, which waits for the published state.
    testing::clickButton(button);
    CHECK(controller.note_lock_toggle_count == 1);
    CHECK(button.getToggleState());
}

// The startup restore raises the interrupted-restore prompt before the main window shows; it opens
// once the view is on screen to own it, never behind the window.
TEST_CASE("EditorView opens the interrupted-restore prompt once on screen", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };
    view.setBounds(0, 0, 1280, 800);

    core::EditorViewState state;
    state.restore_interrupted_prompt = core::RestoreInterruptedPrompt{"song.rhp"};
    view.setState(state);
    CHECK(juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0);

    showOnScreen(view);
    const juce::Component* const prompt =
        juce::ModalComponentManager::getInstance()->getModalComponent(0);
    REQUIRE(prompt != nullptr);
    CHECK(prompt->getName() == "Project did not finish opening");
}

// The View menu's highway pickers list what the registry holds, tick what the pushed settings
// resolve to, and hand each pick to the controller: the palette's top entry names the palette it
// resolves to and unpins. The lane-count picker hands its picks over the same way, and each
// reading aid is ticked while it draws and asks for the opposite.
TEST_CASE("EditorView lists the highway themes and palettes to pick", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };
    // The items of one View submenu, in order, separators included.
    const auto submenu_items = [&view](const juce::String& name) {
        std::vector<juce::PopupMenu::Item> items;
        const juce::PopupMenu menu = view.getMenuForIndex(2, "View");
        juce::PopupMenu::MenuItemIterator top_level{menu};
        while (top_level.next())
        {
            const juce::PopupMenu::Item& item = top_level.getItem();
            if (item.text != name || item.subMenu == nullptr)
            {
                continue;
            }
            juce::PopupMenu::MenuItemIterator entries{*item.subMenu};
            while (entries.next())
            {
                items.push_back(entries.getItem());
            }
        }
        return items;
    };

    // Every built-in lists, so the default theme's entries are found rather than counted.
    const common::ui::HighwayThemeRegistry& registry = fixtureHighwayThemeRegistry();
    const std::span<const common::ui::LoadedHighwayTheme> listed_themes = registry.themes();
    const auto default_theme = static_cast<std::size_t>(std::distance(
        listed_themes.begin(),
        std::ranges::find(
            listed_themes,
            common::ui::g_default_highway_theme_id,
            &common::ui::LoadedHighwayTheme::id)));
    REQUIRE(default_theme < listed_themes.size());
    const std::vector<std::reference_wrapper<const common::ui::HighwayThemePalette>>
        listed_palettes = registry.palettes();
    const auto default_palette = static_cast<std::size_t>(std::distance(
        listed_palettes.begin(),
        std::ranges::find_if(listed_palettes, [](const common::ui::HighwayThemePalette& palette) {
            return palette.source_id == common::ui::g_default_highway_theme_id;
        })));
    REQUIRE(default_palette < listed_palettes.size());
    const juce::String default_name =
        juce::String::fromUTF8(listed_themes[default_theme].palette.name.c_str());

    view.setState(core::EditorViewState{});
    const std::vector<juce::PopupMenu::Item> themes = submenu_items("Highway Theme");
    REQUIRE(themes.size() == listed_themes.size());
    for (std::size_t index = 0; index < themes.size(); ++index)
    {
        CHECK(themes[index].text == juce::String::fromUTF8(listed_themes[index].name.c_str()));
        CHECK(themes[index].isTicked == (index == default_theme));
    }
    std::vector<juce::PopupMenu::Item> palettes = submenu_items("String Colors");
    REQUIRE(palettes.size() == listed_palettes.size() + 2);
    CHECK(palettes[0].text == "Theme's choice (" + default_name + ")");
    CHECK(palettes[0].isTicked);
    CHECK(palettes[1].isSeparator);
    const juce::PopupMenu::Item& default_palette_item = palettes[default_palette + 2];
    CHECK(default_palette_item.text == default_name);
    CHECK_FALSE(default_palette_item.isTicked);

    REQUIRE(themes[default_theme].action != nullptr);
    themes[default_theme].action();
    CHECK(
        controller.last_highway_theme_id ==
        std::optional<std::string>{std::string{common::ui::g_default_highway_theme_id}});
    REQUIRE(default_palette_item.action != nullptr);
    default_palette_item.action();
    CHECK(
        controller.last_pinned_palette_id == std::optional<std::optional<std::string>>{
                                                 std::string{common::ui::g_default_highway_theme_id}
                                             });

    core::EditorViewState pinned;
    pinned.highway_settings.pinned_palette_id = std::string{common::ui::g_default_highway_theme_id};
    view.setState(pinned);
    palettes = submenu_items("String Colors");
    REQUIRE(palettes.size() == listed_palettes.size() + 2);
    CHECK_FALSE(palettes[0].isTicked);
    CHECK(palettes[default_palette + 2].isTicked);
    REQUIRE(palettes[0].action != nullptr);
    palettes[0].action();
    CHECK(
        controller.last_pinned_palette_id ==
        std::optional<std::optional<std::string>>{std::optional<std::string>{}});

    // "Match Chart", then a minimum of 4 strings and up.
    const std::vector<juce::PopupMenu::Item> lane_counts = submenu_items("Tablature Strings");
    REQUIRE(lane_counts.size() >= 3);
    CHECK(lane_counts[0].text == "Match Chart");
    CHECK(lane_counts[0].isTicked);
    CHECK(lane_counts[2].text == "5 Strings");
    REQUIRE(lane_counts[2].action != nullptr);
    lane_counts[2].action();
    CHECK(controller.last_tab_minimum_displayed_strings == std::optional<int>{5});

    // The view toggles, in the menu's order, under settings with only the rotation turned off.
    core::EditorViewState rotation_off;
    rotation_off.highway_settings.view.show_note_rotation = false;
    view.setState(rotation_off);
    const std::vector<juce::PopupMenu::Item> toggles = submenu_items("Highway Display");
    REQUIRE(toggles.size() == 4);
    CHECK(toggles[0].text == "Invert Strings");
    CHECK_FALSE(toggles[0].isTicked);
    CHECK(toggles[1].text == "Note Targets");
    CHECK(toggles[1].isTicked);
    CHECK(toggles[2].text == "Note Rotation");
    CHECK_FALSE(toggles[2].isTicked);
    CHECK(toggles[3].text == "Arpeggio Brackets");
    CHECK(toggles[3].isTicked);
    using RequestedFlag = core::testing::RecordingEditorController::HighwayViewFlagChange;
    REQUIRE(toggles[0].action != nullptr);
    toggles[0].action();
    CHECK(
        controller.last_highway_view_flag ==
        std::optional{RequestedFlag{
            .flag = &common::core::HighwayViewPreferences::invert_string_order, .on = true
        }});
    REQUIRE(toggles[2].action != nullptr);
    toggles[2].action();
    CHECK(
        controller.last_highway_view_flag ==
        std::optional{RequestedFlag{
            .flag = &common::core::HighwayViewPreferences::show_note_rotation, .on = true
        }});
    REQUIRE(toggles[3].action != nullptr);
    toggles[3].action();
    CHECK(
        controller.last_highway_view_flag ==
        std::optional{RequestedFlag{
            .flag = &common::core::HighwayViewPreferences::show_arpeggio_brackets, .on = false
        }});
}

} // namespace rock_hero::editor::ui
