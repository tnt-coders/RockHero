/*!
\file editor_controller_test_harness.h
\brief Shared editor-controller test fakes and setup helpers.
*/

#pragma once

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <optional>
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <rock_hero/common/audio/input/i_live_input.h>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>
#include <rock_hero/common/audio/song/i_song_audio.h>
#include <rock_hero/common/audio/testing/configurable_audio_device_configuration.h>
#include <rock_hero/common/audio/testing/configurable_song_audio.h>
#include <rock_hero/common/audio/testing/recording_plugin_host.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/audio/transport/transport_state.h>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/song/audio_asset.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/controller/editor_controller.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <rock_hero/editor/core/controller/i_editor_view.h>
#include <rock_hero/editor/core/settings/editor_settings.h>
#include <rock_hero/editor/core/settings/i_editor_settings.h>
#include <rock_hero/editor/core/tasks/i_editor_task_runner.h>
#include <rock_hero/editor/core/testing/immediate_editor_task_runner.h>
#include <rock_hero/editor/core/testing/immediate_message_thread_scheduler.h>
#include <rock_hero/editor/core/testing/null_editor_settings.h>
#include <rock_hero/editor/core/testing/recording_editor_controller.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef TEST_SETTINGS_DIR
/*! \brief Build-local directory used by settings-backed controller tests. */
#define TEST_SETTINGS_DIR "."
#endif

namespace rock_hero::editor::core
{

/*! \brief Stable Lead arrangement ID used by controller test fixtures. */
constexpr const char* g_lead_arrangement_id = "4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4";

/*! \brief Stable Bass arrangement ID used by controller test fixtures. */
constexpr const char* g_bass_arrangement_id = "7aa55c5a-0e97-4e71-8f74-86b05bb6a2c9";

/*! \brief Stable tone document reference used by controller test fixtures. */
constexpr const char* g_tone_document_ref = "tones/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d/tone.json";

/*! \brief Short alias for the shared configurable audio-device fake. */
using ConfigurableAudioDeviceConfiguration =
    common::audio::testing::ConfigurableAudioDeviceConfiguration;

/*! \brief Short alias for the shared configurable song-audio fake. */
using ConfigurableSongAudio = common::audio::testing::ConfigurableSongAudio;

/*! \brief Short alias for the shared recording plugin-host fake. */
using RecordingPluginHost = common::audio::testing::RecordingPluginHost;

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
        if (defer_busy_overlay_paint_callbacks)
        {
            if (callback)
            {
                busy_overlay_paint_callbacks.push_back(std::move(callback));
            }
            return;
        }

        if (callback)
        {
            callback();
        }
    }

    // Runs the busy-clear paint fence inline so controller tests stay deterministic.
    void runAfterBusyOverlayRemoved(std::function<void()> callback) override
    {
        busy_overlay_removed_callback_count += 1;
        if (callback)
        {
            callback();
        }
    }

    // Reports whether a deferred busy-overlay paint callback is waiting to be released.
    [[nodiscard]] bool hasBusyOverlayPaintCallback() const noexcept
    {
        return !busy_overlay_paint_callbacks.empty();
    }

    // Releases the oldest deferred busy-overlay paint callback to simulate the view painting.
    void runNextBusyOverlayPaintCallback()
    {
        if (busy_overlay_paint_callbacks.empty())
        {
            return;
        }

        const std::function<void()> callback = std::move(busy_overlay_paint_callbacks.front());
        busy_overlay_paint_callbacks.erase(busy_overlay_paint_callbacks.begin());
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

    // Number of busy-overlay removal callbacks requested by the controller.
    int busy_overlay_removed_callback_count{0};

    // True when tests need to release busy paint fences manually.
    bool defer_busy_overlay_paint_callbacks{false};

    // Deferred busy paint-fence callbacks waiting for test-controlled release.
    std::vector<std::function<void()>> busy_overlay_paint_callbacks{};
};

/*!
\brief Returns the shared no-op settings for tests that do not observe persistence.
\return Process-lifetime null settings fake.
*/
[[nodiscard]] inline testing::NullEditorSettings& nullEditorSettings() noexcept
{
    static testing::NullEditorSettings g_settings;
    return g_settings;
}

/*!
\brief Returns the shared synchronous runner for tests that do not need deferred completions.
\return Process-lifetime immediate task runner fake.
*/
[[nodiscard]] inline testing::ImmediateEditorTaskRunner& immediateTaskRunner() noexcept
{
    static testing::ImmediateEditorTaskRunner g_task_runner;
    return g_task_runner;
}

/*!
\brief Returns the shared synchronous message-thread scheduler for controller tests.
\return Process-lifetime immediate message-thread scheduler fake.
*/
[[nodiscard]] inline testing::ImmediateMessageThreadScheduler&
immediateMessageThreadScheduler() noexcept
{
    static testing::ImmediateMessageThreadScheduler g_scheduler;
    return g_scheduler;
}

/*!
\brief Builds the required service bundle from test-owned or shared fake services.
\param settings Settings port used by the controller under test.
\param task_runner Task runner used by the controller under test.
\param message_thread_scheduler Message-thread scheduler used by the controller under test.
\return Controller service bundle.
*/
[[nodiscard]] inline EditorController::Services controllerServices(
    IEditorSettings& settings, IEditorTaskRunner& task_runner,
    IMessageThreadScheduler& message_thread_scheduler) noexcept
{
    return EditorController::Services{
        .settings = settings,
        .task_runner = task_runner,
        .message_thread_scheduler = message_thread_scheduler,
    };
}

/*!
\brief Builds the required service bundle with the default synchronous message-thread scheduler.
\param settings Settings port used by the controller under test.
\param task_runner Task runner used by the controller under test.
\return Controller service bundle.
*/
[[nodiscard]] inline EditorController::Services controllerServices(
    IEditorSettings& settings, IEditorTaskRunner& task_runner) noexcept
{
    return controllerServices(settings, task_runner, immediateMessageThreadScheduler());
}

/*!
\brief Builds the default no-op service bundle for tests that only exercise controller state policy.
\return Controller service bundle using shared no-op/synchronous fakes.
*/
[[nodiscard]] inline EditorController::Services defaultControllerServices() noexcept
{
    return controllerServices(nullEditorSettings(), immediateTaskRunner());
}

/*!
\brief Uses specific settings while keeping project work synchronous.
\param settings Settings port used by the controller under test.
\return Controller service bundle.
*/
[[nodiscard]] inline EditorController::Services controllerServices(
    IEditorSettings& settings) noexcept
{
    return controllerServices(settings, immediateTaskRunner());
}

/*!
\brief Uses a specific task runner while ignoring settings persistence.
\param task_runner Task runner used by the controller under test.
\return Controller service bundle.
*/
[[nodiscard]] inline EditorController::Services controllerServices(
    IEditorTaskRunner& task_runner) noexcept
{
    return controllerServices(nullEditorSettings(), task_runner);
}

/*!
\brief Returns the no-op host-exit callback used by tests that do not assert exit behavior.
\return Exit callback that does nothing.
*/
[[nodiscard]] inline EditorController::ExitFunction noopExitFunction()
{
    return [] {};
}

/*!
\brief Returns a nullable pointer so tests can satisfy optional-access lint after a REQUIRE.
\param state Optional editor state to inspect.
\return Pointer to contained state, or nullptr.
*/
[[nodiscard]] inline const EditorViewState* stateOrNull(
    const std::optional<EditorViewState>& state) noexcept
{
    return state.has_value() ? &*state : nullptr;
}

/*!
\brief Returns a nullable pointer for the nested busy state after the owning state is known present.
\param state Editor state that may or may not carry busy state.
\return Pointer to contained busy state, or nullptr.
*/
[[nodiscard]] inline const BusyViewState* busyOrNull(const EditorViewState& state) noexcept
{
    return state.busy.has_value() ? &*state.busy : nullptr;
}

/*!
\brief Extracts live-rig busy states so tests can assert the progress sequence directly.
\param view Fake view containing pushed state history.
\return Busy states whose operation is LoadingLiveRig.
*/
[[nodiscard]] inline std::vector<BusyViewState> liveRigBusyStates(const FakeEditorView& view)
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

// Records control intents and exposes a manual notification hook for controller tests.
class FakeTransport final : public common::audio::ITransport, public common::audio::ILiveInput
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

    [[nodiscard]] common::audio::Gain inputGain() const override
    {
        return current_input_gain;
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError> setInputGain(
        common::audio::Gain gain) override
    {
        if (next_set_input_gain_error.has_value())
        {
            common::audio::LiveInputError error = std::move(*next_set_input_gain_error);
            next_set_input_gain_error.reset();
            return std::unexpected{std::move(error)};
        }

        current_input_gain = common::audio::clampGain(gain);
        set_input_gain_call_count += 1;
        return {};
    }

    [[nodiscard]] common::audio::AudioMeterLevel rawInputMeterLevel() const override
    {
        return raw_input_meter_level;
    }

    [[nodiscard]] bool liveInputMonitoringEnabled() const override
    {
        return live_input_monitoring_enabled;
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError> setLiveInputMonitoringEnabled(
        bool enabled) override
    {
        if (next_set_live_input_monitoring_error.has_value())
        {
            common::audio::LiveInputError error = std::move(*next_set_live_input_monitoring_error);
            next_set_live_input_monitoring_error.reset();
            return std::unexpected{std::move(error)};
        }

        live_input_monitoring_enabled = enabled;
        set_live_input_monitoring_call_count += 1;
        return {};
    }

    [[nodiscard]] bool calibrationInputMonitoringEnabled() const override
    {
        return calibration_input_monitoring_enabled;
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError>
    setCalibrationInputMonitoringEnabled(bool enabled) override
    {
        if (next_set_calibration_input_monitoring_error.has_value())
        {
            common::audio::LiveInputError error =
                std::move(*next_set_calibration_input_monitoring_error);
            next_set_calibration_input_monitoring_error.reset();
            return std::unexpected{std::move(error)};
        }

        calibration_input_monitoring_enabled = enabled;
        set_calibration_input_monitoring_call_count += 1;
        return {};
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

    common::audio::Gain current_input_gain{};
    common::audio::AudioMeterLevel raw_input_meter_level{};
    bool live_input_monitoring_enabled{false};
    bool calibration_input_monitoring_enabled{false};

    // One-shot failures injected before mutating live-input state.
    std::optional<common::audio::LiveInputError> next_set_input_gain_error{};
    std::optional<common::audio::LiveInputError> next_set_live_input_monitoring_error{};
    std::optional<common::audio::LiveInputError> next_set_calibration_input_monitoring_error{};

    int set_input_gain_call_count{0};
    int set_live_input_monitoring_call_count{0};
    int set_calibration_input_monitoring_call_count{0};
};

// Configurable live rig fake that records project-boundary save and restore requests.
struct FakeLiveRig final : public common::audio::ILiveRig
{
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
    void loadLiveRig(
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
                    .active_plugin_name = {},
                });
            for (std::size_t plugin_index = 0; plugin_index < total_plugins; ++plugin_index)
            {
                const common::audio::PluginChainEntry& plugin =
                    next_load_result.plugins[plugin_index];
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

    // Records audible-tone switches and returns the configured load result as the new chain.
    [[nodiscard]] std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>
    setAudibleTone(const std::string& tone_document_ref) override
    {
        last_audible_tone_ref = tone_document_ref;
        set_audible_tone_call_count += 1;
        if (next_set_audible_tone_error.has_value())
        {
            return std::unexpected{*next_set_audible_tone_error};
        }

        return next_load_result;
    }

    // Records explicit clear requests made during project teardown.
    [[nodiscard]] std::expected<void, common::audio::LiveRigError> clearLiveRig() override
    {
        clear_call_count += 1;
        if (next_clear_error.has_value())
        {
            return std::unexpected{*next_clear_error};
        }

        return {};
    }

    // Returns the current output gain stored by setOutputGain or the default.
    [[nodiscard]] common::audio::Gain outputGain() const override
    {
        return current_output_gain;
    }

    // Records the output gain and returns success.
    [[nodiscard]] std::expected<void, common::audio::LiveRigError> setOutputGain(
        common::audio::Gain gain) override
    {
        if (next_set_output_gain_error.has_value())
        {
            return std::unexpected{*next_set_output_gain_error};
        }

        current_output_gain = common::audio::clampGain(gain);
        set_output_gain_call_count += 1;
        return {};
    }

    // Snapshot returned by the next successful capture.
    common::audio::LiveRigSnapshot next_capture_snapshot{
        .tone_document_ref = g_tone_document_ref,
        .plugins =
            {
                common::audio::PluginChainEntry{
                    .instance_id = "captured-instance",
                    .plugin_id = "captured-plugin",
                    .name = "Captured Amp",
                    .manufacturer = "Example Audio",
                    .format_name = "VST3",
                    .category = {},
                    .chain_index = 0,
                    .display_type_override = {},
                },
            },
        .output_gain = common::audio::Gain{},
    };

    // Result returned by the next successful load.
    common::audio::LiveRigLoadResult next_load_result{
        .plugins =
            {
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
            },
        .output_gain = common::audio::Gain{},
    };

    // Optional capture error returned instead of the configured snapshot.
    std::optional<common::audio::LiveRigError> next_capture_error{};

    // Optional load error returned instead of the configured load result.
    std::optional<common::audio::LiveRigError> next_load_error{};

    // Optional clear error returned instead of success.
    std::optional<common::audio::LiveRigError> next_clear_error{};

    // Optional output-gain error returned instead of success.
    std::optional<common::audio::LiveRigError> next_set_output_gain_error{};

    // Optional audible-tone error returned instead of success.
    std::optional<common::audio::LiveRigError> next_set_audible_tone_error{};

    // Last audible tone reference observed by the fake.
    std::optional<std::string> last_audible_tone_ref{};

    // Number of setAudibleTone calls received.
    int set_audible_tone_call_count{0};

    // When set, loadLiveRig stores its completion so tests can finish it explicitly.
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

    // Current output gain value stored by setOutputGain.
    common::audio::Gain current_output_gain{};

    // Number of setOutputGain calls received.
    int set_output_gain_call_count{0};

    // Deferred live-rig completion captured with the result configured at load time.
    struct PendingLoad
    {
        common::audio::LiveRigLoadResultCallback on_result;
        std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError> result{};
    };

    // Pending completion stored when tests need explicit control over live-rig load timing.
    std::optional<PendingLoad> pending_load{};
};

/*!
\brief Supplies a default audio-device port for tests that do not care about hardware state.
\return Process-lifetime configurable audio-device fake.
*/
[[nodiscard]] inline ConfigurableAudioDeviceConfiguration& defaultAudioDevices() noexcept
{
    static ConfigurableAudioDeviceConfiguration g_audio_devices;
    return g_audio_devices;
}

/*!
\brief Supplies a default plugin-host port for tests that do not care about plugin behavior.
\return Process-lifetime recording plugin-host fake.
*/
[[nodiscard]] inline RecordingPluginHost& defaultPluginHost()
{
    static RecordingPluginHost g_plugin_host;
    return g_plugin_host;
}

/*!
\brief Supplies a default live-rig port for tests that do not care about tone persistence behavior.
\return Process-lifetime fake live rig.
*/
[[nodiscard]] inline FakeLiveRig& defaultLiveRig()
{
    static FakeLiveRig g_live_rig = [] {
        FakeLiveRig rig;
        rig.next_load_result.plugins.clear();
        return rig;
    }();
    return g_live_rig;
}

/*!
\brief Builds the controller audio-port bundle used by most tests.

FakeTransport also implements the live-input port, which preserves the old test composition while
keeping construction explicit.

\param transport Transport and live-input fake used by the controller under test.
\param song_audio Song-audio fake used by the controller under test.
\return Controller audio-port bundle.
*/
[[nodiscard]] inline EditorController::AudioPorts audioPorts(
    FakeTransport& transport, ConfigurableSongAudio& song_audio)
{
    return EditorController::AudioPorts{
        .transport = transport,
        .song_audio = song_audio,
        .audio_devices = defaultAudioDevices(),
        .plugin_host = defaultPluginHost(),
        .live_rig = defaultLiveRig(),
        .live_input = transport,
    };
}

/*!
\brief Replaces the default audio-device port in the common test controller bundle.
\param transport Transport and live-input fake used by the controller under test.
\param song_audio Song-audio fake used by the controller under test.
\param audio_devices Audio-device fake used by the controller under test.
\return Controller audio-port bundle.
*/
[[nodiscard]] inline EditorController::AudioPorts audioPorts(
    FakeTransport& transport, ConfigurableSongAudio& song_audio,
    ConfigurableAudioDeviceConfiguration& audio_devices)
{
    return EditorController::AudioPorts{
        .transport = transport,
        .song_audio = song_audio,
        .audio_devices = audio_devices,
        .plugin_host = defaultPluginHost(),
        .live_rig = defaultLiveRig(),
        .live_input = transport,
    };
}

/*!
\brief Replaces the default plugin-host port in the common test controller bundle.
\param transport Transport and live-input fake used by the controller under test.
\param song_audio Song-audio fake used by the controller under test.
\param plugin_host Plugin-host fake used by the controller under test.
\return Controller audio-port bundle.
*/
[[nodiscard]] inline EditorController::AudioPorts audioPorts(
    FakeTransport& transport, ConfigurableSongAudio& song_audio, RecordingPluginHost& plugin_host)
{
    return EditorController::AudioPorts{
        .transport = transport,
        .song_audio = song_audio,
        .audio_devices = defaultAudioDevices(),
        .plugin_host = plugin_host,
        .live_rig = defaultLiveRig(),
        .live_input = transport,
    };
}

/*!
\brief Replaces the default plugin-host and live-rig ports in the common test controller bundle.
\param transport Transport and live-input fake used by the controller under test.
\param song_audio Song-audio fake used by the controller under test.
\param plugin_host Plugin-host fake used by the controller under test.
\param live_rig Live-rig fake used by the controller under test.
\return Controller audio-port bundle.
*/
[[nodiscard]] inline EditorController::AudioPorts audioPorts(
    FakeTransport& transport, ConfigurableSongAudio& song_audio, RecordingPluginHost& plugin_host,
    FakeLiveRig& live_rig)
{
    return EditorController::AudioPorts{
        .transport = transport,
        .song_audio = song_audio,
        .audio_devices = defaultAudioDevices(),
        .plugin_host = plugin_host,
        .live_rig = live_rig,
        .live_input = transport,
    };
}

/*!
\brief Replaces the default audio-device and plugin-host ports in the common test controller bundle.
\param transport Transport and live-input fake used by the controller under test.
\param song_audio Song-audio fake used by the controller under test.
\param audio_devices Audio-device fake used by the controller under test.
\param plugin_host Plugin-host fake used by the controller under test.
\return Controller audio-port bundle.
*/
[[nodiscard]] inline EditorController::AudioPorts audioPorts(
    FakeTransport& transport, ConfigurableSongAudio& song_audio,
    ConfigurableAudioDeviceConfiguration& audio_devices, RecordingPluginHost& plugin_host)
{
    return EditorController::AudioPorts{
        .transport = transport,
        .song_audio = song_audio,
        .audio_devices = audio_devices,
        .plugin_host = plugin_host,
        .live_rig = defaultLiveRig(),
        .live_input = transport,
    };
}

/*!
\brief Replaces every default controller audio port represented in controller tests.
\param transport Transport and live-input fake used by the controller under test.
\param song_audio Song-audio fake used by the controller under test.
\param audio_devices Audio-device fake used by the controller under test.
\param plugin_host Plugin-host fake used by the controller under test.
\param live_rig Live-rig fake used by the controller under test.
\return Controller audio-port bundle.
*/
[[nodiscard]] inline EditorController::AudioPorts audioPorts(
    FakeTransport& transport, ConfigurableSongAudio& song_audio,
    ConfigurableAudioDeviceConfiguration& audio_devices, RecordingPluginHost& plugin_host,
    FakeLiveRig& live_rig)
{
    return EditorController::AudioPorts{
        .transport = transport,
        .song_audio = song_audio,
        .audio_devices = audio_devices,
        .plugin_host = plugin_host,
        .live_rig = live_rig,
        .live_input = transport,
    };
}

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
        return
            [this](
                Project& project,
                const std::filesystem::path& file,
                const EditorController::ProjectOperationProgress&) { return open(project, file); };
    }

    // Returns the bound import callback shape expected by EditorController services.
    [[nodiscard]] EditorController::ImportFunction importFunction() noexcept
    {
        return [this](
                   Project& project,
                   const std::filesystem::path& file,
                   const EditorController::ProjectOperationProgress&) {
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

/*!
\brief Provides a standard loaded-content range for controller tests.
\param end_seconds End time for the loaded range.
\return Timeline range starting at zero and ending at end_seconds.
*/
[[nodiscard]] inline common::core::TimeRange loadedTimelineRange(double end_seconds = 4.0) noexcept
{
    return common::core::TimeRange{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{end_seconds},
    };
}

/*!
\brief Builds a stable route identity for calibration-gate tests.
\param input_device_name Physical input device name.
\param channel_index Zero-based input channel index.
\param input_channel_name Optional physical input channel name.
\return Input route identity using the test ASIO backend name.
*/
[[nodiscard]] inline common::audio::InputDeviceIdentity makeInputDeviceIdentity(
    std::string input_device_name = "Interface A", int channel_index = 0,
    std::string input_channel_name = {})
{
    if (input_channel_name.empty())
    {
        input_channel_name = "Input " + std::to_string(channel_index + 1);
    }

    return common::audio::InputDeviceIdentity{
        .backend_name = "ASIO",
        .input_device_name = std::move(input_device_name),
        .input_channel_index = channel_index,
        .input_channel_name = std::move(input_channel_name),
    };
}

/*!
\brief Builds song data with one arrangement.
\param path Backing audio path assigned to the Lead arrangement.
\param timeline_range Timeline range whose duration becomes the arrangement audio duration.
\param tone_document_ref Optional tone document reference assigned to the arrangement.
\return Song fixture with one Lead arrangement.
*/
[[nodiscard]] inline common::core::Song makeSong(
    std::filesystem::path path, common::core::TimeRange timeline_range = loadedTimelineRange(),
    std::string tone_document_ref = {})
{
    common::core::Song song;
    song.arrangements.push_back(
        common::core::Arrangement{
            .id = g_lead_arrangement_id,
            .part = common::core::Part::Lead,
            .difficulty = common::core::DifficultyRating{},
            .audio_asset =
                common::core::AudioAsset{.path = std::move(path), .normalization = std::nullopt},
            .audio_duration = timeline_range.duration(),
            .tone_document_ref = std::move(tone_document_ref),
            .tone_track = {},
        });

    return song;
}

/*!
\brief Builds a normalization record representing an already-analyzed asset whose gain is current.
\return Current normalization fixture.
*/
[[nodiscard]] inline common::core::AudioNormalization makeCurrentNormalization()
{
    return common::core::AudioNormalization{
        .gain_db = -4.0,
        .validation_sha256 = std::string(64, 'b'),
    };
}

/*!
\brief Builds an open seam that reports the analysis phase before returning loaded song data.
\param audio_path Backing audio path assigned to the returned song.
\param analysis_progress_call_count Counter incremented when analysis progress is reported.
\return Open callback for EditorController tests.
*/
[[nodiscard]] inline EditorController::OpenFunction makeAnalyzingOpenFunction(
    std::filesystem::path audio_path, int& analysis_progress_call_count)
{
    return [audio_path = std::move(audio_path), &analysis_progress_call_count](
               Project&,
               const std::filesystem::path&,
               const EditorController::ProjectOperationProgress& report_progress)
               -> std::expected<common::core::Song, ProjectError> {
        if (report_progress)
        {
            report_progress(EditorController::ProjectOperationPhase::AnalyzingBackingAudio);
        }
        ++analysis_progress_call_count;
        common::core::Song song = makeSong(audio_path);
        song.arrangements.front().audio_asset.normalization = makeCurrentNormalization();
        return song;
    };
}

/*!
\brief Builds an import seam that reports the analysis phase before returning imported song data.
\param audio_path Backing audio path assigned to the returned song.
\param analysis_progress_call_count Counter incremented when analysis progress is reported.
\return Import callback for EditorController tests.
*/
[[nodiscard]] inline EditorController::ImportFunction makeAnalyzingImportFunction(
    std::filesystem::path audio_path, int& analysis_progress_call_count)
{
    return [audio_path = std::move(audio_path), &analysis_progress_call_count](
               Project&,
               const std::filesystem::path&,
               const EditorController::ProjectOperationProgress& report_progress)
               -> std::expected<common::core::Song, ProjectError> {
        if (report_progress)
        {
            report_progress(EditorController::ProjectOperationPhase::AnalyzingBackingAudio);
        }
        ++analysis_progress_call_count;
        common::core::Song song = makeSong(audio_path);
        song.arrangements.front().audio_asset.normalization = makeCurrentNormalization();
        return song;
    };
}

/*!
\brief Builds song data with two arrangements so controller selection policy can be tested.
\param lead_path Backing audio path assigned to the Lead arrangement.
\param bass_path Backing audio path assigned to the Bass arrangement.
\return Song fixture with Lead and Bass arrangements.
*/
[[nodiscard]] inline common::core::Song makeTwoArrangementSong(
    std::filesystem::path lead_path, std::filesystem::path bass_path)
{
    common::core::Song song;
    song.arrangements.push_back(
        common::core::Arrangement{
            .id = g_lead_arrangement_id,
            .part = common::core::Part::Lead,
            .difficulty = common::core::DifficultyRating{},
            .audio_asset =
                common::core::AudioAsset{
                    .path = std::move(lead_path), .normalization = std::nullopt
                },
            .audio_duration = common::core::TimeDuration{},
            .tone_document_ref = {},
            .tone_track = {},
        });
    song.arrangements.push_back(
        common::core::Arrangement{
            .id = g_bass_arrangement_id,
            .part = common::core::Part::Bass,
            .difficulty = common::core::DifficultyRating{},
            .audio_asset =
                common::core::AudioAsset{
                    .path = std::move(bass_path), .normalization = std::nullopt
                },
            .audio_duration = common::core::TimeDuration{},
            .tone_document_ref = {},
            .tone_track = {},
        });

    return song;
}

/*!
\brief Loads arrangement audio through the controller so tests keep backend/session coupling.
\param controller Controller under test.
\param project_services Project services fake used by the controller.
\param audio Song-audio fake used by the controller.
\param path Backing audio path assigned to the loaded song fixture.
\param timeline_range Timeline range assigned to the loaded arrangement.
\return True when the controller loaded an active arrangement.
*/
[[nodiscard]] inline bool loadArrangement(
    EditorController& controller, FakeProjectServices& project_services,
    ConfigurableSongAudio& audio, std::filesystem::path path,
    common::core::TimeRange timeline_range = loadedTimelineRange())
{
    audio.next_prepared_audio_duration = timeline_range.duration();
    audio.next_set_active_arrangement_result = true;
    project_services.next_song = makeSong(std::move(path), timeline_range);
    controller.onOpenRequested(std::filesystem::path{"loaded.rhp"});
    return controller.session().currentArrangement() != nullptr;
}

/*!
\brief Loads arrangement audio and applies a neutral calibration for plugin-chain tests.
\param controller Controller under test.
\param project_services Project services fake used by the controller.
\param audio Song-audio fake used by the controller.
\param audio_devices Audio-device fake that receives the active input identity.
\param path Backing audio path assigned to the loaded song fixture.
\param timeline_range Timeline range assigned to the loaded arrangement.
\return True when arrangement load and manual calibration both succeeded.
*/
[[nodiscard]] inline bool loadCalibratedArrangement(
    EditorController& controller, FakeProjectServices& project_services,
    ConfigurableSongAudio& audio, ConfigurableAudioDeviceConfiguration& audio_devices,
    std::filesystem::path path, common::core::TimeRange timeline_range = loadedTimelineRange())
{
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    if (!loadArrangement(controller, project_services, audio, std::move(path), timeline_range))
    {
        return false;
    }

    controller.onInputCalibrationRequested();
    const auto calibrated = controller.onInputCalibrationManuallySet(0.0);
    if (!calibrated.has_value())
    {
        return false;
    }

    controller.onInputCalibrationDismissed();
    return true;
}

/*!
\brief Adds the default known plugin through the same browser route used by production UI.
\param controller Controller under test.
\param plugin_id Opaque plugin candidate ID selected through the browser.
*/
inline void addKnownPlugin(
    EditorController& controller, std::string plugin_id = "catalog-plugin-id")
{
    controller.onPluginBrowserRequested();
    controller.onSelectedPluginInsertRequested(std::move(plugin_id));
}

/*!
\brief Exposes stop enabledness as an optional value for combined presence/value assertions.
\param view Fake view containing the last pushed editor state.
\return Stop enabledness, or empty before any state has been pushed.
*/
[[nodiscard]] inline std::optional<bool> lastStopEnabled(const FakeEditorView& view)
{
    const EditorViewState* state = stateOrNull(view.last_state);
    if (state == nullptr)
    {
        return std::nullopt;
    }

    return state->transport.stop_enabled;
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

} // namespace rock_hero::editor::core
