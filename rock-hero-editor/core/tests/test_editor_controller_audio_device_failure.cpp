#include <cstdint>
#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/device/audio_device_status.h>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/settings/active_device_route.h>
#include <rock_hero/common/audio/settings/audio_config_store.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/testing/in_memory_audio_config_store.h>
#include <rock_hero/editor/core/audio/editor_audio_config_store.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Builds a closed-device status carrying the backend's recorded unavailable reason, mirroring how
// the engine publishes a failed open on its status snapshot.
[[nodiscard]] common::audio::AudioDeviceStatus closedStatusWithReason(std::string reason)
{
    common::audio::AudioDeviceStatus status;
    status.unavailable_reason = std::move(reason);
    return status;
}

// Builds a minimal open-device status, the shape the engine publishes after a successful open.
[[nodiscard]] common::audio::AudioDeviceStatus openStatus()
{
    return common::audio::AudioDeviceStatus{
        .open = true,
        .device_name = "Interface A",
        .backend_name = "ASIO",
        .sample_rate_hz = 48000.0,
        .bit_depth = 24,
        .input_channels = 1,
        .output_channels = 2,
        .buffer_size_samples = 128,
        .input_latency_ms = 4.5,
        .output_latency_ms = 7.5,
        .unavailable_reason = {},
    };
}

// Composition for the failure-prompt tests: a saved route, a controllable device port left in the
// configured state, a test-local live-input monitor (so settings-window open state never leaks
// between tests through the shared default monitor), and a controller already bound to the
// recording view.
struct FailurePromptHarness
{
    explicit FailurePromptHarness(
        std::string saved_route_blob = "own-device-state",
        common::audio::DeviceRestoreOutcome restore_outcome =
            common::audio::DeviceRestoreOutcome::DeviceUnavailable,
        common::audio::AudioDeviceStatus status = closedStatusWithReason("driver init failed"))
    {
        if (!saved_route_blob.empty())
        {
            REQUIRE(store
                        .setActiveDeviceRoute(
                            common::audio::ActiveDeviceRoute{
                                .serialized_state = saved_route_blob, .identity = std::nullopt
                            })
                        .has_value());
        }

        audio_devices.restore_serialized_device_state_outcome = restore_outcome;
        audio_devices.current_status = std::move(status);
        // Device events persist the current blob; keeping the fake's answer equal to the saved
        // route mirrors the engine (lastExplicitSettings survives a close) and keeps the route
        // present across notifyChanged() calls.
        audio_devices.serialized_device_state =
            saved_route_blob.empty() ? std::nullopt : std::optional{saved_route_blob};

        // Constructed after the fakes are configured so the startup route application observes
        // the intended device state, then bound to the recording view.
        controller.emplace(
            audioPorts(transport, audio, audio_devices),
            controllerServices(nullEditorSettings(), store, monitor),
            noopExitFunction());
        controller->attachView(view);
    }

    // The staged failure prompt in the most recently pushed view state, if any.
    [[nodiscard]] const std::optional<AudioDeviceFailurePrompt>& prompt() const
    {
        REQUIRE(view.last_state.has_value());
        return view.last_state->audio_device_failure_prompt;
    }

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    common::audio::testing::InMemoryAudioConfigStore store;
    common::audio::testing::FakeLiveInput live_input;
    common::audio::LiveInputMonitor monitor{live_input, audio_devices, store};
    FakeEditorView view;
    std::optional<EditorController> controller;
};

} // namespace

// A startup restore that leaves the saved device closed stages the persistent failure prompt with
// the backend's recorded reason.
TEST_CASE(
    "EditorController stages the failure prompt when startup leaves the device closed",
    "[core][editor-controller][audio-device-failure]")
{
    FailurePromptHarness harness;

    REQUIRE(harness.prompt().has_value());
    CHECK(harness.prompt()->message == "driver init failed");
    CHECK(harness.view.last_state->audio_device_status_text == "[audio device closed]");
}

// A startup that opens the device stages nothing, and a closed device with no saved route stays
// quiet too (fresh install: the settings window is the resolution path, not a retry loop).
TEST_CASE(
    "EditorController stays quiet when the device opens or no route exists",
    "[core][editor-controller][audio-device-failure]")
{
    SECTION("device opened")
    {
        FailurePromptHarness harness{
            "own-device-state", common::audio::DeviceRestoreOutcome::Opened, openStatus()
        };
        CHECK_FALSE(harness.prompt().has_value());
    }

    SECTION("no saved route")
    {
        FailurePromptHarness harness{
            std::string{},
            common::audio::DeviceRestoreOutcome::DeviceUnavailable,
            closedStatusWithReason("driver init failed")
        };
        CHECK_FALSE(harness.prompt().has_value());
        CHECK(harness.view.last_state->audio_device_status_text == "[audio device closed]");
    }
}

// Repeat device events while the device stays closed keep the same prompt (the blocking overlay
// follows the value directly, so an unchanged reason simply re-derives the same prompt); a device
// event carrying a fresher reason live-updates the overlay's text.
TEST_CASE(
    "EditorController keeps the failure prompt across repeat device events and live-updates its "
    "reason",
    "[core][editor-controller][audio-device-failure]")
{
    FailurePromptHarness harness;
    REQUIRE(harness.prompt().has_value());
    CHECK(harness.prompt()->message == "driver init failed");

    harness.audio_devices.notifyChanged();
    REQUIRE(harness.prompt().has_value());
    CHECK(harness.prompt()->message == "driver init failed");

    // A device event with a fresher backend reason updates the shown text in place.
    harness.audio_devices.current_status = closedStatusWithReason("device is in use");
    harness.audio_devices.notifyChanged();
    REQUIRE(harness.prompt().has_value());
    CHECK(harness.prompt()->message == "device is in use");
}

// A device event reporting the device open clears the staged prompt.
TEST_CASE(
    "EditorController clears the failure prompt when the device comes back",
    "[core][editor-controller][audio-device-failure]")
{
    FailurePromptHarness harness;
    REQUIRE(harness.prompt().has_value());

    harness.audio_devices.current_status = openStatus();
    harness.audio_devices.notifyChanged();

    CHECK_FALSE(harness.prompt().has_value());
}

// The settings window owns the closed-device situation while it is open: the prompt clears at
// open, stays clear through the close notification (the staged edit may still be rolling back),
// and re-stages only at teardown-complete when the device is still closed.
TEST_CASE(
    "EditorController suppresses the failure prompt for the settings window and re-stages at "
    "teardown",
    "[core][editor-controller][audio-device-failure]")
{
    FailurePromptHarness harness;
    REQUIRE(harness.prompt().has_value());

    REQUIRE(harness.controller->onAudioDeviceSettingsOpenRequested());
    CHECK_FALSE(harness.prompt().has_value());

    // Device events while the window is open stay suppressed.
    harness.audio_devices.notifyChanged();
    CHECK_FALSE(harness.prompt().has_value());

    // The close notification alone must not stage (the native-close cancel backstop may not have
    // rolled the device back yet); teardown-complete is the trustworthy evaluation point.
    harness.controller->onAudioDeviceSettingsClosed();
    CHECK_FALSE(harness.prompt().has_value());

    harness.controller->onAudioDeviceSettingsTeardownComplete();
    REQUIRE(harness.prompt().has_value());
}

// Retry re-applies the saved route behind the busy overlay. A still-failing device re-stages the
// prompt with the freshly recorded reason (the busy-clear evaluation is the single staging point,
// so nothing flashes mid-operation); a succeeding retry stays cleared.
TEST_CASE(
    "EditorController Retry re-stages on failure and clears on success",
    "[core][editor-controller][audio-device-failure]")
{
    FailurePromptHarness harness;
    REQUIRE(harness.prompt().has_value());
    const int startup_restore_count =
        harness.audio_devices.restore_serialized_device_state_call_count;

    // Failed retry: the device is still unavailable, now with a fresher backend reason.
    harness.audio_devices.current_status = closedStatusWithReason("device is in use");
    harness.controller->onAudioDeviceFailureDecision(AudioDeviceFailureDecision::Retry);

    CHECK(
        harness.audio_devices.restore_serialized_device_state_call_count ==
        startup_restore_count + 1);
    REQUIRE(harness.prompt().has_value());
    CHECK(harness.prompt()->message == "device is in use");

    // Successful retry: the route opens, and the prompt stays cleared.
    harness.audio_devices.restore_serialized_device_state_outcome =
        common::audio::DeviceRestoreOutcome::Opened;
    harness.audio_devices.current_status = openStatus();
    harness.controller->onAudioDeviceFailureDecision(AudioDeviceFailureDecision::Retry);

    CHECK(
        harness.audio_devices.restore_serialized_device_state_call_count ==
        startup_restore_count + 2);
    CHECK_FALSE(harness.prompt().has_value());
}

// OpenSettings clears the prompt so the settings window can take over; nothing re-stages until
// the window's teardown reports the device still closed.
TEST_CASE(
    "EditorController OpenSettings hands the failure prompt to the settings window",
    "[core][editor-controller][audio-device-failure]")
{
    FailurePromptHarness harness;
    REQUIRE(harness.prompt().has_value());

    harness.controller->onAudioDeviceFailureDecision(AudioDeviceFailureDecision::OpenSettings);
    CHECK_FALSE(harness.prompt().has_value());

    REQUIRE(harness.controller->onAudioDeviceSettingsOpenRequested());
    harness.controller->onAudioDeviceSettingsClosed();
    harness.controller->onAudioDeviceSettingsTeardownComplete();

    REQUIRE(harness.prompt().has_value());
}

namespace
{

// Owns a build-local game audio-config file so a game-source test starts and ends clean.
class ScopedGameFile final
{
public:
    explicit ScopedGameFile(std::string_view file_name)
        : m_path(std::filesystem::path{TEST_SETTINGS_DIR} / file_name)
    {
        remove();
    }

    ~ScopedGameFile()
    {
        remove();
    }

    ScopedGameFile(const ScopedGameFile&) = delete;
    ScopedGameFile& operator=(const ScopedGameFile&) = delete;
    ScopedGameFile(ScopedGameFile&&) = delete;
    ScopedGameFile& operator=(ScopedGameFile&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    void remove() const
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }

    std::filesystem::path m_path;
};

// Persists a calibrated game route so the editor's config store arms and reads the game source.
void writeCalibratedGameRoute(const std::filesystem::path& game_file, const std::string& blob)
{
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    common::audio::AudioConfigStore game_store{
        game_file, common::audio::AudioConfigStore::Access::ReadWrite
    };
    REQUIRE(
        game_store
            .setActiveDeviceRoute(
                common::audio::ActiveDeviceRoute{.serialized_state = blob, .identity = identity})
            .has_value());
    REQUIRE(game_store
                .saveInputCalibration(
                    common::audio::InputCalibrationState{
                        .calibration_gain = common::audio::Gain{6.0},
                        .input_device_identity = identity,
                    })
                .has_value());
}

} // namespace

// The plan-48 startup prompts have precedence: a staged game-audio recommendation defers the
// failure prompt (never two modals at once), and the recommendation's decision handler surfaces
// the deferred notice.
TEST_CASE(
    "EditorController defers the failure prompt behind the startup game-audio recommendation",
    "[core][editor-controller][audio-device-failure]")
{
    const ScopedControllerFiles files{"failure_prompt_recommendation_precedence"};
    const ScopedGameFile game_file{"failure_prompt_precedence.settings"};
    writeCalibratedGameRoute(game_file.path(), "game-device-state");

    EditorSettings settings{files.settingsFile()};
    common::audio::testing::InMemoryAudioConfigStore own_store;
    REQUIRE(own_store
                .setActiveDeviceRoute(
                    common::audio::ActiveDeviceRoute{
                        .serialized_state = "own-device-state", .identity = std::nullopt
                    })
                .has_value());
    EditorAudioConfigStore editor_store{own_store, game_file.path()};

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.restore_serialized_device_state_outcome =
        common::audio::DeviceRestoreOutcome::DeviceUnavailable;
    audio_devices.current_status = closedStatusWithReason("driver init failed");
    audio_devices.serialized_device_state = "own-device-state";
    common::audio::testing::FakeLiveInput live_input;
    common::audio::LiveInputMonitor monitor{live_input, audio_devices, editor_store};

    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        EditorController::Services{
            .settings = settings,
            .task_runner = immediateTaskRunner(),
            .message_thread_scheduler = immediateMessageThreadScheduler(),
            .audio_config_store = editor_store,
            .live_input_monitor = monitor,
            .editor_audio_config_store = &editor_store,
        },
        noopExitFunction()
    };
    FakeEditorView view;
    controller.attachView(view);

    // The recommendation is staged and owns the startup modal slot; the failure prompt defers.
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->game_audio_recommendation_prompt);
    CHECK_FALSE(view.last_state->audio_device_failure_prompt.has_value());
    CHECK_FALSE(view.last_state->use_game_audio_settings);

    // Once the recommendation resolves, the deferred closed-device notice surfaces.
    controller.onGameAudioRecommendationDecision(GameAudioRecommendationDecision::Dismissed, false);

    CHECK_FALSE(view.last_state->game_audio_recommendation_prompt);
    REQUIRE(view.last_state->audio_device_failure_prompt.has_value());
    CHECK(view.last_state->audio_device_failure_prompt->message == "driver init failed");
}

} // namespace rock_hero::editor::core
