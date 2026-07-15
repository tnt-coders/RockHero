#include "tone/tone_track_view.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <compare>
#include <expected>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/tone/tone_track_view_state.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <string>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Left-button modifiers with Alt held: the insert quasimode's click.
const juce::ModifierKeys g_alt_click{
    juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier
};

// Records tone-track intents emitted by the view.
struct RecordingToneTrackListener final : public ToneTrackView::Listener
{
    void onToneRegionSelected(std::string region_id) override
    {
        last_selected_region_id = std::move(region_id);
        select_count += 1;
    }

    void onToneRegionActivated() override
    {
        activate_count += 1;
    }

    void onToneRegionResizeRequested(
        std::string region_id, common::core::GridPosition /*start*/,
        common::core::GridPosition /*end*/) override
    {
        last_resize_region_id = std::move(region_id);
        resize_count += 1;
    }

    void onToneBoundaryMoveRequested(
        std::string right_region_id, common::core::GridPosition /*position*/) override
    {
        last_boundary_region_id = std::move(right_region_id);
        boundary_move_count += 1;
    }

    void onToneRenamePromptRequested(
        std::string /*tone_document_ref*/, std::string /*current_name*/) override
    {
        rename_count += 1;
    }

    void onToneChangeInsertRequested(common::core::GridPosition position) override
    {
        last_insert_position = position;
        insert_count += 1;
    }

    void onToneRegionDeleteRequested(std::string region_id) override
    {
        last_delete_region_id = std::move(region_id);
        delete_count += 1;
    }

    std::string last_selected_region_id;
    int select_count = 0;
    int activate_count = 0;
    std::string last_resize_region_id;
    int resize_count = 0;
    std::string last_boundary_region_id;
    int boundary_move_count = 0;
    int rename_count = 0;
    std::optional<common::core::GridPosition> last_insert_position;
    int insert_count = 0;
    std::string last_delete_region_id;
    int delete_count = 0;
};

// Manually controlled transport; the view samples position() at render cadence only.
struct StubTransport final : public common::audio::ITransport
{
    void play() override
    {}

    void pause() override
    {}

    void stop() override
    {}

    void seek(common::core::TimePosition position_value) override
    {
        current_position = position_value;
    }

    [[nodiscard]] common::audio::TransportState state() const noexcept override
    {
        return {};
    }

    [[nodiscard]] common::core::TimePosition position() const noexcept override
    {
        return current_position;
    }

    // Contract-shaped stub: these view tests never drive speed, so only 1.0 is accepted.
    [[nodiscard]] std::expected<void, common::audio::TransportError> setPlaybackSpeed(
        double factor) override
    {
        if (std::is_neq(factor <=> 1.0))
        {
            return std::unexpected{
                common::audio::TransportError{common::audio::TransportErrorCode::SpeedNotSupported}
            };
        }

        return {};
    }

    [[nodiscard]] double playbackSpeed() const noexcept override
    {
        return 1.0;
    }

    // Contract-shaped stub storing the normalized region; these view tests never drive loops.
    [[nodiscard]] std::expected<void, common::audio::TransportError> setLoopRegion(
        common::core::TimeRange region) override
    {
        const common::core::TimeRange normalized{
            .start = common::core::TimePosition{std::min(region.start.seconds, region.end.seconds)},
            .end = common::core::TimePosition{std::max(region.start.seconds, region.end.seconds)},
        };
        if (normalized.duration().seconds < common::audio::g_minimum_loop_region_duration.seconds)
        {
            return std::unexpected{
                common::audio::TransportError{common::audio::TransportErrorCode::LoopRegionTooShort}
            };
        }

        loop_region = normalized;
        return {};
    }

    void clearLoopRegion() override
    {
        loop_region.reset();
    }

    [[nodiscard]] std::optional<common::core::TimeRange> loopRegion() const noexcept override
    {
        return loop_region;
    }

    void addListener(Listener& /*listener*/) override
    {}

    void removeListener(Listener& /*listener*/) override
    {}

    // Position returned to the view's render-cadence sampling.
    common::core::TimePosition current_position{};

    // Engaged normalized loop region; nullopt while looping is disengaged.
    std::optional<common::core::TimeRange> loop_region{};
};

// Two gap-free regions over the default 120 BPM 4/4 map: region-a spans 0..4 s (measures 1-2)
// and region-b spans 4..8 s (measures 3-4), so 100 px = 1 s at the 800 px test width.
[[nodiscard]] core::ToneTrackViewState makeState()
{
    core::ToneTrackViewState state;
    core::ToneRegionViewState region_a;
    region_a.id = "region-a";
    region_a.name = "Clean";
    region_a.tone_document_ref = "tones/a/tone.json";
    region_a.grid_start = {.measure = 1, .beat = 1, .offset = {}};
    region_a.grid_end = {.measure = 3, .beat = 1, .offset = {}};
    region_a.time_range = {
        .start = common::core::TimePosition{0.0}, .end = common::core::TimePosition{4.0}
    };
    core::ToneRegionViewState region_b;
    region_b.id = "region-b";
    region_b.name = "Lead";
    region_b.tone_document_ref = "tones/b/tone.json";
    region_b.grid_start = {.measure = 3, .beat = 1, .offset = {}};
    region_b.grid_end = {.measure = 5, .beat = 1, .offset = {}};
    region_b.time_range = {
        .start = common::core::TimePosition{4.0}, .end = common::core::TimePosition{8.0}
    };
    state.regions = {std::move(region_a), std::move(region_b)};
    return state;
}

// Owns the JUCE runtime the component needs for fonts and cursors in headless tests.
struct ToneTrackHarness
{
    juce::ScopedJuceInitialiser_GUI gui;
    common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{8.0});
    RecordingToneTrackListener listener;
    StubTransport transport;
    ToneTrackView view{listener, tempo_map, transport};

    ToneTrackHarness()
    {
        view.setSize(800, 40);
        view.setVisibleTimeline(
            common::core::TimeRange{
                .start = common::core::TimePosition{0.0},
                .end = common::core::TimePosition{8.0},
            });
        view.setState(makeState());
    }
};

// Renders one viewport-sized window of the (possibly huge) content-wide row, emulating how the
// real viewport paints it: origin shifted left by the scroll offset, clip = the window bounds.
[[nodiscard]] juce::Image renderWindow(ToneTrackView& view, int window_x, int window_width)
{
    juce::Image image{
        juce::Image::ARGB, window_width, view.getHeight(), true, juce::SoftwareImageType{}
    };
    juce::Graphics graphics{image};
    graphics.setOrigin({-window_x, 0});
    view.paint(graphics);
    return image;
}

// Reports whether any pixel in the given local-image column band reads as the near-white
// selection outline (the strongest border, drawn last, so it must be present wherever the
// border passes through the window).
[[nodiscard]] bool columnHasSelectionOutline(
    const juce::Image& image, int x, int y_begin, int y_end)
{
    for (int y = y_begin; y < y_end; ++y)
    {
        const juce::Colour pixel = image.getPixelAt(x, y);
        if (pixel.getRed() > 200 && pixel.getGreen() > 200 && pixel.getBlue() > 200)
        {
            return true;
        }
    }
    return false;
}

} // namespace

// Alt+click inside a region requests a tone change at the grid-snapped click position; a plain
// click on the same spot selects the region instead of inserting anything.
TEST_CASE("Tone track Alt-click requests a tone change at the position", "[ui][tone-track]")
{
    ToneTrackHarness harness;

    // x 250 = 2.5 s snaps to the quarter-note line at measure 2 beat 2, inside region-a.
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 250.0f, 20.0f, g_alt_click));
    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 250.0f, 20.0f, g_alt_click));

    REQUIRE(harness.listener.insert_count == 1);
    CHECK(
        harness.listener.last_insert_position ==
        std::optional{common::core::GridPosition{.measure = 2, .beat = 2, .offset = {}}});
    CHECK(harness.listener.select_count == 0);
}

// A plain click never mutates: it emits exactly one selection intent for the clicked region.
TEST_CASE("Tone track plain click selects without inserting", "[ui][tone-track]")
{
    ToneTrackHarness harness;

    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 250.0f, 20.0f));
    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 250.0f, 20.0f));

    CHECK(harness.listener.select_count == 1);
    CHECK(harness.listener.last_selected_region_id == "region-a");
    CHECK(harness.listener.insert_count == 0);
    CHECK(harness.listener.boundary_move_count == 0);
}

// Cancelling an in-flight edge drag (the editor routes Esc here) commits nothing on release.
TEST_CASE("Tone track cancels an in-flight edge drag on request", "[ui][tone-track]")
{
    ToneTrackHarness harness;

    // Grab the shared boundary at x 400 (region-b's start edge) and drag it right.
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 400.0f, 20.0f));
    harness.view.mouseDrag(testing::makeMouseDragEvent(harness.view, 440.0f, 20.0f, 400.0f, 20.0f));
    CHECK(harness.view.cancelActiveGesture());
    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 440.0f, 20.0f));

    CHECK(harness.listener.boundary_move_count == 0);
    CHECK(harness.listener.resize_count == 0);

    // With no gesture active the cancel reports unhandled so Esc can serve other owners.
    CHECK_FALSE(harness.view.cancelActiveGesture());
}

// Regression for the Direct2D border dropout: at high zoom the content-wide row used to hand the
// renderer a rounded-rect border path spanning hundreds of thousands of pixels, and Windows' D2D
// peer dropped parts of such strokes (only the region's left edge survived). The paint now clamps
// fill/border geometry to the clip neighborhood; this guards that the clamped drawing still
// paints complete borders through viewport-sized windows and cursor-strip-sized slivers anywhere
// along the region.
TEST_CASE("Tone track paints region borders through a window at high zoom", "[ui][tone-track]")
{
    ToneTrackHarness harness;

    // A 4-minute song at the 1264 px/s zoom cap: the row is ~303k px wide. Split at 16 s; the
    // second region (16..240 s) is active and selected, exactly the reported scenario.
    const double duration = 240.0;
    const int width = static_cast<int>(duration * 1264.0);
    harness.view.setSize(width, 40);
    harness.view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{0.0},
            .end = common::core::TimePosition{duration},
        });
    core::ToneTrackViewState state = makeState();
    state.regions[0].time_range = {
        .start = common::core::TimePosition{0.0}, .end = common::core::TimePosition{16.0}
    };
    state.regions[1].time_range = {
        .start = common::core::TimePosition{16.0}, .end = common::core::TimePosition{duration}
    };
    state.regions[1].active = true;
    state.regions[1].selected = true;
    harness.view.setState(state);

    // Window deep inside the second region (about 120 s in): only its horizontal border runs
    // cross this window, so the white selection outline must appear at the top and bottom.
    const int window_width = 1264;
    const juce::Image middle = renderWindow(harness.view, width / 2, window_width);
    CHECK(columnHasSelectionOutline(middle, window_width / 2, 0, 10));
    CHECK(columnHasSelectionOutline(middle, window_width / 2, 30, 40));

    // Window over the split boundary (16 s = x 20224): the left edge must be present too.
    const juce::Image boundary = renderWindow(harness.view, 20224 - 100, window_width);
    bool left_edge_found = false;
    for (int x = 80; x < 130 && !left_edge_found; ++x)
    {
        left_edge_found = columnHasSelectionOutline(boundary, x, 10, 30);
    }
    CHECK(left_edge_found);

    // Narrow cursor-strip-sized repaints (the real app's most common partial repaint) swept
    // across the region must each show the top and bottom border runs.
    for (int step = 0; step < 12; ++step)
    {
        const int strip_x = 20324 + ((width - 20524) * step) / 12;
        const juce::Image strip = renderWindow(harness.view, strip_x, 8);
        CHECK(columnHasSelectionOutline(strip, 4, 0, 10));
        CHECK(columnHasSelectionOutline(strip, 4, 30, 40));
    }
}

// Alt+drag places the ghost boundary before committing: the release position, not the press
// position, becomes the requested tone change.
TEST_CASE("Tone track Alt-drag places the insert before committing", "[ui][tone-track]")
{
    ToneTrackHarness harness;

    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 250.0f, 20.0f, g_alt_click));
    harness.view.mouseDrag(
        testing::makeMouseDragEvent(harness.view, 310.0f, 20.0f, 250.0f, 20.0f, g_alt_click));
    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 310.0f, 20.0f, g_alt_click));

    REQUIRE(harness.listener.insert_count == 1);
    // x 310 = 3.1 s snaps to the 3.0 s quarter-note line: measure 2 beat 3.
    CHECK(
        harness.listener.last_insert_position ==
        std::optional{common::core::GridPosition{.measure = 2, .beat = 3, .offset = {}}});
}

} // namespace rock_hero::editor::ui
