#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <rock_hero/audio/i_edit.h>
#include <rock_hero/audio/i_transport.h>
#include <rock_hero/audio/transport_state.h>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/project.h>
#include <rock_hero/core/session.h>
#include <rock_hero/core/timeline.h>
#include <rock_hero/ui/edit_coordinator.h>
#include <rock_hero/ui/editor_controller.h>
#include <rock_hero/ui/editor_view_state.h>
#include <rock_hero/ui/i_editor_controller.h>
#include <rock_hero/ui/i_editor_view.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::ui
{

namespace
{

// Captures the most recent editor view state pushed through the framework-free view contract.
class FakeEditorView final : public IEditorView
{
public:
    // Records the supplied state so tests can observe what a controller would render.
    void setState(const EditorViewState& state) override
    {
        last_state = state;
        set_state_call_count += 1;
    }

    std::optional<EditorViewState> last_state{};
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

    void onImportRequested(std::filesystem::path file) override
    {
        last_import_file = std::move(file);
        import_request_count += 1;
    }

    void onSaveRequested() override
    {
        save_request_count += 1;
    }

    void onSaveAsRequested(std::filesystem::path file) override
    {
        last_save_as_file = std::move(file);
        save_as_request_count += 1;
    }

    void onPlayPausePressed() override
    {
        play_pause_press_count += 1;
    }

    void onStopPressed() override
    {
        stop_press_count += 1;
    }

    void onWaveformClicked(double normalized_x) override
    {
        last_normalized_x = normalized_x;
        waveform_click_count += 1;
    }

    std::optional<std::filesystem::path> last_open_file{};
    std::optional<std::filesystem::path> last_import_file{};
    std::optional<std::filesystem::path> last_save_as_file{};
    std::optional<double> last_normalized_x{};
    int open_request_count{0};
    int import_request_count{0};
    int save_request_count{0};
    int save_as_request_count{0};
    int play_pause_press_count{0};
    int stop_press_count{0};
    int waveform_click_count{0};
};

// Records control intents and exposes a manual notification hook for controller tests.
class FakeTransport final : public audio::ITransport
{
public:
    void play() override
    {
        ++play_call_count;
    }

    void pause() override
    {
        ++pause_call_count;
    }

    void stop() override
    {
        current_state.playing = false;
        current_position = core::TimePosition{};
        ++stop_call_count;
    }

    // Records the requested seek so tests can verify clamping and timeline scaling.
    void seek(core::TimePosition position) override
    {
        last_seek_position = position;
        current_position = position;
        ++seek_call_count;
    }

    [[nodiscard]] audio::TransportState state() const noexcept override
    {
        return current_state;
    }

    [[nodiscard]] core::TimePosition position() const noexcept override
    {
        return current_position;
    }

    void addListener(Listener& listener) override
    {
        listeners.push_back(&listener);
    }

    void removeListener(Listener& listener) override
    {
        std::erase(listeners, &listener);
    }

    // Updates the state and fires a coarse listener callback to mimic a real transition.
    void setStateAndNotify(const audio::TransportState& new_state)
    {
        current_state = new_state;
        for (Listener* listener : listeners)
        {
            listener->onTransportStateChanged(current_state);
        }
    }

    audio::TransportState current_state{};
    core::TimePosition current_position{};
    std::vector<Listener*> listeners{};
    std::optional<core::TimePosition> last_seek_position{};
    int play_call_count{0};
    int pause_call_count{0};
    int stop_call_count{0};
    int seek_call_count{0};
};

// Configurable IEdit fake that records calls and can simulate reentrant notifications.
class FakeEdit final : public audio::IEdit
{
public:
    // Records the requested audio load and optionally fires an injected reentrant action.
    std::optional<core::TimeDuration> loadAudio(const core::AudioAsset& audio_asset) override
    {
        last_audio_asset = audio_asset;
        ++load_audio_call_count;
        if (during_edit_action)
        {
            during_edit_action();
        }
        return next_audio_duration;
    }

    std::optional<core::TimeDuration> next_audio_duration{core::TimeDuration{4.0}};
    int load_audio_call_count{0};
    std::optional<core::AudioAsset> last_audio_asset{};
    std::function<void()> during_edit_action{};
};

// Provides project IO results without touching JUCE or the filesystem.
class FakeProjectIo final
{
public:
    std::expected<core::Song, std::string> open(
        core::Project& project, const std::filesystem::path& file)
    {
        (void)project;
        last_open_file = file;
        ++open_call_count;
        if (!next_song.has_value())
        {
            return std::unexpected<std::string>{next_error_message};
        }

        core::Song song = std::move(*next_song);
        next_song.reset();
        return song;
    }

    std::expected<core::Song, std::string> import(
        core::Project& project, const std::filesystem::path& file)
    {
        (void)project;
        last_import_file = file;
        ++import_call_count;
        if (!next_import_song.has_value())
        {
            return std::unexpected<std::string>{next_import_error_message};
        }

        core::Song song = std::move(*next_import_song);
        next_import_song.reset();
        return song;
    }

    std::expected<void, std::string> save(core::Project& project, const core::Song& song)
    {
        (void)project;
        last_save_audio_path = firstAudioPath(song);
        ++save_call_count;
        if (next_save_error.has_value())
        {
            return std::unexpected<std::string>{*next_save_error};
        }
        return std::expected<void, std::string>{};
    }

    std::expected<void, std::string> saveAs(
        core::Project& project, const std::filesystem::path& file, const core::Song& song)
    {
        (void)project;
        last_save_as_file = file;
        last_save_as_audio_path = firstAudioPath(song);
        ++save_as_call_count;
        if (next_save_as_error.has_value())
        {
            return std::unexpected<std::string>{*next_save_as_error};
        }
        return std::expected<void, std::string>{};
    }

    [[nodiscard]] OpenFunction openFunction() noexcept
    {
        return [this](core::Project& project, const std::filesystem::path& file) {
            return open(project, file);
        };
    }

    [[nodiscard]] ImportFunction importFunction() noexcept
    {
        return [this](core::Project& project, const std::filesystem::path& file) {
            return import(project, file);
        };
    }

    [[nodiscard]] SaveFunction saveFunction() noexcept
    {
        return
            [this](core::Project& project, const core::Song& song) { return save(project, song); };
    }

    [[nodiscard]] SaveAsFunction saveAsFunction() noexcept
    {
        return
            [this](
                core::Project& project, const std::filesystem::path& file, const core::Song& song) {
                return saveAs(project, file, song);
            };
    }

    std::optional<core::Song> next_song{};
    std::optional<core::Song> next_import_song{};
    std::string next_error_message{"Open failed"};
    std::string next_import_error_message{"Import failed"};
    std::optional<std::string> next_save_error{};
    std::optional<std::string> next_save_as_error{};
    std::optional<std::filesystem::path> last_open_file{};
    std::optional<std::filesystem::path> last_import_file{};
    std::optional<std::filesystem::path> last_save_as_file{};
    std::optional<std::filesystem::path> last_save_audio_path{};
    std::optional<std::filesystem::path> last_save_as_audio_path{};
    int open_call_count{0};
    int import_call_count{0};
    int save_call_count{0};
    int save_as_call_count{0};

private:
    // Returns the first arrangement audio path to verify the saved session content.
    [[nodiscard]] static std::optional<std::filesystem::path> firstAudioPath(const core::Song& song)
    {
        if (song.chart.arrangements.empty() ||
            !song.chart.arrangements.front().audio_asset.has_value())
        {
            return std::nullopt;
        }
        return song.chart.arrangements.front().audio_asset->path;
    }
};

// Provides a standard loaded-content range for controller tests.
[[nodiscard]] core::TimeRange loadedTimelineRange(double end_seconds = 4.0) noexcept
{
    return core::TimeRange{
        .start = core::TimePosition{},
        .end = core::TimePosition{end_seconds},
    };
}

// Builds song data with one arrangement.
[[nodiscard]] core::Song makeSong(
    std::filesystem::path path, core::TimeRange timeline_range = loadedTimelineRange())
{
    core::Song song;
    song.chart.arrangements.push_back(
        core::Arrangement{
            .part = core::Part::Lead,
            .audio_asset = core::AudioAsset{std::move(path)},
            .audio_duration = timeline_range.duration(),
        });

    return song;
}

// Builds song data with two arrangements so controller selection policy can be tested.
[[nodiscard]] core::Song makeTwoArrangementSong(
    std::filesystem::path lead_path, std::filesystem::path bass_path)
{
    core::Song song;
    song.chart.arrangements.push_back(
        core::Arrangement{
            .part = core::Part::Lead,
            .audio_asset = core::AudioAsset{std::move(lead_path)},
        });
    song.chart.arrangements.push_back(
        core::Arrangement{
            .part = core::Part::Bass,
            .audio_asset = core::AudioAsset{std::move(bass_path)},
        });

    return song;
}

// Loads arrangement audio through the coordinator so tests keep backend/session coupling.
void loadArrangement(
    EditCoordinator& edit_coordinator, FakeEdit& edit, std::filesystem::path path,
    core::TimeRange timeline_range = loadedTimelineRange())
{
    edit.next_audio_duration = timeline_range.duration();
    const bool song_loaded =
        edit_coordinator.loadSong(makeSong(std::move(path), timeline_range), 0);
    REQUIRE(song_loaded);
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
TEST_CASE("EditorViewState represents one arrangement", "[ui][editor-controller]")
{
    const EditorViewState empty_state{};

    CHECK(empty_state.open_enabled == false);
    CHECK(empty_state.import_enabled == false);
    CHECK(empty_state.save_enabled == false);
    CHECK(empty_state.save_as_enabled == false);
    CHECK(empty_state.save_requires_destination == false);
    CHECK(empty_state.play_pause_enabled == false);
    CHECK(empty_state.stop_enabled == false);
    CHECK(empty_state.play_pause_shows_pause_icon == false);
    CHECK(empty_state.visible_timeline == core::TimeRange{});
    CHECK_FALSE(empty_state.arrangement.hasAudio());
    CHECK_FALSE(empty_state.last_error.has_value());

    const core::AudioAsset audio_asset{std::filesystem::path{"full_mix.wav"}};
    const EditorViewState loaded_state{
        .open_enabled = true,
        .import_enabled = true,
        .save_enabled = true,
        .save_as_enabled = true,
        .save_requires_destination = false,
        .play_pause_enabled = true,
        .stop_enabled = true,
        .play_pause_shows_pause_icon = true,
        .visible_timeline = loadedTimelineRange(180.0),
        .arrangement =
            ArrangementViewState{
                .audio_asset = audio_asset,
                .audio_duration = core::TimeDuration{180.0},
            },
        .last_error = std::string{"Could not open"},
    };

    CHECK(loaded_state.arrangement.audio_asset == std::optional{audio_asset});
    CHECK(loaded_state.arrangement.audioTimelineRange() == loadedTimelineRange(180.0));
    CHECK(loaded_state.arrangement.hasAudio());
    CHECK(loaded_state.last_error == std::optional<std::string>{"Could not open"});
}

// Verifies a fake controller can receive editor intents without JUCE callback types.
TEST_CASE("IEditorController fake receives editor intents", "[ui][editor-controller]")
{
    FakeEditorController controller;
    const std::filesystem::path open_file{"song.rhp"};
    const std::filesystem::path import_file{"song.psarc"};
    const std::filesystem::path save_as_file{"saved.rhp"};

    controller.onOpenRequested(open_file);
    controller.onImportRequested(import_file);
    controller.onSaveRequested();
    controller.onSaveAsRequested(save_as_file);
    controller.onPlayPausePressed();
    controller.onStopPressed();
    controller.onWaveformClicked(0.75);

    CHECK(controller.open_request_count == 1);
    CHECK(controller.last_open_file == std::optional{open_file});
    CHECK(controller.import_request_count == 1);
    CHECK(controller.last_import_file == std::optional{import_file});
    CHECK(controller.save_request_count == 1);
    CHECK(controller.save_as_request_count == 1);
    CHECK(controller.last_save_as_file == std::optional{save_as_file});
    CHECK(controller.play_pause_press_count == 1);
    CHECK(controller.stop_press_count == 1);
    CHECK(controller.waveform_click_count == 1);
    CHECK(controller.last_normalized_x == std::optional{0.75});
}

// Confirms attachView immediately delivers the controller's cached arrangement state.
TEST_CASE("EditorController pushes derived state on view attachment", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;

    controller.attachView(view);

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == 1);
    CHECK(view.last_state->open_enabled == true);
    CHECK(view.last_state->import_enabled == true);
    CHECK(view.last_state->save_enabled == false);
    CHECK(view.last_state->save_as_enabled == false);
    CHECK(view.last_state->play_pause_enabled == false);
    CHECK(view.last_state->stop_enabled == false);
    CHECK(view.last_state->play_pause_shows_pause_icon == false);
    CHECK(view.last_state->visible_timeline == core::TimeRange{});
    CHECK_FALSE(view.last_state->arrangement.hasAudio());
    CHECK_FALSE(view.last_state->last_error.has_value());
    CHECK(edit.load_audio_call_count == 0);
}

// Verifies the controller pushes session timeline mapping from loaded arrangement audio.
TEST_CASE("EditorController derives visible timeline range", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    loadArrangement(
        edit_coordinator, edit, std::filesystem::path{"a.wav"}, loadedTimelineRange(8.0));
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;
    controller.attachView(view);

    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->visible_timeline == loadedTimelineRange(8.0));
    CHECK(view.last_state->arrangement.audio_duration == core::TimeDuration{8.0});
}

// Each coarse transport transition produces exactly one fresh push so the view stays current.
TEST_CASE("EditorController pushes one state per coarse transition", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    loadArrangement(edit_coordinator, edit, std::filesystem::path{"a.wav"});
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;
    controller.attachView(view);

    transport.setStateAndNotify(
        audio::TransportState{
            .playing = true,
        });

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == 2);
    CHECK(view.last_state->play_pause_shows_pause_icon == true);
    CHECK(view.last_state->stop_enabled == true);
    CHECK(view.last_state->visible_timeline == loadedTimelineRange());

    transport.setStateAndNotify(
        audio::TransportState{
            .playing = false,
        });

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == 3);
    CHECK(view.last_state->play_pause_shows_pause_icon == false);
    CHECK(view.last_state->stop_enabled == false);
}

// Play intent issues play() when stopped and pause() when playing, once audio is loaded.
TEST_CASE("EditorController play intent toggles loaded transport", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    loadArrangement(edit_coordinator, edit, std::filesystem::path{"a.wav"});
    EditorController controller{transport, edit_coordinator};

    controller.onPlayPausePressed();
    CHECK(transport.play_call_count == 1);
    CHECK(transport.pause_call_count == 0);

    transport.current_state.playing = true;
    controller.onPlayPausePressed();
    CHECK(transport.play_call_count == 1);
    CHECK(transport.pause_call_count == 1);
}

// Without arrangement audio there is nothing to play, so the intent is a no-op.
TEST_CASE("EditorController ignores play intent without audio", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    EditorController controller{transport, edit_coordinator};

    controller.onPlayPausePressed();

    CHECK(transport.play_call_count == 0);
    CHECK(transport.pause_call_count == 0);
}

// The stop intent respects the same gate the view publishes.
TEST_CASE("EditorController stop intent follows reset gate", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    loadArrangement(edit_coordinator, edit, std::filesystem::path{"a.wav"});
    EditorController controller{transport, edit_coordinator};

    controller.onStopPressed();
    CHECK(transport.stop_call_count == 0);

    transport.current_position = core::TimePosition{1.5};
    controller.onStopPressed();
    CHECK(transport.stop_call_count == 1);
    CHECK(transport.current_position == core::TimePosition{});

    transport.current_state.playing = true;
    controller.onStopPressed();
    CHECK(transport.stop_call_count == 2);
}

// Stopping from a paused non-start cursor refreshes the view directly after stop().
TEST_CASE("EditorController stop intent refreshes paused reset state", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    loadArrangement(edit_coordinator, edit, std::filesystem::path{"a.wav"});
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;
    controller.attachView(view);

    transport.current_position = core::TimePosition{1.5};
    transport.setStateAndNotify(
        audio::TransportState{
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
TEST_CASE("EditorController waveform click clamps and scales", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    loadArrangement(
        edit_coordinator, edit, std::filesystem::path{"a.wav"}, loadedTimelineRange(4.0));
    EditorController controller{transport, edit_coordinator};

    controller.onWaveformClicked(0.5);
    CHECK(transport.last_seek_position == std::optional{core::TimePosition{2.0}});

    controller.onWaveformClicked(-0.25);
    CHECK(transport.last_seek_position == std::optional{core::TimePosition{0.0}});

    controller.onWaveformClicked(1.5);
    CHECK(transport.last_seek_position == std::optional{core::TimePosition{4.0}});
}

// A seek issued by the controller refreshes whether Stop can reset the cursor.
TEST_CASE("EditorController waveform click refreshes stop state", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    loadArrangement(
        edit_coordinator, edit, std::filesystem::path{"a.wav"}, loadedTimelineRange(4.0));
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;
    controller.attachView(view);

    CHECK(lastStopEnabled(view) == std::optional{false});

    controller.onWaveformClicked(0.5);

    CHECK(transport.last_seek_position == std::optional{core::TimePosition{2.0}});
    CHECK(lastStopEnabled(view) == std::optional{true});

    controller.onWaveformClicked(0.0);

    CHECK(transport.last_seek_position == std::optional{core::TimePosition{}});
    CHECK(lastStopEnabled(view) == std::optional{false});
}

// A failed project-audio load leaves the session unchanged and surfaces an error.
TEST_CASE("EditorController failed load preserves session", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    loadArrangement(
        edit_coordinator, edit, std::filesystem::path{"old.wav"}, loadedTimelineRange(6.0));
    edit.next_audio_duration = std::nullopt;
    FakeProjectIo project_load;
    project_load.next_song = makeSong(std::filesystem::path{"new.wav"});
    EditorController controller{transport, edit_coordinator, project_load.openFunction()};
    FakeEditorView view;
    controller.attachView(view);

    controller.onOpenRequested(std::filesystem::path{"new.rhp"});

    const core::Session& session = edit_coordinator.session();
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(
        session.currentArrangement()->audio_asset ==
        std::optional{core::AudioAsset{std::filesystem::path{"old.wav"}}});
    CHECK(session.timeline() == loadedTimelineRange(6.0));
    REQUIRE(view.last_state.has_value());
    REQUIRE(view.last_state->last_error.has_value());
    CHECK(view.last_state->last_error->find("new.rhp") != std::string::npos);
}

// A successful open stores the selected audio and clears prior error.
TEST_CASE("EditorController successful open stores audio", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    FakeProjectIo project_load;
    EditorController controller{transport, edit_coordinator, project_load.openFunction()};
    FakeEditorView view;
    controller.attachView(view);

    project_load.next_song = makeSong(std::filesystem::path{"first.wav"});
    edit.next_audio_duration = std::nullopt;
    controller.onOpenRequested(std::filesystem::path{"first.rhp"});
    REQUIRE(view.last_state.has_value());
    REQUIRE(view.last_state->last_error.has_value());
    const int pushes_before_success = view.set_state_call_count;

    edit.next_audio_duration = core::TimeDuration{4.0};
    const core::AudioAsset replacement{std::filesystem::path{"second.wav"}};
    project_load.next_song = makeSong(replacement.path, loadedTimelineRange(4.0));
    controller.onOpenRequested(std::filesystem::path{"second.rhp"});

    const core::Session& session = edit_coordinator.session();
    CHECK(edit.load_audio_call_count == 2);
    CHECK(edit.last_audio_asset == std::optional{replacement});
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(session.currentArrangement()->audio_asset == std::optional{replacement});
    CHECK(session.currentArrangement()->audio_duration == core::TimeDuration{4.0});
    CHECK(session.timeline() == loadedTimelineRange(4.0));
    REQUIRE(view.last_state.has_value());
    CHECK_FALSE(view.last_state->last_error.has_value());
    CHECK(view.last_state->arrangement.audio_asset == std::optional{replacement});
    CHECK(view.last_state->save_enabled == true);
    CHECK(view.last_state->save_as_enabled == true);
    CHECK(view.last_state->save_requires_destination == false);
    CHECK(view.set_state_call_count == pushes_before_success + 1);
}

// Save writes the currently loaded session song through the injected persistence seam.
TEST_CASE("EditorController save writes current session song", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    FakeProjectIo project_io;
    EditorController controller{
        transport,
        edit_coordinator,
        project_io.openFunction(),
        ImportFunction{},
        project_io.saveFunction(),
        project_io.saveAsFunction(),
    };
    FakeEditorView view;
    controller.attachView(view);

    const core::AudioAsset audio_asset{std::filesystem::path{"song.wav"}};
    project_io.next_song = makeSong(audio_asset.path);
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    controller.onSaveRequested();

    CHECK(project_io.save_call_count == 1);
    CHECK(project_io.save_as_call_count == 0);
    CHECK(project_io.last_save_audio_path == std::optional{audio_asset.path});
    REQUIRE(view.last_state.has_value());
    CHECK_FALSE(view.last_state->last_error.has_value());
}

// Save failures are surfaced without clearing the loaded session.
TEST_CASE("EditorController save failure surfaces an error", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    FakeProjectIo project_io;
    EditorController controller{
        transport,
        edit_coordinator,
        project_io.openFunction(),
        ImportFunction{},
        project_io.saveFunction(),
        project_io.saveAsFunction(),
    };
    FakeEditorView view;
    controller.attachView(view);

    project_io.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    project_io.next_save_error = std::string{"disk full"};

    controller.onSaveRequested();

    CHECK(project_io.save_call_count == 1);
    REQUIRE(view.last_state.has_value());
    REQUIRE(view.last_state->last_error.has_value());
    CHECK(view.last_state->last_error->find("disk full") != std::string::npos);
}

// A failed import leaves the current session unchanged and surfaces an error.
TEST_CASE("EditorController failed import preserves session", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    loadArrangement(
        edit_coordinator, edit, std::filesystem::path{"old.wav"}, loadedTimelineRange(6.0));
    FakeProjectIo project_load;
    EditorController controller{
        transport, edit_coordinator, OpenFunction{}, project_load.importFunction()
    };
    FakeEditorView view;
    controller.attachView(view);

    controller.onImportRequested(std::filesystem::path{"broken.psarc"});

    const core::Session& session = edit_coordinator.session();
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(
        session.currentArrangement()->audio_asset ==
        std::optional{core::AudioAsset{std::filesystem::path{"old.wav"}}});
    CHECK(session.timeline() == loadedTimelineRange(6.0));
    CHECK(project_load.import_call_count == 1);
    CHECK(project_load.open_call_count == 0);
    REQUIRE(view.last_state.has_value());
    REQUIRE(view.last_state->last_error.has_value());
    CHECK(view.last_state->last_error->find("import") != std::string::npos);
}

// A successful import stores the imported audio and clears prior error.
TEST_CASE("EditorController successful import stores audio", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    FakeProjectIo project_load;
    EditorController controller{
        transport, edit_coordinator, OpenFunction{}, project_load.importFunction()
    };
    FakeEditorView view;
    controller.attachView(view);

    project_load.next_import_song = makeSong(std::filesystem::path{"first.ogg"});
    edit.next_audio_duration = std::nullopt;
    controller.onImportRequested(std::filesystem::path{"first.psarc"});
    REQUIRE(view.last_state.has_value());
    REQUIRE(view.last_state->last_error.has_value());
    const int pushes_before_success = view.set_state_call_count;

    edit.next_audio_duration = core::TimeDuration{4.0};
    const core::AudioAsset replacement{std::filesystem::path{"imported.ogg"}};
    project_load.next_import_song = makeSong(replacement.path, loadedTimelineRange(4.0));
    controller.onImportRequested(std::filesystem::path{"second.psarc"});

    const core::Session& session = edit_coordinator.session();
    CHECK(project_load.import_call_count == 2);
    CHECK(project_load.open_call_count == 0);
    CHECK(edit.load_audio_call_count == 2);
    CHECK(edit.last_audio_asset == std::optional{replacement});
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(session.currentArrangement()->audio_asset == std::optional{replacement});
    CHECK(session.currentArrangement()->audio_duration == core::TimeDuration{4.0});
    CHECK(session.timeline() == loadedTimelineRange(4.0));
    REQUIRE(view.last_state.has_value());
    CHECK_FALSE(view.last_state->last_error.has_value());
    CHECK(view.last_state->arrangement.audio_asset == std::optional{replacement});
    CHECK(view.last_state->save_enabled == true);
    CHECK(view.last_state->save_as_enabled == true);
    CHECK(view.last_state->save_requires_destination == true);
    CHECK(view.set_state_call_count == pushes_before_success + 1);
}

// Imported content requires Save As before direct Save can write to a destination.
TEST_CASE("EditorController import requires Save As destination", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    FakeProjectIo project_io;
    EditorController controller{
        transport,
        edit_coordinator,
        OpenFunction{},
        project_io.importFunction(),
        project_io.saveFunction(),
        project_io.saveAsFunction(),
    };
    FakeEditorView view;
    controller.attachView(view);

    const core::AudioAsset audio_asset{std::filesystem::path{"imported.ogg"}};
    project_io.next_import_song = makeSong(audio_asset.path);
    controller.onImportRequested(std::filesystem::path{"song.psarc"});
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->save_requires_destination == true);

    controller.onSaveRequested();

    CHECK(project_io.save_call_count == 0);

    controller.onSaveAsRequested(std::filesystem::path{"saved.rhp"});

    CHECK(project_io.save_as_call_count == 1);
    CHECK(project_io.last_save_as_file == std::optional{std::filesystem::path{"saved.rhp"}});
    CHECK(project_io.last_save_as_audio_path == std::optional{audio_asset.path});
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->save_requires_destination == false);

    controller.onSaveRequested();

    CHECK(project_io.save_call_count == 1);
}

// Project packages do not carry editor selection state, so the controller opens index zero.
TEST_CASE("EditorController defaults open to first arrangement", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    FakeProjectIo project_load;
    EditorController controller{transport, edit_coordinator, project_load.openFunction()};

    const core::AudioAsset lead_asset{std::filesystem::path{"lead.wav"}};
    const core::AudioAsset bass_asset{std::filesystem::path{"bass.wav"}};
    project_load.next_song = makeTwoArrangementSong(lead_asset.path, bass_asset.path);

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    CHECK(edit.last_audio_asset == std::optional{lead_asset});
    REQUIRE(edit_coordinator.session().currentArrangement() != nullptr);
    CHECK(edit_coordinator.session().currentArrangement()->part == core::Part::Lead);
    CHECK(
        edit_coordinator.session().currentArrangement()->audio_asset == std::optional{lead_asset});
}

// Reentrant transport notifications during an in-flight edit are coalesced into one final push.
TEST_CASE("EditorController coalesces reentrant edit callbacks", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    FakeProjectIo project_load;
    EditorController controller{transport, edit_coordinator, project_load.openFunction()};
    FakeEditorView view;
    controller.attachView(view);
    const int pushes_before_load = view.set_state_call_count;

    edit.during_edit_action = [&] {
        transport.setStateAndNotify(
            audio::TransportState{
                .playing = true,
            });
    };

    const core::AudioAsset replacement{std::filesystem::path{"loop.wav"}};
    project_load.next_song = makeSong(replacement.path);
    controller.onOpenRequested(std::filesystem::path{"loop.rhp"});

    CHECK(view.set_state_call_count == pushes_before_load + 1);
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->arrangement.audio_asset == std::optional{replacement});
    CHECK(view.last_state->play_pause_shows_pause_icon == true);
}

// Later transport transitions preserve the existing workflow error until success clears it.
TEST_CASE("EditorController preserves workflow error across transitions", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    loadArrangement(edit_coordinator, edit, std::filesystem::path{"old.wav"});
    edit.next_audio_duration = std::nullopt;
    FakeProjectIo project_load;
    project_load.next_song = makeSong(std::filesystem::path{"new.wav"});
    EditorController controller{transport, edit_coordinator, project_load.openFunction()};
    FakeEditorView view;
    controller.attachView(view);
    controller.onOpenRequested(std::filesystem::path{"new.rhp"});

    REQUIRE(view.last_state.has_value());
    REQUIRE(view.last_state->last_error.has_value());
    const std::optional<std::string> original_error = view.last_state->last_error;

    transport.setStateAndNotify(
        audio::TransportState{
            .playing = true,
        });

    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->last_error == original_error);
}

} // namespace rock_hero::ui
