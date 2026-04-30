#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <optional>
#include <rock_hero/audio/i_edit.h>
#include <rock_hero/audio/i_transport.h>
#include <rock_hero/audio/transport_state.h>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/session.h>
#include <rock_hero/core/timeline.h>
#include <rock_hero/core/track.h>
#include <rock_hero/ui/editor_controller.h>
#include <rock_hero/ui/editor_view_state.h>
#include <rock_hero/ui/i_editor_controller.h>
#include <rock_hero/ui/i_editor_view.h>
#include <rock_hero/ui/track_view_state.h>
#include <rock_hero/ui/transport_controls_state.h>
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

    // Most recent state delivered through the view contract, if any.
    std::optional<EditorViewState> last_state{};

    // Number of times the view contract has been invoked.
    int set_state_call_count{0};
};

// Records incoming editor intents so tests can verify the controller contract headlessly.
class FakeEditorController final : public IEditorController
{
public:
    // Captures the most recent audio-load request made through the controller contract.
    void onLoadAudioAssetRequested(core::TrackId track_id, core::AudioAsset audio_asset) override
    {
        last_track_id = track_id;
        last_audio_asset = std::move(audio_asset);
        load_request_count += 1;
    }

    // Captures play/pause requests from the view layer.
    void onPlayPausePressed() override
    {
        play_pause_press_count += 1;
    }

    // Captures stop requests from the view layer.
    void onStopPressed() override
    {
        stop_press_count += 1;
    }

    // Captures waveform seek requests as normalized horizontal positions.
    void onWaveformClicked(double normalized_x) override
    {
        last_normalized_x = normalized_x;
        waveform_click_count += 1;
    }

    // Track id from the most recent load request, if any.
    std::optional<core::TrackId> last_track_id{};

    // Audio asset from the most recent load request, if any.
    std::optional<core::AudioAsset> last_audio_asset{};

    // Normalized waveform position from the most recent click request, if any.
    std::optional<double> last_normalized_x{};

    // Number of load requests received.
    int load_request_count{0};

    // Number of play/pause requests received.
    int play_pause_press_count{0};

    // Number of stop requests received.
    int stop_press_count{0};

    // Number of waveform click requests received.
    int waveform_click_count{0};
};

// Records control intents and exposes a manual notification hook so controller tests can drive
// coarse transition-shaped callbacks deterministically.
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
        ++stop_call_count;
    }

    // Records the requested seek so tests can verify clamping and duration scaling.
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

// Configurable IEdit fake that records calls and can simulate reentrant transport notifications
// arriving while an edit is mid-flight.
class FakeEdit final : public audio::IEdit
{
public:
    // Returns the configured next_result and optionally fires an injected reentrant action
    // before returning so reentrancy paths can be exercised deterministically.
    bool setTrackAudioSource(core::TrackId track_id, const core::AudioAsset& audio_asset) override
    {
        last_track_id = track_id;
        last_audio_asset = audio_asset;
        ++set_track_audio_source_call_count;
        if (during_edit_action)
        {
            during_edit_action();
        }
        return next_result;
    }

    bool next_result{true};
    int set_track_audio_source_call_count{0};
    std::optional<core::TrackId> last_track_id{};
    std::optional<core::AudioAsset> last_audio_asset{};
    std::function<void()> during_edit_action{};
};

} // namespace

// Verifies the editor state can represent an empty editor and a single-track editor without JUCE.
TEST_CASE("EditorViewState represents empty and single-track editors", "[ui][editor-controller]")
{
    const EditorViewState empty_state{};

    CHECK(empty_state.load_button_enabled == false);
    CHECK(empty_state.play_pause_enabled == false);
    CHECK(empty_state.stop_enabled == false);
    CHECK(empty_state.play_pause_shows_pause_icon == false);
    CHECK(empty_state.visible_timeline_start == core::TimePosition{});
    CHECK(empty_state.visible_timeline_duration == core::TimeDuration{});
    CHECK(empty_state.tracks.empty());
    CHECK_FALSE(empty_state.last_load_error.has_value());

    const EditorViewState single_track_state{
        .load_button_enabled = true,
        .play_pause_enabled = true,
        .stop_enabled = true,
        .play_pause_shows_pause_icon = true,
        .visible_timeline_start = core::TimePosition{},
        .visible_timeline_duration = core::TimeDuration{180.0},
        .tracks = {TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio_asset = core::AudioAsset{std::filesystem::path{"full_mix.wav"}},
        }},
        .last_load_error = std::string{"Could not load file"},
    };

    REQUIRE(single_track_state.tracks.size() == 1);
    CHECK(single_track_state.tracks.front().track_id == core::TrackId{1});
    CHECK(single_track_state.visible_timeline_start == core::TimePosition{});
    CHECK(single_track_state.visible_timeline_duration == core::TimeDuration{180.0});
    CHECK(single_track_state.tracks.front().display_name == "Full Mix");
    const auto& audio_asset = single_track_state.tracks.front().audio_asset;
    std::optional<std::filesystem::path> loaded_audio_path{};
    if (audio_asset.has_value())
    {
        loaded_audio_path = audio_asset->path;
    }
    REQUIRE(loaded_audio_path.has_value());
    CHECK(
        loaded_audio_path ==
        std::optional<std::filesystem::path>{std::filesystem::path{"full_mix.wav"}});
    CHECK(single_track_state.last_load_error == std::optional<std::string>{"Could not load file"});
}

// Verifies row and editor state types support value comparisons for duplicate suppression.
TEST_CASE("Editor view-state types support value comparison", "[ui][editor-controller]")
{
    const TrackViewState track_state{
        .track_id = core::TrackId{7},
        .display_name = "Guitar",
        .audio_asset = core::AudioAsset{std::filesystem::path{"guitar.wav"}},
    };
    const TrackViewState same_track_state{
        .track_id = core::TrackId{7},
        .display_name = "Guitar",
        .audio_asset = core::AudioAsset{std::filesystem::path{"guitar.wav"}},
    };

    const EditorViewState first_state{
        .load_button_enabled = true,
        .play_pause_enabled = true,
        .stop_enabled = false,
        .play_pause_shows_pause_icon = false,
        .visible_timeline_start = core::TimePosition{},
        .visible_timeline_duration = core::TimeDuration{4.0},
        .tracks = {track_state},
        .last_load_error = std::nullopt,
    };

    const EditorViewState same_state{
        .load_button_enabled = true,
        .play_pause_enabled = true,
        .stop_enabled = false,
        .play_pause_shows_pause_icon = false,
        .visible_timeline_start = core::TimePosition{},
        .visible_timeline_duration = core::TimeDuration{4.0},
        .tracks = {same_track_state},
        .last_load_error = std::nullopt,
    };
    const EditorViewState different_state{
        .load_button_enabled = true,
        .play_pause_enabled = true,
        .stop_enabled = true,
        .play_pause_shows_pause_icon = false,
        .visible_timeline_start = core::TimePosition{},
        .visible_timeline_duration = core::TimeDuration{4.0},
        .tracks = {track_state},
        .last_load_error = std::nullopt,
    };

    CHECK(track_state == same_track_state);
    CHECK(first_state == same_state);
    CHECK_FALSE(first_state == different_state);
}

// Verifies the transport-controls state stays a small framework-free value type.
TEST_CASE("TransportControlsState stores simple transport UI flags", "[ui][editor-controller]")
{
    const TransportControlsState default_state{};
    const TransportControlsState pause_state{
        .play_pause_enabled = true,
        .stop_enabled = true,
        .play_pause_shows_pause_icon = true,
    };
    const TransportControlsState same_pause_state{
        .play_pause_enabled = true,
        .stop_enabled = true,
        .play_pause_shows_pause_icon = true,
    };

    CHECK(default_state.play_pause_enabled == false);
    CHECK(default_state.stop_enabled == false);
    CHECK(default_state.play_pause_shows_pause_icon == false);
    CHECK(pause_state == same_pause_state);
}

// Verifies a fake view can receive framework-free editor state without JUCE initialization.
TEST_CASE("IEditorView fake receives editor state", "[ui][editor-controller]")
{
    FakeEditorView view;
    const EditorViewState state{
        .load_button_enabled = true,
        .play_pause_enabled = false,
        .stop_enabled = false,
        .play_pause_shows_pause_icon = false,
        .visible_timeline_start = core::TimePosition{},
        .visible_timeline_duration = core::TimeDuration{},
        .tracks = {TrackViewState{
            .track_id = core::TrackId{3},
            .display_name = "Backing Track",
            .audio_asset = core::AudioAsset{std::filesystem::path{"backing.wav"}},
        }},
        .last_load_error = std::nullopt,
    };

    view.setState(state);

    CHECK(view.set_state_call_count == 1);
    CHECK(view.last_state == std::optional<EditorViewState>{state});
}

// Verifies a fake controller can receive the current editor intents without JUCE callback types.
TEST_CASE("IEditorController fake receives editor intents", "[ui][editor-controller]")
{
    FakeEditorController controller;
    const core::TrackId track_id{9};
    const core::AudioAsset audio_asset{std::filesystem::path{"lead.wav"}};

    controller.onLoadAudioAssetRequested(track_id, audio_asset);
    controller.onPlayPausePressed();
    controller.onStopPressed();
    controller.onWaveformClicked(0.75);

    CHECK(controller.load_request_count == 1);
    CHECK(controller.last_track_id == std::optional<core::TrackId>{track_id});
    CHECK(controller.last_audio_asset == std::optional<core::AudioAsset>{audio_asset});
    CHECK(controller.play_pause_press_count == 1);
    CHECK(controller.stop_press_count == 1);
    CHECK(controller.waveform_click_count == 1);
    CHECK(controller.last_normalized_x == std::optional<double>{0.75});
}

// Confirms attachView immediately delivers the controller's cached state so the view never
// renders against a stale or default snapshot before the first transition arrives.
TEST_CASE("EditorController pushes derived state on view attachment", "[ui][editor-controller]")
{
    core::Session session;
    session.addTrack("Full Mix", std::nullopt);
    FakeTransport transport;
    FakeEdit edit;
    EditorController controller{session, transport, edit};
    FakeEditorView view;

    controller.attachView(view);

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == 1);
    if (view.last_state.has_value())
    {
        CHECK(view.last_state->load_button_enabled == true);
        CHECK(view.last_state->play_pause_enabled == false);
        CHECK(view.last_state->stop_enabled == false);
        CHECK(view.last_state->play_pause_shows_pause_icon == false);
        CHECK(view.last_state->visible_timeline_start == core::TimePosition{});
        CHECK(view.last_state->visible_timeline_duration == core::TimeDuration{});
        REQUIRE(view.last_state->tracks.size() == 1);
        CHECK(view.last_state->tracks.front().display_name == "Full Mix");
        CHECK_FALSE(view.last_state->last_load_error.has_value());
    }
}

// Verifies the controller does not poll transport state independently; only listener callbacks
// drive view pushes, so position-only changes never reach the view.
TEST_CASE("EditorController does not push without a transport callback", "[ui][editor-controller]")
{
    core::Session session;
    session.addTrack("Full Mix", core::AudioAsset{std::filesystem::path{"a.wav"}});
    FakeTransport transport;
    FakeEdit edit;
    EditorController controller{session, transport, edit};
    FakeEditorView view;
    controller.attachView(view);

    transport.current_position = core::TimePosition{1.5};

    CHECK(view.set_state_call_count == 1);
}

// Verifies the controller pushes timeline mapping data without adding current cursor position.
TEST_CASE(
    "EditorController derives visible range from transport duration", "[ui][editor-controller]")
{
    core::Session session;
    session.addTrack("Full Mix", core::AudioAsset{std::filesystem::path{"a.wav"}});
    FakeTransport transport;
    transport.current_state.duration = core::TimeDuration{8.0};
    FakeEdit edit;
    EditorController controller{session, transport, edit};
    FakeEditorView view;
    controller.attachView(view);

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        CHECK(view.last_state->visible_timeline_start == core::TimePosition{});
        CHECK(view.last_state->visible_timeline_duration == core::TimeDuration{8.0});
    }
}

// Each coarse transport transition produces exactly one fresh push so the view stays current
// without an extra duplicate-suppression layer in the controller.
TEST_CASE("EditorController pushes one state per coarse transition", "[ui][editor-controller]")
{
    core::Session session;
    session.addTrack("Full Mix", core::AudioAsset{std::filesystem::path{"a.wav"}});
    FakeTransport transport;
    FakeEdit edit;
    EditorController controller{session, transport, edit};
    FakeEditorView view;
    controller.attachView(view);

    transport.setStateAndNotify(
        audio::TransportState{
            .playing = true,
            .duration = core::TimeDuration{4.0},
        });

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == 2);
    if (view.last_state.has_value())
    {
        CHECK(view.last_state->play_pause_shows_pause_icon == true);
        CHECK(view.last_state->stop_enabled == true);
        CHECK(view.last_state->visible_timeline_duration == core::TimeDuration{4.0});
    }

    transport.setStateAndNotify(
        audio::TransportState{
            .playing = false,
            .duration = core::TimeDuration{4.0},
        });

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == 3);
    if (view.last_state.has_value())
    {
        CHECK(view.last_state->play_pause_shows_pause_icon == false);
        CHECK(view.last_state->stop_enabled == false);
    }
}

// Play intent issues play() when stopped and pause() when playing, mirroring the toggle the view
// would render through play_pause_shows_pause_icon.
TEST_CASE(
    "EditorController play intent toggles transport when a track has audio",
    "[ui][editor-controller]")
{
    core::Session session;
    session.addTrack("Full Mix", core::AudioAsset{std::filesystem::path{"a.wav"}});
    FakeTransport transport;
    FakeEdit edit;
    EditorController controller{session, transport, edit};

    controller.onPlayPausePressed();
    CHECK(transport.play_call_count == 1);
    CHECK(transport.pause_call_count == 0);

    transport.current_state.playing = true;
    controller.onPlayPausePressed();
    CHECK(transport.play_call_count == 1);
    CHECK(transport.pause_call_count == 1);
}

// Without a track that owns an audio asset there is nothing to play, so the intent must be a
// no-op rather than start a silent transport.
TEST_CASE(
    "EditorController play intent is ignored when no track has an asset", "[ui][editor-controller]")
{
    core::Session session;
    session.addTrack("Full Mix", std::nullopt);
    FakeTransport transport;
    FakeEdit edit;
    EditorController controller{session, transport, edit};

    controller.onPlayPausePressed();

    CHECK(transport.play_call_count == 0);
    CHECK(transport.pause_call_count == 0);
}

// The stop intent must respect the same gate the view publishes, so a stopped transport stays
// stopped even when an alternate input path tries to fire a stop.
TEST_CASE(
    "EditorController stop intent fires only while transport is playing", "[ui][editor-controller]")
{
    core::Session session;
    session.addTrack("Full Mix", core::AudioAsset{std::filesystem::path{"a.wav"}});
    FakeTransport transport;
    FakeEdit edit;
    EditorController controller{session, transport, edit};

    controller.onStopPressed();
    CHECK(transport.stop_call_count == 0);

    transport.current_state.playing = true;
    controller.onStopPressed();
    CHECK(transport.stop_call_count == 1);
}

// Waveform clicks clamp out-of-range input and convert normalized positions through the current
// transport duration so the seek target stays inside loaded content.
TEST_CASE(
    "EditorController waveform click clamps and scales by duration", "[ui][editor-controller]")
{
    core::Session session;
    FakeTransport transport;
    transport.current_state.duration = core::TimeDuration{4.0};
    FakeEdit edit;
    EditorController controller{session, transport, edit};

    controller.onWaveformClicked(0.5);
    CHECK(
        transport.last_seek_position == std::optional<core::TimePosition>{core::TimePosition{2.0}});

    controller.onWaveformClicked(-0.25);
    CHECK(
        transport.last_seek_position == std::optional<core::TimePosition>{core::TimePosition{0.0}});

    controller.onWaveformClicked(1.5);
    CHECK(
        transport.last_seek_position == std::optional<core::TimePosition>{core::TimePosition{4.0}});
}

// Invalid track ids must not reach the audio backend; otherwise the edit could mutate playback
// for content the session has no record of.
TEST_CASE("EditorController ignores load requests for unknown track ids", "[ui][editor-controller]")
{
    core::Session session;
    const core::TrackId valid_id = session.addTrack("Full Mix", std::nullopt);
    FakeTransport transport;
    FakeEdit edit;
    EditorController controller{session, transport, edit};
    FakeEditorView view;
    controller.attachView(view);

    const core::TrackId unknown_id{valid_id.value + 999};
    controller.onLoadAudioAssetRequested(
        unknown_id, core::AudioAsset{std::filesystem::path{"b.wav"}});

    CHECK(edit.set_track_audio_source_call_count == 0);
    CHECK(view.set_state_call_count == 1);
}

// A failed IEdit call must leave the session unchanged and surface a controller-composed error
// message that includes the rejected file path.
TEST_CASE(
    "EditorController failed load preserves session and reports error", "[ui][editor-controller]")
{
    core::Session session;
    const core::TrackId track_id =
        session.addTrack("Full Mix", core::AudioAsset{std::filesystem::path{"old.wav"}});
    FakeTransport transport;
    FakeEdit edit;
    edit.next_result = false;
    EditorController controller{session, transport, edit};
    FakeEditorView view;
    controller.attachView(view);

    const core::AudioAsset replacement{std::filesystem::path{"new.wav"}};
    controller.onLoadAudioAssetRequested(track_id, replacement);

    REQUIRE(session.findTrack(track_id) != nullptr);
    CHECK(
        session.findTrack(track_id)->audio_asset ==
        std::optional<core::AudioAsset>{core::AudioAsset{std::filesystem::path{"old.wav"}}});
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        REQUIRE(view.last_state->last_load_error.has_value());
        if (view.last_state->last_load_error.has_value())
        {
            CHECK(view.last_state->last_load_error->find("new.wav") != std::string::npos);
        }
    }
}

// A successful IEdit call commits the asset to the session, clears any prior error, and emits
// a single post-load push.
TEST_CASE(
    "EditorController successful load commits asset and clears error", "[ui][editor-controller]")
{
    core::Session session;
    const core::TrackId track_id = session.addTrack("Full Mix", std::nullopt);
    FakeTransport transport;
    FakeEdit edit;
    EditorController controller{session, transport, edit};
    FakeEditorView view;
    controller.attachView(view);

    // Force an initial error so the success path has something to clear.
    edit.next_result = false;
    controller.onLoadAudioAssetRequested(
        track_id, core::AudioAsset{std::filesystem::path{"first.wav"}});
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        REQUIRE(view.last_state->last_load_error.has_value());
    }
    const int pushes_before_success = view.set_state_call_count;

    edit.next_result = true;
    const core::AudioAsset replacement{std::filesystem::path{"second.wav"}};
    controller.onLoadAudioAssetRequested(track_id, replacement);

    REQUIRE(session.findTrack(track_id) != nullptr);
    CHECK(session.findTrack(track_id)->audio_asset == std::optional<core::AudioAsset>{replacement});
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        CHECK_FALSE(view.last_state->last_load_error.has_value());
    }
    CHECK(view.set_state_call_count == pushes_before_success + 1);
}

// Reentrant transport notifications during an in-flight edit must be coalesced into a single
// final push so the view never observes a view state derived from pre-commit session data.
TEST_CASE(
    "EditorController coalesces reentrant edit callbacks into one push", "[ui][editor-controller]")
{
    core::Session session;
    const core::TrackId track_id = session.addTrack("Full Mix", std::nullopt);
    FakeTransport transport;
    FakeEdit edit;
    EditorController controller{session, transport, edit};
    FakeEditorView view;
    controller.attachView(view);
    const int pushes_before_load = view.set_state_call_count;

    edit.during_edit_action = [&] {
        transport.setStateAndNotify(
            audio::TransportState{
                .playing = true,
                .duration = core::TimeDuration{4.0},
            });
    };

    const core::AudioAsset replacement{std::filesystem::path{"loop.wav"}};
    controller.onLoadAudioAssetRequested(track_id, replacement);

    CHECK(view.set_state_call_count == pushes_before_load + 1);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        REQUIRE(view.last_state->tracks.size() == 1);
        CHECK(
            view.last_state->tracks.front().audio_asset ==
            std::optional<core::AudioAsset>{replacement});
        CHECK(view.last_state->play_pause_shows_pause_icon == true);
    }
}

// Coarse transport transitions that arrive after a failed load must preserve the existing error;
// nothing in the controller's listener path should silently clear load-error state.
TEST_CASE(
    "EditorController preserves load error across later transitions", "[ui][editor-controller]")
{
    core::Session session;
    const core::TrackId track_id =
        session.addTrack("Full Mix", core::AudioAsset{std::filesystem::path{"old.wav"}});
    FakeTransport transport;
    FakeEdit edit;
    edit.next_result = false;
    EditorController controller{session, transport, edit};
    FakeEditorView view;
    controller.attachView(view);
    controller.onLoadAudioAssetRequested(
        track_id, core::AudioAsset{std::filesystem::path{"new.wav"}});

    REQUIRE(view.last_state.has_value());
    std::optional<std::string> original_error{};
    if (view.last_state.has_value())
    {
        REQUIRE(view.last_state->last_load_error.has_value());
        original_error = view.last_state->last_load_error;
    }

    transport.setStateAndNotify(
        audio::TransportState{
            .playing = true,
            .duration = core::TimeDuration{4.0},
        });

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        CHECK(view.last_state->last_load_error == original_error);
    }
}

} // namespace rock_hero::ui
