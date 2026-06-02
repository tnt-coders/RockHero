#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>

namespace rock_hero::editor::core
{

// A loaded arrangement with a plugin host enables the add-plugin command.
TEST_CASE("EditorController enables plugin add after load", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& initial_state = view.last_state.value();
        CHECK_FALSE(initial_state.signal_chain.add_plugin_enabled);
        CHECK_FALSE(initial_state.signal_chain.remove_plugins_enabled);
    }

    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& loaded_state = view.last_state.value();
        CHECK(loaded_state.signal_chain.add_plugin_enabled);
        CHECK_FALSE(loaded_state.signal_chain.remove_plugins_enabled);
        CHECK(loaded_state.signal_chain.plugins.empty());
    }
}

// Opening the plugin browser makes it visible from the already-known catalog only.
TEST_CASE("EditorController opens plugin browser catalog", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));

    controller.onPluginBrowserRequested();

    CHECK(plugin_host.known_candidates_call_count == 1);
    CHECK(plugin_host.catalog_scan_call_count == 0);
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK(final_state->plugin_browser.visible);
    CHECK(final_state->plugin_browser.scan_enabled);
    CHECK(final_state->plugin_browser.add_enabled);
    REQUIRE(final_state->plugin_browser.plugins.size() == 1);
    CHECK(final_state->plugin_browser.plugins[0].id == "catalog-plugin-id");
    CHECK(final_state->plugin_browser.plugins[0].name == "Catalog Amp");
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(view.shown_errors.empty());
}

// Rescan is the explicit expensive catalog discovery path behind the browser button.
TEST_CASE("EditorController rescans plugin browser catalog", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    plugin_host.next_known_candidates = {
        common::audio::PluginCandidate{
            .id = "known-plugin-id",
            .name = "Known Amp",
            .manufacturer = "Example Audio",
            .format_name = "VST3",
            .file_path = std::filesystem::path{"known-amp.vst3"},
        },
    };
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));
    controller.onPluginBrowserRequested();

    const EditorViewState* browser_state = stateOrNull(view.last_state);
    REQUIRE(browser_state != nullptr);
    REQUIRE(browser_state->plugin_browser.plugins.size() == 1);
    CHECK(browser_state->plugin_browser.plugins[0].id == "known-plugin-id");

    controller.onPluginCatalogScanRequested();

    CHECK(plugin_host.catalog_scan_call_count == 1);
    CHECK(plugin_host.known_candidates_call_count == 2);
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK(final_state->plugin_browser.visible);
    REQUIRE(final_state->plugin_browser.plugins.size() == 1);
    CHECK(final_state->plugin_browser.plugins[0].id == "catalog-plugin-id");
    CHECK_FALSE(final_state->busy.has_value());
}

// Adding a browser plugin uses the current catalog metadata and appends the selected plugin.
TEST_CASE("EditorController adds a browser plugin", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));
    controller.onPluginBrowserRequested();

    controller.onAddPluginRequested("catalog-plugin-id");

    CHECK(plugin_host.add_call_count == 1);
    CHECK(
        plugin_host.last_added_plugin_candidate ==
        std::optional{plugin_host.next_known_candidates.front()});
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(view.busy_overlay_paint_callback_count == 1);
    CHECK_FALSE(final_state->plugin_browser.visible);
    REQUIRE(final_state->signal_chain.plugins.size() == 1);
    CHECK(final_state->signal_chain.add_plugin_enabled);
    CHECK(final_state->signal_chain.remove_plugins_enabled);
    CHECK(final_state->signal_chain.plugins[0].instance_id == "instance-id");
    CHECK(final_state->signal_chain.plugins[0].plugin_id == "catalog-plugin-id");
    CHECK(final_state->signal_chain.plugins[0].name == "Catalog Amp");
    CHECK(final_state->signal_chain.plugins[0].manufacturer == "Example Audio");
    CHECK(final_state->signal_chain.plugins[0].format_name == "VST3");
    CHECK(final_state->signal_chain.plugins[0].chain_index == 0);
    CHECK(view.shown_errors.empty());
}

// A failed browser plugin add leaves the browser open so the user can retry or pick another.
TEST_CASE("EditorController keeps plugin browser open after add error", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));
    controller.onPluginBrowserRequested();
    plugin_host.next_add_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::PluginLoadFailed,
        "plugin rejected",
    };

    controller.onAddPluginRequested("catalog-plugin-id");

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(final_state->plugin_browser.visible);
    CHECK(final_state->signal_chain.plugins.empty());
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not add plugin: plugin rejected");
    REQUIRE(view.states_seen_at_errors.size() == 1);
    REQUIRE(view.states_seen_at_errors.back().has_value());
    const EditorViewState* error_state = stateOrNull(view.states_seen_at_errors.back());
    REQUIRE(error_state != nullptr);
    CHECK_FALSE(error_state->busy.has_value());
}

// Browser close only hides the window state; it does not discard the current catalog.
TEST_CASE("EditorController closes plugin browser", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));
    controller.onPluginBrowserRequested();

    controller.onPluginBrowserClosed();

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->plugin_browser.visible);
    REQUIRE(final_state->plugin_browser.plugins.size() == 1);
    CHECK(final_state->plugin_browser.plugins[0].id == "catalog-plugin-id");
}

// Catalog scan failures clear busy before the transient error is displayed.
TEST_CASE("EditorController reports plugin catalog scan errors", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));
    plugin_host.next_catalog_scan_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::PluginScanFailed,
        "catalog scanner rejected",
    };

    controller.onPluginBrowserRequested();
    controller.onPluginCatalogScanRequested();

    CHECK(plugin_host.catalog_scan_call_count == 1);
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not scan plugins: catalog scanner rejected");
    REQUIRE(view.states_seen_at_errors.size() == 1);
    REQUIRE(view.states_seen_at_errors.back().has_value());
    const EditorViewState* error_state = stateOrNull(view.states_seen_at_errors.back());
    REQUIRE(error_state != nullptr);
    CHECK_FALSE(error_state->busy.has_value());
    CHECK(error_state->plugin_browser.visible);
}

// Opening a project restores the selected arrangement's tone document through the live rig port.
TEST_CASE("EditorController loads live rig on open", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song =
        makeSong(std::filesystem::path{"song.wav"}, loadedTimelineRange(), g_tone_document_ref);

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    CHECK(live_rig.load_call_count == 1);
    REQUIRE(live_rig.last_load_request.has_value());
    if (live_rig.last_load_request.has_value())
    {
        const auto& load_request = live_rig.last_load_request.value();
        CHECK(load_request.song_directory == std::filesystem::path{"song"});
        CHECK(load_request.tone_document_ref == g_tone_document_ref);
    }

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->signal_chain.plugins.size() == 1);
    CHECK(state->signal_chain.plugins[0].instance_id == "loaded-instance");
    CHECK(state->signal_chain.plugins[0].name == "Loaded Amp");
}

// Project load switches to determinate live-rig progress before restoring saved plugins.
TEST_CASE("EditorController reports live rig plugin load progress", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins = {
        common::audio::LiveRigPlugin{
            .instance_id = "amp-instance",
            .plugin_id = "amp-plugin",
            .name = "Amp Sim",
            .manufacturer = "Example Audio",
            .format_name = "VST3",
            .chain_index = 0,
        },
        common::audio::LiveRigPlugin{
            .instance_id = "cab-instance",
            .plugin_id = "cab-plugin",
            .name = "Cab IR",
            .manufacturer = "Example Audio",
            .format_name = "VST3",
            .chain_index = 1,
        },
    };
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song =
        makeSong(std::filesystem::path{"song.wav"}, loadedTimelineRange(), g_tone_document_ref);

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    const std::vector<BusyViewState> progress_states = liveRigBusyStates(view);
    REQUIRE_FALSE(progress_states.empty());
    CHECK(progress_states.front().message == "Loading live rig...");
    CHECK(progress_states.front().progress == std::optional<double>{0.0});
    CHECK(progress_states.front().indicator == BusyIndicator::DeterminateProgress);
    CHECK(std::ranges::any_of(progress_states, [](const BusyViewState& state) {
        return state.message == "Loading Amp Sim (1 of 2)..." &&
               state.progress == std::optional<double>{0.5};
    }));
    CHECK(std::ranges::any_of(progress_states, [](const BusyViewState& state) {
        return state.message == "Loading Cab IR (2 of 2)..." &&
               state.progress == std::optional<double>{1.0};
    }));
    CHECK(std::ranges::any_of(progress_states, [](const BusyViewState& state) {
        return state.message == "Live rig loaded." && state.progress == std::optional<double>{1.0};
    }));
    CHECK(view.busy_overlay_paint_callback_count == 1);
}

// A live-rig completion delivered after close cannot repopulate project or plugin state.
TEST_CASE(
    "EditorController close during live rig load supersedes open", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.defer_load_completion = true;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song =
        makeSong(std::filesystem::path{"song.wav"}, loadedTimelineRange(), g_tone_document_ref);

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    CHECK(live_rig.load_call_count == 1);
    CHECK_FALSE(controller.currentProjectFile().has_value());
    const EditorViewState* loading_state = stateOrNull(view.last_state);
    REQUIRE(loading_state != nullptr);
    REQUIRE(loading_state->busy.has_value());
    if (loading_state->busy.has_value())
    {
        const auto& busy = loading_state->busy.value();
        CHECK(busy.operation == BusyOperation::LoadingLiveRig);
    }

    controller.onCloseRequested();

    CHECK(live_rig.clear_call_count == 1);
    const EditorViewState* closed_state = stateOrNull(view.last_state);
    REQUIRE(closed_state != nullptr);
    CHECK_FALSE(closed_state->busy.has_value());
    CHECK_FALSE(closed_state->project_loaded);
    CHECK(closed_state->signal_chain.plugins.empty());

    CHECK(live_rig.completePendingLoad());

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    CHECK_FALSE(final_state->project_loaded);
    CHECK(final_state->signal_chain.plugins.empty());
    CHECK_FALSE(controller.currentProjectFile().has_value());
    CHECK(view.shown_errors.empty());
}

// Saving captures the active live rig and writes its document reference into the song.
TEST_CASE("EditorController captures live rig before save", "[core][editor-controller]")
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
            .save_function = project_services.saveFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));

    controller.onSaveRequested();

    CHECK(live_rig.capture_call_count == 1);
    REQUIRE(live_rig.last_capture_request.has_value());
    if (live_rig.last_capture_request.has_value())
    {
        const auto& capture_request = live_rig.last_capture_request.value();
        CHECK(capture_request.song_directory == std::filesystem::path{"song"});
        CHECK(capture_request.arrangement_id == g_lead_arrangement_id);
        CHECK(capture_request.existing_tone_document_ref.empty());
    }
    CHECK(project_services.save_call_count == 1);
    CHECK(
        project_services.last_save_tone_document_ref ==
        std::optional<std::string>{g_tone_document_ref});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->signal_chain.plugins.size() == 1);
    CHECK(state->signal_chain.plugins[0].instance_id == "captured-instance");
    CHECK(state->signal_chain.plugins[0].name == "Captured Amp");
}

// Once tone persistence is available, plugin mutations become unsaved project changes.
TEST_CASE("EditorController plugin add marks tone dirty", "[core][editor-controller]")
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
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));

    addKnownPlugin(controller);
    controller.onCloseRequested();

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->unsaved_changes_prompt.has_value());
    if (state->unsaved_changes_prompt.has_value())
    {
        const auto& prompt = state->unsaved_changes_prompt.value();
        CHECK(prompt.prompted_action == EditorActionId::CloseProject);
    }
}

// Removing a plugin updates runtime state and reindexes the remaining linear chain.
TEST_CASE("EditorController removes a plugin", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));

    plugin_host.next_handle.instance_id = "instance-a";
    plugin_host.next_handle.chain_index = 0;
    addKnownPlugin(controller);
    plugin_host.next_handle.instance_id = "instance-b";
    plugin_host.next_handle.chain_index = 1;
    addKnownPlugin(controller);

    controller.onRemovePluginRequested("instance-a");

    CHECK(plugin_host.remove_call_count == 1);
    CHECK(plugin_host.last_removed_instance_id == std::optional<std::string>{"instance-a"});
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    REQUIRE(final_state->signal_chain.plugins.size() == 1);
    CHECK(final_state->signal_chain.plugins[0].instance_id == "instance-b");
    CHECK(final_state->signal_chain.plugins[0].chain_index == 0);
    CHECK(final_state->signal_chain.remove_plugins_enabled);
    CHECK(view.shown_errors.empty());
}

// A stale UI instance ID is ignored before calling the backend.
TEST_CASE("EditorController ignores stale plugin removal", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));

    addKnownPlugin(controller);
    controller.onRemovePluginRequested("stale-instance");

    CHECK(plugin_host.remove_call_count == 0);
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    REQUIRE(final_state->signal_chain.plugins.size() == 1);
    CHECK(final_state->signal_chain.plugins[0].instance_id == "instance-id");
    CHECK(view.shown_errors.empty());
}

// Opening a plugin window validates the row instance before delegating to the plugin host.
TEST_CASE("EditorController opens plugin windows", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));
    addKnownPlugin(controller);

    controller.onOpenPluginRequested("instance-id");

    CHECK(plugin_host.open_call_count == 1);
    CHECK(plugin_host.last_opened_instance_id == std::optional<std::string>{"instance-id"});
    CHECK(view.shown_errors.empty());
}

// A stale plugin-window row ID is ignored before calling the backend.
TEST_CASE("EditorController ignores stale plugin window requests", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));
    addKnownPlugin(controller);

    controller.onOpenPluginRequested("stale-instance");

    CHECK(plugin_host.open_call_count == 0);
    CHECK(view.shown_errors.empty());
}

// Backend plugin-window failures surface as transient editor errors.
TEST_CASE("EditorController reports plugin window errors", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));
    addKnownPlugin(controller);
    plugin_host.next_open_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::PluginWindowUnavailable,
        "plugin has no editor",
    };

    controller.onOpenPluginRequested("instance-id");

    CHECK(plugin_host.open_call_count == 1);
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not open plugin: plugin has no editor");
}

// Backend removal failures report an error without erasing controller-owned runtime state.
TEST_CASE("EditorController reports plugin remove errors", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));
    addKnownPlugin(controller);
    plugin_host.next_remove_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::PluginInstanceNotFound,
        "backend rejected removal",
    };

    controller.onRemovePluginRequested("instance-id");

    CHECK(plugin_host.remove_call_count == 1);
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    REQUIRE(final_state->signal_chain.plugins.size() == 1);
    CHECK(final_state->signal_chain.plugins[0].instance_id == "instance-id");
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not remove plugin: backend rejected removal");
}

} // namespace rock_hero::editor::core
