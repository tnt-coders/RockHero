#include <algorithm>
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
#include <rock_hero/core/track.h>
#include <rock_hero/ui/edit_coordinator.h>
#include <rock_hero/ui/editor_controller.h>
#include <rock_hero/ui/editor_view_state.h>
#include <rock_hero/ui/i_editor_controller.h>
#include <rock_hero/ui/i_editor_view.h>
#include <rock_hero/ui/track_view_state.h>
#include <rock_hero/ui/transport_controls_state.h>
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

// Configurable IEdit fake that records calls and can simulate reentrant transport notifications
// arriving while an edit is mid-flight.
class FakeEdit final : public audio::IEdit
{
public:
    // Records backend track provisioning requested by the edit coordinator.
    std::optional<core::TrackSpec> provisionTrack(
        core::TrackId track_id, const std::string& name) override
    {
        last_provisioned_track_id = track_id;
        last_provisioned_track_name = name;
        ++provision_track_call_count;
        if (!next_provision_track_result)
        {
            return std::nullopt;
        }

        return core::TrackSpec{
            .name = name,
        };
    }

    // Records the requested clip provision and optionally fires an injected reentrant action
    // before returning so reentrancy paths can be exercised deterministically.
    std::optional<core::AudioClipSpec> provisionAudioClip(
        core::TrackId track_id, core::AudioClipId audio_clip_id,
        const core::AudioAsset& audio_asset, core::TimePosition position) override
    {
        last_track_id = track_id;
        last_audio_clip_id = audio_clip_id;
        last_audio_asset = audio_asset;
        last_position = position;
        ++provision_audio_clip_call_count;
        if (during_edit_action)
        {
            during_edit_action();
        }
        if (!next_provision_audio_clip_result || !next_audio_asset_duration.has_value())
        {
            return std::nullopt;
        }

        const core::TimeDuration asset_duration =
            next_audio_asset_duration.value_or(core::TimeDuration{});
        return core::AudioClipSpec{
            .asset = audio_asset,
            .asset_duration = asset_duration,
            .source_range =
                core::TimeRange{
                    .start = core::TimePosition{},
                    .end = core::TimePosition{asset_duration.seconds},
                },
            .position = position,
        };
    }

    std::optional<core::TimeDuration> next_audio_asset_duration{core::TimeDuration{4.0}};
    bool next_provision_track_result{true};
    bool next_provision_audio_clip_result{true};
    int provision_track_call_count{0};
    int provision_audio_clip_call_count{0};
    std::optional<core::TrackId> last_provisioned_track_id{};
    std::optional<std::string> last_provisioned_track_name{};
    std::optional<core::TrackId> last_track_id{};
    std::optional<core::AudioClipId> last_audio_clip_id{};
    std::optional<core::AudioAsset> last_audio_asset{};
    std::optional<core::TimePosition> last_position{};
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

// Builds a clip value for optional comparisons without unchecked optional dereferences.
[[nodiscard]] core::AudioClip makeAudioClip(
    core::AudioClipId id, std::filesystem::path path, core::TimeRange timeline_range)
{
    return core::AudioClip{
        .id = id,
        .asset = core::AudioAsset{std::move(path)},
        .asset_duration = timeline_range.duration(),
        .source_range = timeline_range,
        .position = core::TimePosition{},
    };
}

// Builds expected clip-view state without reaching through nullable session fields in assertions.
[[nodiscard]] AudioClipViewState makeAudioClipViewState(
    core::AudioClipId id, std::filesystem::path path, core::TimeRange timeline_range)
{
    const core::TimeRange source_range{
        .start = core::TimePosition{},
        .end = core::TimePosition{timeline_range.duration().seconds},
    };

    return AudioClipViewState{
        .audio_clip_id = id,
        .asset = core::AudioAsset{std::move(path)},
        .source_range = source_range,
        .timeline_range = timeline_range,
    };
}

// Creates synthetic editor tracks through the same coordinator path used by production code.
core::TrackId addTestTrack(EditCoordinator& edit_coordinator, const std::string& name = {})
{
    const core::TrackId track_id = edit_coordinator.createTrack(name);
    REQUIRE(track_id.isValid());
    return track_id;
}

// Adds one loaded track through the coordinator so tests do not bypass backend/session coupling.
core::TrackId addLoadedTrack(
    EditCoordinator& edit_coordinator, FakeEdit& edit, const std::string& name,
    std::filesystem::path path, core::TimeRange timeline_range = loadedTimelineRange())
{
    const core::TrackId track_id = addTestTrack(edit_coordinator, name);
    edit.next_audio_asset_duration = timeline_range.duration();
    const auto audio_clip_id = edit_coordinator.createAudioClip(
        track_id, core::AudioAsset{std::move(path)}, timeline_range.start);
    REQUIRE(audio_clip_id.has_value());
    return track_id;
}

// Reads a track clip by value so tests do not chain through nullable lookup results.
[[nodiscard]] std::optional<core::AudioClip> findTrackAudioClip(
    const core::Session& session, core::TrackId track_id)
{
    const core::Track* const track = session.findTrack(track_id);
    if (track == nullptr)
    {
        return std::nullopt;
    }

    return track->audio_clip;
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

// Verifies the editor state can represent an empty editor and a single-track editor without JUCE.
TEST_CASE("EditorViewState represents empty and single-track editors", "[ui][editor-controller]")
{
    const EditorViewState empty_state{};

    CHECK(empty_state.load_button_enabled == false);
    CHECK(empty_state.play_pause_enabled == false);
    CHECK(empty_state.stop_enabled == false);
    CHECK(empty_state.play_pause_shows_pause_icon == false);
    CHECK(empty_state.visible_timeline == core::TimeRange{});
    CHECK(empty_state.tracks.empty());
    CHECK_FALSE(empty_state.last_load_error.has_value());

    const EditorViewState single_track_state{
        .load_button_enabled = true,
        .play_pause_enabled = true,
        .stop_enabled = true,
        .play_pause_shows_pause_icon = true,
        .visible_timeline = loadedTimelineRange(180.0),
        .tracks = {TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio_clips = {makeAudioClipViewState(
                core::AudioClipId{7},
                std::filesystem::path{"full_mix.wav"},
                loadedTimelineRange(180.0))},
        }},
        .last_load_error = std::string{"Could not load file"},
    };

    REQUIRE(single_track_state.tracks.size() == 1);
    CHECK(single_track_state.tracks.front().track_id == core::TrackId{1});
    CHECK(single_track_state.visible_timeline == loadedTimelineRange(180.0));
    CHECK(single_track_state.tracks.front().display_name == "Full Mix");
    REQUIRE(single_track_state.tracks.front().audio_clips.size() == 1);
    const AudioClipViewState& audio_clip = single_track_state.tracks.front().audio_clips.front();
    CHECK(audio_clip.audio_clip_id == core::AudioClipId{7});
    CHECK(audio_clip.asset.path == std::filesystem::path{"full_mix.wav"});
    CHECK(audio_clip.timeline_range == loadedTimelineRange(180.0));
    CHECK(single_track_state.last_load_error == std::optional<std::string>{"Could not load file"});
}

// Verifies row and editor state types support value comparisons for duplicate suppression.
TEST_CASE("Editor view-state types support value comparison", "[ui][editor-controller]")
{
    const TrackViewState track_state{
        .track_id = core::TrackId{7},
        .display_name = "Guitar",
        .audio_clips = {makeAudioClipViewState(
            core::AudioClipId{1}, std::filesystem::path{"guitar.wav"}, loadedTimelineRange())},
    };
    const TrackViewState same_track_state{
        .track_id = core::TrackId{7},
        .display_name = "Guitar",
        .audio_clips = {makeAudioClipViewState(
            core::AudioClipId{1}, std::filesystem::path{"guitar.wav"}, loadedTimelineRange())},
    };

    const EditorViewState first_state{
        .load_button_enabled = true,
        .play_pause_enabled = true,
        .stop_enabled = false,
        .play_pause_shows_pause_icon = false,
        .visible_timeline = loadedTimelineRange(),
        .tracks = {track_state},
        .last_load_error = std::nullopt,
    };

    const EditorViewState same_state{
        .load_button_enabled = true,
        .play_pause_enabled = true,
        .stop_enabled = false,
        .play_pause_shows_pause_icon = false,
        .visible_timeline = loadedTimelineRange(),
        .tracks = {same_track_state},
        .last_load_error = std::nullopt,
    };
    const EditorViewState different_state{
        .load_button_enabled = true,
        .play_pause_enabled = true,
        .stop_enabled = true,
        .play_pause_shows_pause_icon = false,
        .visible_timeline = loadedTimelineRange(),
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
    const AudioClipViewState backing_clip_state = makeAudioClipViewState(
        core::AudioClipId{1}, std::filesystem::path{"backing.wav"}, loadedTimelineRange());
    const EditorViewState state{
        .load_button_enabled = true,
        .play_pause_enabled = false,
        .stop_enabled = false,
        .play_pause_shows_pause_icon = false,
        .visible_timeline = core::TimeRange{},
        .tracks = {TrackViewState{
            .track_id = core::TrackId{3},
            .display_name = "Backing Track",
            .audio_clips = {backing_clip_state},
        }},
        .last_load_error = std::nullopt,
    };

    view.setState(state);

    CHECK(view.set_state_call_count == 1);
    CHECK(view.last_state == std::optional{state});
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
    CHECK(controller.last_track_id == std::optional{track_id});
    CHECK(controller.last_audio_asset == std::optional{audio_asset});
    CHECK(controller.play_pause_press_count == 1);
    CHECK(controller.stop_press_count == 1);
    CHECK(controller.waveform_click_count == 1);
    CHECK(controller.last_normalized_x == std::optional{0.75});
}

// Confirms attachView immediately delivers the controller's cached state so the view never
// renders against a stale or default snapshot before the first transition arrives.
TEST_CASE("EditorController pushes derived state on view attachment", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    addTestTrack(edit_coordinator, "Full Mix");
    EditorController controller{transport, edit_coordinator};
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
        CHECK(view.last_state->visible_timeline == core::TimeRange{});
        REQUIRE(view.last_state->tracks.size() == 1);
        CHECK(view.last_state->tracks.front().display_name == "Full Mix");
        CHECK_FALSE(view.last_state->last_load_error.has_value());
    }
}

// Verifies the editor workflow owns the default track policy instead of the app composition root.
TEST_CASE(
    "EditorController creates a default track for an empty session", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};

    const EditorController controller{transport, edit_coordinator};
    const core::Session& session = edit_coordinator.session();

    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].id == core::TrackId{1});
    CHECK(session.tracks()[0].name == "Full Mix");
    CHECK_FALSE(session.tracks()[0].audio_clip.has_value());
    CHECK(edit.provision_track_call_count == 1);
    CHECK(edit.last_provisioned_track_id == std::optional{core::TrackId{1}});
    CHECK(edit.last_provisioned_track_name == std::optional<std::string>{"Full Mix"});
    CHECK(edit.provision_audio_clip_call_count == 0);
}

// Verifies the controller does not poll transport state independently; only listener callbacks
// drive view pushes, so position-only changes never reach the view.
TEST_CASE("EditorController does not push without a transport callback", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    addLoadedTrack(edit_coordinator, edit, "Full Mix", std::filesystem::path{"a.wav"});
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;
    controller.attachView(view);

    transport.current_position = core::TimePosition{1.5};

    CHECK(view.set_state_call_count == 1);
}

// Verifies the controller pushes session timeline mapping without adding current cursor position.
TEST_CASE("EditorController derives visible timeline range", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    [[maybe_unused]] const core::TrackId track_id = addLoadedTrack(
        edit_coordinator,
        edit,
        "Full Mix",
        std::filesystem::path{"a.wav"},
        loadedTimelineRange(8.0));
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;
    controller.attachView(view);

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        CHECK(view.last_state->visible_timeline == loadedTimelineRange(8.0));
    }
}

// Each coarse transport transition produces exactly one fresh push so the view stays current
// without an extra duplicate-suppression layer in the controller.
TEST_CASE("EditorController pushes one state per coarse transition", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    addLoadedTrack(edit_coordinator, edit, "Full Mix", std::filesystem::path{"a.wav"});
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;
    controller.attachView(view);

    transport.setStateAndNotify(
        audio::TransportState{
            .playing = true,
        });

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == 2);
    if (view.last_state.has_value())
    {
        CHECK(view.last_state->play_pause_shows_pause_icon == true);
        CHECK(view.last_state->stop_enabled == true);
        CHECK(view.last_state->visible_timeline == loadedTimelineRange());
    }

    transport.setStateAndNotify(
        audio::TransportState{
            .playing = false,
        });

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == 3);
    if (view.last_state.has_value())
    {
        CHECK(view.last_state->play_pause_shows_pause_icon == false);
        CHECK(view.last_state->stop_enabled == false);
    }
}

// A paused cursor away from the loaded timeline start can still be reset, so Stop remains active
// even though the play/pause button has returned to its Play icon.
TEST_CASE(
    "EditorController keeps stop enabled when paused away from the timeline start",
    "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    [[maybe_unused]] const core::TrackId track_id =
        addLoadedTrack(edit_coordinator, edit, "Full Mix", std::filesystem::path{"a.wav"});
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;
    controller.attachView(view);

    transport.current_position = core::TimePosition{1.5};
    transport.setStateAndNotify(
        audio::TransportState{
            .playing = false,
        });

    REQUIRE(view.last_state.has_value());
    CHECK(view.set_state_call_count == 2);
    if (view.last_state.has_value())
    {
        CHECK(view.last_state->play_pause_shows_pause_icon == false);
        CHECK(view.last_state->stop_enabled == true);
    }
}

// Play intent issues play() when stopped and pause() when playing, mirroring the toggle the view
// would render through play_pause_shows_pause_icon.
TEST_CASE(
    "EditorController play intent toggles transport when a track has audio",
    "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    addLoadedTrack(edit_coordinator, edit, "Full Mix", std::filesystem::path{"a.wav"});
    EditorController controller{transport, edit_coordinator};

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
TEST_CASE("EditorController ignores play intent without audio", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    addTestTrack(edit_coordinator, "Full Mix");
    EditorController controller{transport, edit_coordinator};

    controller.onPlayPausePressed();

    CHECK(transport.play_call_count == 0);
    CHECK(transport.pause_call_count == 0);
}

// The stop intent must respect the same gate the view publishes, so it is ignored only after the
// transport is paused or stopped at the timeline start.
TEST_CASE(
    "EditorController stop intent fires while playing or away from the timeline start",
    "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    [[maybe_unused]] const core::TrackId track_id =
        addLoadedTrack(edit_coordinator, edit, "Full Mix", std::filesystem::path{"a.wav"});
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

// Stopping from a paused non-start cursor does not necessarily produce a coarse transport state
// callback, so the controller refreshes the view directly after issuing stop().
TEST_CASE("EditorController stop intent refreshes paused reset state", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    [[maybe_unused]] const core::TrackId track_id =
        addLoadedTrack(edit_coordinator, edit, "Full Mix", std::filesystem::path{"a.wav"});
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

// Waveform clicks clamp out-of-range input and convert normalized positions through the current
// session timeline so the seek target stays inside loaded content.
TEST_CASE(
    "EditorController waveform click clamps and scales by timeline", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    [[maybe_unused]] const core::TrackId track_id = addLoadedTrack(
        edit_coordinator,
        edit,
        "Full Mix",
        std::filesystem::path{"a.wav"},
        loadedTimelineRange(4.0));
    EditorController controller{transport, edit_coordinator};

    controller.onWaveformClicked(0.5);
    CHECK(transport.last_seek_position == std::optional{core::TimePosition{2.0}});

    controller.onWaveformClicked(-0.25);
    CHECK(transport.last_seek_position == std::optional{core::TimePosition{0.0}});

    controller.onWaveformClicked(1.5);
    CHECK(transport.last_seek_position == std::optional{core::TimePosition{4.0}});
}

// A seek issued by the controller changes whether Stop can reset the cursor, so the controller
// refreshes the discrete view state immediately after the seek intent.
TEST_CASE("EditorController waveform click refreshes stop enabledness", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    [[maybe_unused]] const core::TrackId track_id = addLoadedTrack(
        edit_coordinator,
        edit,
        "Full Mix",
        std::filesystem::path{"a.wav"},
        loadedTimelineRange(4.0));
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

// Invalid track ids must not reach the audio backend; otherwise the edit could mutate playback
// for content the session has no record of.
TEST_CASE("EditorController ignores loads for unknown tracks", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    const core::TrackId valid_id = addTestTrack(edit_coordinator, "Full Mix");
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;
    controller.attachView(view);

    const core::TrackId unknown_id{valid_id.value + 999};
    controller.onLoadAudioAssetRequested(
        unknown_id, core::AudioAsset{std::filesystem::path{"b.wav"}});

    CHECK(edit.provision_audio_clip_call_count == 0);
    CHECK(view.set_state_call_count == 1);
}

// A failed audio load must leave the session unchanged and surface a controller-composed error
// message that includes the rejected file path.
TEST_CASE(
    "EditorController failed load preserves session and reports error", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    const core::TimeRange original_range = loadedTimelineRange(6.0);
    const core::TrackId track_id = addLoadedTrack(
        edit_coordinator, edit, "Full Mix", std::filesystem::path{"old.wav"}, original_range);
    edit.next_provision_audio_clip_result = false;
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;
    controller.attachView(view);
    const core::Session& session = edit_coordinator.session();

    const core::AudioAsset replacement{std::filesystem::path{"new.wav"}};
    controller.onLoadAudioAssetRequested(track_id, replacement);

    CHECK(
        findTrackAudioClip(session, track_id) ==
        std::optional{makeAudioClip(
            core::AudioClipId{1}, std::filesystem::path{"old.wav"}, original_range)});
    CHECK(session.timeline() == original_range);
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

// A successful audio load stores the asset and timeline, clears any prior error, and emits one
// post-load push.
TEST_CASE("EditorController successful load stores asset and timeline", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    const core::TrackId track_id = addTestTrack(edit_coordinator, "Full Mix");
    EditorController controller{transport, edit_coordinator};
    FakeEditorView view;
    controller.attachView(view);
    const core::Session& session = edit_coordinator.session();

    // Force an initial error so the success path has something to clear.
    edit.next_audio_asset_duration = std::nullopt;
    controller.onLoadAudioAssetRequested(
        track_id, core::AudioAsset{std::filesystem::path{"first.wav"}});
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        REQUIRE(view.last_state->last_load_error.has_value());
    }
    const int pushes_before_success = view.set_state_call_count;

    edit.next_audio_asset_duration = core::TimeDuration{4.0};
    const core::AudioAsset replacement{std::filesystem::path{"second.wav"}};
    controller.onLoadAudioAssetRequested(track_id, replacement);

    CHECK(edit.provision_audio_clip_call_count == 2);
    CHECK(edit.last_track_id == std::optional{track_id});
    CHECK(edit.last_audio_clip_id == std::optional{core::AudioClipId{2}});
    CHECK(edit.last_audio_asset == std::optional{replacement});
    CHECK(edit.last_position == std::optional{core::TimePosition{}});
    CHECK(
        findTrackAudioClip(session, track_id) ==
        std::optional{makeAudioClip(
            core::AudioClipId{2}, replacement.path, loadedTimelineRange(4.0))});
    CHECK(session.timeline() == loadedTimelineRange(4.0));
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        CHECK_FALSE(view.last_state->last_load_error.has_value());
    }
    CHECK(view.set_state_call_count == pushes_before_success + 1);
}

// Reentrant transport notifications during an in-flight edit must be coalesced into a single
// final push so the view never observes a view state derived from stale session data.
TEST_CASE(
    "EditorController coalesces reentrant edit callbacks into one push", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    const core::TrackId track_id = addTestTrack(edit_coordinator, "Full Mix");
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
    controller.onLoadAudioAssetRequested(track_id, replacement);

    CHECK(view.set_state_call_count == pushes_before_load + 1);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        REQUIRE(view.last_state->tracks.size() == 1);
        REQUIRE(view.last_state->tracks.front().audio_clips.size() == 1);
        CHECK(view.last_state->tracks.front().audio_clips.front().asset == replacement);
        CHECK(view.last_state->play_pause_shows_pause_icon == true);
    }
}

// Coarse transport transitions that arrive after a failed load must preserve the existing error;
// nothing in the controller's listener path should silently clear load-error state.
TEST_CASE(
    "EditorController preserves load error across later transitions", "[ui][editor-controller]")
{
    FakeTransport transport;
    FakeEdit edit;
    EditCoordinator edit_coordinator{edit};
    const core::TrackId track_id =
        addLoadedTrack(edit_coordinator, edit, "Full Mix", std::filesystem::path{"old.wav"});
    edit.next_provision_audio_clip_result = false;
    EditorController controller{transport, edit_coordinator};
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
        });

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        CHECK(view.last_state->last_load_error == original_error);
    }
}

} // namespace rock_hero::ui
