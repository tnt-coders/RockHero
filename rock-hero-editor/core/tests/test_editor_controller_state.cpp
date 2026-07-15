#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/editor/core/settings/editor_settings.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string_view>
#include <system_error>

#ifndef TEST_SETTINGS_DIR
#define TEST_SETTINGS_DIR "."
#endif

namespace rock_hero::editor::core
{

namespace
{

// Owns one build-local settings file so grid note-value persistence starts from clean state.
class ScopedControllerSettingsFile final
{
public:
    // Creates a settings-file path and removes any stale file from a prior test run.
    explicit ScopedControllerSettingsFile(std::string_view file_name)
        : m_path(std::filesystem::path{TEST_SETTINGS_DIR} / file_name)
    {
        removeFile();
    }

    // Removes the settings file so persistence tests cannot leak state into later tests.
    ~ScopedControllerSettingsFile()
    {
        removeFile();
    }

    ScopedControllerSettingsFile(const ScopedControllerSettingsFile&) = delete;
    ScopedControllerSettingsFile& operator=(const ScopedControllerSettingsFile&) = delete;
    ScopedControllerSettingsFile(ScopedControllerSettingsFile&&) = delete;
    ScopedControllerSettingsFile& operator=(ScopedControllerSettingsFile&&) = delete;

    // Returns the test-owned settings-file path.
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    // Removes the settings file on a best-effort basis.
    void removeFile() const
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }

    // Build-local settings path owned by this fixture.
    std::filesystem::path m_path;
};

} // namespace

// Verifies editor state represents a single displayed arrangement without extra identity.
TEST_CASE("EditorViewState represents one arrangement", "[core][editor-controller]")
{
    const EditorViewState empty_state{};

    CHECK(empty_state.open_enabled == false);
    CHECK(empty_state.import_enabled == false);
    CHECK(empty_state.save_enabled == false);
    CHECK(empty_state.save_as_enabled == false);
    CHECK(empty_state.publish_enabled == false);
    CHECK(empty_state.undo_enabled == false);
    CHECK_FALSE(empty_state.undo_label.has_value());
    CHECK(empty_state.redo_enabled == false);
    CHECK_FALSE(empty_state.redo_label.has_value());
    CHECK(empty_state.suggested_publish_file.empty());
    CHECK(empty_state.close_enabled == false);
    CHECK(empty_state.project_loaded == false);
    CHECK(empty_state.save_requires_destination == false);
    CHECK(empty_state.transport.play_pause_enabled == false);
    CHECK(empty_state.transport.stop_enabled == false);
    CHECK(empty_state.transport.play_pause_shows_pause_icon == false);
    CHECK(empty_state.audio_device_status_text == "[audio device closed]");
    CHECK_FALSE(empty_state.audio_device_failure_prompt.has_value());
    CHECK(empty_state.visible_timeline == common::core::TimeRange{});
    CHECK(empty_state.grid_note_value == common::core::Fraction{1, 4});
    CHECK_FALSE(empty_state.arrangement.hasAudio());
    CHECK(empty_state.signal_chain.insert_plugin_enabled == false);
    CHECK(empty_state.signal_chain.remove_plugins_enabled == false);
    CHECK(empty_state.signal_chain.plugins.empty());
    CHECK(empty_state.plugin_browser.visible == false);
    CHECK(empty_state.plugin_browser.scan_enabled == false);
    CHECK(empty_state.plugin_browser.add_enabled == false);
    CHECK(empty_state.plugin_browser.plugins.empty());
    CHECK_FALSE(empty_state.unsaved_changes_prompt.has_value());
    CHECK_FALSE(empty_state.save_as_prompt.has_value());
    CHECK_FALSE(empty_state.restore_interrupted_prompt.has_value());

    const common::core::AudioAsset audio_asset{
        .path = std::filesystem::path{"full_mix.wav"},
        .normalization = std::nullopt,
        .start_offset = {}
    };
    const EditorViewState loaded_state{
        .open_enabled = true,
        .import_enabled = true,
        .save_enabled = true,
        .save_as_enabled = true,
        .publish_enabled = true,
        .undo_enabled = true,
        .undo_label = std::string{"Move Plugin"},
        .redo_enabled = true,
        .redo_label = std::string{"Restore Plugin"},
        .suggested_publish_file = std::filesystem::path{"saved.rock"},
        .close_enabled = true,
        .project_loaded = true,
        .save_requires_destination = false,
        .transport =
            TransportViewState{
                .play_pause_enabled = true,
                .stop_enabled = true,
                .play_pause_shows_pause_icon = true,
            },
        .audio_device_status_text = "[48kHz 24bit: 8/8ch 128spls ~5.1/8.5ms ASIO]",
        .visible_timeline = loadedTimelineRange(180.0),
        .arrangement =
            ArrangementViewState{
                .audio_asset = audio_asset,
                .audio_duration = common::core::TimeDuration{180.0},
                .choices = {},
            },
        .signal_chain =
            SignalChainViewState{
                .insert_plugin_enabled = true,
                .remove_plugins_enabled = true,
                .plugins =
                    {
                        PluginViewState{
                            .instance_id = "instance",
                            .plugin_id = "plugin",
                            .name = "Amp Sim",
                            .manufacturer = "Example Audio",
                            .format_name = "VST3",
                            .chain_index = 0,
                        },
                    },
                .disabled_message = {},
            },
        .plugin_browser =
            PluginBrowserViewState{
                .visible = true,
                .scan_enabled = true,
                .add_enabled = true,
                .plugins =
                    {
                        PluginCandidateViewState{
                            .id = "plugin",
                            .name = "Amp Sim",
                            .manufacturer = "Example Audio",
                            .format_name = "VST3",
                            .primary_display_type = PluginDisplayType::Distortion,
                        },
                    },
            },
        .unsaved_changes_prompt = UnsavedChangesPrompt{EditorActionId::CloseProject},
        .save_as_prompt = SaveAsPrompt{EditorActionId::CloseProject},
        .restore_interrupted_prompt =
            RestoreInterruptedPrompt{std::filesystem::path{"interrupted.rhp"}},
        .input_calibration_prompt = std::nullopt,
        .busy = std::nullopt,
    };

    CHECK(loaded_state.arrangement.audio_asset == std::optional{audio_asset});
    CHECK(loaded_state.audio_device_status_text == "[48kHz 24bit: 8/8ch 128spls ~5.1/8.5ms ASIO]");
    CHECK(loaded_state.undo_enabled);
    CHECK(loaded_state.undo_label == std::optional<std::string>{"Move Plugin"});
    CHECK(loaded_state.redo_enabled);
    CHECK(loaded_state.redo_label == std::optional<std::string>{"Restore Plugin"});
    CHECK(loaded_state.arrangement.audioTimelineRange() == loadedTimelineRange(180.0));
    CHECK(loaded_state.arrangement.hasAudio());
    CHECK(loaded_state.signal_chain.insert_plugin_enabled);
    CHECK(loaded_state.signal_chain.remove_plugins_enabled);
    REQUIRE(loaded_state.signal_chain.plugins.size() == 1);
    CHECK(loaded_state.signal_chain.plugins[0].name == "Amp Sim");
    CHECK(loaded_state.plugin_browser.visible);
    CHECK(loaded_state.plugin_browser.scan_enabled);
    CHECK(loaded_state.plugin_browser.add_enabled);
    REQUIRE(loaded_state.plugin_browser.plugins.size() == 1);
    CHECK(loaded_state.plugin_browser.plugins[0].name == "Amp Sim");
    CHECK(
        loaded_state.unsaved_changes_prompt ==
        std::optional{UnsavedChangesPrompt{EditorActionId::CloseProject}});
    CHECK(loaded_state.save_as_prompt == std::optional{SaveAsPrompt{EditorActionId::CloseProject}});
    CHECK(
        loaded_state.restore_interrupted_prompt ==
        std::optional{RestoreInterruptedPrompt{std::filesystem::path{"interrupted.rhp"}}});
}

// Verifies a fake controller can receive editor intents without JUCE callback types.
TEST_CASE("IEditorController fake receives editor intents", "[core][editor-controller]")
{
    testing::RecordingEditorController controller;
    const std::filesystem::path open_file{"song.rhp"};
    const std::filesystem::path import_file{"song.rock"};
    const std::filesystem::path save_as_file{"saved.rhp"};
    const std::filesystem::path publish_file{"saved.rock"};

    controller.onOpenRequested(open_file);
    controller.onImportRequested(import_file);
    controller.onSaveRequested();
    controller.onSaveAsRequested(save_as_file);
    controller.onPublishRequested(publish_file);
    controller.onSaveAsCancelled();
    controller.onBusyCancelRequested();
    controller.onUndoRequested();
    controller.onRedoRequested();
    controller.onCloseRequested();
    controller.onExitRequested();
    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Discard);
    controller.onRestoreInterruptedDecision(RestoreInterruptedDecision::Retry);
    controller.onPlayPausePressed();
    controller.onStopPressed();
    controller.onTimelineSeekRequested(common::core::TimePosition{0.75});
    controller.onPluginBrowserRequested();
    controller.onPluginBrowserClosed();
    controller.onPluginCatalogScanRequested();
    controller.onSelectedPluginInsertRequested("catalog-plugin-id");
    controller.onRemovePluginRequested("instance-id");
    controller.onPluginDisplayTypeOverrideChanged("instance-id", PluginDisplayType::Cab);
    controller.onOpenPluginRequested("instance-id");

    CHECK(controller.open_request_count == 1);
    CHECK(controller.last_open_file == std::optional{open_file});
    CHECK(controller.import_request_count == 1);
    CHECK(controller.last_import_file == std::optional{import_file});
    CHECK(controller.save_request_count == 1);
    CHECK(controller.save_as_request_count == 1);
    CHECK(controller.last_save_as_file == std::optional{save_as_file});
    CHECK(controller.publish_request_count == 1);
    CHECK(controller.last_publish_file == std::optional{publish_file});
    CHECK(controller.save_as_cancel_count == 1);
    CHECK(controller.busy_cancel_request_count == 1);
    CHECK(controller.undo_request_count == 1);
    CHECK(controller.redo_request_count == 1);
    CHECK(controller.close_request_count == 1);
    CHECK(controller.exit_request_count == 1);
    CHECK(controller.unsaved_changes_decision_count == 1);
    CHECK(
        controller.last_unsaved_changes_decision == std::optional{UnsavedChangesDecision::Discard});
    CHECK(controller.restore_interrupted_decision_count == 1);
    CHECK(
        controller.last_restore_interrupted_decision ==
        std::optional{RestoreInterruptedDecision::Retry});
    CHECK(controller.play_pause_press_count == 1);
    CHECK(controller.stop_press_count == 1);
    CHECK(controller.timeline_seek_count == 1);
    CHECK(controller.last_seek_position == std::optional{common::core::TimePosition{0.75}});
    CHECK(controller.plugin_browser_request_count == 1);
    CHECK(controller.plugin_browser_close_count == 1);
    CHECK(controller.plugin_catalog_scan_request_count == 1);
    CHECK(controller.selected_plugin_insert_request_count == 1);
    CHECK(controller.last_selected_plugin_id == std::optional<std::string>{"catalog-plugin-id"});
    CHECK(controller.remove_plugin_request_count == 1);
    CHECK(controller.last_removed_plugin_instance_id == std::optional<std::string>{"instance-id"});
    CHECK(controller.plugin_display_type_override_change_count == 1);
    CHECK(
        controller.last_display_type_override_instance_id ==
        std::optional<std::string>{"instance-id"});
    CHECK(controller.last_display_type_override == std::optional{PluginDisplayType::Cab});
    CHECK(controller.open_plugin_request_count == 1);
    CHECK(controller.last_opened_plugin_instance_id == std::optional<std::string>{"instance-id"});
}

// Verifies the controller publishes current audio-device status through view state.
TEST_CASE("EditorController publishes current audio device", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_status = common::audio::AudioDeviceStatus{
        .open = true,
        .device_name = "Interface A",
        .backend_name = "ASIO",
        .sample_rate_hz = 48000.0,
        .bit_depth = 24,
        .input_channels = 2,
        .output_channels = 2,
        .buffer_size_samples = 128,
        .input_latency_ms = 4.5,
        .output_latency_ms = 7.5,
    };
    EditorController controller{
        audioPorts(transport, audio, audio_devices), defaultControllerServices(), noopExitFunction()
    };
    FakeEditorView view;

    controller.attachView(view);

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.audio_device_status_text == "[48kHz 24bit: 2/2ch 128spls ~4.5/7.5ms ASIO]");
    }
}

// Device-manager change notifications re-derive view state through the listener relay.
TEST_CASE("EditorController re-derives state on device change", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    EditorController controller{
        audioPorts(transport, audio, audio_devices), defaultControllerServices(), noopExitFunction()
    };
    FakeEditorView view;
    controller.attachView(view);
    const int baseline_pushes = view.set_state_call_count;

    audio_devices.current_status = common::audio::AudioDeviceStatus{
        .open = true,
        .device_name = "Interface B",
        .backend_name = "Windows Audio",
        .sample_rate_hz = 44100.0,
        .bit_depth = 24,
        .input_channels = 1,
        .output_channels = 2,
        .buffer_size_samples = 512,
        .input_latency_ms = 9.5,
        .output_latency_ms = 30.0,
    };
    audio_devices.notifyChanged();

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == baseline_pushes + 1);
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.audio_device_status_text == "[44.1kHz 24bit: 1/2ch 512spls ~9.5/30ms WASAPI]");
    }
}

// Confirms attachView immediately delivers the controller's cached arrangement state.
TEST_CASE("EditorController pushes derived state on view attachment", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    EditorController controller{
        audioPorts(transport, audio), defaultControllerServices(), noopExitFunction()
    };
    FakeEditorView view;

    controller.attachView(view);

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == 1);
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.open_enabled == true);
        CHECK(state.import_enabled == true);
        CHECK(state.save_enabled == false);
        CHECK(state.save_as_enabled == false);
        CHECK(state.publish_enabled == false);
        CHECK(state.undo_enabled == false);
        CHECK_FALSE(state.undo_label.has_value());
        CHECK(state.redo_enabled == false);
        CHECK_FALSE(state.redo_label.has_value());
        CHECK(state.close_enabled == false);
        CHECK(state.project_loaded == false);
        CHECK(state.transport.play_pause_enabled == false);
        CHECK(state.transport.stop_enabled == false);
        CHECK(state.transport.play_pause_shows_pause_icon == false);
        CHECK(state.audio_device_status_text == "[audio device closed]");
        CHECK(state.visible_timeline == common::core::TimeRange{});
        CHECK_FALSE(state.arrangement.hasAudio());
        CHECK_FALSE(state.signal_chain.insert_plugin_enabled);
        CHECK(state.signal_chain.plugins.empty());
        CHECK_FALSE(state.unsaved_changes_prompt.has_value());
        CHECK_FALSE(state.save_as_prompt.has_value());
        CHECK_FALSE(state.restore_interrupted_prompt.has_value());
    }

    const int baseline_pushes = view.set_state_call_count;
    controller.onUndoRequested();
    controller.onRedoRequested();

    CHECK(view.set_state_call_count == baseline_pushes);
    CHECK(audio.set_active_arrangement_call_count == 0);
}

// Verifies the controller pushes session timeline mapping from loaded arrangement audio.
TEST_CASE("EditorController derives visible timeline range", "[core][editor-controller]")
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
        std::filesystem::path{"a.wav"},
        loadedTimelineRange(8.0)));
    FakeEditorView view;
    controller.attachView(view);

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.visible_timeline == loadedTimelineRange(8.0));
        CHECK(state.project_loaded == true);
        CHECK(state.arrangement.audio_duration == common::core::TimeDuration{8.0});
    }
}

// Each coarse transport transition produces exactly one fresh push so the view stays current.
TEST_CASE("EditorController pushes one state per coarse transition", "[core][editor-controller]")
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
    REQUIRE(loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"}));
    FakeEditorView view;
    controller.attachView(view);

    transport.setStateAndNotify(
        common::audio::TransportState{
            .playing = true,
        });

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == 2);
    if (view.last_state.has_value())
    {
        const EditorViewState& playing_state = view.last_state.value();
        CHECK(playing_state.transport.play_pause_shows_pause_icon == true);
        CHECK(playing_state.transport.stop_enabled == true);
        CHECK(playing_state.visible_timeline == loadedTimelineRange());
    }

    transport.setStateAndNotify(
        common::audio::TransportState{
            .playing = false,
        });

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == 3);
    if (view.last_state.has_value())
    {
        const EditorViewState& stopped_state = view.last_state.value();
        CHECK(stopped_state.transport.play_pause_shows_pause_icon == false);
        CHECK(stopped_state.transport.stop_enabled == false);
    }
}

// Reentrant transport notifications during in-flight arrangement activation coalesce once.
TEST_CASE("EditorController coalesces reentrant audio callbacks", "[core][editor-controller]")
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
    const int pushes_before_load = view.set_state_call_count;

    audio.during_active_arrangement_action = [&] {
        transport.setStateAndNotify(
            common::audio::TransportState{
                .playing = true,
            });
    };

    const common::core::AudioAsset replacement{
        .path = std::filesystem::path{"loop.wav"}, .normalization = std::nullopt, .start_offset = {}
    };
    project_services.next_song = makeSong(replacement.path);
    controller.onOpenRequested(std::filesystem::path{"loop.rhp"});

    // Loading emits the busy-state push, the tone-bearing rig-load progress pushes (begin,
    // presentation-ready, selection sync), and one final committed-state push; the reentrant
    // transport callback should not add a further load-related update.
    CHECK(view.set_state_call_count == pushes_before_load + 5);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.arrangement.audio_asset == std::optional{replacement});
        CHECK(state.transport.play_pause_shows_pause_icon == true);
    }
}

// Later transport transitions do not replay a one-shot workflow error.
TEST_CASE("EditorController does not replay errors across transitions", "[core][editor-controller]")
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
    REQUIRE(loadArrangement(controller, project_services, audio, std::filesystem::path{"old.wav"}));
    audio.next_set_active_arrangement_result = false;
    project_services.next_song = makeSong(std::filesystem::path{"new.wav"});
    FakeEditorView view;
    controller.attachView(view);
    controller.onOpenRequested(std::filesystem::path{"new.rhp"});

    REQUIRE(view.shown_errors.size() == 1);
    CHECK(
        view.shown_errors.back() ==
        "Could not load audio from: new.rhp: Configured active-arrangement failure");

    transport.setStateAndNotify(
        common::audio::TransportState{
            .playing = true,
        });

    CHECK(view.shown_errors.size() == 1);
}

// Grid note-value intents publish the new grid to the view and persist it for the project.
TEST_CASE(
    "EditorController publishes and persists the grid note value", "[core][editor-controller]")
{
    const ScopedControllerSettingsFile settings_file{"controller_grid_spacing.settings"};
    EditorSettings settings{settings_file.path()};
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
    REQUIRE(loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"}));
    FakeEditorView view;
    controller.attachView(view);

    controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 8});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->grid_note_value == common::core::Fraction{1, 8});

    const auto stored = settings.projectGridNoteValueFor(std::filesystem::path{"loaded.rhp"});
    CHECK(stored == std::optional{common::core::Fraction{1, 8}});
}

// Out-of-bounds note-value intents are ignored so the published grid can never go degenerate.
TEST_CASE("EditorController ignores invalid grid note values", "[core][editor-controller]")
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
    REQUIRE(loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"}));
    FakeEditorView view;
    controller.attachView(view);

    controller.onGridNoteValueChangeRequested(common::core::Fraction{});
    controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 2048});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->grid_note_value == common::core::Fraction{1, 4});
}

// Reopening a project restores its stored grid note value from app-local settings. The stored
// value deliberately differs from the quarter-note default so a silent fallback cannot pass.
TEST_CASE(
    "EditorController restores the project grid note value on open", "[core][editor-controller]")
{
    const ScopedControllerSettingsFile settings_file{"restore_grid_spacing.settings"};
    EditorSettings settings{settings_file.path()};
    REQUIRE(settings
                .saveProjectGridNoteValue(
                    std::filesystem::path{"loaded.rhp"}, common::core::Fraction{1, 16})
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
    REQUIRE(loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"}));
    FakeEditorView view;
    controller.attachView(view);

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->grid_note_value == common::core::Fraction{1, 16});
}

// Closing a project resets the grid to the quarter-note default for the next project.
TEST_CASE("EditorController resets the grid note value on close", "[core][editor-controller]")
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
    REQUIRE(loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"}));
    FakeEditorView view;
    controller.attachView(view);

    controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 8});
    controller.onCloseRequested();

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->project_loaded == false);
    CHECK(state->grid_note_value == common::core::Fraction{1, 4});
}

// Importing over an open project resets the grid to the quarter-note default, because a fresh
// import has no per-project record to restore.
TEST_CASE("EditorController resets the grid note value on import", "[core][editor-controller]")
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
    REQUIRE(loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"}));
    FakeEditorView view;
    controller.attachView(view);

    controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 8});
    project_services.next_import_song = makeSong(std::filesystem::path{"imported.ogg"});
    controller.onImportRequested(std::filesystem::path{"song.rock"});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->project_loaded == true);
    CHECK(state->grid_note_value == common::core::Fraction{1, 4});
}

// Save As is the first moment an imported project has a path, so it persists the active grid
// note value under that path; without this, a selection made before the first save is lost.
TEST_CASE("EditorController persists the grid note value on save-as", "[core][editor-controller]")
{
    const ScopedControllerSettingsFile settings_file{"save_as_grid_spacing.settings"};
    EditorSettings settings{settings_file.path()};
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
    controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 16});
    controller.onSaveAsRequested(std::filesystem::path{"saved.rhp"});

    const auto stored = settings.projectGridNoteValueFor(std::filesystem::path{"saved.rhp"});
    CHECK(stored == std::optional{common::core::Fraction{1, 16}});
}

// Both preferences are app-wide display state: published in every derived state, persisted on
// change, clamped to the chart string cap, and restored by a fresh controller.
TEST_CASE(
    "EditorController publishes and persists tab display preferences", "[core][editor-controller]")
{
    const ScopedControllerSettingsFile settings_file{"tab_display_preferences.settings"};
    EditorSettings settings{settings_file.path()};
    FakeTransport transport;
    ConfigurableSongAudio audio;

    {
        EditorController controller{
            audioPorts(transport, audio), controllerServices(settings), noopExitFunction()
        };
        FakeEditorView view;
        controller.attachView(view);

        REQUIRE(view.last_state.has_value());
        if (view.last_state.has_value())
        {
            CHECK(view.last_state->waveform_visible);
            CHECK(view.last_state->tab_minimum_displayed_strings == 0);
        }

        controller.onWaveformVisibleChangeRequested(false);
        controller.onTabMinimumDisplayedStringsChangeRequested(6);
        if (view.last_state.has_value())
        {
            CHECK_FALSE(view.last_state->waveform_visible);
            CHECK(view.last_state->tab_minimum_displayed_strings == 6);
        }

        controller.onTabMinimumDisplayedStringsChangeRequested(99);
        if (view.last_state.has_value())
        {
            CHECK(
                view.last_state->tab_minimum_displayed_strings ==
                common::core::g_max_chart_strings);
        }
    }

    CHECK(settings.waveformVisible() == std::optional{false});
    CHECK(
        settings.tabMinimumDisplayedStrings() == std::optional{common::core::g_max_chart_strings});

    EditorController restored_controller{
        audioPorts(transport, audio), controllerServices(settings), noopExitFunction()
    };
    FakeEditorView restored_view;
    restored_controller.attachView(restored_view);
    REQUIRE(restored_view.last_state.has_value());
    if (restored_view.last_state.has_value())
    {
        CHECK_FALSE(restored_view.last_state->waveform_visible);
        CHECK(
            restored_view.last_state->tab_minimum_displayed_strings ==
            common::core::g_max_chart_strings);
    }
}

} // namespace rock_hero::editor::core
