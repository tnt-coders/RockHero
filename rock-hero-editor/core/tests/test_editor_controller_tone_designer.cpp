#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>

namespace rock_hero::editor::core
{

namespace
{

// Builds the standard designer-mode harness controller and enters the resting state through the
// startup path (no last project stored in the null settings).
struct ToneDesignerHarness
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, plugin_host, live_rig),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };

    ToneDesignerHarness()
    {
        // The designer's resting rig is empty; the fake's default one-plugin result would
        // misrepresent the empty-refs load contract.
        live_rig.next_load_result.plugins.clear();
        controller.attachView(view);
        controller.restoreLastOpenProject();
    }

    // Reads the latest pushed view state, failing the test when none exists.
    [[nodiscard]] const EditorViewState& state()
    {
        const auto* const latest = stateOrNull(view.last_state);
        REQUIRE(latest != nullptr);
        return *latest;
    }

    // Dirties the designer document through a committed, undoable output-gain edit.
    void dirtyDocument(double gain_db)
    {
        controller.onOutputGainChanged(gain_db);
    }
};

// Project-mode harness with the calibrated live-input route that chain mutations (and therefore
// tone import) gate on; the plain designer harness never has an input device.
struct CalibratedProjectHarness
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::testing::InMemoryAudioConfigStore store;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };

    CalibratedProjectHarness()
    {
        controller.attachView(view);
    }

    // Reads the latest pushed view state, failing the test when none exists.
    [[nodiscard]] const EditorViewState& state()
    {
        const auto* const latest = stateOrNull(view.last_state);
        REQUIRE(latest != nullptr);
        return *latest;
    }
};

} // namespace

// Startup with no restorable project lands in a clean untitled Tone Designer.
TEST_CASE("Startup without a project enters a clean tone designer", "[core][editor-controller]")
{
    ToneDesignerHarness harness;

    CHECK(harness.live_rig.load_call_count == 1);
    const EditorViewState& state = harness.state();
    CHECK_FALSE(state.project_loaded);
    CHECK(state.tone_designer.active);
    CHECK(state.tone_designer.document_name == "Untitled");
    CHECK_FALSE(state.tone_designer.dirty);
    CHECK_FALSE(state.tone_designer.has_destination);
    CHECK(state.signal_chain.output_gain_controls_enabled);
}

// Closing a project falls back to a fresh clean designer document.
TEST_CASE("Closing a project lands in a clean tone designer", "[core][editor-controller]")
{
    ToneDesignerHarness harness;
    REQUIRE(loadArrangement(
        harness.controller,
        harness.project_services,
        harness.audio,
        std::filesystem::path{"song.wav"}));
    REQUIRE_FALSE(harness.state().tone_designer.active);

    harness.controller.onCloseRequested();

    const EditorViewState& state = harness.state();
    CHECK_FALSE(state.project_loaded);
    CHECK(state.tone_designer.active);
    CHECK(state.tone_designer.document_name == "Untitled");
    CHECK_FALSE(state.tone_designer.dirty);
}

// A committed edit dirties the designer document; saving to a chosen file cleans and associates.
TEST_CASE("Tone designer save-as cleans and associates the document", "[core][editor-controller]")
{
    ToneDesignerHarness harness;
    harness.dirtyDocument(-3.0);
    CHECK(harness.state().tone_designer.dirty);

    harness.controller.onSaveToneAsRequested(std::filesystem::path{"leads/Crunch.rocktone"});

    CHECK(harness.live_rig.export_call_count == 1);
    REQUIRE(harness.live_rig.last_export_request.has_value());
    CHECK(
        harness.live_rig.last_export_request->tone_file_path ==
        std::filesystem::path{"leads/Crunch.rocktone"});
    const EditorViewState& state = harness.state();
    CHECK_FALSE(state.tone_designer.dirty);
    CHECK(state.tone_designer.has_destination);
    CHECK(state.tone_designer.document_name == "Crunch");
}

// Save overwrites the associated file; without an association it does nothing (the view routes
// untitled saves through the Save As chooser instead).
TEST_CASE("Tone designer save requires an association", "[core][editor-controller]")
{
    ToneDesignerHarness harness;
    harness.dirtyDocument(-3.0);

    harness.controller.onSaveToneRequested();
    CHECK(harness.live_rig.export_call_count == 0);

    harness.controller.onSaveToneAsRequested(std::filesystem::path{"A.rocktone"});
    harness.dirtyDocument(-6.0);
    harness.controller.onSaveToneRequested();

    CHECK(harness.live_rig.export_call_count == 2);
    REQUIRE(harness.live_rig.last_export_request.has_value());
    CHECK(
        harness.live_rig.last_export_request->tone_file_path ==
        std::filesystem::path{"A.rocktone"});
    CHECK_FALSE(harness.state().tone_designer.dirty);
}

// Saves move only the clean checkpoint; they never enter the undo history.
TEST_CASE("Tone designer save pushes no undo entry", "[core][editor-controller]")
{
    ToneDesignerHarness harness;
    harness.dirtyDocument(-3.0);
    const bool undo_before_save = harness.state().undo_enabled;

    harness.controller.onSaveToneAsRequested(std::filesystem::path{"A.rocktone"});

    const EditorViewState& state = harness.state();
    CHECK(undo_before_save);
    CHECK(state.undo_enabled == undo_before_save);
    CHECK_FALSE(state.tone_designer.dirty);
}

// Opening a tone file repoints the document and is one undoable entry.
TEST_CASE("Tone designer open replaces the document undoably", "[core][editor-controller]")
{
    ToneDesignerHarness harness;

    harness.controller.onOpenToneFileRequested(std::filesystem::path{"riffs/Lead.rocktone"});

    CHECK(harness.live_rig.replace_call_count == 1);
    CHECK(
        harness.live_rig.last_replace_file_path ==
        std::optional{std::filesystem::path{"riffs/Lead.rocktone"}});
    const EditorViewState& opened = harness.state();
    CHECK(opened.tone_designer.document_name == "Lead");
    CHECK(opened.tone_designer.has_destination);
    CHECK_FALSE(opened.tone_designer.dirty);
    CHECK(opened.undo_enabled);

    harness.controller.onUndoRequested();

    CHECK(harness.live_rig.restore_call_count == 1);
    const EditorViewState& undone = harness.state();
    CHECK(undone.tone_designer.document_name == "Untitled");
    CHECK_FALSE(undone.tone_designer.has_destination);
    // The pre-open document was clean, so undoing the open must land clean too.
    CHECK_FALSE(undone.tone_designer.dirty);
    CHECK(undone.redo_enabled);
}

// Undo across an open lands clean on the previously saved document, and redo returns clean.
TEST_CASE("Undo across a tone open lands clean on the saved file", "[core][editor-controller]")
{
    ToneDesignerHarness harness;
    harness.dirtyDocument(-3.0);
    harness.controller.onSaveToneAsRequested(std::filesystem::path{"A.rocktone"});
    REQUIRE_FALSE(harness.state().tone_designer.dirty);

    harness.controller.onOpenToneFileRequested(std::filesystem::path{"B.rocktone"});
    REQUIRE(harness.state().tone_designer.document_name == "B");
    REQUIRE_FALSE(harness.state().tone_designer.dirty);

    harness.controller.onUndoRequested();
    const EditorViewState& undone = harness.state();
    CHECK(undone.tone_designer.document_name == "A");
    CHECK_FALSE(undone.tone_designer.dirty);

    harness.controller.onRedoRequested();
    const EditorViewState& redone = harness.state();
    CHECK(redone.tone_designer.document_name == "B");
    CHECK_FALSE(redone.tone_designer.dirty);
}

// A failed tone-file open reports the error and leaves the document untouched.
TEST_CASE("Tone designer open failure leaves the document intact", "[core][editor-controller]")
{
    ToneDesignerHarness harness;
    harness.live_rig.next_replace_error = common::audio::LiveRigError{
        common::audio::LiveRigErrorCode::InvalidToneFile, "File is not a tone file"
    };

    harness.controller.onOpenToneFileRequested(std::filesystem::path{"broken.rocktone"});

    const EditorViewState& state = harness.state();
    CHECK(state.tone_designer.document_name == "Untitled");
    CHECK_FALSE(state.tone_designer.dirty);
    CHECK_FALSE(state.undo_enabled);
    REQUIRE_FALSE(harness.view.shown_errors.empty());
    CHECK(harness.view.shown_errors.back() == "Could not open tone file: File is not a tone file");
}

// New resets to an untitled document as one undoable entry.
TEST_CASE("Tone designer new resets to untitled undoably", "[core][editor-controller]")
{
    ToneDesignerHarness harness;
    harness.controller.onSaveToneAsRequested(std::filesystem::path{"A.rocktone"});
    REQUIRE(harness.state().tone_designer.document_name == "A");

    harness.controller.onNewToneRequested();

    CHECK(harness.live_rig.restore_call_count == 1);
    const EditorViewState& fresh = harness.state();
    CHECK(fresh.tone_designer.document_name == "Untitled");
    CHECK_FALSE(fresh.tone_designer.has_destination);
    CHECK_FALSE(fresh.tone_designer.dirty);

    harness.controller.onUndoRequested();
    const EditorViewState& undone = harness.state();
    CHECK(undone.tone_designer.document_name == "A");
    CHECK_FALSE(undone.tone_designer.dirty);
}

// A dirty designer defers a project open behind the unsaved-changes prompt; Discard proceeds.
TEST_CASE("Dirty tone designer defers project open until discarded", "[core][editor-controller]")
{
    ToneDesignerHarness harness;
    harness.dirtyDocument(-3.0);
    harness.project_services.next_song =
        makeSong(std::filesystem::path{"song.wav"}, loadedTimelineRange(), g_tone_document_ref);

    harness.controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    const EditorViewState& prompted = harness.state();
    REQUIRE(prompted.unsaved_changes_prompt.has_value());
    CHECK(prompted.unsaved_changes_prompt->prompted_action == EditorActionId::OpenProject);
    CHECK_FALSE(prompted.project_loaded);

    harness.controller.onUnsavedChangesDecision(UnsavedChangesDecision::Discard);

    const EditorViewState& opened = harness.state();
    CHECK(opened.project_loaded);
    CHECK_FALSE(opened.tone_designer.active);
}

// Save-then-replay on an associated document exports the tone, then opens the project.
TEST_CASE("Dirty tone designer saves before replaying a project open", "[core][editor-controller]")
{
    ToneDesignerHarness harness;
    harness.dirtyDocument(-3.0);
    harness.controller.onSaveToneAsRequested(std::filesystem::path{"A.rocktone"});
    harness.dirtyDocument(-6.0);
    REQUIRE(harness.state().tone_designer.dirty);
    harness.project_services.next_song =
        makeSong(std::filesystem::path{"song.wav"}, loadedTimelineRange(), g_tone_document_ref);

    harness.controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    REQUIRE(harness.state().unsaved_changes_prompt.has_value());

    harness.controller.onUnsavedChangesDecision(UnsavedChangesDecision::Save);

    CHECK(harness.live_rig.export_call_count == 2);
    const EditorViewState& opened = harness.state();
    CHECK(opened.project_loaded);
    CHECK_FALSE(opened.tone_designer.active);
}

// An untitled dirty designer routes the protective save through the tone Save As chooser.
TEST_CASE("Untitled dirty designer replays after the Save As chooser", "[core][editor-controller]")
{
    ToneDesignerHarness harness;
    harness.dirtyDocument(-3.0);
    harness.project_services.next_song =
        makeSong(std::filesystem::path{"song.wav"}, loadedTimelineRange(), g_tone_document_ref);

    harness.controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    REQUIRE(harness.state().unsaved_changes_prompt.has_value());

    harness.controller.onUnsavedChangesDecision(UnsavedChangesDecision::Save);

    // No association: the deferral moves to the Save As chooser instead of saving directly.
    const EditorViewState& choosing = harness.state();
    CHECK(harness.live_rig.export_call_count == 0);
    REQUIRE(choosing.save_as_prompt.has_value());
    CHECK(choosing.save_as_prompt->prompted_action == EditorActionId::OpenProject);

    harness.controller.onSaveToneAsRequested(std::filesystem::path{"A.rocktone"});

    CHECK(harness.live_rig.export_call_count == 1);
    const EditorViewState& opened = harness.state();
    CHECK(opened.project_loaded);
    CHECK_FALSE(opened.tone_designer.active);
}

// Cancelling the protective Save As chooser drops the deferred action and keeps the designer.
TEST_CASE("Cancelling the tone Save As chooser keeps the designer", "[core][editor-controller]")
{
    ToneDesignerHarness harness;
    harness.dirtyDocument(-3.0);

    harness.controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    REQUIRE(harness.state().unsaved_changes_prompt.has_value());
    harness.controller.onUnsavedChangesDecision(UnsavedChangesDecision::Save);
    REQUIRE(harness.state().save_as_prompt.has_value());

    harness.controller.onSaveAsCancelled();

    const EditorViewState& state = harness.state();
    CHECK_FALSE(state.save_as_prompt.has_value());
    CHECK_FALSE(state.unsaved_changes_prompt.has_value());
    CHECK_FALSE(state.project_loaded);
    CHECK(state.tone_designer.active);
    CHECK(state.tone_designer.dirty);
}

// Cancel on the unsaved-changes prompt keeps the dirty designer document as-is.
TEST_CASE("Tone designer prompt cancel keeps the dirty document", "[core][editor-controller]")
{
    ToneDesignerHarness harness;
    harness.dirtyDocument(-3.0);

    harness.controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    REQUIRE(harness.state().unsaved_changes_prompt.has_value());

    harness.controller.onUnsavedChangesDecision(UnsavedChangesDecision::Cancel);

    const EditorViewState& state = harness.state();
    CHECK_FALSE(state.unsaved_changes_prompt.has_value());
    CHECK_FALSE(state.project_loaded);
    CHECK(state.tone_designer.active);
    CHECK(state.tone_designer.dirty);
}

// Export writes the active tone's rig without dirtying the project or touching history.
TEST_CASE("Project tone export is a pure read", "[core][editor-controller]")
{
    ToneDesignerHarness harness;
    REQUIRE(loadArrangement(
        harness.controller,
        harness.project_services,
        harness.audio,
        std::filesystem::path{"song.wav"}));

    harness.controller.onExportToneFileRequested(std::filesystem::path{"Lead.rocktone"});

    CHECK(harness.live_rig.export_call_count == 1);
    REQUIRE(harness.live_rig.last_export_request.has_value());
    CHECK(
        harness.live_rig.last_export_request->tone_file_path ==
        std::filesystem::path{"Lead.rocktone"});
    const EditorViewState& state = harness.state();
    CHECK_FALSE(state.undo_enabled);
    CHECK_FALSE(state.unsaved_changes_prompt.has_value());
}

// Importing over an automation-free tone proceeds silently as one undoable entry. Import shares
// the chain-mutation gates, so the project must carry a calibrated live-input route.
TEST_CASE("Project tone import without automation skips the prompt", "[core][editor-controller]")
{
    CalibratedProjectHarness harness;
    REQUIRE(loadCalibratedArrangement(
        harness.controller,
        harness.project_services,
        harness.audio,
        harness.audio_devices,
        std::filesystem::path{"song.wav"}));

    harness.controller.onImportToneFileRequested(std::filesystem::path{"riffs/Lead.rocktone"});

    CHECK_FALSE(harness.state().tone_import_prompt.has_value());
    CHECK(harness.live_rig.replace_call_count == 1);
    CHECK(
        harness.live_rig.last_replace_file_path ==
        std::optional{std::filesystem::path{"riffs/Lead.rocktone"}});
    CHECK(harness.state().undo_enabled);

    harness.controller.onUndoRequested();
    CHECK(harness.live_rig.restore_call_count == 1);
}

// Importing over a tone with automation confirms first; Cancel leaves everything untouched and
// Import proceeds after acceptance.
TEST_CASE("Project tone import confirms before dropping automation", "[core][editor-controller]")
{
    CalibratedProjectHarness harness;
    // The loaded rig reports a durable id for the fake's default chain instance, and the song
    // carries one automation entry keyed by that id — the work an import would drop.
    harness.live_rig.next_load_result.plugins = {
        common::audio::PluginChainEntry{
            .instance_id = "loaded-instance",
            .plugin_id = "loaded-plugin",
            .name = "Loaded Amp",
            .manufacturer = "Example Audio",
            .format_name = "VST3",
            .category = {},
            .chain_index = 0,
            .display_type_override = {},
        },
    };
    harness.live_rig.next_load_result.tone_chains = {
        common::audio::LoadedToneChainIdentities{
            .tone_document_ref = std::string{g_tone_document_ref},
            .plugins =
                {
                    common::audio::LoadedTonePluginIdentity{
                        .instance_id = "loaded-instance",
                        .stable_id = "durable-1",
                    },
                },
            .summed_reported_latency_seconds = 0.0,
        },
    };
    harness.audio_devices.current_input_identity = makeInputDeviceIdentity();
    harness.project_services.next_song =
        makeSong(std::filesystem::path{"song.wav"}, loadedTimelineRange(), g_tone_document_ref);
    common::core::ToneParameterAutomation automation_entry;
    automation_entry.plugin_id = "durable-1";
    automation_entry.param_id = "0";
    common::core::ToneAutomationPoint point{};
    point.norm_value = 0.5F;
    automation_entry.points = {point};
    harness.project_services.next_song->arrangements.front().tone_automation = {automation_entry};
    harness.controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    REQUIRE(harness.state().project_loaded);
    // Calibrate the input route so the chain-mutation gates (which import shares) pass.
    harness.controller.onInputCalibrationRequested();
    REQUIRE(harness.controller.onInputCalibrationManuallySet(0.0).has_value());
    harness.controller.onInputCalibrationDismissed();

    harness.controller.onImportToneFileRequested(std::filesystem::path{"B.rocktone"});

    const EditorViewState& prompted = harness.state();
    REQUIRE(prompted.tone_import_prompt.has_value());
    CHECK(prompted.tone_import_prompt->automation_parameter_count == 1);
    CHECK(harness.live_rig.replace_call_count == 0);

    harness.controller.onToneImportDecision(ToneImportDecision::Cancel);
    CHECK_FALSE(harness.state().tone_import_prompt.has_value());
    CHECK(harness.live_rig.replace_call_count == 0);

    harness.controller.onImportToneFileRequested(std::filesystem::path{"B.rocktone"});
    REQUIRE(harness.state().tone_import_prompt.has_value());
    harness.controller.onToneImportDecision(ToneImportDecision::Import);

    CHECK(harness.live_rig.replace_call_count == 1);
    CHECK(harness.state().undo_enabled);
}

// Opening a dirty tone document over another tone file defers behind the same prompt.
TEST_CASE("Dirty tone designer defers opening another tone file", "[core][editor-controller]")
{
    ToneDesignerHarness harness;
    harness.dirtyDocument(-3.0);

    harness.controller.onOpenToneFileRequested(std::filesystem::path{"B.rocktone"});

    const EditorViewState& prompted = harness.state();
    REQUIRE(prompted.unsaved_changes_prompt.has_value());
    CHECK(prompted.unsaved_changes_prompt->prompted_action == EditorActionId::OpenToneFile);
    CHECK(harness.live_rig.replace_call_count == 0);

    harness.controller.onUnsavedChangesDecision(UnsavedChangesDecision::Discard);

    CHECK(harness.live_rig.replace_call_count == 1);
    const EditorViewState& opened = harness.state();
    CHECK(opened.tone_designer.document_name == "B");
    CHECK_FALSE(opened.tone_designer.dirty);
}

} // namespace rock_hero::editor::core
