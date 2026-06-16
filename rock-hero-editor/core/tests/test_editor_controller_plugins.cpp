#include <algorithm>
#include <functional>
#include <optional>
#include <rock_hero/common/audio/plugin_chain_limits.h>
#include <rock_hero/editor/core/plugin_block_assignment.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Task runner that defers both the worker body and its completion so a test can inject an action
// (here, cancellation) between submission and execution. The shared DeferredEditorTaskRunner runs
// work immediately, which cannot exercise a worker observing cancellation mid-run.
class DeferredWorkTaskRunner final : public IEditorTaskRunner
{
public:
    void submit(std::function<void()> work, std::function<void()> completion) override
    {
        m_work.push_back(std::move(work));
        m_completion.push_back(std::move(completion));
    }

    // Runs every deferred worker body in submission order.
    void runWork()
    {
        drain(m_work);
    }

    // Runs every deferred completion in submission order.
    void runCompletions()
    {
        drain(m_completion);
    }

private:
    static void drain(std::vector<std::function<void()>>& queue)
    {
        std::vector<std::function<void()>> to_run;
        to_run.swap(queue);
        for (auto& fn : to_run)
        {
            if (fn)
            {
                fn();
            }
        }
    }

    std::vector<std::function<void()>> m_work;
    std::vector<std::function<void()>> m_completion;
};

// Builds an instance-keyed visual block assignment for controller intent tests.
[[nodiscard]] PluginBlockAssignment blockAssignment(std::string instance_id, std::size_t block)
{
    return PluginBlockAssignment{
        .instance_id = std::move(instance_id),
        .block_index = block,
    };
}

// Builds a plugin snapshot entry shared by the fake live rig and fake plugin host.
[[nodiscard]] common::audio::PluginChainEntry pluginEntry(
    std::string instance_id, std::size_t chain_index, std::size_t block_index)
{
    const std::string plugin_id = instance_id + "-plugin";
    return common::audio::PluginChainEntry{
        .instance_id = std::move(instance_id),
        .plugin_id = plugin_id,
        .name = plugin_id,
        .manufacturer = "Example Audio",
        .format_name = "VST3",
        .chain_index = chain_index,
        .block_index = block_index,
    };
}

// Extracts determinate plugin-scan busy states pushed during a catalog refresh.
[[nodiscard]] std::vector<BusyViewState> pluginScanProgressStates(const FakeEditorView& view)
{
    std::vector<BusyViewState> states;
    for (const EditorViewState& state : view.pushed_states)
    {
        if (state.busy.has_value() && state.busy->operation == BusyOperation::ScanningPlugins &&
            state.busy->progress.has_value())
        {
            states.push_back(*state.busy);
        }
    }

    return states;
}

// Reports whether a LoadingPlugin busy state was pushed after a known state index.
[[nodiscard]] bool loadingPluginPushedSince(const FakeEditorView& view, const std::size_t start)
{
    for (std::size_t index = start; index < view.pushed_states.size(); ++index)
    {
        const std::optional<BusyViewState>& busy = view.pushed_states[index].busy;
        if (busy.has_value() && busy->operation == BusyOperation::LoadingPlugin)
        {
            return true;
        }
    }

    return false;
}

} // namespace

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
        CHECK_FALSE(initial_state.signal_chain.insert_plugin_enabled);
        CHECK_FALSE(initial_state.signal_chain.remove_plugins_enabled);
    }

    REQUIRE(loadCalibratedArrangement(
        controller, project_services, audio, audio_devices, std::filesystem::path{"song.wav"}));

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& loaded_state = view.last_state.value();
        CHECK(loaded_state.signal_chain.insert_plugin_enabled);
        CHECK_FALSE(loaded_state.signal_chain.remove_plugins_enabled);
        CHECK(loaded_state.signal_chain.plugins.empty());
    }
}

// The editor disables plugin insertion once the product chain cap is reached.
TEST_CASE("EditorController disables plugin insertion at limit", "[core][editor-controller]")
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

    for (std::size_t index = 0; index < common::audio::max_signal_chain_plugins; ++index)
    {
        plugin_host.next_instance_id = "instance-" + std::to_string(index);
        addKnownPlugin(controller);
    }
    const int insert_call_count = plugin_host.insert_call_count;

    const EditorViewState* full_state = stateOrNull(view.last_state);
    REQUIRE(full_state != nullptr);
    REQUIRE(full_state->signal_chain.plugins.size() == common::audio::max_signal_chain_plugins);
    CHECK_FALSE(full_state->signal_chain.insert_plugin_enabled);
    CHECK_FALSE(full_state->plugin_browser.visible);

    controller.onPluginBrowserRequested();
    controller.onSelectedPluginInsertRequested("catalog-plugin-id");

    CHECK(plugin_host.insert_call_count == insert_call_count);
    CHECK(view.shown_errors.empty());
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
            .category = "Fx|Distortion",
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
    CHECK(
        browser_state->plugin_browser.plugins[0].primary_display_type ==
        PluginDisplayType::Distortion);

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

// Catalog scan progress is rendered as determinate busy-overlay state while rescan runs.
TEST_CASE("EditorController reports plugin catalog scan progress", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    plugin_host.next_catalog_scan_progress = {
        common::audio::PluginCatalogScanProgress{
            .completed_plugins = 0,
            .total_plugins = 2,
            .active_plugin_path = std::filesystem::path{"Amp.vst3"},
        },
        common::audio::PluginCatalogScanProgress{
            .completed_plugins = 1,
            .total_plugins = 2,
            .active_plugin_path = std::filesystem::path{"Cab.vst3"},
        },
        common::audio::PluginCatalogScanProgress{
            .completed_plugins = 2,
            .total_plugins = 2,
            .active_plugin_path = std::filesystem::path{"Cab.vst3"},
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

    controller.onPluginCatalogScanRequested();

    const std::vector<BusyViewState> progress_states = pluginScanProgressStates(view);
    REQUIRE_FALSE(progress_states.empty());
    CHECK(progress_states.front().message == "Scanning plugin (1/2)...\nAmp.vst3");
    CHECK(progress_states.front().indicator == BusyIndicator::DeterminateProgress);
    CHECK(progress_states.front().progress == std::optional<double>{0.0});
    CHECK(std::ranges::any_of(progress_states, [](const BusyViewState& state) {
        return state.message == "Scanning plugin (2/2)...\nCab.vst3" &&
               state.progress == std::optional<double>{0.5};
    }));
    CHECK(std::ranges::any_of(progress_states, [](const BusyViewState& state) {
        return state.message == "Scanning plugin (2/2)...\nCab.vst3" &&
               state.progress == std::optional<double>{1.0};
    }));
}

// Cancelling a plugin scan keeps the host catalog accumulated so far and drops the completion.
TEST_CASE("EditorController cancel scan keeps known plugins", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    plugin_host.next_known_candidates = {
        common::audio::PluginCandidate{
            .id = "known-plugin-id",
            .name = "Known Amp",
            .manufacturer = "Example Audio",
            .format_name = "VST3",
            .category = "Fx|Distortion",
            .file_path = std::filesystem::path{"known-amp.vst3"},
        },
    };
    plugin_host.next_catalog_candidates = {
        common::audio::PluginCandidate{
            .id = "scanned-plugin-id",
            .name = "Scanned Delay",
            .manufacturer = "Example Audio",
            .format_name = "VST3",
            .category = "Fx|Delay",
            .file_path = std::filesystem::path{"scanned-delay.vst3"},
        },
    };
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    audio_devices.current_input_identity = makeInputDeviceIdentity();
    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    runner.runPendingCompletions();
    controller.onInputCalibrationRequested();
    const auto calibrated = controller.onInputCalibrationManuallySet(0.0);
    REQUIRE(calibrated.has_value());
    controller.onInputCalibrationDismissed();
    controller.onPluginBrowserRequested();

    controller.onPluginCatalogScanRequested();
    const EditorViewState* scanning_state = stateOrNull(view.last_state);
    REQUIRE(scanning_state != nullptr);
    REQUIRE(scanning_state->busy.has_value());
    CHECK(scanning_state->busy->operation == BusyOperation::ScanningPlugins);
    CHECK(scanning_state->busy->cancel_enabled);

    controller.onBusyCancelRequested();

    const EditorViewState* canceled_state = stateOrNull(view.last_state);
    REQUIRE(canceled_state != nullptr);
    CHECK_FALSE(canceled_state->busy.has_value());
    REQUIRE(canceled_state->plugin_browser.plugins.size() == 1);
    CHECK(canceled_state->plugin_browser.plugins[0].id == "scanned-plugin-id");
    const int known_reads_after_cancel = plugin_host.known_candidates_call_count;

    runner.runPendingCompletions();

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    REQUIRE(final_state->plugin_browser.plugins.size() == 1);
    CHECK(final_state->plugin_browser.plugins[0].id == "scanned-plugin-id");
    CHECK(plugin_host.known_candidates_call_count == known_reads_after_cancel);
    CHECK(view.shown_errors.empty());
}

// Cancelling a scan trips the worker's cancellation token, so the scan worker observes it and stops
// at the next candidate instead of running to completion and publishing the scanned catalog.
TEST_CASE("EditorController cancel scan stops the scan worker", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    DeferredWorkTaskRunner runner;
    plugin_host.next_known_candidates = {
        common::audio::PluginCandidate{
            .id = "known-plugin-id",
            .name = "Known Amp",
            .manufacturer = "Example Audio",
            .format_name = "VST3",
            .category = "Fx|Distortion",
            .file_path = std::filesystem::path{"known-amp.vst3"},
        },
    };
    plugin_host.next_catalog_candidates = {
        common::audio::PluginCandidate{
            .id = "scanned-plugin-id",
            .name = "Scanned Delay",
            .manufacturer = "Example Audio",
            .format_name = "VST3",
            .category = "Fx|Delay",
            .file_path = std::filesystem::path{"scanned-delay.vst3"},
        },
    };
    // At least one progress step so the worker reaches a cancellation checkpoint before it would
    // otherwise publish the scanned catalog.
    plugin_host.next_catalog_scan_progress = {
        common::audio::PluginCatalogScanProgress{
            .completed_plugins = 0,
            .total_plugins = 1,
            .active_plugin_path = std::filesystem::path{"scanned-delay.vst3"},
        },
    };
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    audio_devices.current_input_identity = makeInputDeviceIdentity();
    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    runner.runWork();
    runner.runCompletions();
    controller.onInputCalibrationRequested();
    const auto calibrated = controller.onInputCalibrationManuallySet(0.0);
    REQUIRE(calibrated.has_value());
    controller.onInputCalibrationDismissed();
    controller.onPluginBrowserRequested();

    controller.onPluginCatalogScanRequested();
    const EditorViewState* scanning_state = stateOrNull(view.last_state);
    REQUIRE(scanning_state != nullptr);
    REQUIRE(scanning_state->busy.has_value());
    CHECK(scanning_state->busy->operation == BusyOperation::ScanningPlugins);

    // Cancel before the deferred worker body runs, then let it run. The worker must observe
    // cancellation and stop without publishing the scanned catalog.
    controller.onBusyCancelRequested();
    runner.runWork();

    CHECK(plugin_host.catalog_scan_canceled);

    runner.runCompletions();

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    REQUIRE(final_state->plugin_browser.plugins.size() == 1);
    CHECK(final_state->plugin_browser.plugins[0].id == "known-plugin-id");
    CHECK(view.shown_errors.empty());
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

    controller.onSelectedPluginInsertRequested("catalog-plugin-id");

    CHECK(plugin_host.insert_call_count == 1);
    CHECK(
        plugin_host.last_inserted_plugin_candidate ==
        std::optional{plugin_host.next_known_candidates.front()});
    CHECK(plugin_host.last_insert_index == std::optional<std::size_t>{0});
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(view.busy_overlay_paint_callback_count == 1);
    CHECK_FALSE(final_state->plugin_browser.visible);
    REQUIRE(final_state->signal_chain.plugins.size() == 1);
    CHECK(final_state->signal_chain.insert_plugin_enabled);
    CHECK(final_state->signal_chain.move_plugins_enabled);
    CHECK(final_state->signal_chain.remove_plugins_enabled);
    CHECK(final_state->signal_chain.plugins[0].instance_id == "instance-id");
    CHECK(final_state->signal_chain.plugins[0].plugin_id == "catalog-plugin-id");
    CHECK(final_state->signal_chain.plugins[0].name == "Catalog Amp");
    CHECK(final_state->signal_chain.plugins[0].manufacturer == "Example Audio");
    CHECK(final_state->signal_chain.plugins[0].format_name == "VST3");
    CHECK(
        final_state->signal_chain.plugins[0].primary_display_type == PluginDisplayType::Distortion);
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
    plugin_host.next_insert_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::PluginLoadFailed,
        "plugin rejected",
    };

    controller.onSelectedPluginInsertRequested("catalog-plugin-id");

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(final_state->plugin_browser.visible);
    CHECK(final_state->signal_chain.plugins.empty());
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not insert plugin: plugin rejected");
    REQUIRE(view.states_seen_at_errors.size() == 1);
    REQUIRE(view.states_seen_at_errors.back().has_value());
    const EditorViewState* error_state = stateOrNull(view.states_seen_at_errors.back());
    REQUIRE(error_state != nullptr);
    CHECK_FALSE(error_state->busy.has_value());
}

// If undo preparation fails after insertion, rolling back the insert preserves prior history.
TEST_CASE("EditorController rolls back insert when undo prep fails", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    plugin_host.next_instance_id = "instance-b";
    plugin_host.next_capture_state_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::PluginStateCaptureFailed,
        "capture rejected",
    };

    addKnownPlugin(controller);

    CHECK(plugin_host.remove_call_count == 1);
    CHECK(plugin_host.last_removed_instance_id == std::optional<std::string>{"instance-b"});
    const EditorViewState* failed_state = stateOrNull(view.last_state);
    REQUIRE(failed_state != nullptr);
    CHECK_FALSE(failed_state->busy.has_value());
    CHECK_FALSE(failed_state->plugin_browser.visible);
    REQUIRE(failed_state->signal_chain.plugins.size() == 1);
    CHECK(failed_state->signal_chain.plugins[0].instance_id == "instance-a");
    CHECK(failed_state->undo_label == std::optional<std::string>{"Insert Plugin"});
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back().find("insert was rolled back") != std::string::npos);

    controller.onUndoRequested();
    controller.onCloseRequested();

    const EditorViewState* closed_state = stateOrNull(view.last_state);
    REQUIRE(closed_state != nullptr);
    CHECK_FALSE(closed_state->unsaved_changes_prompt.has_value());
}

// If rollback fails after an untracked insert, the controller clears partial undo history.
TEST_CASE("EditorController clears undo when insert rollback fails", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    plugin_host.next_instance_id = "instance-b";
    plugin_host.next_capture_state_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::PluginStateCaptureFailed,
        "capture rejected",
    };
    plugin_host.next_remove_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::PluginRemovalFailed,
        "remove rejected",
    };

    addKnownPlugin(controller);

    CHECK(plugin_host.remove_call_count == 1);
    const EditorViewState* failed_state = stateOrNull(view.last_state);
    REQUIRE(failed_state != nullptr);
    REQUIRE(failed_state->signal_chain.plugins.size() == 2);
    CHECK(failed_state->signal_chain.plugins[0].instance_id == "instance-a");
    CHECK(failed_state->signal_chain.plugins[1].instance_id == "instance-b");
    CHECK_FALSE(failed_state->undo_label.has_value());
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back().find("rollback failed") != std::string::npos);

    controller.onCloseRequested();

    const EditorViewState* prompt_state = stateOrNull(view.last_state);
    REQUIRE(prompt_state != nullptr);
    CHECK(
        prompt_state->unsaved_changes_prompt ==
        std::optional{UnsavedChangesPrompt{EditorActionId::CloseProject}});
}

// A rollback-contract violation during insert cleanup faults the current editor session.
TEST_CASE("EditorController faults when insert rollback breaks", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    plugin_host.next_capture_state_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::PluginStateCaptureFailed,
        "capture rejected",
    };
    plugin_host.next_remove_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::RollbackContractViolation,
        "rollback contract rejected",
    };

    addKnownPlugin(controller);

    const EditorViewState* faulted_state = stateOrNull(view.last_state);
    REQUIRE(faulted_state != nullptr);
    CHECK(faulted_state->open_enabled);
    CHECK(faulted_state->import_enabled);
    CHECK(faulted_state->close_enabled);
    CHECK_FALSE(faulted_state->save_enabled);
    CHECK_FALSE(faulted_state->save_as_enabled);
    CHECK_FALSE(faulted_state->publish_enabled);
    CHECK_FALSE(faulted_state->signal_chain.insert_plugin_enabled);
    CHECK_FALSE(faulted_state->signal_chain.move_plugins_enabled);
    CHECK_FALSE(faulted_state->signal_chain.remove_plugins_enabled);
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(
        view.shown_errors.back() ==
        "An unexpected internal error left the live editor state untrusted. Please report this "
        "bug and attach the editor log file, then reopen or close the project before continuing.");
}

// Opening the browser from a gap inserts the selected plugin at that backend slot and block.
TEST_CASE("EditorController inserts browser plugin at a gap", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    plugin_host.next_instance_id = "instance-b";
    addKnownPlugin(controller);
    controller.onSignalChainPlacementChanged(
        {blockAssignment("instance-a", 0), blockAssignment("instance-b", 4)});
    plugin_host.next_instance_id = "instance-c";

    controller.onPluginInsertSlotSelected(1, 2);
    controller.onSelectedPluginInsertRequested("catalog-plugin-id");

    CHECK(plugin_host.insert_call_count == 3);
    CHECK(plugin_host.last_insert_index == std::optional<std::size_t>{1});
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    REQUIRE(final_state->signal_chain.plugins.size() == 3);
    CHECK(final_state->signal_chain.plugins[0].instance_id == "instance-a");
    CHECK(final_state->signal_chain.plugins[0].chain_index == 0);
    CHECK(final_state->signal_chain.plugins[0].block_index == 0);
    CHECK(final_state->signal_chain.plugins[1].instance_id == "instance-c");
    CHECK(final_state->signal_chain.plugins[1].chain_index == 1);
    CHECK(final_state->signal_chain.plugins[1].block_index == 2);
    CHECK(final_state->signal_chain.plugins[2].instance_id == "instance-b");
    CHECK(final_state->signal_chain.plugins[2].chain_index == 2);
    CHECK(final_state->signal_chain.plugins[2].block_index == 4);
    CHECK_FALSE(final_state->plugin_browser.visible);
    CHECK(view.shown_errors.empty());
}

// Insert undo removes the plugin, and redo recreates it with the same runtime id and block.
TEST_CASE("EditorController undoes plugin inserts", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    plugin_host.next_instance_id = "instance-b";
    addKnownPlugin(controller);
    controller.onSignalChainPlacementChanged(
        {blockAssignment("instance-a", 0), blockAssignment("instance-b", 4)});
    plugin_host.next_instance_id = "instance-c";

    controller.onPluginInsertSlotSelected(1, 2);
    controller.onSelectedPluginInsertRequested("catalog-plugin-id");

    const EditorViewState* inserted_state = stateOrNull(view.last_state);
    REQUIRE(inserted_state != nullptr);
    REQUIRE(inserted_state->signal_chain.plugins.size() == 3);
    CHECK(inserted_state->signal_chain.plugins[1].instance_id == "instance-c");
    CHECK(inserted_state->signal_chain.plugins[1].block_index == 2);
    CHECK(inserted_state->undo_label == std::optional<std::string>{"Insert Plugin"});

    controller.onUndoRequested();

    CHECK(plugin_host.last_removed_instance_id == std::optional<std::string>{"instance-c"});
    const EditorViewState* undone_state = stateOrNull(view.last_state);
    REQUIRE(undone_state != nullptr);
    REQUIRE(undone_state->signal_chain.plugins.size() == 2);
    CHECK(undone_state->signal_chain.plugins[0].instance_id == "instance-a");
    CHECK(undone_state->signal_chain.plugins[0].block_index == 0);
    CHECK(undone_state->signal_chain.plugins[1].instance_id == "instance-b");
    CHECK(undone_state->signal_chain.plugins[1].block_index == 4);
    CHECK(undone_state->redo_label == std::optional<std::string>{"Insert Plugin"});

    controller.onRedoRequested();

    CHECK(plugin_host.recreate_state_call_count == 1);
    CHECK(plugin_host.last_recreate_state_index == std::optional<std::size_t>{1});
    const EditorViewState* redone_state = stateOrNull(view.last_state);
    REQUIRE(redone_state != nullptr);
    REQUIRE(redone_state->signal_chain.plugins.size() == 3);
    CHECK(redone_state->signal_chain.plugins[0].instance_id == "instance-a");
    CHECK(redone_state->signal_chain.plugins[0].block_index == 0);
    CHECK(redone_state->signal_chain.plugins[1].instance_id == "instance-c");
    CHECK(redone_state->signal_chain.plugins[1].block_index == 2);
    CHECK(redone_state->signal_chain.plugins[2].instance_id == "instance-b");
    CHECK(redone_state->signal_chain.plugins[2].block_index == 4);
    CHECK(view.shown_errors.empty());
}

// Plugin-recreating directions (remove undo, insert redo) run behind the LoadingPlugin busy fence so
// a slow instantiation cannot block the message thread without feedback; remove redo stays sync.
TEST_CASE("EditorController fences plugin recreate behind loading", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    view.defer_busy_overlay_paint_callbacks = true;

    controller.onRemovePluginRequested("instance-a");
    REQUIRE(stateOrNull(view.last_state) != nullptr);

    const std::size_t before_undo = view.pushed_states.size();
    controller.onUndoRequested();

    CHECK(plugin_host.recreate_state_call_count == 0);
    CHECK(loadingPluginPushedSince(view, before_undo));
    REQUIRE(view.hasBusyOverlayPaintCallback());
    const EditorViewState* loading_state = stateOrNull(view.last_state);
    REQUIRE(loading_state != nullptr);
    REQUIRE(loading_state->busy.has_value());
    CHECK(loading_state->busy->operation == BusyOperation::LoadingPlugin);

    view.runNextBusyOverlayPaintCallback();

    CHECK(plugin_host.recreate_state_call_count == 1);
    const EditorViewState* undone_state = stateOrNull(view.last_state);
    REQUIRE(undone_state != nullptr);
    CHECK_FALSE(undone_state->busy.has_value());
    CHECK(undone_state->signal_chain.plugins.size() == 1);

    const std::size_t before_redo = view.pushed_states.size();
    controller.onRedoRequested();

    CHECK_FALSE(loadingPluginPushedSince(view, before_redo));
    CHECK(plugin_host.last_removed_instance_id == std::optional<std::string>{"instance-a"});

    view.defer_busy_overlay_paint_callbacks = false;
    plugin_host.next_instance_id = "instance-b";
    addKnownPlugin(controller);
    controller.onUndoRequested();

    view.defer_busy_overlay_paint_callbacks = true;
    const int before_insert_redo_recreate = plugin_host.recreate_state_call_count;
    const std::size_t before_insert_redo = view.pushed_states.size();
    controller.onRedoRequested();

    CHECK(plugin_host.recreate_state_call_count == before_insert_redo_recreate);
    CHECK(loadingPluginPushedSince(view, before_insert_redo));
    REQUIRE(view.hasBusyOverlayPaintCallback());

    view.runNextBusyOverlayPaintCallback();

    CHECK(plugin_host.recreate_state_call_count == before_insert_redo_recreate + 1);
}

// A close/exit takeover can supersede a plugin-recreate busy token before the undo side effect
// starts. Releasing that stale paint callback must abort the pending history transition.
TEST_CASE("EditorController aborts stale plugin recreate before close", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    view.defer_busy_overlay_paint_callbacks = true;

    controller.onRemovePluginRequested("instance-a");
    controller.onUndoRequested();

    CHECK(plugin_host.recreate_state_call_count == 0);
    REQUIRE(view.hasBusyOverlayPaintCallback());

    controller.onCloseRequested();

    const EditorViewState* prompt_state = stateOrNull(view.last_state);
    REQUIRE(prompt_state != nullptr);
    REQUIRE(prompt_state->unsaved_changes_prompt.has_value());
    CHECK(prompt_state->unsaved_changes_prompt->prompted_action == EditorActionId::CloseProject);
    CHECK_FALSE(prompt_state->busy.has_value());

    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Cancel);
    view.runNextBusyOverlayPaintCallback();

    const EditorViewState* restored_state = stateOrNull(view.last_state);
    REQUIRE(restored_state != nullptr);
    CHECK_FALSE(restored_state->unsaved_changes_prompt.has_value());
    CHECK(restored_state->undo_label == std::optional<std::string>{"Remove Plugin"});

    controller.onUndoRequested();

    CHECK(plugin_host.recreate_state_call_count == 0);
    REQUIRE(view.hasBusyOverlayPaintCallback());
    view.runNextBusyOverlayPaintCallback();

    CHECK(plugin_host.recreate_state_call_count == 1);
    const EditorViewState* undone_state = stateOrNull(view.last_state);
    REQUIRE(undone_state != nullptr);
    REQUIRE(undone_state->signal_chain.plugins.size() == 1);
    CHECK(undone_state->signal_chain.plugins[0].instance_id == "instance-a");
}

// Add failures preserve the selected gap so a retry inserts at the same position.
TEST_CASE("EditorController preserves failed insert target", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    plugin_host.next_instance_id = "instance-b";
    addKnownPlugin(controller);
    controller.onSignalChainPlacementChanged(
        {blockAssignment("instance-a", 0), blockAssignment("instance-b", 4)});
    plugin_host.next_instance_id = "retry-instance";
    plugin_host.next_insert_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::PluginLoadFailed,
        "plugin rejected",
    };

    controller.onPluginInsertSlotSelected(1, 2);
    controller.onSelectedPluginInsertRequested("catalog-plugin-id");
    plugin_host.next_insert_error.reset();
    controller.onSelectedPluginInsertRequested("catalog-plugin-id");

    CHECK(plugin_host.last_insert_index == std::optional<std::size_t>{1});
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    REQUIRE(final_state->signal_chain.plugins.size() == 3);
    CHECK(final_state->signal_chain.plugins[1].instance_id == "retry-instance");
    CHECK(final_state->signal_chain.plugins[1].block_index == 2);
    CHECK(final_state->signal_chain.plugins[2].block_index == 4);
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

// Opening a project aborts before commit when the restored live rig exceeds the plugin cap.
TEST_CASE("EditorController rejects over-limit live rig on open", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.clear();
    for (std::size_t index = 0; index <= common::audio::max_signal_chain_plugins; ++index)
    {
        live_rig.next_load_result.plugins.push_back(
            common::audio::PluginChainEntry{
                .instance_id = "loaded-instance-" + std::to_string(index),
                .plugin_id = "loaded-plugin-" + std::to_string(index),
                .name = "Loaded Plugin " + std::to_string(index),
                .manufacturer = "Example Audio",
                .format_name = "VST3",
                .chain_index = index,
            });
    }
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
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->project_loaded);
    CHECK(state->signal_chain.plugins.empty());
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(
        view.shown_errors.back() == "Could not load live rig from: song.rhp: " +
                                        common::audio::pluginChainLimitExceededMessage(
                                            common::audio::max_signal_chain_plugins + 1));
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
        common::audio::PluginChainEntry{
            .instance_id = "amp-instance",
            .plugin_id = "amp-plugin",
            .name = "Amp Sim",
            .manufacturer = "Example Audio",
            .format_name = "VST3",
            .chain_index = 0,
        },
        common::audio::PluginChainEntry{
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
        return state.message == "Loading plugin (1/2)...\nAmp Sim" &&
               state.progress == std::optional<double>{0.5};
    }));
    CHECK(std::ranges::any_of(progress_states, [](const BusyViewState& state) {
        return state.message == "Loading plugin (2/2)...\nCab IR" &&
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

// Saving after a placement-only edit captures the exact authored block indices.
TEST_CASE("EditorController captures signal-chain placement", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.front().block_index = 1;
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
    controller.onSignalChainPlacementChanged({blockAssignment("loaded-instance", 3)});

    controller.onSaveRequested();

    REQUIRE(live_rig.last_capture_request.has_value());
    CHECK(live_rig.last_capture_request->block_indices == std::vector<std::size_t>{3});
}

// Saving after a display override captures the authored display type token.
TEST_CASE(
    "EditorController captures signal-chain display type override", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.front().category = "Fx|Delay";
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

    controller.onPluginDisplayTypeOverrideChanged("loaded-instance", PluginDisplayType::Cab);

    const EditorViewState* edited_state = stateOrNull(view.last_state);
    REQUIRE(edited_state != nullptr);
    REQUIRE(edited_state->signal_chain.plugins.size() == 1);
    CHECK(edited_state->signal_chain.plugins[0].automatic_display_type == PluginDisplayType::Delay);
    CHECK(edited_state->signal_chain.plugins[0].primary_display_type == PluginDisplayType::Cab);
    CHECK(
        edited_state->signal_chain.plugins[0].display_type_override ==
        std::optional{PluginDisplayType::Cab});

    controller.onSaveRequested();

    REQUIRE(live_rig.last_capture_request.has_value());
    CHECK(live_rig.last_capture_request->display_type_overrides == std::vector<std::string>{"cab"});
}

// Plugin insertions are tracked as undoable tone edits, which makes the project dirty.
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

// Placement-only edits are persisted tone changes even when the plugin chain order is unchanged.
TEST_CASE("EditorController placement edit marks tone dirty", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.front().block_index = 1;
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
    const EditorViewState* loaded_state = stateOrNull(view.last_state);
    REQUIRE(loaded_state != nullptr);
    REQUIRE(loaded_state->signal_chain.plugins.size() == 1);
    CHECK(loaded_state->signal_chain.plugins[0].block_index == 1);

    const int loaded_state_count = view.set_state_call_count;
    controller.onSignalChainPlacementChanged({blockAssignment("loaded-instance", 1)});

    CHECK(view.set_state_call_count == loaded_state_count);

    controller.onSignalChainPlacementChanged({blockAssignment("loaded-instance", 3)});

    const EditorViewState* edited_state = stateOrNull(view.last_state);
    REQUIRE(edited_state != nullptr);
    REQUIRE(edited_state->signal_chain.plugins.size() == 1);
    CHECK(edited_state->signal_chain.plugins[0].block_index == 3);

    controller.onCloseRequested();

    const EditorViewState* prompt_state = stateOrNull(view.last_state);
    REQUIRE(prompt_state != nullptr);
    REQUIRE(prompt_state->unsaved_changes_prompt.has_value());
    CHECK(prompt_state->unsaved_changes_prompt->prompted_action == EditorActionId::CloseProject);
}

// Placement-only undo and redo restore editor metadata without exposing user-facing commands yet.
TEST_CASE("EditorController undoes signal-chain placement", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.front().block_index = 1;
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

    controller.onSignalChainPlacementChanged({blockAssignment("loaded-instance", 3)});

    const EditorViewState* edited_state = stateOrNull(view.last_state);
    REQUIRE(edited_state != nullptr);
    REQUIRE(edited_state->signal_chain.plugins.size() == 1);
    CHECK(edited_state->signal_chain.plugins[0].block_index == 3);
    CHECK(edited_state->undo_enabled);
    CHECK(edited_state->undo_label == std::optional<std::string>{"Move Plugin Block"});

    controller.onUndoRequested();

    const EditorViewState* undone_state = stateOrNull(view.last_state);
    REQUIRE(undone_state != nullptr);
    REQUIRE(undone_state->signal_chain.plugins.size() == 1);
    CHECK(undone_state->signal_chain.plugins[0].block_index == 1);
    CHECK(undone_state->redo_enabled);
    CHECK(undone_state->redo_label == std::optional<std::string>{"Move Plugin Block"});

    controller.onRedoRequested();

    const EditorViewState* redone_state = stateOrNull(view.last_state);
    REQUIRE(redone_state != nullptr);
    REQUIRE(redone_state->signal_chain.plugins.size() == 1);
    CHECK(redone_state->signal_chain.plugins[0].block_index == 3);

    controller.onUndoRequested();
    controller.onCloseRequested();

    const EditorViewState* closed_state = stateOrNull(view.last_state);
    REQUIRE(closed_state != nullptr);
    CHECK_FALSE(closed_state->unsaved_changes_prompt.has_value());
}

// Redo returns the controller to an edited history position and should prompt before replacement.
TEST_CASE("EditorController redo makes clean placement dirty", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.front().block_index = 1;
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
    controller.onSignalChainPlacementChanged({blockAssignment("loaded-instance", 3)});
    controller.onUndoRequested();
    controller.onRedoRequested();

    const EditorViewState* redone_state = stateOrNull(view.last_state);
    REQUIRE(redone_state != nullptr);
    REQUIRE(redone_state->signal_chain.plugins.size() == 1);
    CHECK(redone_state->signal_chain.plugins[0].block_index == 3);

    controller.onCloseRequested();

    const EditorViewState* prompt_state = stateOrNull(view.last_state);
    REQUIRE(prompt_state != nullptr);
    CHECK(
        prompt_state->unsaved_changes_prompt ==
        std::optional{UnsavedChangesPrompt{EditorActionId::CloseProject}});
}

// A new edit after undo discards the saved redo branch, so the old clean marker is unreachable.
TEST_CASE("EditorController forked redo branch stays dirty", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.front().block_index = 1;
    live_rig.next_capture_snapshot.plugins = {pluginEntry("loaded-instance", 0, 3)};
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
    controller.onSignalChainPlacementChanged({blockAssignment("loaded-instance", 3)});
    controller.onSaveRequested();
    controller.onUndoRequested();
    controller.onSignalChainPlacementChanged({blockAssignment("loaded-instance", 4)});
    controller.onUndoRequested();

    const EditorViewState* undone_state = stateOrNull(view.last_state);
    REQUIRE(undone_state != nullptr);
    REQUIRE(undone_state->signal_chain.plugins.size() == 1);
    CHECK(undone_state->signal_chain.plugins[0].block_index == 1);

    controller.onCloseRequested();

    const EditorViewState* prompt_state = stateOrNull(view.last_state);
    REQUIRE(prompt_state != nullptr);
    CHECK(
        prompt_state->unsaved_changes_prompt ==
        std::optional{UnsavedChangesPrompt{EditorActionId::CloseProject}});
}

// Once bounded history evicts the clean marker, undoing all reachable edits still stays dirty.
TEST_CASE("EditorController evicted clean marker stays dirty", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.front().block_index = 0;
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

    for (std::size_t index = 0; index < 101; ++index)
    {
        const std::size_t block_index = index % 2 == 0 ? 1 : 0;
        controller.onSignalChainPlacementChanged({blockAssignment("loaded-instance", block_index)});
    }

    for (std::size_t index = 0; index < 100; ++index)
    {
        controller.onUndoRequested();
    }

    const EditorViewState* undone_state = stateOrNull(view.last_state);
    REQUIRE(undone_state != nullptr);
    REQUIRE(undone_state->signal_chain.plugins.size() == 1);
    CHECK(undone_state->signal_chain.plugins[0].block_index == 1);

    controller.onCloseRequested();

    const EditorViewState* prompt_state = stateOrNull(view.last_state);
    REQUIRE(prompt_state != nullptr);
    CHECK(
        prompt_state->unsaved_changes_prompt ==
        std::optional{UnsavedChangesPrompt{EditorActionId::CloseProject}});
}

// Display-type undo and redo restore editor-owned classification metadata only.
TEST_CASE("EditorController undoes display type override", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.front().category = "Fx|Delay";
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

    controller.onPluginDisplayTypeOverrideChanged("loaded-instance", PluginDisplayType::Cab);

    const EditorViewState* edited_state = stateOrNull(view.last_state);
    REQUIRE(edited_state != nullptr);
    REQUIRE(edited_state->signal_chain.plugins.size() == 1);
    CHECK(edited_state->signal_chain.plugins[0].primary_display_type == PluginDisplayType::Cab);
    CHECK(
        edited_state->signal_chain.plugins[0].display_type_override ==
        std::optional{PluginDisplayType::Cab});
    CHECK(edited_state->undo_enabled);
    CHECK(edited_state->undo_label == std::optional<std::string>{"Set Plugin Display Type"});

    controller.onUndoRequested();

    const EditorViewState* undone_state = stateOrNull(view.last_state);
    REQUIRE(undone_state != nullptr);
    REQUIRE(undone_state->signal_chain.plugins.size() == 1);
    CHECK(undone_state->signal_chain.plugins[0].primary_display_type == PluginDisplayType::Delay);
    CHECK_FALSE(undone_state->signal_chain.plugins[0].display_type_override.has_value());

    controller.onRedoRequested();

    const EditorViewState* redone_state = stateOrNull(view.last_state);
    REQUIRE(redone_state != nullptr);
    REQUIRE(redone_state->signal_chain.plugins.size() == 1);
    CHECK(redone_state->signal_chain.plugins[0].primary_display_type == PluginDisplayType::Cab);
    CHECK(
        redone_state->signal_chain.plugins[0].display_type_override ==
        std::optional{PluginDisplayType::Cab});
}

// A pending plugin-parameter edit makes Undo available even with an otherwise empty history,
// because the action gate flushes it into a real undo entry before undoing.
TEST_CASE("EditorController offers undo for a pending parameter edit", "[core][editor-controller]")
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

    const EditorViewState* clean_state = stateOrNull(view.last_state);
    REQUIRE(clean_state != nullptr);
    CHECK_FALSE(clean_state->undo_enabled);

    plugin_host.queuePendingPluginParameterEdit(
        common::audio::PluginParameterEdit{
            .instance_id = "instance-a",
            .parameter_id = "gain",
            .parameter_index = 1,
            .before_normalized = 0.25,
            .after_normalized = 0.75,
            .label_hint = "Gain",
        });

    const EditorViewState* pending_view = stateOrNull(view.last_state);
    REQUIRE(pending_view != nullptr);
    CHECK(pending_view->undo_enabled);
}

// Parameter value edits restore through the plugin host on undo and redo.
TEST_CASE("EditorController undoes plugin parameter edits", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    plugin_host.queuePendingPluginParameterEdit(
        common::audio::PluginParameterEdit{
            .instance_id = "instance-a",
            .parameter_id = "gain",
            .parameter_index = 1,
            .before_normalized = 0.25,
            .after_normalized = 0.75,
            .label_hint = "Gain",
        });

    plugin_host.flushPendingPluginParameterEdits();

    const EditorViewState* edited_state = stateOrNull(view.last_state);
    REQUIRE(edited_state != nullptr);
    CHECK(edited_state->undo_label == std::optional<std::string>{"Edit Gain"});

    controller.onUndoRequested();

    CHECK(plugin_host.set_parameter_value_call_count == 1);
    CHECK(plugin_host.last_set_parameter_instance_id == std::optional<std::string>{"instance-a"});
    CHECK(plugin_host.last_set_parameter_id == std::optional<std::string>{"gain"});
    CHECK(plugin_host.last_set_parameter_index == std::optional{1});
    CHECK(plugin_host.last_set_parameter_normalized_value == std::optional{0.25});
    const EditorViewState* undone_state = stateOrNull(view.last_state);
    REQUIRE(undone_state != nullptr);
    CHECK(undone_state->redo_label == std::optional<std::string>{"Edit Gain"});

    controller.onRedoRequested();

    CHECK(plugin_host.set_parameter_value_call_count == 2);
    CHECK(plugin_host.last_set_parameter_instance_id == std::optional<std::string>{"instance-a"});
    CHECK(plugin_host.last_set_parameter_id == std::optional<std::string>{"gain"});
    CHECK(plugin_host.last_set_parameter_index == std::optional{1});
    CHECK(plugin_host.last_set_parameter_normalized_value == std::optional{0.75});
}

// The action gate settles pending parameter values before deciding whether Undo is available.
TEST_CASE("EditorController flushes parameters before undo", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    plugin_host.queuePendingPluginParameterEdit(
        common::audio::PluginParameterEdit{
            .instance_id = "instance-a",
            .parameter_id = "drive",
            .parameter_index = 2,
            .before_normalized = 0.1,
            .after_normalized = 0.9,
            .label_hint = "Drive",
        });

    controller.onUndoRequested();

    CHECK(plugin_host.flush_pending_parameter_edits_call_count == 1);
    CHECK(plugin_host.set_parameter_value_call_count == 1);
    CHECK(plugin_host.last_set_parameter_instance_id == std::optional<std::string>{"instance-a"});
    CHECK(plugin_host.last_set_parameter_id == std::optional<std::string>{"drive"});
    CHECK(plugin_host.last_set_parameter_index == std::optional{2});
    CHECK(plugin_host.last_set_parameter_normalized_value == std::optional{0.1});
    const EditorViewState* undone_state = stateOrNull(view.last_state);
    REQUIRE(undone_state != nullptr);
    CHECK(undone_state->redo_label == std::optional<std::string>{"Edit Drive"});
}

// Plugin editor windows dispatch Undo through the same action gate as the main editor view.
TEST_CASE("EditorController routes plugin window undo", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);

    plugin_host.queuePendingPluginParameterEdit(
        common::audio::PluginParameterEdit{
            .instance_id = "instance-a",
            .parameter_id = "middle",
            .parameter_index = 4,
            .before_normalized = 0.2,
            .after_normalized = 0.8,
            .label_hint = "Middle",
        });

    plugin_host.notifyPluginWindowUndoRequested();

    CHECK(plugin_host.flush_pending_parameter_edits_call_count == 1);
    CHECK(plugin_host.set_parameter_value_call_count == 1);
    CHECK(plugin_host.last_set_parameter_instance_id == std::optional<std::string>{"instance-a"});
    CHECK(plugin_host.last_set_parameter_id == std::optional<std::string>{"middle"});
    CHECK(plugin_host.last_set_parameter_index == std::optional{4});
    CHECK(plugin_host.last_set_parameter_normalized_value == std::optional{0.2});
}

// Output gain is tracked separately and preserves earlier placement undo entries behind it.
TEST_CASE("EditorController keeps placement undo after output gain", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.front().block_index = 1;
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
    controller.onSignalChainPlacementChanged({blockAssignment("loaded-instance", 3)});

    const EditorViewState* edited_state = stateOrNull(view.last_state);
    REQUIRE(edited_state != nullptr);
    CHECK(edited_state->undo_label == std::optional<std::string>{"Move Plugin Block"});

    controller.onOutputGainChanged(-6.0);

    const EditorViewState* gain_state = stateOrNull(view.last_state);
    REQUIRE(gain_state != nullptr);
    CHECK(gain_state->undo_label == std::optional<std::string>{"Set Output Gain"});
    CHECK_FALSE(gain_state->redo_label.has_value());

    controller.onUndoRequested();
    controller.onUndoRequested();

    const EditorViewState* undone_state = stateOrNull(view.last_state);
    REQUIRE(undone_state != nullptr);
    REQUIRE(undone_state->signal_chain.plugins.size() == 1);
    CHECK(undone_state->signal_chain.plugins[0].block_index == 1);
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

    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    plugin_host.next_instance_id = "instance-b";
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

// Remove undo recreates the plugin with its original id, visual block, and display override.
TEST_CASE("EditorController undoes plugin removals", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    plugin_host.next_instance_id = "instance-b";
    addKnownPlugin(controller);
    controller.onSignalChainPlacementChanged(
        {blockAssignment("instance-a", 0), blockAssignment("instance-b", 4)});
    controller.onPluginDisplayTypeOverrideChanged("instance-a", PluginDisplayType::Cab);

    controller.onRemovePluginRequested("instance-a");

    const EditorViewState* removed_state = stateOrNull(view.last_state);
    REQUIRE(removed_state != nullptr);
    REQUIRE(removed_state->signal_chain.plugins.size() == 1);
    CHECK(removed_state->signal_chain.plugins[0].instance_id == "instance-b");
    CHECK(removed_state->signal_chain.plugins[0].block_index == 4);
    CHECK(removed_state->undo_label == std::optional<std::string>{"Remove Plugin"});

    controller.onUndoRequested();

    CHECK(plugin_host.recreate_state_call_count == 1);
    CHECK(plugin_host.last_recreate_state_index == std::optional<std::size_t>{0});
    const EditorViewState* undone_state = stateOrNull(view.last_state);
    REQUIRE(undone_state != nullptr);
    REQUIRE(undone_state->signal_chain.plugins.size() == 2);
    CHECK(undone_state->signal_chain.plugins[0].instance_id == "instance-a");
    CHECK(undone_state->signal_chain.plugins[0].block_index == 0);
    CHECK(
        undone_state->signal_chain.plugins[0].display_type_override ==
        std::optional{PluginDisplayType::Cab});
    CHECK(undone_state->signal_chain.plugins[1].instance_id == "instance-b");
    CHECK(undone_state->signal_chain.plugins[1].block_index == 4);
    CHECK(undone_state->redo_label == std::optional<std::string>{"Remove Plugin"});

    controller.onRedoRequested();

    CHECK(plugin_host.last_removed_instance_id == std::optional<std::string>{"instance-a"});
    const EditorViewState* redone_state = stateOrNull(view.last_state);
    REQUIRE(redone_state != nullptr);
    REQUIRE(redone_state->signal_chain.plugins.size() == 1);
    CHECK(redone_state->signal_chain.plugins[0].instance_id == "instance-b");
    CHECK(redone_state->signal_chain.plugins[0].block_index == 4);
    CHECK(view.shown_errors.empty());
}

// A rollback-contract violation faults the session and leaves only recovery actions available.
TEST_CASE("EditorController faults after rollback violation", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    plugin_host.next_instance_id = "instance-b";
    addKnownPlugin(controller);
    controller.onRemovePluginRequested("instance-a");
    plugin_host.next_recreate_state_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::RollbackContractViolation,
        "fake rollback contract violation",
    };

    controller.onUndoRequested();

    CHECK(plugin_host.recreate_state_call_count == 1);
    const EditorViewState* faulted_state = stateOrNull(view.last_state);
    REQUIRE(faulted_state != nullptr);
    CHECK(faulted_state->open_enabled);
    CHECK(faulted_state->import_enabled);
    CHECK(faulted_state->close_enabled);
    CHECK_FALSE(faulted_state->save_enabled);
    CHECK_FALSE(faulted_state->save_as_enabled);
    CHECK_FALSE(faulted_state->publish_enabled);
    CHECK_FALSE(faulted_state->signal_chain.insert_plugin_enabled);
    CHECK_FALSE(faulted_state->signal_chain.move_plugins_enabled);
    CHECK_FALSE(faulted_state->signal_chain.remove_plugins_enabled);
    CHECK_FALSE(faulted_state->signal_chain.output_gain_controls_enabled);
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(
        view.shown_errors.back() ==
        "An unexpected internal error left the live editor state untrusted. Please report this "
        "bug and attach the editor log file, then reopen or close the project before continuing.");

    controller.onCloseRequested();

    const EditorViewState* prompt_state = stateOrNull(view.last_state);
    REQUIRE(prompt_state != nullptr);
    CHECK(
        prompt_state->unsaved_changes_prompt ==
        std::optional{UnsavedChangesPrompt{EditorActionId::CloseProject}});
}

// Moving a plugin applies the backend's authoritative reordered chain snapshot.
TEST_CASE("EditorController moves plugins", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    plugin_host.next_instance_id = "instance-b";
    addKnownPlugin(controller);
    plugin_host.next_instance_id = "instance-c";
    addKnownPlugin(controller);

    controller.onSignalChainPlacementChanged(
        {blockAssignment("instance-a", 0),
         blockAssignment("instance-b", 3),
         blockAssignment("instance-c", 4)});

    controller.onMovePluginRequested(
        "instance-a",
        2,
        {blockAssignment("instance-a", 5),
         blockAssignment("instance-b", 1),
         blockAssignment("instance-c", 2)});

    CHECK(plugin_host.move_call_count == 1);
    CHECK(plugin_host.last_moved_instance_id == std::optional<std::string>{"instance-a"});
    CHECK(plugin_host.last_move_destination_index == std::optional<std::size_t>{2});
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    REQUIRE(final_state->signal_chain.plugins.size() == 3);
    CHECK(final_state->signal_chain.plugins[0].instance_id == "instance-b");
    CHECK(final_state->signal_chain.plugins[0].chain_index == 0);
    CHECK(final_state->signal_chain.plugins[0].block_index == 1);
    CHECK(final_state->signal_chain.plugins[1].instance_id == "instance-c");
    CHECK(final_state->signal_chain.plugins[1].chain_index == 1);
    CHECK(final_state->signal_chain.plugins[1].block_index == 2);
    CHECK(final_state->signal_chain.plugins[2].instance_id == "instance-a");
    CHECK(final_state->signal_chain.plugins[2].chain_index == 2);
    CHECK(final_state->signal_chain.plugins[2].block_index == 5);
    CHECK(view.shown_errors.empty());
}

// Move undo and redo restore backend order and editor-owned placement as one history entry.
TEST_CASE("EditorController undoes plugin moves", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    const std::vector<common::audio::PluginChainEntry> loaded_plugins{
        pluginEntry("instance-a", 0, 0),
        pluginEntry("instance-b", 1, 3),
        pluginEntry("instance-c", 2, 4),
    };
    plugin_host.chain = loaded_plugins;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins = loaded_plugins;
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

    controller.onMovePluginRequested(
        "instance-a",
        2,
        {blockAssignment("instance-a", 5),
         blockAssignment("instance-b", 1),
         blockAssignment("instance-c", 2)});

    const EditorViewState* moved_state = stateOrNull(view.last_state);
    REQUIRE(moved_state != nullptr);
    REQUIRE(moved_state->signal_chain.plugins.size() == 3);
    CHECK(moved_state->signal_chain.plugins[0].instance_id == "instance-b");
    CHECK(moved_state->signal_chain.plugins[0].block_index == 1);
    CHECK(moved_state->signal_chain.plugins[1].instance_id == "instance-c");
    CHECK(moved_state->signal_chain.plugins[1].block_index == 2);
    CHECK(moved_state->signal_chain.plugins[2].instance_id == "instance-a");
    CHECK(moved_state->signal_chain.plugins[2].block_index == 5);
    CHECK(moved_state->undo_enabled);
    CHECK(moved_state->undo_label == std::optional<std::string>{"Move Plugin"});

    controller.onUndoRequested();

    CHECK(plugin_host.last_move_destination_index == std::optional<std::size_t>{0});
    const EditorViewState* undone_state = stateOrNull(view.last_state);
    REQUIRE(undone_state != nullptr);
    REQUIRE(undone_state->signal_chain.plugins.size() == 3);
    CHECK(undone_state->signal_chain.plugins[0].instance_id == "instance-a");
    CHECK(undone_state->signal_chain.plugins[0].block_index == 0);
    CHECK(undone_state->signal_chain.plugins[1].instance_id == "instance-b");
    CHECK(undone_state->signal_chain.plugins[1].block_index == 3);
    CHECK(undone_state->signal_chain.plugins[2].instance_id == "instance-c");
    CHECK(undone_state->signal_chain.plugins[2].block_index == 4);
    CHECK(undone_state->redo_enabled);
    CHECK(undone_state->redo_label == std::optional<std::string>{"Move Plugin"});

    controller.onRedoRequested();

    CHECK(plugin_host.last_move_destination_index == std::optional<std::size_t>{2});
    const EditorViewState* redone_state = stateOrNull(view.last_state);
    REQUIRE(redone_state != nullptr);
    REQUIRE(redone_state->signal_chain.plugins.size() == 3);
    CHECK(redone_state->signal_chain.plugins[0].instance_id == "instance-b");
    CHECK(redone_state->signal_chain.plugins[0].block_index == 1);
    CHECK(redone_state->signal_chain.plugins[1].instance_id == "instance-c");
    CHECK(redone_state->signal_chain.plugins[1].block_index == 2);
    CHECK(redone_state->signal_chain.plugins[2].instance_id == "instance-a");
    CHECK(redone_state->signal_chain.plugins[2].block_index == 5);

    controller.onUndoRequested();
    controller.onCloseRequested();

    const EditorViewState* closed_state = stateOrNull(view.last_state);
    REQUIRE(closed_state != nullptr);
    CHECK_FALSE(closed_state->unsaved_changes_prompt.has_value());
}

// A same-index move request is ignored before the backend can create a no-op mutation.
TEST_CASE("EditorController ignores same-index plugin moves", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    plugin_host.next_instance_id = "instance-b";
    addKnownPlugin(controller);

    controller.onMovePluginRequested("instance-a", 0, {});

    CHECK(plugin_host.move_call_count == 0);
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    REQUIRE(final_state->signal_chain.plugins.size() == 2);
    CHECK(final_state->signal_chain.plugins[0].instance_id == "instance-a");
    CHECK(final_state->signal_chain.plugins[1].instance_id == "instance-b");
    CHECK(view.shown_errors.empty());
}

// A stale move request is ignored before the backend can mutate the chain.
TEST_CASE("EditorController ignores stale plugin moves", "[core][editor-controller]")
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

    controller.onMovePluginRequested("stale-instance", 0, {});

    CHECK(plugin_host.move_call_count == 0);
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    REQUIRE(final_state->signal_chain.plugins.size() == 1);
    CHECK(final_state->signal_chain.plugins[0].instance_id == "instance-id");
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

// Backend move failures report an error without changing controller-visible row order.
TEST_CASE("EditorController reports plugin move errors", "[core][editor-controller]")
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
    plugin_host.next_instance_id = "instance-a";
    addKnownPlugin(controller);
    plugin_host.next_instance_id = "instance-b";
    addKnownPlugin(controller);
    plugin_host.next_move_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::PluginMoveFailed,
        "backend rejected move",
    };

    controller.onMovePluginRequested(
        "instance-a", 1, {blockAssignment("instance-a", 1), blockAssignment("instance-b", 0)});

    CHECK(plugin_host.move_call_count == 1);
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    REQUIRE(final_state->signal_chain.plugins.size() == 2);
    CHECK(final_state->signal_chain.plugins[0].instance_id == "instance-a");
    CHECK(final_state->signal_chain.plugins[0].block_index == 0);
    CHECK(final_state->signal_chain.plugins[1].instance_id == "instance-b");
    CHECK(final_state->signal_chain.plugins[1].block_index == 1);
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not move plugin: backend rejected move");
}

} // namespace rock_hero::editor::core
