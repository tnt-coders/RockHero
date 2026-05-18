#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <optional>
#include <rock_hero/common/audio/i_audio.h>
#include <rock_hero/common/audio/i_audio_device_configuration.h>
#include <rock_hero/common/audio/i_live_rig.h>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/common/audio/transport_state.h>
#include <rock_hero/common/core/audio_asset.h>
#include <rock_hero/common/core/session.h>
#include <rock_hero/common/core/timeline.h>
#include <rock_hero/editor/core/editor_controller.h>
#include <rock_hero/editor/core/editor_settings.h>
#include <rock_hero/editor/core/editor_view_state.h>
#include <rock_hero/editor/core/i_editor_controller.h>
#include <rock_hero/editor/core/i_editor_task_runner.h>
#include <rock_hero/editor/core/i_editor_view.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef TEST_SETTINGS_DIR
#define TEST_SETTINGS_DIR "."
#endif

namespace rock_hero::editor::core
{

namespace
{

// Captures editor view state and transient effects pushed through the view contract.
class FakeEditorView final : public IEditorView
{
public:
    // Records the supplied state so tests can observe what a controller would render.
    void setState(const EditorViewState& state) override
    {
        last_state = state;
        pushed_states.push_back(state);
        set_state_call_count += 1;
    }

    // Records one-shot workflow errors separately from durable render state.
    void showError(const std::string& message) override
    {
        states_seen_at_errors.push_back(last_state);
        shown_errors.push_back(message);
    }

    // Runs or stores a busy-overlay paint fence callback for controller tests.
    void runAfterBusyOverlayPainted(std::function<void()> callback) override
    {
        busy_overlay_paint_callback_count += 1;
        if (callback)
        {
            callback();
        }
    }

    // Last durable state pushed to the view.
    std::optional<EditorViewState> last_state{};

    // Every durable state pushed to the view, in delivery order.
    std::vector<EditorViewState> pushed_states{};

    // One-shot error messages reported through the transient view-effect channel.
    std::vector<std::string> shown_errors{};

    // Durable state that was current when each one-shot error was shown.
    std::vector<std::optional<EditorViewState>> states_seen_at_errors{};

    // Number of durable state pushes observed by the fake.
    int set_state_call_count{0};

    // Number of busy-overlay paint callbacks requested by the controller.
    int busy_overlay_paint_callback_count{0};
};

// Returns a nullable pointer so tests can satisfy optional-access lint after a REQUIRE.
[[nodiscard]] const EditorViewState* stateOrNull(
    const std::optional<EditorViewState>& state) noexcept
{
    return state.has_value() ? &*state : nullptr;
}

// Returns a nullable pointer for the nested busy state after the owning state is known present.
[[nodiscard]] const BusyViewState* busyOrNull(const EditorViewState& state) noexcept
{
    return state.busy.has_value() ? &*state.busy : nullptr;
}

// Extracts live-rig busy states so tests can assert the progress sequence directly.
[[nodiscard]] std::vector<BusyViewState> liveRigBusyStates(const FakeEditorView& view)
{
    std::vector<BusyViewState> states;
    for (const EditorViewState& state : view.pushed_states)
    {
        if (state.busy.has_value() && state.busy->operation == BusyOperation::LoadingLiveRig)
        {
            states.push_back(*state.busy);
        }
    }

    return states;
}

// Records incoming editor intents so tests can verify the controller contract headlessly.
class FakeEditorController final : public IEditorController
{
public:
    // Captures the most recent open request made through the controller contract.
    void onOpenRequested(std::filesystem::path file) override
    {
        last_open_file = std::move(file);
        open_request_count += 1;
    }

    // Captures the most recent import request made through the controller contract.
    void onImportRequested(std::filesystem::path file) override
    {
        last_import_file = std::move(file);
        import_request_count += 1;
    }

    // Counts direct save intents emitted by the view contract.
    void onSaveRequested() override
    {
        save_request_count += 1;
    }

    // Captures the most recent Save As destination request.
    void onSaveAsRequested(std::filesystem::path file) override
    {
        last_save_as_file = std::move(file);
        save_as_request_count += 1;
    }

    // Captures the most recent publish destination request.
    void onPublishRequested(std::filesystem::path file) override
    {
        last_publish_file = std::move(file);
        publish_request_count += 1;
    }

    // Counts cancelled Save As chooser flows.
    void onSaveAsCancelled() override
    {
        save_as_cancel_count += 1;
    }

    // Counts close intents emitted by the view contract.
    void onCloseRequested() override
    {
        close_request_count += 1;
    }

    // Counts exit intents emitted by the view contract.
    void onExitRequested() override
    {
        exit_request_count += 1;
    }

    // Captures the latest unsaved-changes decision passed back from the view.
    void onUnsavedChangesDecision(UnsavedChangesDecision decision) override
    {
        last_unsaved_changes_decision = decision;
        unsaved_changes_decision_count += 1;
    }

    // Counts play/pause transport intents emitted by the view.
    void onPlayPausePressed() override
    {
        play_pause_press_count += 1;
    }

    // Counts stop transport intents emitted by the view.
    void onStopPressed() override
    {
        stop_press_count += 1;
    }

    // Captures the latest normalized timeline click.
    void onWaveformClicked(double normalized_x) override
    {
        last_normalized_x = normalized_x;
        waveform_click_count += 1;
    }

    // Captures plugin files selected through the controller contract.
    void onAddPluginRequested(std::filesystem::path file) override
    {
        last_plugin_file = std::move(file);
        add_plugin_request_count += 1;
    }

    // Captures plugin instance IDs selected through the controller contract.
    void onRemovePluginRequested(std::string instance_id) override
    {
        last_removed_plugin_instance_id = std::move(instance_id);
        remove_plugin_request_count += 1;
    }

    // Last file passed to onOpenRequested().
    std::optional<std::filesystem::path> last_open_file{};

    // Last file passed to onImportRequested().
    std::optional<std::filesystem::path> last_import_file{};

    // Last destination passed to onSaveAsRequested().
    std::optional<std::filesystem::path> last_save_as_file{};

    // Last destination passed to onPublishRequested().
    std::optional<std::filesystem::path> last_publish_file{};

    // Last normalized timeline click emitted by the view.
    std::optional<double> last_normalized_x{};

    // Last plugin file selected through the controller contract.
    std::optional<std::filesystem::path> last_plugin_file{};

    // Last plugin instance ID selected through the controller contract.
    std::optional<std::string> last_removed_plugin_instance_id{};

    // Last unsaved-changes decision emitted by the view.
    std::optional<UnsavedChangesDecision> last_unsaved_changes_decision{};

    // Number of open intents received.
    int open_request_count{0};

    // Number of import intents received.
    int import_request_count{0};

    // Number of save intents received.
    int save_request_count{0};

    // Number of Save As intents received.
    int save_as_request_count{0};

    // Number of publish intents received.
    int publish_request_count{0};

    // Number of Save As cancellation intents received.
    int save_as_cancel_count{0};

    // Number of close intents received.
    int close_request_count{0};

    // Number of exit intents received.
    int exit_request_count{0};

    // Number of unsaved-changes decisions received.
    int unsaved_changes_decision_count{0};

    // Number of play/pause intents received.
    int play_pause_press_count{0};

    // Number of stop intents received.
    int stop_press_count{0};

    // Number of waveform-click intents received.
    int waveform_click_count{0};

    // Number of add-plugin intents received.
    int add_plugin_request_count{0};

    // Number of remove-plugin intents received.
    int remove_plugin_request_count{0};
};

// Records control intents and exposes a manual notification hook for controller tests.
class FakeTransport final : public common::audio::ITransport
{
public:
    // Records that playback was requested without mutating state unless a test does it directly.
    void play() override
    {
        ++play_call_count;
    }

    // Records that pause was requested without mutating state unless a test does it directly.
    void pause() override
    {
        ++pause_call_count;
    }

    // Simulates stop semantics by clearing playback and returning to timeline start.
    void stop() override
    {
        current_state.playing = false;
        current_position = common::core::TimePosition{};
        ++stop_call_count;
    }

    // Records the requested seek so tests can verify clamping and timeline scaling.
    void seek(common::core::TimePosition position) override
    {
        last_seek_position = position;
        current_position = position;
        ++seek_call_count;
    }

    // Returns the manually controlled coarse transport state.
    [[nodiscard]] common::audio::TransportState state() const noexcept override
    {
        return current_state;
    }

    // Returns the manually controlled current cursor position.
    [[nodiscard]] common::core::TimePosition position() const noexcept override
    {
        return current_position;
    }

    // Registers a non-owning listener pointer for manual transition notifications.
    void addListener(Listener& listener) override
    {
        listeners.push_back(&listener);
    }

    // Removes a previously registered listener pointer.
    void removeListener(Listener& listener) override
    {
        std::erase(listeners, &listener);
    }

    // Updates the state and fires a coarse listener callback to mimic a real transition.
    void setStateAndNotify(const common::audio::TransportState& new_state)
    {
        current_state = new_state;
        for (Listener* listener : listeners)
        {
            listener->onTransportStateChanged(current_state);
        }
    }

    // Coarse transport state returned by state() and sent to listeners.
    common::audio::TransportState current_state{};

    // Current cursor position returned by position().
    common::core::TimePosition current_position{};

    // Non-owning listeners subscribed by the controller under test.
    std::vector<Listener*> listeners{};

    // Last seek position requested through the transport port.
    std::optional<common::core::TimePosition> last_seek_position{};

    // Number of play requests received.
    int play_call_count{0};

    // Number of pause requests received.
    int pause_call_count{0};

    // Number of stop requests received.
    int stop_call_count{0};

    // Number of seek requests received.
    int seek_call_count{0};
};

// Configurable IAudio fake that records calls and can simulate reentrant notifications.
class FakeAudio final : public common::audio::IAudio
{
public:
    // Records project-audio preparation and fills accepted arrangement durations.
    bool prepareSong(common::core::Song& song) override
    {
        ++prepare_song_call_count;
        if (!next_prepare_result)
        {
            return false;
        }

        for (common::core::Arrangement& arrangement : song.arrangements)
        {
            last_prepared_audio_asset = arrangement.audio_asset;
            ++prepared_audio_asset_count;
            if (!failed_prepare_audio_path.empty() &&
                arrangement.audio_asset.path == failed_prepare_audio_path)
            {
                return false;
            }
            arrangement.audio_duration = next_prepared_audio_duration;
        }

        return true;
    }

    // Records the active arrangement and optionally fires an injected reentrant action.
    bool setActiveArrangement(const common::core::Arrangement& arrangement) override
    {
        last_active_audio_asset = arrangement.audio_asset;
        ++set_active_arrangement_call_count;
        if (during_active_arrangement_action)
        {
            during_active_arrangement_action();
        }
        return next_set_active_arrangement_result;
    }

    // Records that the active backend arrangement should be cleared.
    void clearActiveArrangement() override
    {
        last_active_audio_asset.reset();
        ++clear_active_arrangement_call_count;
    }

    // Duration assigned to arrangements during successful preparation.
    common::core::TimeDuration next_prepared_audio_duration{common::core::TimeDuration{4.0}};

    // Controls whether the next prepareSong() call accepts the project song.
    bool next_prepare_result{true};

    // Controls whether the next setActiveArrangement() call accepts the arrangement.
    bool next_set_active_arrangement_result{true};

    // Number of song-preparation calls received.
    int prepare_song_call_count{0};

    // Number of arrangement assets visited during preparation.
    int prepared_audio_asset_count{0};

    // Number of active-arrangement replacement calls received.
    int set_active_arrangement_call_count{0};

    // Number of active-arrangement clear calls received.
    int clear_active_arrangement_call_count{0};

    // Specific arrangement audio path that should fail during preparation.
    std::filesystem::path failed_prepare_audio_path{};

    // Last arrangement audio asset observed during preparation.
    std::optional<common::core::AudioAsset> last_prepared_audio_asset{};

    // Last active arrangement audio asset accepted by the fake backend.
    std::optional<common::core::AudioAsset> last_active_audio_asset{};

    // Optional callback fired from setActiveArrangement() to test reentrant refreshes.
    std::function<void()> during_active_arrangement_action{};
};

// Configurable plugin-host fake that records scanning and chain mutation.
class FakePluginHost final : public common::audio::IPluginHost
{
public:
    // Returns the configured scan candidates or scan error.
    [[nodiscard]] std::expected<
        std::vector<common::audio::PluginCandidate>, common::audio::PluginHostError>
    scanPluginFile(const std::filesystem::path& plugin_path) override
    {
        last_scanned_file = plugin_path;
        scan_call_count += 1;
        if (next_scan_error.has_value())
        {
            return std::unexpected{*next_scan_error};
        }

        return next_candidates;
    }

    // Returns success or the configured validation error while recording the candidate ID.
    [[nodiscard]] std::expected<void, common::audio::PluginHostError> validatePluginLoad(
        const std::string& plugin_id) override
    {
        last_validated_plugin_id = plugin_id;
        validate_call_count += 1;
        if (next_validate_error.has_value())
        {
            return std::unexpected{*next_validate_error};
        }

        return {};
    }

    // Returns the configured insertion handle or insertion error.
    [[nodiscard]] std::expected<common::audio::PluginHandle, common::audio::PluginHostError>
    addPlugin(const std::string& plugin_id) override
    {
        last_added_plugin_id = plugin_id;
        add_call_count += 1;
        if (next_add_error.has_value())
        {
            return std::unexpected{*next_add_error};
        }

        common::audio::PluginHandle handle = next_handle;
        handle.plugin_id = plugin_id;
        return handle;
    }

    // Returns success or the configured removal error while recording the requested instance.
    [[nodiscard]] std::expected<void, common::audio::PluginHostError> removePlugin(
        const std::string& instance_id) override
    {
        last_removed_instance_id = instance_id;
        remove_call_count += 1;
        if (next_remove_error.has_value())
        {
            return std::unexpected{*next_remove_error};
        }

        return {};
    }

    // Candidates returned by the next successful scan.
    std::vector<common::audio::PluginCandidate> next_candidates{common::audio::PluginCandidate{
        .id = "plugin-id",
        .name = "Amp Sim",
        .manufacturer = "Example Audio",
        .format_name = "VST3",
        .file_path = std::filesystem::path{"amp.vst3"},
    }};

    // Handle returned by the next successful plugin insertion.
    common::audio::PluginHandle next_handle{
        .instance_id = "instance-id",
        .plugin_id = {},
        .chain_index = 0,
    };

    // Optional scan error returned instead of candidates.
    std::optional<common::audio::PluginHostError> next_scan_error{};

    // Optional insertion error returned instead of a handle.
    std::optional<common::audio::PluginHostError> next_add_error{};

    // Optional load-validation error returned instead of success.
    std::optional<common::audio::PluginHostError> next_validate_error{};

    // Optional removal error returned instead of success.
    std::optional<common::audio::PluginHostError> next_remove_error{};

    // Last plugin file passed to scanPluginFile().
    std::optional<std::filesystem::path> last_scanned_file{};

    // Last candidate ID passed to addPlugin().
    std::optional<std::string> last_added_plugin_id{};

    // Last candidate ID passed to validatePluginLoad().
    std::optional<std::string> last_validated_plugin_id{};

    // Last instance ID passed to removePlugin().
    std::optional<std::string> last_removed_instance_id{};

    // Number of scan calls received.
    int scan_call_count{0};

    // Number of insertion calls received.
    int add_call_count{0};

    // Number of load-validation calls received.
    int validate_call_count{0};

    // Number of removal calls received.
    int remove_call_count{0};
};

// Configurable live rig fake that records project-boundary save and restore requests.
class FakeLiveRig final : public common::audio::ILiveRig
{
public:
    // Returns the configured capture snapshot or error while recording the save request.
    [[nodiscard]] std::expected<common::audio::LiveRigSnapshot, common::audio::LiveRigError>
    captureActiveRig(const common::audio::LiveRigCaptureRequest& request) override
    {
        last_capture_request = request;
        capture_call_count += 1;
        if (next_capture_error.has_value())
        {
            return std::unexpected{*next_capture_error};
        }

        return next_capture_snapshot;
    }

    // Drives the configured load result or error through the new async callback contract while
    // recording the request and replicating the engine's per-plugin progress sequence.
    void loadRig(
        common::audio::LiveRigLoadRequest request,
        common::audio::LiveRigLoadResultCallback on_result) override
    {
        last_load_request = request;
        load_call_count += 1;

        if (request.progress_callback)
        {
            const std::size_t total_plugins = next_load_result.plugins.size();
            request.progress_callback(
                common::audio::LiveRigLoadProgress{
                    .completed_plugins = 0,
                    .total_plugins = total_plugins,
                });
            for (std::size_t plugin_index = 0; plugin_index < total_plugins; ++plugin_index)
            {
                const common::audio::LiveRigPlugin& plugin = next_load_result.plugins[plugin_index];
                request.progress_callback(
                    common::audio::LiveRigLoadProgress{
                        .completed_plugins = plugin_index,
                        .total_plugins = total_plugins,
                        .active_plugin_index = plugin_index,
                        .active_plugin_name = plugin.name,
                    });
                request.progress_callback(
                    common::audio::LiveRigLoadProgress{
                        .completed_plugins = plugin_index + 1,
                        .total_plugins = total_plugins,
                        .active_plugin_index = plugin_index,
                        .active_plugin_name = plugin.name,
                    });
            }
        }

        std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError> result =
            next_load_result;
        if (next_load_error.has_value())
        {
            result = std::unexpected{*next_load_error};
        }

        if (defer_load_completion)
        {
            pending_load = PendingLoad{
                .on_result = std::move(on_result),
                .result = std::move(result),
            };
            return;
        }

        on_result(std::move(result));
    }

    // Completes one deferred load so tests can supersede the busy operation before delivery.
    [[nodiscard]] bool completePendingLoad()
    {
        if (!pending_load.has_value())
        {
            return false;
        }

        PendingLoad pending = std::move(*pending_load);
        pending_load.reset();
        if (pending.on_result)
        {
            pending.on_result(std::move(pending.result));
        }
        return true;
    }

    // Records explicit clear requests made during project teardown.
    [[nodiscard]] std::expected<void, common::audio::LiveRigError> clearRig() override
    {
        clear_call_count += 1;
        if (next_clear_error.has_value())
        {
            return std::unexpected{*next_clear_error};
        }

        return {};
    }

    // Snapshot returned by the next successful capture.
    common::audio::LiveRigSnapshot next_capture_snapshot{
        .tone_document_ref = "tones/lead.tone.json",
        .plugins = {
            common::audio::LiveRigPlugin{
                .instance_id = "captured-instance",
                .plugin_id = "captured-plugin",
                .name = "Captured Amp",
                .manufacturer = "Example Audio",
                .format_name = "VST3",
                .chain_index = 0,
            },
        },
    };

    // Result returned by the next successful load.
    common::audio::LiveRigLoadResult next_load_result{
        .plugins = {
            common::audio::LiveRigPlugin{
                .instance_id = "loaded-instance",
                .plugin_id = "loaded-plugin",
                .name = "Loaded Amp",
                .manufacturer = "Example Audio",
                .format_name = "VST3",
                .chain_index = 0,
            },
        },
    };

    // Optional capture error returned instead of the configured snapshot.
    std::optional<common::audio::LiveRigError> next_capture_error{};

    // Optional load error returned instead of the configured load result.
    std::optional<common::audio::LiveRigError> next_load_error{};

    // Optional clear error returned instead of success.
    std::optional<common::audio::LiveRigError> next_clear_error{};

    // When set, loadRig stores its completion so tests can finish it explicitly.
    bool defer_load_completion{false};

    // Last capture request observed by the fake.
    std::optional<common::audio::LiveRigCaptureRequest> last_capture_request{};

    // Last load request observed by the fake.
    std::optional<common::audio::LiveRigLoadRequest> last_load_request{};

    // Number of capture calls received.
    int capture_call_count{0};

    // Number of load calls received.
    int load_call_count{0};

    // Number of clear calls received.
    int clear_call_count{0};

private:
    // Deferred live-rig completion captured with the result configured at load time.
    struct PendingLoad
    {
        common::audio::LiveRigLoadResultCallback on_result;
        std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError> result{};
    };

    std::optional<PendingLoad> pending_load{};
};

// Minimal audio-device-configuration fake exposing a real juce::AudioDeviceManager.
class FakeAudioDeviceConfiguration final : public common::audio::IAudioDeviceConfiguration
{
public:
    // Returns the fake-owned device manager so consumers can drive it from tests.
    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept override
    {
        return device_manager;
    }

    // Returns the test-configured current device name.
    [[nodiscard]] std::optional<std::string> currentDeviceName() const override
    {
        current_call_count += 1;
        return current_name;
    }

    // Stores a listener pointer so tests can notify it manually through notifyChanged().
    void addListener(common::audio::IAudioDeviceConfiguration::Listener& listener) override
    {
        listeners.push_back(&listener);
    }

    // Removes a previously registered listener.
    void removeListener(common::audio::IAudioDeviceConfiguration::Listener& listener) override
    {
        std::erase(listeners, &listener);
    }

    // Fires a change broadcast to every registered listener, mirroring real device updates.
    void notifyChanged()
    {
        for (auto* listener : listeners)
        {
            listener->onAudioDeviceConfigurationChanged();
        }
    }

    // JUCE device manager owned by the fake; tests may initialise it explicitly.
    juce::AudioDeviceManager device_manager{};

    // Current device name returned by currentDeviceName().
    std::optional<std::string> current_name{};

    // Non-owning listeners subscribed by the controller under test.
    std::vector<common::audio::IAudioDeviceConfiguration::Listener*> listeners{};

    // Number of current-device-name reads received.
    mutable int current_call_count{0};
};

// Provides controller-facing project service callbacks without touching the filesystem.
class FakeProjectServices final
{
public:
    // Simulates opening a project package and returns either next_song or the open error.
    std::expected<common::core::Song, ProjectError> open(
        Project&, const std::filesystem::path& file)
    {
        last_open_file = file;
        ++open_call_count;
        if (!next_song.has_value())
        {
            return std::unexpected{ProjectError{
                ProjectErrorCode::MissingProjectPackage,
                next_error_message,
            }};
        }

        common::core::Song song = std::move(*next_song);
        next_song.reset();
        return song;
    }

    // Simulates importing a song source and returns either next_import_song or the import error.
    std::expected<common::core::Song, ProjectError> import(
        Project&, const std::filesystem::path& file)
    {
        last_import_file = file;
        ++import_call_count;
        if (!next_import_song.has_value())
        {
            return std::unexpected{ProjectError{
                ProjectErrorCode::SongImportFailed,
                next_import_error_message,
            }};
        }

        common::core::Song song = std::move(*next_import_song);
        next_import_song.reset();
        return song;
    }

    // Simulates saving through the current project destination.
    std::expected<void, ProjectError> save(
        Project&, const common::core::Song& song, ProjectEditorState editor_state)
    {
        last_save_audio_path = firstAudioPath(song);
        last_save_tone_document_ref = firstToneDocumentRef(song);
        last_save_editor_state = std::move(editor_state);
        ++save_call_count;
        if (next_save_error.has_value())
        {
            return std::unexpected{ProjectError{
                ProjectErrorCode::CouldNotWriteProjectFiles,
                *next_save_error,
            }};
        }
        return std::expected<void, ProjectError>{};
    }

    // Simulates Save As and records the destination that becomes the project file.
    std::expected<void, ProjectError> saveAs(
        Project&, const std::filesystem::path& file, const common::core::Song& song,
        ProjectEditorState editor_state)
    {
        last_save_as_file = file;
        last_save_as_audio_path = firstAudioPath(song);
        last_save_as_tone_document_ref = firstToneDocumentRef(song);
        last_save_as_editor_state = std::move(editor_state);
        ++save_as_call_count;
        if (next_save_as_error.has_value())
        {
            return std::unexpected{ProjectError{
                ProjectErrorCode::CouldNotWritePackage,
                *next_save_as_error,
            }};
        }
        return std::expected<void, ProjectError>{};
    }

    // Simulates publishing a native package without changing project save state.
    std::expected<void, ProjectError> publish(
        Project&, const std::filesystem::path& file, const common::core::Song& song)
    {
        last_publish_file = file;
        last_publish_audio_path = firstAudioPath(song);
        last_publish_tone_document_ref = firstToneDocumentRef(song);
        ++publish_call_count;
        if (next_publish_error.has_value())
        {
            return std::unexpected{ProjectError{
                ProjectErrorCode::CouldNotPublishSong,
                *next_publish_error,
            }};
        }
        return std::expected<void, ProjectError>{};
    }

    // Returns the bound callback shape expected by EditorController services.
    [[nodiscard]] EditorController::OpenFunction openFunction() noexcept
    {
        return [this](Project& project, const std::filesystem::path& file) {
            return open(project, file);
        };
    }

    // Returns the bound import callback shape expected by EditorController services.
    [[nodiscard]] EditorController::ImportFunction importFunction() noexcept
    {
        return [this](Project& project, const std::filesystem::path& file) {
            return import(project, file);
        };
    }

    // Returns the bound save callback shape expected by EditorController services.
    [[nodiscard]] EditorController::SaveFunction saveFunction() noexcept
    {
        return
            [this](
                Project& project, const common::core::Song& song, ProjectEditorState editor_state) {
                return save(project, song, std::move(editor_state));
            };
    }

    // Returns the bound Save As callback shape expected by EditorController services.
    [[nodiscard]] EditorController::SaveAsFunction saveAsFunction() noexcept
    {
        return [this](
                   Project& project,
                   const std::filesystem::path& file,
                   const common::core::Song& song,
                   ProjectEditorState editor_state) {
            return saveAs(project, file, song, std::move(editor_state));
        };
    }

    // Returns the bound publish callback shape expected by EditorController services.
    [[nodiscard]] EditorController::PublishFunction publishFunction() noexcept
    {
        return [this](
                   Project& project,
                   const std::filesystem::path& file,
                   const common::core::Song& song) { return publish(project, file, song); };
    }

    // Song returned by the next open call, or empty to force an open error.
    std::optional<common::core::Song> next_song{};

    // Song returned by the next import call, or empty to force an import error.
    std::optional<common::core::Song> next_import_song{};

    // Error returned by open() when next_song is empty.
    std::string next_error_message{"Open failed"};

    // Error returned by import() when next_import_song is empty.
    std::string next_import_error_message{"Import failed"};

    // Error returned by save(), when present.
    std::optional<std::string> next_save_error{};

    // Error returned by saveAs(), when present.
    std::optional<std::string> next_save_as_error{};

    // Error returned by publish(), when present.
    std::optional<std::string> next_publish_error{};

    // Last file passed to open().
    std::optional<std::filesystem::path> last_open_file{};

    // Last file passed to import().
    std::optional<std::filesystem::path> last_import_file{};

    // Last destination passed to saveAs().
    std::optional<std::filesystem::path> last_save_as_file{};

    // Last destination passed to publish().
    std::optional<std::filesystem::path> last_publish_file{};

    // First arrangement audio path seen by save().
    std::optional<std::filesystem::path> last_save_audio_path{};

    // First arrangement audio path seen by saveAs().
    std::optional<std::filesystem::path> last_save_as_audio_path{};

    // First arrangement audio path seen by publish().
    std::optional<std::filesystem::path> last_publish_audio_path{};

    // First arrangement tone document reference seen by save().
    std::optional<std::string> last_save_tone_document_ref{};

    // First arrangement tone document reference seen by saveAs().
    std::optional<std::string> last_save_as_tone_document_ref{};

    // First arrangement tone document reference seen by publish().
    std::optional<std::string> last_publish_tone_document_ref{};

    // Editor state captured by save().
    std::optional<ProjectEditorState> last_save_editor_state{};

    // Editor state captured by saveAs().
    std::optional<ProjectEditorState> last_save_as_editor_state{};

    // Number of open calls received.
    int open_call_count{0};

    // Number of import calls received.
    int import_call_count{0};

    // Number of save calls received.
    int save_call_count{0};

    // Number of Save As calls received.
    int save_as_call_count{0};

    // Number of publish calls received.
    int publish_call_count{0};

private:
    // Returns the first arrangement audio path to verify the saved session content.
    [[nodiscard]] static std::optional<std::filesystem::path> firstAudioPath(
        const common::core::Song& song)
    {
        if (song.arrangements.empty())
        {
            return std::nullopt;
        }

        const common::core::AudioAsset& audio_asset = song.arrangements.front().audio_asset;
        if (audio_asset.path.empty())
        {
            return std::nullopt;
        }

        return audio_asset.path;
    }

    // Returns the first arrangement tone reference so tests can verify persisted song content.
    [[nodiscard]] static std::optional<std::string> firstToneDocumentRef(
        const common::core::Song& song)
    {
        if (song.arrangements.empty())
        {
            return std::nullopt;
        }

        return song.arrangements.front().tone_document_ref;
    }
};

// Owns build-local settings and project files used by restore/exit persistence tests.
class ScopedControllerFiles final
{
public:
    // Creates paired settings/project paths and removes stale files from previous runs.
    explicit ScopedControllerFiles(std::string_view base_name)
        : m_settings_file(
              std::filesystem::path{TEST_SETTINGS_DIR} / (std::string{base_name} + ".settings"))
        , m_project_file(
              std::filesystem::path{TEST_SETTINGS_DIR} / (std::string{base_name} + ".rhp"))
    {
        removeFiles();
    }

    // Cleans up both files so restore tests do not leak persisted state.
    ~ScopedControllerFiles()
    {
        removeFiles();
    }

    ScopedControllerFiles(const ScopedControllerFiles&) = delete;
    ScopedControllerFiles& operator=(const ScopedControllerFiles&) = delete;
    ScopedControllerFiles(ScopedControllerFiles&&) = delete;
    ScopedControllerFiles& operator=(ScopedControllerFiles&&) = delete;

    // Returns the settings file owned by this fixture.
    [[nodiscard]] const std::filesystem::path& settingsFile() const noexcept
    {
        return m_settings_file;
    }

    // Returns the project file path owned by this fixture.
    [[nodiscard]] const std::filesystem::path& projectFile() const noexcept
    {
        return m_project_file;
    }

    // Creates a real placeholder project file so startup restore sees an existing path.
    void createProjectFile() const
    {
        std::ofstream project_file{m_project_file};
        project_file << "test project";
    }

private:
    // Removes both fixture files on a best-effort basis.
    void removeFiles() const
    {
        std::error_code error;
        std::filesystem::remove(m_settings_file, error);
        std::filesystem::remove(m_project_file, error);
    }

    // Build-local settings file used by restore and exit persistence tests.
    std::filesystem::path m_settings_file;

    // Build-local project file used by restore and exit persistence tests.
    std::filesystem::path m_project_file;
};

// Provides a standard loaded-content range for controller tests.
[[nodiscard]] common::core::TimeRange loadedTimelineRange(double end_seconds = 4.0) noexcept
{
    return common::core::TimeRange{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{end_seconds},
    };
}

// Builds song data with one arrangement.
[[nodiscard]] common::core::Song makeSong(
    std::filesystem::path path, common::core::TimeRange timeline_range = loadedTimelineRange(),
    std::string tone_document_ref = {})
{
    common::core::Song song;
    song.arrangements.push_back(
        common::core::Arrangement{
            .id = "lead",
            .part = common::core::Part::Lead,
            .difficulty = common::core::DifficultyRating{},
            .audio_asset = common::core::AudioAsset{std::move(path)},
            .audio_duration = timeline_range.duration(),
            .tone_document_ref = std::move(tone_document_ref),
            .note_events = {},
        });

    return song;
}

// Builds song data with two arrangements so controller selection policy can be tested.
[[nodiscard]] common::core::Song makeTwoArrangementSong(
    std::filesystem::path lead_path, std::filesystem::path bass_path)
{
    common::core::Song song;
    song.arrangements.push_back(
        common::core::Arrangement{
            .id = "lead",
            .part = common::core::Part::Lead,
            .difficulty = common::core::DifficultyRating{},
            .audio_asset = common::core::AudioAsset{std::move(lead_path)},
            .audio_duration = common::core::TimeDuration{},
            .tone_document_ref = {},
            .note_events = {},
        });
    song.arrangements.push_back(
        common::core::Arrangement{
            .id = "bass",
            .part = common::core::Part::Bass,
            .difficulty = common::core::DifficultyRating{},
            .audio_asset = common::core::AudioAsset{std::move(bass_path)},
            .audio_duration = common::core::TimeDuration{},
            .tone_document_ref = {},
            .note_events = {},
        });

    return song;
}

// Loads arrangement audio through the controller so tests keep backend/session coupling.
void loadArrangement(
    EditorController& controller, FakeProjectServices& project_services, FakeAudio& audio,
    std::filesystem::path path, common::core::TimeRange timeline_range = loadedTimelineRange())
{
    audio.next_prepared_audio_duration = timeline_range.duration();
    audio.next_set_active_arrangement_result = true;
    project_services.next_song = makeSong(std::move(path), timeline_range);
    controller.onOpenRequested(std::filesystem::path{"loaded.rhp"});
    REQUIRE(controller.session().currentArrangement() != nullptr);
}

// Exposes stop enabledness as an optional value so tests can assert presence and value together.
[[nodiscard]] std::optional<bool> lastStopEnabled(const FakeEditorView& view)
{
    const EditorViewState* state = stateOrNull(view.last_state);
    if (state == nullptr)
    {
        return std::nullopt;
    }

    return state->stop_enabled;
}

// Task runner fake that lets tests defer completions to simulate async behavior. submit() runs
// work synchronously (no real worker thread) and stores completion for later. Tests fire stored
// completions through runPendingCompletions() so they can assert state between begin and end of
// a busy operation, including stale-completion scenarios.
class DeferredEditorTaskRunner final : public IEditorTaskRunner
{
public:
    void submit(std::function<void()> work, std::function<void()> completion) override
    {
        if (work)
        {
            work();
        }
        m_pending.push_back(std::move(completion));
    }

    // Fires every stored completion in submission order. Captures are moved out first so a
    // completion that submits new work does not re-enter the active vector.
    void runPendingCompletions()
    {
        std::vector<std::function<void()>> to_run;
        to_run.swap(m_pending);
        for (auto& fn : to_run)
        {
            if (fn)
            {
                fn();
            }
        }
    }

    // Reports how many completions are waiting to run.
    [[nodiscard]] std::size_t pendingCount() const noexcept
    {
        return m_pending.size();
    }

private:
    std::vector<std::function<void()>> m_pending{};
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
    CHECK(empty_state.suggested_publish_file.empty());
    CHECK(empty_state.close_enabled == false);
    CHECK(empty_state.project_loaded == false);
    CHECK(empty_state.save_requires_destination == false);
    CHECK(empty_state.play_pause_enabled == false);
    CHECK(empty_state.stop_enabled == false);
    CHECK(empty_state.play_pause_shows_pause_icon == false);
    CHECK_FALSE(empty_state.current_audio_device_name.has_value());
    CHECK(empty_state.audio_devices_available == false);
    CHECK(empty_state.visible_timeline == common::core::TimeRange{});
    CHECK_FALSE(empty_state.arrangement.hasAudio());
    CHECK(empty_state.signal_chain.add_plugin_enabled == false);
    CHECK(empty_state.signal_chain.remove_plugins_enabled == false);
    CHECK(empty_state.signal_chain.plugins.empty());
    CHECK_FALSE(empty_state.unsaved_changes_prompt.has_value());
    CHECK_FALSE(empty_state.save_as_prompt.has_value());

    const common::core::AudioAsset audio_asset{std::filesystem::path{"full_mix.wav"}};
    const EditorViewState loaded_state{
        .open_enabled = true,
        .import_enabled = true,
        .save_enabled = true,
        .save_as_enabled = true,
        .publish_enabled = true,
        .suggested_publish_file = std::filesystem::path{"saved.rock"},
        .close_enabled = true,
        .project_loaded = true,
        .save_requires_destination = false,
        .play_pause_enabled = true,
        .stop_enabled = true,
        .play_pause_shows_pause_icon = true,
        .current_audio_device_name = std::string{"ASIO Driver"},
        .audio_devices_available = true,
        .visible_timeline = loadedTimelineRange(180.0),
        .arrangement =
            ArrangementViewState{
                .audio_asset = audio_asset,
                .audio_duration = common::core::TimeDuration{180.0},
            },
        .signal_chain =
            SignalChainViewState{
                .add_plugin_enabled = true,
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
            },
        .unsaved_changes_prompt = UnsavedChangesPrompt{EditorActionId::CloseProject},
        .save_as_prompt = SaveAsPrompt{EditorActionId::CloseProject},
        .busy = std::nullopt,
    };

    CHECK(loaded_state.arrangement.audio_asset == std::optional{audio_asset});
    CHECK(loaded_state.current_audio_device_name == std::optional<std::string>{"ASIO Driver"});
    CHECK(loaded_state.audio_devices_available);
    CHECK(loaded_state.arrangement.audioTimelineRange() == loadedTimelineRange(180.0));
    CHECK(loaded_state.arrangement.hasAudio());
    CHECK(loaded_state.signal_chain.add_plugin_enabled);
    CHECK(loaded_state.signal_chain.remove_plugins_enabled);
    REQUIRE(loaded_state.signal_chain.plugins.size() == 1);
    CHECK(loaded_state.signal_chain.plugins[0].name == "Amp Sim");
    CHECK(
        loaded_state.unsaved_changes_prompt ==
        std::optional{UnsavedChangesPrompt{EditorActionId::CloseProject}});
    CHECK(loaded_state.save_as_prompt == std::optional{SaveAsPrompt{EditorActionId::CloseProject}});
}

// Verifies a fake controller can receive editor intents without JUCE callback types.
TEST_CASE("IEditorController fake receives editor intents", "[core][editor-controller]")
{
    FakeEditorController controller;
    const std::filesystem::path open_file{"song.rhp"};
    const std::filesystem::path import_file{"song.psarc"};
    const std::filesystem::path save_as_file{"saved.rhp"};
    const std::filesystem::path publish_file{"saved.rock"};

    controller.onOpenRequested(open_file);
    controller.onImportRequested(import_file);
    controller.onSaveRequested();
    controller.onSaveAsRequested(save_as_file);
    controller.onPublishRequested(publish_file);
    controller.onSaveAsCancelled();
    controller.onCloseRequested();
    controller.onExitRequested();
    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Discard);
    controller.onPlayPausePressed();
    controller.onStopPressed();
    controller.onWaveformClicked(0.75);
    controller.onAddPluginRequested(std::filesystem::path{"amp.vst3"});
    controller.onRemovePluginRequested("instance-id");

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
    CHECK(controller.close_request_count == 1);
    CHECK(controller.exit_request_count == 1);
    CHECK(controller.unsaved_changes_decision_count == 1);
    CHECK(
        controller.last_unsaved_changes_decision == std::optional{UnsavedChangesDecision::Discard});
    CHECK(controller.play_pause_press_count == 1);
    CHECK(controller.stop_press_count == 1);
    CHECK(controller.waveform_click_count == 1);
    CHECK(controller.last_normalized_x == std::optional{0.75});
    CHECK(controller.add_plugin_request_count == 1);
    CHECK(controller.last_plugin_file == std::optional{std::filesystem::path{"amp.vst3"}});
    CHECK(controller.remove_plugin_request_count == 1);
    CHECK(controller.last_removed_plugin_instance_id == std::optional<std::string>{"instance-id"});
}

// Verifies the controller publishes the current audio-device name through view state.
TEST_CASE("EditorController publishes current audio device", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeAudioDeviceConfiguration audio_devices;
    audio_devices.current_name = std::string{"Interface A"};
    EditorController controller{transport, audio, audio_devices};
    FakeEditorView view;

    controller.attachView(view);

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.audio_devices_available);
        CHECK(state.current_audio_device_name == std::optional<std::string>{"Interface A"});
    }
}

// A loaded arrangement with a plugin host enables the add-plugin command.
TEST_CASE("EditorController enables plugin add after load", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakePluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        plugin_host,
        EditorController::Services{.open_function = project_services.openFunction()}
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

    loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"});

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& loaded_state = view.last_state.value();
        CHECK(loaded_state.signal_chain.add_plugin_enabled);
        CHECK_FALSE(loaded_state.signal_chain.remove_plugins_enabled);
        CHECK(loaded_state.signal_chain.plugins.empty());
    }
}

// Adding a plugin scans the selected file and publishes the inserted chain item.
TEST_CASE("EditorController adds a plugin", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakePluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        plugin_host,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    FakeEditorView view;
    controller.attachView(view);
    loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"});

    controller.onAddPluginRequested(std::filesystem::path{"amp.vst3"});

    CHECK(plugin_host.scan_call_count == 1);
    CHECK(plugin_host.last_scanned_file == std::optional{std::filesystem::path{"amp.vst3"}});
    CHECK(plugin_host.validate_call_count == 1);
    CHECK(plugin_host.last_validated_plugin_id == std::optional<std::string>{"plugin-id"});
    CHECK(plugin_host.add_call_count == 1);
    CHECK(plugin_host.last_added_plugin_id == std::optional<std::string>{"plugin-id"});
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(view.busy_overlay_paint_callback_count == 1);
    REQUIRE(final_state->signal_chain.plugins.size() == 1);
    CHECK(final_state->signal_chain.add_plugin_enabled);
    CHECK(final_state->signal_chain.remove_plugins_enabled);
    CHECK(final_state->signal_chain.plugins[0].instance_id == "instance-id");
    CHECK(final_state->signal_chain.plugins[0].plugin_id == "plugin-id");
    CHECK(final_state->signal_chain.plugins[0].name == "Amp Sim");
    CHECK(final_state->signal_chain.plugins[0].manufacturer == "Example Audio");
    CHECK(final_state->signal_chain.plugins[0].format_name == "VST3");
    CHECK(final_state->signal_chain.plugins[0].chain_index == 0);
    CHECK(view.shown_errors.empty());
}

// Opening a project restores the selected arrangement's tone document through the live rig port.
TEST_CASE("EditorController loads live rig on open", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakePluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        plugin_host,
        live_rig,
        EditorController::Services{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song =
        makeSong(std::filesystem::path{"song.wav"}, loadedTimelineRange(), "tones/lead.tone.json");

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    CHECK(live_rig.load_call_count == 1);
    REQUIRE(live_rig.last_load_request.has_value());
    CHECK(live_rig.last_load_request->song_directory == std::filesystem::path{"song"});
    CHECK(live_rig.last_load_request->tone_document_ref == "tones/lead.tone.json");

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
    FakeAudio audio;
    FakePluginHost plugin_host;
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
        transport,
        audio,
        plugin_host,
        live_rig,
        EditorController::Services{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song =
        makeSong(std::filesystem::path{"song.wav"}, loadedTimelineRange(), "tones/lead.tone.json");

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    const std::vector<BusyViewState> progress_states = liveRigBusyStates(view);
    REQUIRE_FALSE(progress_states.empty());
    CHECK(progress_states.front().message == "Loading live rig...");
    CHECK(progress_states.front().progress == std::optional<double>{0.0});
    CHECK(progress_states.front().presentation == BusyPresentation::Blocking);
    CHECK(std::ranges::any_of(progress_states, [](const BusyViewState& state) {
        return state.message == "Loading Amp Sim (1 of 2)..." &&
               state.progress == std::optional<double>{0.25};
    }));
    CHECK(std::ranges::any_of(progress_states, [](const BusyViewState& state) {
        return state.message == "Loading Amp Sim (1 of 2)..." &&
               state.progress == std::optional<double>{0.5};
    }));
    CHECK(std::ranges::any_of(progress_states, [](const BusyViewState& state) {
        return state.message == "Loading Cab IR (2 of 2)..." &&
               state.progress == std::optional<double>{1.0};
    }));
    CHECK(view.busy_overlay_paint_callback_count == 1);
}

// A live-rig completion delivered after close cannot repopulate project or plugin state.
TEST_CASE(
    "EditorController close during live rig load supersedes open", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakePluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.defer_load_completion = true;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        plugin_host,
        live_rig,
        EditorController::Services{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song =
        makeSong(std::filesystem::path{"song.wav"}, loadedTimelineRange(), "tones/lead.tone.json");

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    CHECK(live_rig.load_call_count == 1);
    CHECK_FALSE(controller.currentProjectFile().has_value());
    const EditorViewState* loading_state = stateOrNull(view.last_state);
    REQUIRE(loading_state != nullptr);
    REQUIRE(loading_state->busy.has_value());
    CHECK(loading_state->busy->operation == BusyOperation::LoadingLiveRig);

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
    FakeAudio audio;
    FakePluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.clear();
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        plugin_host,
        live_rig,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    controller.onSaveRequested();

    CHECK(live_rig.capture_call_count == 1);
    REQUIRE(live_rig.last_capture_request.has_value());
    CHECK(live_rig.last_capture_request->song_directory == std::filesystem::path{"song"});
    CHECK(live_rig.last_capture_request->arrangement_id == "lead");
    CHECK(live_rig.last_capture_request->existing_tone_document_ref.empty());
    CHECK(project_services.save_call_count == 1);
    CHECK(
        project_services.last_save_tone_document_ref ==
        std::optional<std::string>{"tones/lead.tone.json"});

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
    FakeAudio audio;
    FakePluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.clear();
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        plugin_host,
        live_rig,
        EditorController::Services{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    controller.onAddPluginRequested(std::filesystem::path{"amp.vst3"});
    controller.onCloseRequested();

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->unsaved_changes_prompt.has_value());
    CHECK(state->unsaved_changes_prompt->prompted_action == EditorActionId::CloseProject);
}

// Removing a plugin updates runtime state and reindexes the remaining linear chain.
TEST_CASE("EditorController removes a plugin", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakePluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        plugin_host,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    FakeEditorView view;
    controller.attachView(view);
    loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"});

    plugin_host.next_handle.instance_id = "instance-a";
    plugin_host.next_handle.chain_index = 0;
    controller.onAddPluginRequested(std::filesystem::path{"amp-a.vst3"});
    plugin_host.next_handle.instance_id = "instance-b";
    plugin_host.next_handle.chain_index = 1;
    controller.onAddPluginRequested(std::filesystem::path{"amp-b.vst3"});

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
    FakeAudio audio;
    FakePluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        plugin_host,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    FakeEditorView view;
    controller.attachView(view);
    loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"});

    controller.onAddPluginRequested(std::filesystem::path{"amp.vst3"});
    controller.onRemovePluginRequested("stale-instance");

    CHECK(plugin_host.remove_call_count == 0);
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    REQUIRE(final_state->signal_chain.plugins.size() == 1);
    CHECK(final_state->signal_chain.plugins[0].instance_id == "instance-id");
    CHECK(view.shown_errors.empty());
}

// Backend removal failures report an error without erasing controller-owned runtime state.
TEST_CASE("EditorController reports plugin remove errors", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakePluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        plugin_host,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    FakeEditorView view;
    controller.attachView(view);
    loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"});
    controller.onAddPluginRequested(std::filesystem::path{"amp.vst3"});
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

// Plugin-host failures surface as transient errors without adding a chain item.
TEST_CASE("EditorController reports plugin add errors", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakePluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        plugin_host,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    FakeEditorView view;
    controller.attachView(view);
    loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"});
    plugin_host.next_add_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::PluginLoadFailed,
        "plugin rejected",
    };

    controller.onAddPluginRequested(std::filesystem::path{"broken.vst3"});

    CHECK(plugin_host.scan_call_count == 1);
    CHECK(plugin_host.validate_call_count == 1);
    CHECK(plugin_host.add_call_count == 1);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.signal_chain.plugins.empty());
    }
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not add plugin: plugin rejected");
    REQUIRE(view.states_seen_at_errors.size() == 1);
    REQUIRE(view.states_seen_at_errors.back().has_value());
    const EditorViewState* error_state = stateOrNull(view.states_seen_at_errors.back());
    REQUIRE(error_state != nullptr);
    CHECK_FALSE(error_state->busy.has_value());
}

// Load-validation failures report through the transient error channel and skip insertion.
TEST_CASE("EditorController reports plugin load validation failures", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakePluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        plugin_host,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    FakeEditorView view;
    controller.attachView(view);
    loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"});
    plugin_host.next_validate_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::PluginLoadFailed,
        "plugin timed out",
    };

    controller.onAddPluginRequested(std::filesystem::path{"broken.vst3"});

    CHECK(plugin_host.scan_call_count == 1);
    CHECK(plugin_host.validate_call_count == 1);
    CHECK(plugin_host.last_validated_plugin_id == std::optional<std::string>{"plugin-id"});
    CHECK(plugin_host.add_call_count == 0);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->signal_chain.plugins.empty());
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not load plugin: plugin timed out");
    REQUIRE(view.states_seen_at_errors.size() == 1);
    REQUIRE(view.states_seen_at_errors.back().has_value());
    const EditorViewState* error_state = stateOrNull(view.states_seen_at_errors.back());
    REQUIRE(error_state != nullptr);
    CHECK_FALSE(error_state->busy.has_value());
}

// Scanner failures report through the transient error channel and skip chain mutation.
TEST_CASE("EditorController reports plugin scan errors", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakePluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        plugin_host,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    FakeEditorView view;
    controller.attachView(view);
    loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"});
    plugin_host.next_scan_error = common::audio::PluginHostError{
        common::audio::PluginHostErrorCode::PluginScanFailed,
        "scanner rejected",
    };

    controller.onAddPluginRequested(std::filesystem::path{"broken.vst3"});

    CHECK(plugin_host.scan_call_count == 1);
    CHECK(plugin_host.validate_call_count == 0);
    CHECK(plugin_host.add_call_count == 0);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->signal_chain.plugins.empty());
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not scan plugin: scanner rejected");
    REQUIRE(view.states_seen_at_errors.size() == 1);
    REQUIRE(view.states_seen_at_errors.back().has_value());
    const EditorViewState* error_state = stateOrNull(view.states_seen_at_errors.back());
    REQUIRE(error_state != nullptr);
    CHECK_FALSE(error_state->busy.has_value());
}

// Device-manager change notifications re-derive view state through the listener relay.
TEST_CASE("EditorController re-derives state on device change", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeAudioDeviceConfiguration audio_devices;
    EditorController controller{transport, audio, audio_devices};
    FakeEditorView view;
    controller.attachView(view);
    const int baseline_pushes = view.set_state_call_count;

    audio_devices.current_name = std::string{"Interface B"};
    audio_devices.notifyChanged();

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == baseline_pushes + 1);
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.current_audio_device_name == std::optional<std::string>{"Interface B"});
    }
}

// Confirms attachView immediately delivers the controller's cached arrangement state.
TEST_CASE("EditorController pushes derived state on view attachment", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    EditorController controller{transport, audio};
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
        CHECK(state.close_enabled == false);
        CHECK(state.project_loaded == false);
        CHECK(state.play_pause_enabled == false);
        CHECK(state.stop_enabled == false);
        CHECK(state.play_pause_shows_pause_icon == false);
        CHECK_FALSE(state.audio_devices_available);
        CHECK_FALSE(state.current_audio_device_name.has_value());
        CHECK(state.visible_timeline == common::core::TimeRange{});
        CHECK_FALSE(state.arrangement.hasAudio());
        CHECK_FALSE(state.signal_chain.add_plugin_enabled);
        CHECK(state.signal_chain.plugins.empty());
        CHECK_FALSE(state.unsaved_changes_prompt.has_value());
        CHECK_FALSE(state.save_as_prompt.has_value());
    }
    CHECK(audio.set_active_arrangement_call_count == 0);
}

// Verifies the controller pushes session timeline mapping from loaded arrangement audio.
TEST_CASE("EditorController derives visible timeline range", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    loadArrangement(
        controller,
        project_services,
        audio,
        std::filesystem::path{"a.wav"},
        loadedTimelineRange(8.0));
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
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"});
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
        CHECK(playing_state.play_pause_shows_pause_icon == true);
        CHECK(playing_state.stop_enabled == true);
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
        CHECK(stopped_state.play_pause_shows_pause_icon == false);
        CHECK(stopped_state.stop_enabled == false);
    }
}

// Play intent issues play() when stopped and pause() when playing, once audio is loaded.
TEST_CASE("EditorController play intent toggles loaded transport", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"});

    controller.onPlayPausePressed();
    CHECK(transport.play_call_count == 1);
    CHECK(transport.pause_call_count == 0);

    transport.current_state.playing = true;
    controller.onPlayPausePressed();
    CHECK(transport.play_call_count == 1);
    CHECK(transport.pause_call_count == 1);
}

// Without arrangement audio there is nothing to play, so the intent is a no-op.
TEST_CASE("EditorController ignores play intent without audio", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    EditorController controller{transport, audio};

    controller.onPlayPausePressed();

    CHECK(transport.play_call_count == 0);
    CHECK(transport.pause_call_count == 0);
}

// The stop intent respects the same gate the view publishes.
TEST_CASE("EditorController stop intent follows reset gate", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"});

    controller.onStopPressed();
    CHECK(transport.stop_call_count == 0);

    transport.current_position = common::core::TimePosition{1.5};
    controller.onStopPressed();
    CHECK(transport.stop_call_count == 1);
    CHECK(transport.current_position == common::core::TimePosition{});

    transport.current_state.playing = true;
    controller.onStopPressed();
    CHECK(transport.stop_call_count == 2);
}

// Stopping from a paused non-start cursor refreshes the view directly after stop().
TEST_CASE("EditorController stop intent refreshes paused reset state", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"});
    FakeEditorView view;
    controller.attachView(view);

    transport.current_position = common::core::TimePosition{1.5};
    transport.setStateAndNotify(
        common::audio::TransportState{
            .playing = false,
        });
    CHECK(lastStopEnabled(view) == std::optional{true});
    const int pushes_before_stop = view.set_state_call_count;

    controller.onStopPressed();

    CHECK(transport.stop_call_count == 1);
    CHECK(view.set_state_call_count == pushes_before_stop + 1);
    CHECK(lastStopEnabled(view) == std::optional{false});
}

// Waveform clicks clamp out-of-range input and convert positions through the session timeline.
TEST_CASE("EditorController waveform click clamps and scales", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    loadArrangement(
        controller,
        project_services,
        audio,
        std::filesystem::path{"a.wav"},
        loadedTimelineRange(4.0));

    controller.onWaveformClicked(0.5);
    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{2.0}});

    controller.onWaveformClicked(-0.25);
    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{0.0}});

    controller.onWaveformClicked(1.5);
    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{4.0}});
}

// A seek issued by the controller refreshes whether Stop can reset the cursor.
TEST_CASE("EditorController waveform click refreshes stop state", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    loadArrangement(
        controller,
        project_services,
        audio,
        std::filesystem::path{"a.wav"},
        loadedTimelineRange(4.0));
    FakeEditorView view;
    controller.attachView(view);

    CHECK(lastStopEnabled(view) == std::optional{false});

    controller.onWaveformClicked(0.5);

    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{2.0}});
    CHECK(lastStopEnabled(view) == std::optional{true});

    controller.onWaveformClicked(0.0);

    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{}});
    CHECK(lastStopEnabled(view) == std::optional{false});
}

// A failed project-audio activation leaves the session unchanged and surfaces an error.
TEST_CASE("EditorController failed activation preserves session", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    loadArrangement(
        controller,
        project_services,
        audio,
        std::filesystem::path{"old.wav"},
        loadedTimelineRange(6.0));
    audio.next_set_active_arrangement_result = false;
    project_services.next_song = makeSong(std::filesystem::path{"new.wav"});
    FakeEditorView view;
    controller.attachView(view);

    controller.onOpenRequested(std::filesystem::path{"new.rhp"});

    const common::core::Session& session = controller.session();
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(
        session.currentArrangement()->audio_asset ==
        common::core::AudioAsset{std::filesystem::path{"old.wav"}});
    CHECK(session.timeline() == loadedTimelineRange(6.0));
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.project_loaded == true);
    }
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not load audio from: new.rhp");
}

// A successful open stores the selected audio without replaying a prior error.
TEST_CASE("EditorController successful open stores audio", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"first.wav"});
    audio.next_set_active_arrangement_result = false;
    controller.onOpenRequested(std::filesystem::path{"first.rhp"});
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not load audio from: first.rhp");
    const int pushes_before_success = view.set_state_call_count;

    audio.next_set_active_arrangement_result = true;
    const common::core::AudioAsset replacement{std::filesystem::path{"second.wav"}};
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
        CHECK(state.save_requires_destination == false);
        CHECK_FALSE(state.unsaved_changes_prompt.has_value());
    }
    // Each open now produces two pushes: one when busy state begins, one when the task
    // completion commits or fails. The inline default task runner runs both synchronously.
    CHECK(view.set_state_call_count == pushes_before_success + 2);
    CHECK(view.shown_errors.size() == 1);
}

// Close stops playback, clears backend audio, and returns the view to an empty project state.
TEST_CASE("EditorController close clears loaded project", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.open_function = project_services.openFunction()}
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
        CHECK(state.play_pause_enabled == false);
        CHECK(state.visible_timeline == common::core::TimeRange{});
        CHECK_FALSE(state.arrangement.hasAudio());
    }
}

// Missing restore paths are cleared without asking project IO to open anything.
TEST_CASE("EditorController clears missing restore path", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"missing_restore_path"};
    EditorSettings settings{files.settingsFile()};
    settings.setLastOpenProject(files.projectFile());
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .settings = &settings,
        },
    };

    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == 0);
    CHECK_FALSE(settings.lastOpenProject().has_value());
}

// Valid restore paths are opened and kept when the controller accepts the project.
TEST_CASE("EditorController restores valid last project", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"valid_restore_path"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    settings.setLastOpenProject(files.projectFile());
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .settings = &settings,
        },
    };

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == 1);
    CHECK(project_services.last_open_file == std::optional{files.projectFile()});
    CHECK(controller.currentProjectFile() == std::optional{files.projectFile()});
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
}

// Startup restore leaves the saved path intact until the async open completion resolves.
TEST_CASE("EditorController restore keeps path while open is pending", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"pending_restore_path"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    settings.setLastOpenProject(files.projectFile());
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .settings = &settings,
            .task_runner = &runner,
        },
    };

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == 1);
    CHECK(project_services.last_open_file == std::optional{files.projectFile()});
    CHECK_FALSE(controller.currentProjectFile().has_value());
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});

    runner.runPendingCompletions();

    CHECK(controller.currentProjectFile() == std::optional{files.projectFile()});
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
}

// A stored project path rejected by open is removed from future startup restore state.
TEST_CASE("EditorController clears restore path when open fails", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"failed_restore_path"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    settings.setLastOpenProject(files.projectFile());
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .settings = &settings,
        },
    };

    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == 1);
    CHECK_FALSE(controller.currentProjectFile().has_value());
    CHECK_FALSE(settings.lastOpenProject().has_value());
}

// Startup restore clears failed paths only from the async completion path, not scheduling.
TEST_CASE("EditorController restore clears path after async failure", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"async_failed_restore_path"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    settings.setLastOpenProject(files.projectFile());
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .settings = &settings,
            .task_runner = &runner,
        },
    };

    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == 1);
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});

    runner.runPendingCompletions();

    CHECK_FALSE(controller.currentProjectFile().has_value());
    CHECK_FALSE(settings.lastOpenProject().has_value());
}

// A restore request fired while the controller already has dirty work routes through the same
// unsaved-changes gate as Open, instead of overwriting the in-progress project. Today this only
// matters as a guard for future call sites that invoke RestoreProject after startup (a
// reopen-last-session menu item, a crash-recovery flow, etc.); the startup path is unaffected
// because the controller has nothing loaded yet at that point.
TEST_CASE("EditorController restore prompts for unsaved changes", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"restore_prompts_unsaved"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    settings.setLastOpenProject(files.projectFile());
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .import_function = project_services.importFunction(),
            .settings = &settings,
        },
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_import_song = makeSong(std::filesystem::path{"imported.ogg"});
    controller.onImportRequested(std::filesystem::path{"song.psarc"});

    const int open_call_count_before_restore = project_services.open_call_count;
    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == open_call_count_before_restore);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& prompt_state = view.last_state.value();
        CHECK(
            prompt_state.unsaved_changes_prompt ==
            std::optional{UnsavedChangesPrompt{EditorActionId::RestoreProject}});
    }
}

// Exiting persists the editor project path before requesting host shutdown.
TEST_CASE("EditorController persists project file on exit", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"persist_loaded_exit"};
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    int exit_call_count = 0;
    std::optional<std::filesystem::path> setting_seen_at_exit{};
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .exit_function =
                [&exit_call_count, &setting_seen_at_exit, &settings] {
                    setting_seen_at_exit = settings.lastOpenProject();
                    ++exit_call_count;
                },
            .settings = &settings,
        },
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
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        },
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset audio_asset{std::filesystem::path{"song.wav"}};
    project_services.next_song = makeSong(audio_asset.path);
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    transport.current_position = common::core::TimePosition{1.25};

    controller.onSaveRequested();

    CHECK(project_services.save_call_count == 1);
    CHECK(project_services.save_as_call_count == 0);
    CHECK(project_services.last_save_audio_path == std::optional{audio_asset.path});
    CHECK(
        project_services.last_save_editor_state ==
        std::optional{ProjectEditorState{
            .cursor_position = common::core::TimePosition{1.25},
            .selected_arrangement = std::string{"lead"},
        }});
    CHECK(view.shown_errors.empty());
}

// Save failures are surfaced without clearing the loaded session.
TEST_CASE("EditorController save failure surfaces an error", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        },
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
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .save_as_function = project_services.saveAsFunction(),
        },
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
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
            .publish_function = project_services.publishFunction(),
        },
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset audio_asset{std::filesystem::path{"song.wav"}};
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
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .publish_function = project_services.publishFunction(),
        },
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
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .import_function = project_services.importFunction(),
        }
    };
    loadArrangement(
        controller,
        project_services,
        audio,
        std::filesystem::path{"old.wav"},
        loadedTimelineRange(6.0));
    project_services.open_call_count = 0;
    project_services.last_open_file.reset();
    FakeEditorView view;
    controller.attachView(view);

    controller.onImportRequested(std::filesystem::path{"broken.psarc"});

    const common::core::Session& session = controller.session();
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(
        session.currentArrangement()->audio_asset ==
        common::core::AudioAsset{std::filesystem::path{"old.wav"}});
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
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.import_function = project_services.importFunction()}
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_import_song = makeSong(std::filesystem::path{"first.ogg"});
    audio.next_set_active_arrangement_result = false;
    controller.onImportRequested(std::filesystem::path{"first.psarc"});
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not load imported audio from: first.psarc");
    const int pushes_before_success = view.set_state_call_count;

    audio.next_set_active_arrangement_result = true;
    const common::core::AudioAsset replacement{std::filesystem::path{"imported.ogg"}};
    project_services.next_import_song = makeSong(replacement.path, loadedTimelineRange(4.0));
    controller.onImportRequested(std::filesystem::path{"second.psarc"});

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
        CHECK(state.save_requires_destination == true);
    }
    // Each import produces two pushes: one when busy state begins, one when the task completion
    // commits or fails. The inline default task runner runs both synchronously.
    CHECK(view.set_state_call_count == pushes_before_success + 2);
    CHECK(view.shown_errors.size() == 1);
}

// Imported content requires Save As before direct Save can write to a destination.
TEST_CASE("EditorController import requires Save As destination", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        },
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset audio_asset{std::filesystem::path{"imported.ogg"}};
    project_services.next_import_song = makeSong(audio_asset.path);
    controller.onImportRequested(std::filesystem::path{"song.psarc"});
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
    CHECK(
        project_services.last_save_as_editor_state ==
        std::optional{ProjectEditorState{
            .cursor_position = common::core::TimePosition{2.5},
            .selected_arrangement = std::string{"lead"},
        }});
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
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        },
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_import_song = makeSong(std::filesystem::path{"imported.ogg"});
    controller.onImportRequested(std::filesystem::path{"song.psarc"});

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

// Choosing Save for an unsaved import asks for a destination, saves, and then closes.
TEST_CASE("EditorController saves prompted import before close", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        },
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset audio_asset{std::filesystem::path{"imported.ogg"}};
    project_services.next_import_song = makeSong(audio_asset.path);
    controller.onImportRequested(std::filesystem::path{"song.psarc"});

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
    settings.setLastOpenProject(files.projectFile());
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    int exit_call_count = 0;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
            .exit_function = [&exit_call_count] { ++exit_call_count; },
            .settings = &settings,
        },
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_import_song = makeSong(std::filesystem::path{"imported.ogg"});
    controller.onImportRequested(std::filesystem::path{"song.psarc"});

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

// Project packages do not carry editor selection state, so the controller opens index zero.
TEST_CASE("EditorController defaults open to first arrangement", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.open_function = project_services.openFunction()}
    };

    const common::core::AudioAsset lead_asset{std::filesystem::path{"lead.wav"}};
    const common::core::AudioAsset bass_asset{std::filesystem::path{"bass.wav"}};
    project_services.next_song = makeTwoArrangementSong(lead_asset.path, bass_asset.path);

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    CHECK(audio.last_active_audio_asset == std::optional{lead_asset});
    REQUIRE(controller.session().currentArrangement() != nullptr);
    CHECK(controller.session().currentArrangement()->part == common::core::Part::Lead);
    CHECK(controller.session().currentArrangement()->audio_asset == lead_asset);
}

// Opening a project validates every arrangement before the selected arrangement is loaded.
TEST_CASE("EditorController rejects invalid project arrangement audio", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset lead_asset{std::filesystem::path{"lead.wav"}};
    const common::core::AudioAsset bass_asset{std::filesystem::path{"bass.wav"}};
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
    CHECK(view.shown_errors.back() == "Could not load audio from: song.rhp");
}

// Reentrant transport notifications during in-flight arrangement activation coalesce once.
TEST_CASE("EditorController coalesces reentrant audio callbacks", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.open_function = project_services.openFunction()}
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

    const common::core::AudioAsset replacement{std::filesystem::path{"loop.wav"}};
    project_services.next_song = makeSong(replacement.path);
    controller.onOpenRequested(std::filesystem::path{"loop.rhp"});

    // Loading emits one busy-state push and one final committed-state push; the reentrant
    // transport callback should not add a third load-related update.
    CHECK(view.set_state_call_count == pushes_before_load + 2);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.arrangement.audio_asset == std::optional{replacement});
        CHECK(state.play_pause_shows_pause_icon == true);
    }
}

// Later transport transitions do not replay a one-shot workflow error.
TEST_CASE("EditorController does not replay errors across transitions", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{.open_function = project_services.openFunction()}
    };
    loadArrangement(controller, project_services, audio, std::filesystem::path{"old.wav"});
    audio.next_set_active_arrangement_result = false;
    project_services.next_song = makeSong(std::filesystem::path{"new.wav"});
    FakeEditorView view;
    controller.attachView(view);
    controller.onOpenRequested(std::filesystem::path{"new.rhp"});

    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not load audio from: new.rhp");

    transport.setStateAndNotify(
        common::audio::TransportState{
            .playing = true,
        });

    CHECK(view.shown_errors.size() == 1);
}

// Open sets busy=OpeningProject with the default message before the worker's completion runs.
TEST_CASE("EditorController open begins busy with default message", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"first.wav"});
    controller.onOpenRequested(std::filesystem::path{"first.rhp"});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const BusyViewState* busy = busyOrNull(*state);
    REQUIRE(busy != nullptr);
    CHECK(busy->operation == BusyOperation::OpeningProject);
    CHECK(busy->message == "Opening project...");
    CHECK(busy->presentation == BusyPresentation::Animated);
    CHECK_FALSE(busy->progress.has_value());
    CHECK(busy->cancel_enabled == false);
}

// Import sets busy=ImportingProject with the default message before the worker's completion runs.
TEST_CASE("EditorController import begins busy with default message", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .import_function = project_services.importFunction(),
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_import_song = makeSong(std::filesystem::path{"first.ogg"});
    controller.onImportRequested(std::filesystem::path{"first.psarc"});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const BusyViewState* busy = busyOrNull(*state);
    REQUIRE(busy != nullptr);
    CHECK(busy->operation == BusyOperation::ImportingProject);
    CHECK(busy->message == "Importing project...");
}

// Save sets busy=SavingProject before the deferred write completion restores normal state.
TEST_CASE("EditorController save begins busy with default message", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    runner.runPendingCompletions();

    controller.onSaveRequested();

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const BusyViewState* busy = busyOrNull(*state);
    REQUIRE(busy != nullptr);
    CHECK(busy->operation == BusyOperation::SavingProject);
    CHECK(busy->message == "Saving project...");
    CHECK(project_services.save_call_count == 1);
}

// Save As sets busy=SavingProjectAs before the deferred write completion commits the path.
TEST_CASE("EditorController save as begins busy with default message", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .save_as_function = project_services.saveAsFunction(),
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    runner.runPendingCompletions();

    controller.onSaveAsRequested(std::filesystem::path{"renamed.rhp"});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const BusyViewState* busy = busyOrNull(*state);
    REQUIRE(busy != nullptr);
    CHECK(busy->operation == BusyOperation::SavingProjectAs);
    CHECK(busy->message == "Saving project...");
    CHECK(project_services.save_as_call_count == 1);
}

// Publish sets busy=PublishingProject before the deferred package completion restores state.
TEST_CASE("EditorController publish begins busy with default message", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .publish_function = project_services.publishFunction(),
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    runner.runPendingCompletions();

    controller.onPublishRequested(std::filesystem::path{"song.rock"});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const BusyViewState* busy = busyOrNull(*state);
    REQUIRE(busy != nullptr);
    CHECK(busy->operation == BusyOperation::PublishingProject);
    CHECK(busy->message == "Publishing project...");
    CHECK(project_services.publish_call_count == 1);
}

// While busy, action routing disables ordinary commands and keeps Close available to supersede.
TEST_CASE("EditorController busy routing disables ordinary commands", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->open_enabled == false);
    CHECK(state->import_enabled == false);
    CHECK(state->save_enabled == false);
    CHECK(state->save_as_enabled == false);
    CHECK(state->publish_enabled == false);
    CHECK(state->play_pause_enabled == false);
    CHECK(state->stop_enabled == false);
    CHECK(state->signal_chain.add_plugin_enabled == false);
    CHECK(state->signal_chain.remove_plugins_enabled == false);
    CHECK(state->close_enabled == true);
}

// Verifies Close remains available through the same supersede policy when a project is loaded.
TEST_CASE(
    "EditorController busy keeps close enabled for a loaded project", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"loaded.wav"});
    controller.onOpenRequested(std::filesystem::path{"loaded.rhp"});
    runner.runPendingCompletions();

    const EditorViewState* loaded_state = stateOrNull(view.last_state);
    REQUIRE(loaded_state != nullptr);
    CHECK_FALSE(loaded_state->busy.has_value());
    CHECK(loaded_state->close_enabled == true);

    project_services.next_song = makeSong(std::filesystem::path{"second.wav"});
    controller.onOpenRequested(std::filesystem::path{"second.rhp"});

    const EditorViewState* busy_state = stateOrNull(view.last_state);
    REQUIRE(busy_state != nullptr);
    REQUIRE(busy_state->busy.has_value());
    CHECK(busy_state->close_enabled == true);
}

// Direct controller calls still go through action routing, so busy state blocks mutations even
// when the JUCE overlay is bypassed.
TEST_CASE("EditorController busy routing blocks direct commands", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakePluginHost plugin_host;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        plugin_host,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
            .publish_function = project_services.publishFunction(),
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"loaded.wav"});
    controller.onOpenRequested(std::filesystem::path{"loaded.rhp"});
    runner.runPendingCompletions();
    controller.onAddPluginRequested(std::filesystem::path{"loaded.vst3"});
    runner.runPendingCompletions();
    plugin_host.scan_call_count = 0;
    plugin_host.validate_call_count = 0;
    plugin_host.add_call_count = 0;
    plugin_host.remove_call_count = 0;

    transport.current_position = common::core::TimePosition{1.0};
    project_services.next_song = makeSong(std::filesystem::path{"pending.wav"});
    controller.onOpenRequested(std::filesystem::path{"pending.rhp"});
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->busy.has_value());

    controller.onOpenRequested(std::filesystem::path{"blocked.rhp"});
    controller.onImportRequested(std::filesystem::path{"blocked.psarc"});
    controller.onSaveRequested();
    controller.onSaveAsRequested(std::filesystem::path{"blocked.rhp"});
    controller.onPublishRequested(std::filesystem::path{"blocked.rock"});
    controller.onPlayPausePressed();
    controller.onStopPressed();
    controller.onWaveformClicked(0.5);
    controller.onAddPluginRequested(std::filesystem::path{"blocked.vst3"});
    controller.onRemovePluginRequested("instance-id");

    CHECK(runner.pendingCount() == 1);
    CHECK(project_services.open_call_count == 2);
    CHECK(project_services.import_call_count == 0);
    CHECK(project_services.save_call_count == 0);
    CHECK(project_services.save_as_call_count == 0);
    CHECK(project_services.publish_call_count == 0);
    CHECK(transport.play_call_count == 0);
    CHECK(transport.stop_call_count == 0);
    CHECK(transport.seek_call_count == 1);
    CHECK(plugin_host.scan_call_count == 0);
    CHECK(plugin_host.validate_call_count == 0);
    CHECK(plugin_host.remove_call_count == 0);
}

// Completion of a successful open clears busy and publishes the final committed state.
TEST_CASE("EditorController open completion clears busy and commits", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    const EditorViewState* busy_state = stateOrNull(view.last_state);
    REQUIRE(busy_state != nullptr);
    REQUIRE(busy_state->busy.has_value());

    runner.runPendingCompletions();

    const EditorViewState* loaded_state = stateOrNull(view.last_state);
    REQUIRE(loaded_state != nullptr);
    CHECK_FALSE(loaded_state->busy.has_value());
    CHECK(loaded_state->project_loaded == true);
    CHECK(view.shown_errors.empty());
}

// A failed open clears busy first, then reports the error through the existing one-shot path.
TEST_CASE(
    "EditorController failed open clears busy then reports error", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_error_message = "missing package";
    controller.onOpenRequested(std::filesystem::path{"missing.rhp"});

    runner.runPendingCompletions();

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->busy.has_value());
    REQUIRE(view.states_seen_at_errors.size() == 1);
    REQUIRE(view.states_seen_at_errors.back().has_value());
    const EditorViewState* error_state = stateOrNull(view.states_seen_at_errors.back());
    REQUIRE(error_state != nullptr);
    CHECK_FALSE(error_state->busy.has_value());
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not open: missing package");
}

// Close during a busy open supersedes the in-flight operation through action routing. The
// worker's deferred completion sees a generation mismatch and the loaded song is never committed.
TEST_CASE("EditorController close during busy supersedes open", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"superseded.wav"});
    controller.onOpenRequested(std::filesystem::path{"superseded.rhp"});
    const EditorViewState* busy_state = stateOrNull(view.last_state);
    REQUIRE(busy_state != nullptr);
    REQUIRE(busy_state->busy.has_value());

    controller.onCloseRequested();
    const EditorViewState* closed_state = stateOrNull(view.last_state);
    REQUIRE(closed_state != nullptr);
    CHECK_FALSE(closed_state->busy.has_value());

    runner.runPendingCompletions();

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(final_state->project_loaded == false);
    CHECK(audio.set_active_arrangement_call_count == 0);
    CHECK(view.shown_errors.empty());
}

// Exit during a busy open follows the same supersede path through closeProject() and triggers
// the composition host's exit callback.
TEST_CASE("EditorController exit during busy supersedes open", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    int exit_call_count = 0;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .exit_function = [&exit_call_count]() { ++exit_call_count; },
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"never_committed.wav"});
    controller.onOpenRequested(std::filesystem::path{"never_committed.rhp"});
    const EditorViewState* busy_state = stateOrNull(view.last_state);
    REQUIRE(busy_state != nullptr);
    REQUIRE(busy_state->busy.has_value());

    controller.onExitRequested();
    CHECK(exit_call_count == 1);

    runner.runPendingCompletions();

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(final_state->project_loaded == false);
    CHECK(audio.set_active_arrangement_call_count == 0);
}

// Close during a busy save invalidates the deferred completion so it cannot restore the project.
TEST_CASE("EditorController close during busy save supersedes write", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    runner.runPendingCompletions();

    controller.onSaveRequested();
    const EditorViewState* busy_state = stateOrNull(view.last_state);
    REQUIRE(busy_state != nullptr);
    REQUIRE(busy_state->busy.has_value());

    controller.onCloseRequested();
    const EditorViewState* closed_state = stateOrNull(view.last_state);
    REQUIRE(closed_state != nullptr);
    CHECK_FALSE(closed_state->busy.has_value());
    CHECK(closed_state->project_loaded == false);

    runner.runPendingCompletions();

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(final_state->project_loaded == false);
    CHECK_FALSE(controller.currentProjectFile().has_value());
    CHECK(transport.stop_call_count == 1);
    CHECK(view.shown_errors.empty());
}

// Exiting during a busy save still remembers the current project path before closing the session.
TEST_CASE("EditorController exit during busy save persists file", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"busy_save_exit"};
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    int exit_call_count = 0;
    std::optional<std::filesystem::path> setting_seen_at_exit{};
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
            .exit_function =
                [&exit_call_count, &setting_seen_at_exit, &settings] {
                    setting_seen_at_exit = settings.lastOpenProject();
                    ++exit_call_count;
                },
            .settings = &settings,
            .task_runner = &runner,
        },
    };

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(files.projectFile());
    runner.runPendingCompletions();

    controller.onSaveRequested();
    controller.onExitRequested();

    CHECK(exit_call_count == 1);
    CHECK(setting_seen_at_exit == std::optional{files.projectFile()});
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
    CHECK_FALSE(controller.currentProjectFile().has_value());

    runner.runPendingCompletions();

    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
}

// A stale completion (generation no longer matches) does not finish busy state: if another
// operation is busy when the stale completion fires, the live busy state is preserved.
TEST_CASE(
    "EditorController stale completion preserves live busy state", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"first.wav"});
    controller.onOpenRequested(std::filesystem::path{"first.rhp"});

    // Supersede the first open by closing, then start a second open. The first open's pending
    // completion is now stale. The second open's busy state must survive the stale completion.
    controller.onCloseRequested();
    project_services.next_song = makeSong(std::filesystem::path{"second.wav"});
    controller.onOpenRequested(std::filesystem::path{"second.rhp"});

    const EditorViewState* busy_state = stateOrNull(view.last_state);
    REQUIRE(busy_state != nullptr);
    REQUIRE(busy_state->busy.has_value());

    runner.runPendingCompletions();

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    // The second open's completion fires after the first (stale) one. The first's discard does
    // not clear busy; the second's successful commit does. project_loaded should reflect the
    // second song only.
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(final_state->project_loaded == true);
    CHECK(audio.set_active_arrangement_call_count == 1);
    const common::core::AudioAsset* active_asset =
        audio.last_active_audio_asset.has_value() ? &*audio.last_active_audio_asset : nullptr;
    REQUIRE(active_asset != nullptr);
    CHECK(active_asset->path == std::filesystem::path{"second.wav"});
}

// Stop on the message thread is not required by the task runner contract, but the controller
// must still call IAudio::prepareSong() during the message-thread commit stage rather than the
// worker. The deferred runner exposes this: prepareSong is not called until completion runs.
TEST_CASE(
    "EditorController prepareSong runs on message-thread completion stage",
    "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        transport,
        audio,
        EditorController::Services{
            .open_function = project_services.openFunction(),
            .task_runner = &runner,
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    // After submit, work has run (open_function called) but completion is deferred.
    CHECK(project_services.open_call_count == 1);
    CHECK(audio.prepare_song_call_count == 0);

    runner.runPendingCompletions();

    CHECK(audio.prepare_song_call_count == 1);
}

} // namespace rock_hero::editor::core
