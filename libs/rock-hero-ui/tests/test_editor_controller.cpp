#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <functional>
#include <optional>
#include <rock_hero/audio/i_edit.h>
#include <rock_hero/audio/i_transport.h>
#include <rock_hero/audio/transport_state.h>
#include <rock_hero/core/audio_asset.h>
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
    // Captures the most recent audio-load request made through the controller contract.
    void onLoadAudioAssetRequested(core::AudioAsset audio_asset) override
    {
        last_audio_asset = std::move(audio_asset);
        load_request_count += 1;
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

    std::optional<core::AudioAsset> last_audio_asset{};
    std::optional<double> last_normalized_x{};
    int load_request_count{0};
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

// Provides a standard loaded-content range for controller tests.
[[nodiscard]] core::TimeRange loadedTimelineRange(double end_seconds = 4.0) noexcept
{
    return core::TimeRange{
        .start = core::TimePosition{},
        .end = core::TimePosition{end_seconds},
    };
}

// Loads arrangement audio through the coordinator so tests keep backend/session coupling.
void loadArrangement(
    EditCoordinator& edit_coordinator, FakeEdit& edit, std::filesystem::path path,
    core::TimeRange timeline_range = loadedTimelineRange())
{
    edit.next_audio_duration = timeline_range.duration();
    const bool audio_set = edit_coordinator.setArrangementAudio(core::AudioAsset{std::move(path)});
    REQUIRE(audio_set);
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

    CHECK(empty_state.load_button_enabled == false);
    CHECK(empty_state.play_pause_enabled == false);
    CHECK(empty_state.stop_enabled == false);
    CHECK(empty_state.play_pause_shows_pause_icon == false);
    CHECK(empty_state.visible_timeline == core::TimeRange{});
    CHECK_FALSE(empty_state.arrangement.hasAudio());
    CHECK_FALSE(empty_state.last_load_error.has_value());

    const core::AudioAsset audio_asset{std::filesystem::path{"full_mix.wav"}};
    const EditorViewState loaded_state{
        .load_button_enabled = true,
        .play_pause_enabled = true,
        .stop_enabled = true,
        .play_pause_shows_pause_icon = true,
        .visible_timeline = loadedTimelineRange(180.0),
        .arrangement =
            ArrangementViewState{
                .audio_asset = audio_asset,
                .audio_duration = core::TimeDuration{180.0},
            },
        .last_load_error = std::string{"Could not load file"},
    };

    CHECK(loaded_state.arrangement.audio_asset == std::optional{audio_asset});
    CHECK(loaded_state.arrangement.audioTimelineRange() == loadedTimelineRange(180.0));
    CHECK(loaded_state.arrangement.hasAudio());
    CHECK(loaded_state.last_load_error == std::optional<std::string>{"Could not load file"});
}

// Verifies a fake controller can receive editor intents without JUCE callback types.
TEST_CASE("IEditorController fake receives editor intents", "[ui][editor-controller]")
{
    FakeEditorController controller;
    const core::AudioAsset audio_asset{std::filesystem::path{"lead.wav"}};

    controller.onLoadAudioAssetRequested(audio_asset);
    controller.onPlayPausePressed();
    controller.onStopPressed();
    controller.onWaveformClicked(0.75);

    CHECK(controller.load_request_count == 1);
    CHECK(controller.last_audio_asset == std::optional{audio_asset});
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
    CHECK(view.last_state->load_button_enabled == true);
    CHECK(view.last_state->play_pause_enabled == false);
    CHECK(view.last_state->stop_enabled == false);
    CHECK(view.last_state->play_pause_shows_pause_icon == false);
    CHECK(view.last_state->visible_timeline == core::TimeRange{});
    CHECK_FALSE(view.last_state->arrangement.hasAudio());
    CHECK_FALSE(view.last_state->last_load_error.has_value());
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

// A failed audio load leaves the session unchanged and surfaces a controller-composed error.
TEST_CASE("EditorController failed load preserves session", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    loadArrangement(
        edit_coordinator, edit, std::filesystem::path{"old.wav"}, loadedTimelineRange(6.0));
    edit.next_audio_duration = std::nullopt;
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;
    controller.attachView(view);

    const core::AudioAsset replacement{std::filesystem::path{"new.wav"}};
    controller.onLoadAudioAssetRequested(replacement);

    const core::Session& session = edit_coordinator.session();
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(
        session.currentArrangement()->audio_asset ==
        std::optional{core::AudioAsset{std::filesystem::path{"old.wav"}}});
    CHECK(session.timeline() == loadedTimelineRange(6.0));
    REQUIRE(view.last_state.has_value());
    REQUIRE(view.last_state->last_load_error.has_value());
    CHECK(view.last_state->last_load_error->find("new.wav") != std::string::npos);
}

// A successful audio load stores the asset and timeline, clears prior error, and pushes once.
TEST_CASE("EditorController successful load stores audio", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;
    controller.attachView(view);

    edit.next_audio_duration = std::nullopt;
    controller.onLoadAudioAssetRequested(core::AudioAsset{std::filesystem::path{"first.wav"}});
    REQUIRE(view.last_state.has_value());
    REQUIRE(view.last_state->last_load_error.has_value());
    const int pushes_before_success = view.set_state_call_count;

    edit.next_audio_duration = core::TimeDuration{4.0};
    const core::AudioAsset replacement{std::filesystem::path{"second.wav"}};
    controller.onLoadAudioAssetRequested(replacement);

    const core::Session& session = edit_coordinator.session();
    CHECK(edit.load_audio_call_count == 2);
    CHECK(edit.last_audio_asset == std::optional{replacement});
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(session.currentArrangement()->audio_asset == std::optional{replacement});
    CHECK(session.currentArrangement()->audio_duration == core::TimeDuration{4.0});
    CHECK(session.timeline() == loadedTimelineRange(4.0));
    REQUIRE(view.last_state.has_value());
    CHECK_FALSE(view.last_state->last_load_error.has_value());
    CHECK(view.last_state->arrangement.audio_asset == std::optional{replacement});
    CHECK(view.set_state_call_count == pushes_before_success + 1);
}

// Reentrant transport notifications during an in-flight edit are coalesced into one final push.
TEST_CASE("EditorController coalesces reentrant edit callbacks", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    EditorController controller{transport, edit_coordinator};
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
    controller.onLoadAudioAssetRequested(replacement);

    CHECK(view.set_state_call_count == pushes_before_load + 1);
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->arrangement.audio_asset == std::optional{replacement});
    CHECK(view.last_state->play_pause_shows_pause_icon == true);
}

// Later transport transitions preserve the existing load error until a successful load clears it.
TEST_CASE("EditorController preserves load error across transitions", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    loadArrangement(edit_coordinator, edit, std::filesystem::path{"old.wav"});
    edit.next_audio_duration = std::nullopt;
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;
    controller.attachView(view);
    controller.onLoadAudioAssetRequested(core::AudioAsset{std::filesystem::path{"new.wav"}});

    REQUIRE(view.last_state.has_value());
    REQUIRE(view.last_state->last_load_error.has_value());
    const std::optional<std::string> original_error = view.last_state->last_load_error;

    transport.setStateAndNotify(
        audio::TransportState{
            .playing = true,
        });

    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->last_load_error == original_error);
}

} // namespace rock_hero::ui
