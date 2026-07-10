#include <compare>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>

namespace rock_hero::editor::core
{

// Persisted normalization metadata is forwarded to the audio backend through the loaded session.
TEST_CASE("EditorController forwards normalization to audio backend", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    FakeProjectServices project_services;
    common::core::Song song = makeSong(std::filesystem::path{"song.wav"});
    song.arrangements.front().audio_asset.normalization = makeCurrentNormalization();
    project_services.next_song = std::move(song);
    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    REQUIRE(audio.last_active_audio_asset.has_value());
    if (audio.last_active_audio_asset.has_value())
    {
        REQUIRE(audio.last_active_audio_asset->normalization.has_value());
        if (audio.last_active_audio_asset->normalization.has_value())
        {
            CHECK(std::is_eq(audio.last_active_audio_asset->normalization->gain_db <=> -4.0));
        }
    }
}

// A failed project-audio activation leaves the session unchanged and surfaces an error.
TEST_CASE("EditorController failed activation preserves session", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    REQUIRE(loadArrangement(
        controller,
        project_services,
        audio,
        std::filesystem::path{"old.wav"},
        loadedTimelineRange(6.0)));
    audio.next_set_active_arrangement_result = false;
    project_services.next_song = makeSong(std::filesystem::path{"new.wav"});
    FakeEditorView view;
    controller.attachView(view);

    controller.onOpenRequested(std::filesystem::path{"new.rhp"});

    const common::core::Session& session = controller.session();
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(
        session.currentArrangement()->audio_asset == common::core::AudioAsset{
                                                         .path = std::filesystem::path{"old.wav"},
                                                         .normalization = std::nullopt,
                                                         .start_offset = {},
                                                     });
    CHECK(session.timeline() == loadedTimelineRange(6.0));
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.project_loaded == true);
    }
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(
        view.shown_errors.back() ==
        "Could not load audio from: new.rhp: Configured active-arrangement failure");
}

// A successful open stores the selected audio without replaying a prior error.
TEST_CASE("EditorController successful open stores audio", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"first.wav"});
    audio.next_set_active_arrangement_result = false;
    controller.onOpenRequested(std::filesystem::path{"first.rhp"});
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(
        view.shown_errors.back() ==
        "Could not load audio from: first.rhp: Configured active-arrangement failure");
    const int pushes_before_success = view.set_state_call_count;

    audio.next_set_active_arrangement_result = true;
    const common::core::AudioAsset replacement{
        .path = std::filesystem::path{"second.wav"},
        .normalization = std::nullopt,
        .start_offset = {}
    };
    project_services.next_song = makeSong(replacement.path, loadedTimelineRange(4.0));
    controller.onOpenRequested(std::filesystem::path{"second.rhp"});

    const common::core::Session& session = controller.session();
    CHECK(audio.set_active_arrangement_call_count == 2);
    CHECK(audio.last_active_audio_asset == std::optional{replacement});
    CHECK(controller.currentProjectFile() == std::optional{std::filesystem::path{"second.rhp"}});
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(session.currentArrangement()->audio_asset == replacement);
    CHECK(session.currentArrangement()->audio_duration == common::core::TimeDuration{4.0});
    CHECK(session.timeline() == loadedTimelineRange(4.0));
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.arrangement.audio_asset == std::optional{replacement});
        CHECK(state.save_enabled == true);
        CHECK(state.save_as_enabled == true);
        CHECK(state.publish_enabled == true);
        CHECK(state.suggested_publish_file == std::filesystem::path{"second.rock"});
        CHECK(state.close_enabled == true);
        CHECK(state.project_loaded == true);
        CHECK(state.project_load_id == 1);
        CHECK(state.save_requires_destination == false);
        CHECK_FALSE(state.unsaved_changes_prompt.has_value());
    }
    // Each open produces the busy-begin push, the tone-bearing rig-load progress pushes (begin,
    // presentation-ready, selection sync), and the completion push. The inline default task
    // runner runs them all synchronously.
    CHECK(view.set_state_call_count == pushes_before_success + 5);
    CHECK(view.shown_errors.size() == 1);
}

// Opening a saved project restores its resume cursor from app-local settings.
TEST_CASE("EditorController open restores settings cursor", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"open_restores_settings_cursor"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    REQUIRE(
        settings.saveProjectCursorPosition(files.projectFile(), common::core::TimePosition{2.75})
            .has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song =
        makeSong(std::filesystem::path{"song.wav"}, loadedTimelineRange(6.0));
    controller.onOpenRequested(files.projectFile());

    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{2.75}});
    CHECK(transport.current_position == common::core::TimePosition{2.75});
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->project_loaded == true);
    CHECK(state->project_load_id == 1);
}

// Close stops playback, clears backend audio, and returns the view to an empty project state.
TEST_CASE("EditorController close clears loaded project", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    controller.onCloseRequested();

    CHECK(transport.stop_call_count == 1);
    CHECK(audio.clear_active_arrangement_call_count == 1);
    CHECK_FALSE(controller.currentProjectFile().has_value());
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.save_enabled == false);
        CHECK(state.save_as_enabled == false);
        CHECK(state.publish_enabled == false);
        CHECK(state.suggested_publish_file.empty());
        CHECK(state.close_enabled == false);
        CHECK(state.project_loaded == false);
        CHECK(state.transport.play_pause_enabled == false);
        CHECK(state.visible_timeline == common::core::TimeRange{});
        CHECK_FALSE(state.arrangement.hasAudio());
    }
}

// Closing a saved project stores the current cursor before transport stop resets it.
TEST_CASE("EditorController close stores settings cursor", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"close_stores_settings_cursor"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song =
        makeSong(std::filesystem::path{"song.wav"}, loadedTimelineRange(6.0));
    controller.onOpenRequested(files.projectFile());
    transport.current_position = common::core::TimePosition{3.5};

    controller.onCloseRequested();

    const auto stored_cursor = settings.projectCursorPositionFor(files.projectFile());
    CHECK(stored_cursor == std::optional{common::core::TimePosition{3.5}});
}

// Exiting persists the editor project path before requesting host shutdown.
TEST_CASE("EditorController persists project file on exit", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"persist_loaded_exit"};
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    int exit_call_count = 0;
    std::optional<std::filesystem::path> setting_seen_at_exit{};
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        [&exit_call_count, &setting_seen_at_exit, &settings] {
            setting_seen_at_exit = settings.lastOpenProject();
            ++exit_call_count;
        },
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(files.projectFile());

    controller.onExitRequested();

    CHECK(exit_call_count == 1);
    CHECK(setting_seen_at_exit == std::optional{files.projectFile()});
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
    CHECK_FALSE(controller.currentProjectFile().has_value());
}

// Save writes the currently loaded session song through the injected persistence seam.
TEST_CASE("EditorController save writes current session song", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"save_project_cursor_position"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset audio_asset{
        .path = std::filesystem::path{"song.wav"}, .normalization = std::nullopt, .start_offset = {}
    };
    project_services.next_song = makeSong(audio_asset.path);
    controller.onOpenRequested(files.projectFile());
    transport.current_position = common::core::TimePosition{1.25};

    controller.onSaveRequested();

    CHECK(project_services.save_call_count == 1);
    CHECK(project_services.save_as_call_count == 0);
    CHECK(project_services.last_save_audio_path == std::optional{audio_asset.path});
    const auto stored_cursor = settings.projectCursorPositionFor(files.projectFile());
    CHECK(stored_cursor == std::optional{common::core::TimePosition{1.25}});
    CHECK(view.shown_errors.empty());
}

// Save failures are surfaced without clearing the loaded session.
TEST_CASE("EditorController save failure surfaces an error", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    project_services.next_save_error = std::string{"disk full"};

    controller.onSaveRequested();

    CHECK(project_services.save_call_count == 1);
    REQUIRE(view.states_seen_at_errors.size() == 1);
    REQUIRE(view.states_seen_at_errors.back().has_value());
    const EditorViewState* error_state = stateOrNull(view.states_seen_at_errors.back());
    REQUIRE(error_state != nullptr);
    CHECK_FALSE(error_state->busy.has_value());
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not save: disk full");
}

// Save As failures clear busy before reporting the error and keep the loaded project.
TEST_CASE("EditorController save as failure clears busy first", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    project_services.next_save_as_error = std::string{"disk full"};

    controller.onSaveAsRequested(std::filesystem::path{"renamed.rhp"});

    CHECK(project_services.save_as_call_count == 1);
    REQUIRE(view.states_seen_at_errors.size() == 1);
    REQUIRE(view.states_seen_at_errors.back().has_value());
    const EditorViewState* error_state = stateOrNull(view.states_seen_at_errors.back());
    REQUIRE(error_state != nullptr);
    CHECK_FALSE(error_state->busy.has_value());
    CHECK(error_state->project_loaded == true);
    CHECK(controller.currentProjectFile() == std::optional{std::filesystem::path{"song.rhp"}});
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not save as: disk full");
}

// Publish writes a native song package copy without changing save-destination state.
TEST_CASE("EditorController publish writes package copy", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
            .publish_function = project_services.publishFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset audio_asset{
        .path = std::filesystem::path{"song.wav"}, .normalization = std::nullopt, .start_offset = {}
    };
    project_services.next_song = makeSong(audio_asset.path);
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    controller.onPublishRequested(std::filesystem::path{"song.rock"});

    CHECK(project_services.publish_call_count == 1);
    CHECK(project_services.save_as_call_count == 0);
    CHECK(project_services.last_publish_file == std::optional{std::filesystem::path{"song.rock"}});
    CHECK(project_services.last_publish_audio_path == std::optional{audio_asset.path});
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.save_requires_destination == false);
    }
    CHECK(view.shown_errors.empty());
}

// Publish failures surface an error without closing or retargeting the current project.
TEST_CASE("EditorController publish failure surfaces an error", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .publish_function = project_services.publishFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    project_services.next_publish_error = std::string{"disk full"};

    controller.onPublishRequested(std::filesystem::path{"song.rock"});

    CHECK(project_services.publish_call_count == 1);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.publish_enabled == true);
        CHECK(state.close_enabled == true);
    }
    REQUIRE(view.states_seen_at_errors.size() == 1);
    REQUIRE(view.states_seen_at_errors.back().has_value());
    const EditorViewState* error_state = stateOrNull(view.states_seen_at_errors.back());
    REQUIRE(error_state != nullptr);
    CHECK_FALSE(error_state->busy.has_value());
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not publish: disk full");
}

// A failed import leaves the current session unchanged and surfaces an error.
TEST_CASE("EditorController failed import preserves session", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .import_function = project_services.importFunction(),
        }
    };
    REQUIRE(loadArrangement(
        controller,
        project_services,
        audio,
        std::filesystem::path{"old.wav"},
        loadedTimelineRange(6.0)));
    project_services.open_call_count = 0;
    project_services.last_open_file.reset();
    FakeEditorView view;
    controller.attachView(view);

    controller.onImportRequested(std::filesystem::path{"broken.rock"});

    const common::core::Session& session = controller.session();
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(
        session.currentArrangement()->audio_asset == common::core::AudioAsset{
                                                         .path = std::filesystem::path{"old.wav"},
                                                         .normalization = std::nullopt,
                                                         .start_offset = {},
                                                     });
    CHECK(session.timeline() == loadedTimelineRange(6.0));
    CHECK(project_services.import_call_count == 1);
    CHECK(project_services.open_call_count == 0);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.project_loaded == true);
    }
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not import: Import failed");
}

// A successful import stores the imported audio without replaying a prior error.
TEST_CASE("EditorController successful import stores audio", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .import_function = project_services.importFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_import_song = makeSong(std::filesystem::path{"first.ogg"});
    audio.next_set_active_arrangement_result = false;
    controller.onImportRequested(std::filesystem::path{"first.rock"});
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(
        view.shown_errors.back() ==
        "Could not load imported audio from: first.rock: Configured active-arrangement failure");
    const int pushes_before_success = view.set_state_call_count;

    audio.next_set_active_arrangement_result = true;
    const common::core::AudioAsset replacement{
        .path = std::filesystem::path{"imported.ogg"},
        .normalization = std::nullopt,
        .start_offset = {}
    };
    project_services.next_import_song = makeSong(replacement.path, loadedTimelineRange(4.0));
    transport.current_position = common::core::TimePosition{2.5};
    transport.last_seek_position.reset();
    controller.onImportRequested(std::filesystem::path{"second.rock"});

    const common::core::Session& session = controller.session();
    CHECK(project_services.import_call_count == 2);
    CHECK(project_services.open_call_count == 0);
    CHECK(audio.set_active_arrangement_call_count == 2);
    CHECK(audio.last_active_audio_asset == std::optional{replacement});
    CHECK_FALSE(controller.currentProjectFile().has_value());
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(session.currentArrangement()->audio_asset == replacement);
    CHECK(session.currentArrangement()->audio_duration == common::core::TimeDuration{4.0});
    CHECK(session.timeline() == loadedTimelineRange(4.0));
    const common::core::TimePosition imported_timeline_start = session.timeline().start;
    CHECK(transport.current_position == imported_timeline_start);
    CHECK(
        transport.last_seek_position ==
        std::optional<common::core::TimePosition>{imported_timeline_start});
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.arrangement.audio_asset == std::optional{replacement});
        CHECK(state.save_enabled == true);
        CHECK(state.save_as_enabled == true);
        CHECK(state.publish_enabled == true);
        CHECK(state.close_enabled == true);
        CHECK(state.project_loaded == true);
        CHECK(state.project_load_id == 1);
        CHECK(state.save_requires_destination == true);
    }
    // Each import produces the busy-begin push, the tone-bearing rig-load progress pushes
    // (begin, presentation-ready, selection sync), and the completion push. The inline default
    // task runner runs them all synchronously.
    CHECK(view.set_state_call_count == pushes_before_success + 5);
    CHECK(view.shown_errors.size() == 1);
}

// Imported content requires Save As before direct Save can write to a destination.
TEST_CASE("EditorController import requires Save As destination", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset audio_asset{
        .path = std::filesystem::path{"imported.ogg"},
        .normalization = std::nullopt,
        .start_offset = {}
    };
    project_services.next_import_song = makeSong(audio_asset.path);
    controller.onImportRequested(std::filesystem::path{"song.rock"});
    transport.current_position = common::core::TimePosition{2.5};
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& imported_state = view.last_state.value();
        CHECK(imported_state.save_requires_destination == true);
        CHECK(imported_state.publish_enabled == true);
        CHECK(imported_state.suggested_publish_file.empty());
        CHECK(imported_state.close_enabled == true);
        CHECK(imported_state.project_loaded == true);
    }

    controller.onSaveRequested();

    CHECK(project_services.save_call_count == 0);

    controller.onSaveAsRequested(std::filesystem::path{"saved.rhp"});

    CHECK(project_services.save_as_call_count == 1);
    CHECK(project_services.last_save_as_file == std::optional{std::filesystem::path{"saved.rhp"}});
    CHECK(project_services.last_save_as_audio_path == std::optional{audio_asset.path});
    CHECK(controller.currentProjectFile() == std::optional{std::filesystem::path{"saved.rhp"}});
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& saved_state = view.last_state.value();
        CHECK(saved_state.save_requires_destination == false);
        CHECK(saved_state.suggested_publish_file == std::filesystem::path{"saved.rock"});
        CHECK_FALSE(saved_state.unsaved_changes_prompt.has_value());
    }

    controller.onSaveRequested();

    CHECK(project_services.save_call_count == 1);
}

// Unsaved imported content prompts before close and Cancel leaves the project loaded.
TEST_CASE("EditorController prompts before closing unsaved import", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_import_song = makeSong(std::filesystem::path{"imported.ogg"});
    controller.onImportRequested(std::filesystem::path{"song.rock"});

    controller.onCloseRequested();

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& prompt_state = view.last_state.value();
        CHECK(
            prompt_state.unsaved_changes_prompt ==
            std::optional{UnsavedChangesPrompt{EditorActionId::CloseProject}});
    }
    CHECK(audio.clear_active_arrangement_call_count == 0);

    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Cancel);

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& cancel_state = view.last_state.value();
        CHECK_FALSE(cancel_state.unsaved_changes_prompt.has_value());
        CHECK(cancel_state.publish_enabled == true);
        CHECK(cancel_state.close_enabled == true);
        CHECK(cancel_state.project_loaded == true);
        CHECK(cancel_state.save_requires_destination == true);
    }
    CHECK(audio.clear_active_arrangement_call_count == 0);
}

// Discarding a dirty saved project before import still makes the imported project displace it.
TEST_CASE(
    "EditorController discard import reopens dirty displaced project", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.clear();
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const std::filesystem::path existing_project{"existing.rhp"};
    const common::core::AudioAsset original_asset{
        .path = std::filesystem::path{"original.wav"},
        .normalization = std::nullopt,
        .start_offset = {}
    };
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    project_services.next_song = makeSong(original_asset.path);
    controller.onOpenRequested(existing_project);
    REQUIRE(controller.currentProjectFile() == std::optional{existing_project});
    controller.onInputCalibrationRequested();
    const auto calibrated = controller.onInputCalibrationManuallySet(0.0);
    REQUIRE(calibrated.has_value());
    controller.onInputCalibrationDismissed();

    addKnownPlugin(controller);

    const common::core::AudioAsset imported_asset{
        .path = std::filesystem::path{"imported.ogg"},
        .normalization = std::nullopt,
        .start_offset = {}
    };
    project_services.next_import_song = makeSong(imported_asset.path);
    controller.onImportRequested(std::filesystem::path{"song.rock"});

    const EditorViewState* import_prompt_state = stateOrNull(view.last_state);
    REQUIRE(import_prompt_state != nullptr);
    CHECK(
        import_prompt_state->unsaved_changes_prompt ==
        std::optional{UnsavedChangesPrompt{EditorActionId::ImportSong}});

    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Discard);

    CHECK(project_services.import_call_count == 1);
    CHECK_FALSE(controller.currentProjectFile().has_value());
    REQUIRE(controller.session().currentArrangement() != nullptr);
    CHECK(controller.session().currentArrangement()->audio_asset == imported_asset);

    project_services.next_song = makeSong(original_asset.path);
    controller.onCloseRequested();

    const EditorViewState* close_prompt_state = stateOrNull(view.last_state);
    REQUIRE(close_prompt_state != nullptr);
    CHECK(
        close_prompt_state->unsaved_changes_prompt ==
        std::optional{UnsavedChangesPrompt{EditorActionId::CloseProject}});

    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Discard);

    CHECK(project_services.open_call_count == 2);
    CHECK(project_services.last_open_file == std::optional{existing_project});
    CHECK(controller.currentProjectFile() == std::optional{existing_project});
    REQUIRE(controller.session().currentArrangement() != nullptr);
    CHECK(controller.session().currentArrangement()->audio_asset == original_asset);
}

// Choosing Save for an unsaved import asks for a destination, saves, and then closes.
TEST_CASE("EditorController saves prompted import before close", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset audio_asset{
        .path = std::filesystem::path{"imported.ogg"},
        .normalization = std::nullopt,
        .start_offset = {}
    };
    project_services.next_import_song = makeSong(audio_asset.path);
    controller.onImportRequested(std::filesystem::path{"song.rock"});

    controller.onCloseRequested();
    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Save);

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& prompt_state = view.last_state.value();
        CHECK(
            prompt_state.save_as_prompt ==
            std::optional{SaveAsPrompt{EditorActionId::CloseProject}});
    }

    controller.onSaveAsRequested(std::filesystem::path{"saved.rhp"});

    CHECK(project_services.save_as_call_count == 1);
    CHECK(project_services.last_save_as_audio_path == std::optional{audio_asset.path});
    CHECK(audio.clear_active_arrangement_call_count == 1);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& close_state = view.last_state.value();
        CHECK(close_state.publish_enabled == false);
        CHECK(close_state.close_enabled == false);
        CHECK(close_state.project_loaded == false);
        CHECK_FALSE(close_state.arrangement.hasAudio());
    }
}

// Discarding unsaved import changes lets the pending exit request reach the host callback.
TEST_CASE("EditorController prompts before exit with unsaved import", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"discard_unsaved_import_exit"};
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setLastOpenProject(files.projectFile()).has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    int exit_call_count = 0;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        [&exit_call_count] { ++exit_call_count; },
        EditorController::ProjectOperations{
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_import_song = makeSong(std::filesystem::path{"imported.ogg"});
    controller.onImportRequested(std::filesystem::path{"song.rock"});

    controller.onExitRequested();

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& prompt_state = view.last_state.value();
        CHECK(
            prompt_state.unsaved_changes_prompt ==
            std::optional{UnsavedChangesPrompt{EditorActionId::ExitApplication}});
    }
    CHECK(exit_call_count == 0);

    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Discard);

    CHECK(audio.clear_active_arrangement_call_count >= 1);
    CHECK(exit_call_count == 1);
    CHECK_FALSE(settings.lastOpenProject().has_value());
}

// Project packages do not carry editor selection state, so a fresh open defaults to the Lead
// arrangement (here also the first) rather than replaying a saved choice.
TEST_CASE("EditorController defaults open to the Lead arrangement", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };

    const common::core::AudioAsset lead_asset{
        .path = std::filesystem::path{"lead.wav"}, .normalization = std::nullopt, .start_offset = {}
    };
    const common::core::AudioAsset bass_asset{
        .path = std::filesystem::path{"bass.wav"}, .normalization = std::nullopt, .start_offset = {}
    };
    project_services.next_song = makeTwoArrangementSong(lead_asset.path, bass_asset.path);

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    CHECK(audio.last_active_audio_asset == std::optional{lead_asset});
    REQUIRE(controller.session().currentArrangement() != nullptr);
    CHECK(controller.session().currentArrangement()->part == common::core::Part::Lead);
    CHECK(controller.session().currentArrangement()->audio_asset == lead_asset);
}

// The switcher lists arrangements Lead, Rhythm, Bass and a fresh open selects the Lead even when
// the song stores the parts in another order, so the primary guitar always shows first.
TEST_CASE("EditorController orders arrangements and defaults to Lead", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const auto make_arrangement =
        [](std::string id, common::core::Part part, std::filesystem::path path) {
            return common::core::Arrangement{
                .id = std::move(id),
                .part = part,
                .difficulty = common::core::DifficultyRating{},
                .audio_asset =
                    common::core::AudioAsset{
                        .path = std::move(path), .normalization = std::nullopt, .start_offset = {}
                    },
                .audio_duration = common::core::TimeDuration{},
                .tones = {},
                .tone_track = {},
                .tone_automation = {},
                .chart_ref = {},
                .chart = {},
            };
        };

    // Song order is Bass, Rhythm, Lead — the reverse of the desired display order.
    common::core::Song song;
    song.arrangements.push_back(
        make_arrangement(g_bass_arrangement_id, common::core::Part::Bass, "bass.wav"));
    song.arrangements.push_back(make_arrangement(
        "1b2c3d4e-5f6a-4b7c-8d9e-0f1a2b3c4d5e", common::core::Part::Rhythm, "rhythm.wav"));
    song.arrangements.push_back(
        make_arrangement(g_lead_arrangement_id, common::core::Part::Lead, "lead.wav"));
    project_services.next_song = std::move(song);

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    // The Lead is selected by default even though it is stored last.
    REQUIRE(controller.session().currentArrangement() != nullptr);
    CHECK(controller.session().currentArrangement()->part == common::core::Part::Lead);

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const std::vector<ArrangementChoiceViewState>& choices =
            view.last_state->arrangement.choices;
        REQUIRE(choices.size() == 3);
        CHECK(choices[0].label == "Lead");
        CHECK(choices[0].selected);
        CHECK(choices[1].label == "Rhythm");
        CHECK(choices[2].label == "Bass");
    }
}

// A fresh open of a song without a Lead part prefers Rhythm over Bass, so the default falls through
// the preference chain rather than simply picking the first stored arrangement.
TEST_CASE("EditorController defaults to Rhythm when no Lead exists", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const auto make_arrangement =
        [](std::string id, common::core::Part part, std::filesystem::path path) {
            return common::core::Arrangement{
                .id = std::move(id),
                .part = part,
                .difficulty = common::core::DifficultyRating{},
                .audio_asset =
                    common::core::AudioAsset{
                        .path = std::move(path), .normalization = std::nullopt, .start_offset = {}
                    },
                .audio_duration = common::core::TimeDuration{},
                .tones = {},
                .tone_track = {},
                .tone_automation = {},
                .chart_ref = {},
                .chart = {},
            };
        };

    // Bass is stored first, but Rhythm outranks it in the preference chain.
    common::core::Song song;
    song.arrangements.push_back(
        make_arrangement(g_bass_arrangement_id, common::core::Part::Bass, "bass.wav"));
    song.arrangements.push_back(make_arrangement(
        "1b2c3d4e-5f6a-4b7c-8d9e-0f1a2b3c4d5e", common::core::Part::Rhythm, "rhythm.wav"));
    project_services.next_song = std::move(song);

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    REQUIRE(controller.session().currentArrangement() != nullptr);
    CHECK(controller.session().currentArrangement()->part == common::core::Part::Rhythm);
}

// A fresh open restores the arrangement persisted in app-local settings, overriding the default.
TEST_CASE("EditorController open restores persisted arrangement", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"open_restores_persisted_arrangement"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.saveProjectSelectedArrangement(files.projectFile(), g_bass_arrangement_id)
                .has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeTwoArrangementSong(
        std::filesystem::path{"lead.wav"}, std::filesystem::path{"bass.wav"});
    controller.onOpenRequested(files.projectFile());

    REQUIRE(controller.session().currentArrangement() != nullptr);
    CHECK(controller.session().currentArrangement()->part == common::core::Part::Bass);
}

// A persisted id that no longer matches any arrangement falls back to the guitar-forward walk
// (Lead), not raw index 0, and the stale value is left untouched rather than laundered.
TEST_CASE(
    "EditorController open falls back to walk for a stale arrangement", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"open_stale_arrangement_walks"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.saveProjectSelectedArrangement(files.projectFile(), "no-such-arrangement")
                .has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    // Song order is Bass, Lead: raw index 0 is Bass, but the walk prefers Lead.
    const auto make_arrangement =
        [](std::string id, common::core::Part part, std::filesystem::path path) {
            return common::core::Arrangement{
                .id = std::move(id),
                .part = part,
                .difficulty = common::core::DifficultyRating{},
                .audio_asset =
                    common::core::AudioAsset{
                        .path = std::move(path), .normalization = std::nullopt, .start_offset = {}
                    },
                .audio_duration = common::core::TimeDuration{},
                .tones = {},
                .tone_track = {},
                .tone_automation = {},
                .chart_ref = {},
                .chart = {},
            };
        };
    common::core::Song song;
    song.arrangements.push_back(
        make_arrangement(g_bass_arrangement_id, common::core::Part::Bass, "bass.wav"));
    song.arrangements.push_back(
        make_arrangement(g_lead_arrangement_id, common::core::Part::Lead, "lead.wav"));
    project_services.next_song = std::move(song);

    controller.onOpenRequested(files.projectFile());

    REQUIRE(controller.session().currentArrangement() != nullptr);
    CHECK(controller.session().currentArrangement()->part == common::core::Part::Lead);
    CHECK(
        settings.projectSelectedArrangementFor(files.projectFile()) ==
        std::optional<std::string>{"no-such-arrangement"});
}

// Opening never writes the derived default back into settings as if the user chose it.
TEST_CASE(
    "EditorController open does not persist the default arrangement", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"open_no_arrangement_write_back"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeTwoArrangementSong(
        std::filesystem::path{"lead.wav"}, std::filesystem::path{"bass.wav"});
    controller.onOpenRequested(files.projectFile());

    REQUIRE(controller.session().currentArrangement() != nullptr);
    CHECK(controller.session().currentArrangement()->part == common::core::Part::Lead);
    CHECK_FALSE(settings.projectSelectedArrangementFor(files.projectFile()).has_value());
}

// Switching arrangements persists the new choice as app-local view state for the next open.
TEST_CASE("EditorController persists arrangement on switch", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"switch_persists_arrangement"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeTwoArrangementSong(
        std::filesystem::path{"lead.wav"}, std::filesystem::path{"bass.wav"});
    controller.onOpenRequested(files.projectFile());
    REQUIRE(controller.session().currentArrangement() != nullptr);
    REQUIRE(controller.session().currentArrangement()->part == common::core::Part::Lead);

    controller.onArrangementSelected(std::string{g_bass_arrangement_id});

    REQUIRE(controller.session().currentArrangement() != nullptr);
    CHECK(controller.session().currentArrangement()->part == common::core::Part::Bass);
    CHECK(
        settings.projectSelectedArrangementFor(files.projectFile()) ==
        std::optional<std::string>{g_bass_arrangement_id});
}

// Save As keys the displayed arrangement to the chosen path so a pre-first-save selection survives.
TEST_CASE("EditorController persists arrangement on save-as", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"save_as_persists_arrangement"};
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .import_function = project_services.importFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_import_song = makeSong(std::filesystem::path{"imported.ogg"});
    controller.onImportRequested(std::filesystem::path{"song.rock"});
    controller.onSaveAsRequested(files.projectFile());

    CHECK(project_services.save_as_call_count == 1);
    CHECK(
        settings.projectSelectedArrangementFor(files.projectFile()) ==
        std::optional<std::string>{g_lead_arrangement_id});
}

// Opening a project validates every arrangement before the selected arrangement is loaded.
TEST_CASE("EditorController rejects invalid project arrangement audio", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset lead_asset{
        .path = std::filesystem::path{"lead.wav"}, .normalization = std::nullopt, .start_offset = {}
    };
    const common::core::AudioAsset bass_asset{
        .path = std::filesystem::path{"bass.wav"}, .normalization = std::nullopt, .start_offset = {}
    };
    audio.failed_prepare_audio_path = bass_asset.path;
    project_services.next_song = makeTwoArrangementSong(lead_asset.path, bass_asset.path);

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    CHECK(audio.prepared_audio_asset_count == 2);
    CHECK(audio.set_active_arrangement_call_count == 0);
    CHECK(controller.session().currentArrangement() == nullptr);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.project_loaded == false);
    }
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(
        view.shown_errors.back() ==
        "Could not load audio from: song.rhp: Configured song preparation failure for: bass.wav");
}

} // namespace rock_hero::editor::core
