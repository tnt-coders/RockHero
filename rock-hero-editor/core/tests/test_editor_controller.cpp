#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <optional>
#include <rock_hero/common/audio/i_audio.h>
#include <rock_hero/common/audio/i_audio_device_configuration.h>
#include <rock_hero/common/audio/i_guitar_input.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/common/audio/transport_state.h>
#include <rock_hero/common/core/audio_asset.h>
#include <rock_hero/common/core/session.h>
#include <rock_hero/common/core/timeline.h>
#include <rock_hero/editor/core/editor_controller.h>
#include <rock_hero/editor/core/editor_settings.h>
#include <rock_hero/editor/core/editor_view_state.h>
#include <rock_hero/editor/core/i_editor_controller.h>
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
        set_state_call_count += 1;
    }

    // Records one-shot workflow errors separately from durable render state.
    void showError(const std::string& message) override
    {
        shown_errors.push_back(message);
    }

    // Last durable state pushed to the view.
    std::optional<EditorViewState> last_state{};

    // One-shot error messages reported through the transient view-effect channel.
    std::vector<std::string> shown_errors{};

    // Number of durable state pushes observed by the fake.
    int set_state_call_count{0};
};

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

    // Captures the latest live monitoring toggle intent.
    void onLiveMonitoringToggled(bool enabled) override
    {
        last_live_monitoring_enabled = enabled;
        live_monitoring_toggle_count += 1;
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

    // Last live monitoring toggle state emitted by the view.
    std::optional<bool> last_live_monitoring_enabled{};

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

    // Number of live monitoring toggle intents received.
    int live_monitoring_toggle_count{0};
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

    // Returns the manually controlled live cursor position.
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

    // Live cursor position returned by position().
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

// Minimal guitar-input fake that toggles its monitoring flag without touching real hardware.
class FakeGuitarInput final : public common::audio::IGuitarInput
{
public:
    // Enables fake monitoring so view-state derivation can observe the transition.
    void enableGuitarMonitoring() override
    {
        monitoring_enabled = true;
        enable_call_count += 1;
    }

    // Disables fake monitoring so view-state derivation can observe the transition.
    void disableGuitarMonitoring() override
    {
        monitoring_enabled = false;
        disable_call_count += 1;
    }

    // Reports the fake monitoring flag controlled by enable/disable calls.
    [[nodiscard]] bool isGuitarMonitoringEnabled() const noexcept override
    {
        return monitoring_enabled;
    }

    // Number of monitoring-enable calls received.
    int enable_call_count{0};

    // Number of monitoring-disable calls received.
    int disable_call_count{0};

    // Current fake monitoring flag.
    bool monitoring_enabled{false};
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
    std::filesystem::path path, common::core::TimeRange timeline_range = loadedTimelineRange())
{
    common::core::Song song;
    song.arrangements.push_back(
        common::core::Arrangement{
            .id = "lead",
            .part = common::core::Part::Lead,
            .difficulty = common::core::DifficultyRating{},
            .audio_asset = common::core::AudioAsset{std::move(path)},
            .audio_duration = timeline_range.duration(),
            .tone_timeline_ref = {},
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
            .tone_timeline_ref = {},
            .note_events = {},
        });
    song.arrangements.push_back(
        common::core::Arrangement{
            .id = "bass",
            .part = common::core::Part::Bass,
            .difficulty = common::core::DifficultyRating{},
            .audio_asset = common::core::AudioAsset{std::move(bass_path)},
            .audio_duration = common::core::TimeDuration{},
            .tone_timeline_ref = {},
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
    if (!view.last_state.has_value())
    {
        return std::nullopt;
    }

    return view.last_state->stop_enabled;
}

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
    CHECK(empty_state.live_monitoring_enabled == false);
    CHECK(empty_state.visible_timeline == common::core::TimeRange{});
    CHECK_FALSE(empty_state.arrangement.hasAudio());
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
        .live_monitoring_enabled = true,
        .visible_timeline = loadedTimelineRange(180.0),
        .arrangement =
            ArrangementViewState{
                .audio_asset = audio_asset,
                .audio_duration = common::core::TimeDuration{180.0},
            },
        .unsaved_changes_prompt = UnsavedChangesPrompt{.action = PendingProjectAction::Close},
        .save_as_prompt = SaveAsPrompt{.action = PendingProjectAction::Close},
    };

    CHECK(loaded_state.arrangement.audio_asset == std::optional{audio_asset});
    CHECK(loaded_state.current_audio_device_name == std::optional<std::string>{"ASIO Driver"});
    CHECK(loaded_state.audio_devices_available);
    CHECK(loaded_state.live_monitoring_enabled);
    CHECK(loaded_state.arrangement.audioTimelineRange() == loadedTimelineRange(180.0));
    CHECK(loaded_state.arrangement.hasAudio());
    CHECK(
        loaded_state.unsaved_changes_prompt ==
        std::optional{UnsavedChangesPrompt{.action = PendingProjectAction::Close}});
    CHECK(
        loaded_state.save_as_prompt ==
        std::optional{SaveAsPrompt{.action = PendingProjectAction::Close}});
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
    controller.onLiveMonitoringToggled(true);

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
    CHECK(controller.live_monitoring_toggle_count == 1);
    CHECK(controller.last_live_monitoring_enabled == std::optional{true});
}

// Verifies the controller publishes the current audio-device name through view state.
TEST_CASE("EditorController publishes current audio device", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeAudioDeviceConfiguration audio_devices;
    FakeGuitarInput guitar_input;
    audio_devices.current_name = std::string{"Interface A"};
    EditorController controller{transport, audio, audio_devices, guitar_input};
    FakeEditorView view;

    controller.attachView(view);

    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->audio_devices_available);
    CHECK(view.last_state->current_audio_device_name == std::optional<std::string>{"Interface A"});
    CHECK_FALSE(view.last_state->live_monitoring_enabled);
}

// Toggling monitoring routes through the guitar-input port without touching device config.
TEST_CASE("EditorController toggles live monitoring", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeAudioDeviceConfiguration audio_devices;
    FakeGuitarInput guitar_input;
    EditorController controller{transport, audio, audio_devices, guitar_input};
    FakeEditorView view;
    controller.attachView(view);

    controller.onLiveMonitoringToggled(true);

    CHECK(guitar_input.enable_call_count == 1);
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->live_monitoring_enabled);

    controller.onLiveMonitoringToggled(false);

    CHECK(guitar_input.disable_call_count == 1);
    REQUIRE(view.last_state.has_value());
    CHECK_FALSE(view.last_state->live_monitoring_enabled);
}

// Device-manager change notifications re-derive view state through the listener relay.
TEST_CASE("EditorController re-derives state on device change", "[core][editor-controller]")
{
    FakeTransport transport;
    FakeAudio audio;
    FakeAudioDeviceConfiguration audio_devices;
    FakeGuitarInput guitar_input;
    EditorController controller{transport, audio, audio_devices, guitar_input};
    FakeEditorView view;
    controller.attachView(view);
    const int baseline_pushes = view.set_state_call_count;

    audio_devices.current_name = std::string{"Interface B"};
    audio_devices.notifyChanged();

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == baseline_pushes + 1);
    CHECK(view.last_state->current_audio_device_name == std::optional<std::string>{"Interface B"});
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
        CHECK_FALSE(state.live_monitoring_enabled);
        CHECK(state.visible_timeline == common::core::TimeRange{});
        CHECK_FALSE(state.arrangement.hasAudio());
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
    CHECK(view.set_state_call_count == pushes_before_success + 1);
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
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not save: disk full");
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
    CHECK(view.set_state_call_count == pushes_before_success + 1);
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
            std::optional{UnsavedChangesPrompt{.action = PendingProjectAction::Close}});
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
            std::optional{SaveAsPrompt{.action = PendingProjectAction::Close}});
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
            std::optional{UnsavedChangesPrompt{.action = PendingProjectAction::Exit}});
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

    CHECK(view.set_state_call_count == pushes_before_load + 1);
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

} // namespace rock_hero::editor::core
