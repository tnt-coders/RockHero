#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rock_hero/ui/editor/editor_view_state.h>
#include <rock_hero/ui/editor/i_editor_controller.h>
#include <rock_hero/ui/editor/i_editor_view.h>
#include <rock_hero/ui/editor/track_view_state.h>
#include <utility>

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

} // namespace

// Verifies the editor state can represent an empty editor and a single-track editor without JUCE.
TEST_CASE("EditorViewState represents empty and single-track editors", "[ui][editor-controller]")
{
    const EditorViewState empty_state{};

    CHECK(empty_state.load_button_enabled == false);
    CHECK(empty_state.play_pause_enabled == false);
    CHECK(empty_state.stop_enabled == false);
    CHECK(empty_state.play_pause_shows_pause_icon == false);
    CHECK(empty_state.tracks.empty());
    CHECK_FALSE(empty_state.last_load_error.has_value());

    const EditorViewState single_track_state{
        .load_button_enabled = true,
        .play_pause_enabled = true,
        .stop_enabled = true,
        .play_pause_shows_pause_icon = true,
        .tracks = {TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio_asset = core::AudioAsset{std::filesystem::path{"full_mix.wav"}},
        }},
        .last_load_error = std::string{"Could not load file"},
    };

    REQUIRE(single_track_state.tracks.size() == 1);
    CHECK(single_track_state.tracks.front().track_id == core::TrackId{1});
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
        .tracks = {track_state},
        .last_load_error = std::nullopt,
    };

    const EditorViewState same_state{
        .load_button_enabled = true,
        .play_pause_enabled = true,
        .stop_enabled = false,
        .play_pause_shows_pause_icon = false,
        .tracks = {same_track_state},
        .last_load_error = std::nullopt,
    };
    const EditorViewState different_state{
        .load_button_enabled = true,
        .play_pause_enabled = true,
        .stop_enabled = true,
        .play_pause_shows_pause_icon = false,
        .tracks = {track_state},
        .last_load_error = std::nullopt,
    };

    CHECK(track_state == same_track_state);
    CHECK(first_state == same_state);
    CHECK_FALSE(first_state == different_state);
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

} // namespace rock_hero::ui
