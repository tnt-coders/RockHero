#include "timeline_ruler.h"

#include <algorithm>
#include <cmath>
#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>
#include <tuple>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// Builds a one-measure 4/4 map for viewport-grid rendering checks.
[[nodiscard]] common::core::TempoMap makeOneMeasureTempoMap(double measure_seconds)
{
    return common::core::TempoMap{
        std::vector{
            common::core::TimeSignatureChange{
                .measure = 1,
                .numerator = 4,
                .denominator = 4,
            },
        },
        std::vector{
            common::core::BeatAnchor{
                .measure = 1,
                .beat = 1,
                .seconds = 0.0,
            },
            common::core::BeatAnchor{
                .measure = 2,
                .beat = 1,
                .seconds = measure_seconds,
            },
        },
    };
}

// Returns a y coordinate that lands on the 1px-on row of the timeline grid dot pattern.
[[nodiscard]] int gridDotYAtOrAfter(int y) noexcept
{
    return y + y % 2;
}

} // namespace

// Verifies ruler measure ticks span from the top while beat ticks stay in the lower quarter.
TEST_CASE("TimelineRuler draws full measure and short beat ticks", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    constexpr common::core::TimeRange one_measure_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{4.0},
    };
    const common::core::TempoMap tempo_map = makeOneMeasureTempoMap(4.0);
    constexpr common::core::Fraction grid_spacing{1, 1};
    TimelineRuler ruler;
    ruler.setBounds(0, 0, 101, g_timeline_ruler_height);
    ruler.setTimelineView(one_measure_window, ruler.getWidth(), 0);
    ruler.setGrid(tempo_map, grid_spacing);
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map, grid_spacing, one_measure_window, ruler.getWidth(), 0, ruler.getWidth()));
    ruler.setProjectLoaded(true);

    const juce::Image image = ruler.createComponentSnapshot(ruler.getLocalBounds());

    const juce::Colour beat_top = image.getPixelAt(25, 0);
    const juce::Colour measure_top = image.getPixelAt(0, 0);
    const juce::Colour beat_lower_quarter = image.getPixelAt(25, 24);
    CHECK(measure_top != beat_top);
    CHECK(beat_lower_quarter != beat_top);
}

// Verifies subdivision ticks draw shorter than beat ticks so beats stay readable on fine grids.
TEST_CASE("TimelineRuler draws shorter subdivision ticks", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    constexpr common::core::TimeRange one_measure_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{4.0},
    };
    const common::core::TempoMap tempo_map = makeOneMeasureTempoMap(4.0);
    constexpr common::core::Fraction grid_spacing{1, 2};
    TimelineRuler ruler;
    ruler.setBounds(0, 0, 101, g_timeline_ruler_height);
    ruler.setTimelineView(one_measure_window, ruler.getWidth(), 0);
    ruler.setGrid(tempo_map, grid_spacing);
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map, grid_spacing, one_measure_window, ruler.getWidth(), 0, ruler.getWidth()));
    ruler.setProjectLoaded(true);

    const juce::Image image = ruler.createComponentSnapshot(ruler.getLocalBounds());

    // The half-beat subdivision at 0.5s (x = 13) fills only the shorter bottom band, while the
    // beat at 1.0s (x = 25) also fills the taller beat band above it.
    const int bottom_y = g_timeline_ruler_height - 1;
    const int beat_band_y = g_timeline_ruler_height - g_timeline_ruler_height / 4;
    CHECK(image.getPixelAt(13, bottom_y) == image.getPixelAt(25, bottom_y));
    CHECK(image.getPixelAt(13, beat_band_y) != image.getPixelAt(25, beat_band_y));
}

// Verifies a time-signature change draws a signature label at its downbeat in the ruler's top
// band. The change entry repeats 4/4 so both maps produce identical ticks and the only pixel
// difference near the measure-2 tick is the signature label itself.
TEST_CASE("TimelineRuler draws time-signature change labels", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    constexpr common::core::TimeRange two_measure_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{8.0},
    };
    constexpr common::core::Fraction grid_spacing{1, 1};
    const auto ruler_snapshot = [&](const common::core::TempoMap& tempo_map) {
        TimelineRuler ruler;
        ruler.setBounds(0, 0, 201, g_timeline_ruler_height);
        ruler.setTimelineView(two_measure_window, ruler.getWidth(), 0);
        ruler.setGrid(tempo_map, grid_spacing);
        ruler.setGridLines(
            core::visibleTempoGridLines(
                tempo_map,
                grid_spacing,
                two_measure_window,
                ruler.getWidth(),
                0,
                ruler.getWidth()));
        ruler.setProjectLoaded(true);
        return ruler.createComponentSnapshot(ruler.getLocalBounds());
    };

    const std::vector anchors{
        common::core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
        common::core::BeatAnchor{.measure = 3, .beat = 1, .seconds = 8.0},
    };
    const common::core::TempoMap unchanged_map{
        std::vector{
            common::core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
        },
        anchors,
    };
    const common::core::TempoMap changed_map{
        std::vector{
            common::core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
            common::core::TimeSignatureChange{.measure = 2, .numerator = 4, .denominator = 4},
        },
        anchors,
    };

    const juce::Image unchanged_image = ruler_snapshot(unchanged_map);
    const juce::Image changed_image = ruler_snapshot(changed_map);

    // Measure 2 sits at 4.0s of the 8.0s window, so its tick lands at x = 100 of the 201px ruler
    // and the label band starts just right of it.
    bool label_pixels_differ = false;
    for (int x = 100; x < 160 && !label_pixels_differ; ++x)
    {
        for (int y = 2; y < 16 && !label_pixels_differ; ++y)
        {
            label_pixels_differ =
                unchanged_image.getPixelAt(x, y) != changed_image.getPixelAt(x, y);
        }
    }
    CHECK(label_pixels_differ);
}

// Verifies the default zoom maps ten seconds of timeline to the canonical width.
TEST_CASE("EditorView default zoom maps ten seconds", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(20.0));

    auto& track_viewport = findRequiredDescendant<juce::Component>(view, "track_viewport");
    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    CHECK(
        track_content.getBounds() ==
        juce::Rectangle<int>{0, 0, 2528, defaultUsableTrackViewportHeight(viewport)});
    CHECK(
        arrangement_view.getBounds() ==
        juce::Rectangle<int>{0, 0, 2528, defaultTrackHeight(viewport)});
    CHECK(cursor_overlay.getBounds() == track_content.getLocalBounds());
    CHECK(viewport.getViewWidth() <= track_viewport.getWidth());
    CHECK(viewport.getViewHeight() < track_content.getHeight());
}

// Verifies a newly loaded project scrolls the timeline to the restored transport cursor.
TEST_CASE("EditorView project load centers restored cursor", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);
    const auto state = makeLoadedEditorState(20.0);
    transport.current_position = common::core::TimePosition{15.0};
    view.setState(state);

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    const auto cursor_x = cursorXForTimelinePosition(
        transport.current_position, state.visible_timeline, track_content.getWidth());
    REQUIRE(cursor_x.has_value());

    const double screen_x =
        static_cast<double>(*cursor_x) - static_cast<double>(viewport.getViewPositionX());
    CHECK(
        screen_x == Catch::Approx(static_cast<double>(viewport.getViewWidth()) / 2.0).margin(1.0));
}

// Verifies clearing a failed open's busy state does not recenter the still-loaded project.
TEST_CASE("EditorView failed open keeps viewport position", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);
    const auto state = makeLoadedEditorState(20.0);
    view.setState(state);

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    viewport.setViewPosition(0, 0);
    transport.current_position = common::core::TimePosition{15.0};

    auto opening_state = state;
    opening_state.busy = core::BusyViewState{
        .operation = core::BusyOperation::OpeningProject,
        .message = "Opening project...",
    };
    view.setState(opening_state);
    view.setState(state);

    CHECK(viewport.getViewPositionX() == 0);
}

// Verifies loading a different project over an open one recenters on the new project's cursor,
// recognized by a changed project_load_id rather than a content diff.
TEST_CASE("EditorView project reload centers on new load id", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);

    auto first_load = makeLoadedEditorState(20.0);
    first_load.project_load_id = 1;
    transport.current_position = common::core::TimePosition{6.0};
    view.setState(first_load);

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    viewport.setViewPosition(0, 0);

    auto second_load = makeLoadedEditorState(20.0);
    second_load.project_load_id = 2;
    transport.current_position = common::core::TimePosition{12.0};
    view.setState(second_load);

    const auto cursor_x = cursorXForTimelinePosition(
        transport.current_position, second_load.visible_timeline, track_content.getWidth());
    REQUIRE(cursor_x.has_value());

    const double screen_x =
        static_cast<double>(*cursor_x) - static_cast<double>(viewport.getViewPositionX());
    CHECK(
        screen_x == Catch::Approx(static_cast<double>(viewport.getViewWidth()) / 2.0).margin(1.0));
}

// Verifies mouse wheel zoom scales the timeline content instead of seeking transport.
TEST_CASE("EditorView wheel zoom scales track width", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(20.0));

    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    const int default_width = track_content.getWidth();

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = 1.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    CHECK(track_content.getWidth() > default_width);
    CHECK(controller.timeline_seek_count == 0);
}

// Verifies the tempo grid uses the same scaled content width as the timeline viewport.
TEST_CASE("EditorView tempo grid follows zoomed timeline width", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);
    auto state = makeLoadedEditorState(4.0);
    state.tempo_map = makeOneMeasureTempoMap(4.0);
    view.setState(state);

    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");

    const auto grid_line_x = [&track_content, &state](double seconds) {
        const auto x = cursorXForTimelinePosition(
            common::core::TimePosition{seconds}, state.visible_timeline, track_content.getWidth());
        REQUIRE(x.has_value());
        return static_cast<int>(std::round(*x));
    };

    const auto grid_line_brightness = [&track_content, &grid_line_x](double seconds, int y) {
        const int x = grid_line_x(seconds);
        const int background_x = std::min(track_content.getWidth() - 1, x + 12);

        const juce::Image image =
            track_content.createComponentSnapshot(track_content.getLocalBounds());
        return std::tuple{
            x,
            image.getPixelAt(x, y).getBrightness(),
            image.getPixelAt(background_x, y).getBrightness(),
        };
    };

    const int waveform_y = gridDotYAtOrAfter(arrangement_view.getHeight() / 2);
    const int lower_track_y = gridDotYAtOrAfter(arrangement_view.getBottom() + 20);
    const int lower_track_gap_y = lower_track_y + 1;
    REQUIRE(lower_track_gap_y < track_content.getHeight());
    const int default_width = track_content.getWidth();
    const auto [default_x, default_waveform_brightness, default_waveform_background] =
        grid_line_brightness(1.0, waveform_y);
    const auto [default_lower_x, default_lower_brightness, default_lower_background] =
        grid_line_brightness(1.0, lower_track_y);
    const auto [default_gap_x, default_gap_brightness, default_gap_background] =
        grid_line_brightness(1.0, lower_track_gap_y);
    CHECK(default_lower_x == default_x);
    CHECK(default_gap_x == default_x);
    CHECK(default_waveform_brightness > default_waveform_background);
    CHECK(std::abs(default_lower_brightness - default_lower_background) > 0.01f);
    CHECK(std::abs(default_gap_brightness - default_gap_background) < 0.01f);

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = 1.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    const auto [zoomed_x, zoomed_brightness, zoomed_background] =
        grid_line_brightness(1.0, lower_track_y);
    CHECK(track_content.getWidth() > default_width);
    CHECK(zoomed_x > default_x);
    CHECK(std::abs(zoomed_brightness - zoomed_background) > 0.01f);
}

// Verifies the content-level grid is below waveform drawing but above the waveform row background.
TEST_CASE("EditorView tempo grid draws behind the waveform", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);
    auto state = makeLoadedEditorState(4.0);
    state.tempo_map = makeOneMeasureTempoMap(4.0);
    view.setState(state);
    REQUIRE(thumbnail_factory.last_thumbnail != nullptr);
    thumbnail_factory.last_thumbnail->fill_color = juce::Colours::lightgreen;

    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    const auto x = cursorXForTimelinePosition(
        common::core::TimePosition{1.0}, state.visible_timeline, track_content.getWidth());
    REQUIRE(x.has_value());

    const int line_x = static_cast<int>(std::round(*x));
    const int background_x = std::min(track_content.getWidth() - 1, line_x + 12);
    const int waveform_y = arrangement_view.getHeight() / 2;
    const int lower_track_y = gridDotYAtOrAfter(arrangement_view.getBottom() + 20);
    const int lower_track_gap_y = lower_track_y + 1;
    REQUIRE(lower_track_gap_y < track_content.getHeight());
    const juce::Image image = track_content.createComponentSnapshot(track_content.getLocalBounds());

    CHECK(image.getPixelAt(line_x, waveform_y) == juce::Colours::lightgreen);
    CHECK(
        std::abs(
            image.getPixelAt(line_x, lower_track_y).getBrightness() -
            image.getPixelAt(background_x, lower_track_y).getBrightness()) > 0.01f);
    CHECK(
        std::abs(
            image.getPixelAt(line_x, lower_track_gap_y).getBrightness() -
            image.getPixelAt(background_x, lower_track_gap_y).getBrightness()) < 0.01f);
    CHECK(
        std::abs(
            image.getPixelAt(0, lower_track_y).getBrightness() -
            image.getPixelAt(1, lower_track_y).getBrightness()) > 0.01f);
}

// Verifies zooming all the way out can fit a long timeline into the viewport.
TEST_CASE("EditorView wheel zoom out fits full timeline", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(240.0));

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    REQUIRE(track_content.getWidth() > viewport.getViewWidth());

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = -100.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    CHECK(track_content.getWidth() == viewport.getViewWidth());
}

// Verifies wheel zoom uses the visible playhead cursor as the zoom center.
TEST_CASE("EditorView wheel zoom centers visible cursor", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);
    const auto state = makeLoadedEditorState(20.0);
    view.setState(state);

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    transport.current_position = common::core::TimePosition{10.0};
    viewport.setViewPosition(400, 0);
    const auto initial_cursor_x = cursorXForTimelinePosition(
        transport.current_position, state.visible_timeline, track_content.getWidth());
    REQUIRE(initial_cursor_x.has_value());
    const float initial_cursor_position = initial_cursor_x.value_or(0.0f);
    REQUIRE(initial_cursor_position >= static_cast<float>(viewport.getViewPositionX()));
    REQUIRE(
        initial_cursor_position <
        static_cast<float>(viewport.getViewPositionX() + viewport.getViewWidth()));

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = 1.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    const auto zoomed_cursor_x = cursorXForTimelinePosition(
        transport.current_position, state.visible_timeline, track_content.getWidth());
    REQUIRE(zoomed_cursor_x.has_value());
    const double zoomed_cursor_position = static_cast<double>(zoomed_cursor_x.value_or(0.0f));
    const double zoomed_screen_x =
        zoomed_cursor_position - static_cast<double>(viewport.getViewPositionX());
    CHECK(
        zoomed_screen_x ==
        Catch::Approx(static_cast<double>(viewport.getViewWidth()) / 2.0).margin(1.0));
}

// Verifies wheel zoom scrolls to center the playhead cursor even when it starts offscreen.
TEST_CASE("EditorView wheel zoom centers offscreen cursor", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);
    const auto state = makeLoadedEditorState(20.0);
    view.setState(state);

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    transport.current_position = common::core::TimePosition{15.0};
    viewport.setViewPosition(0, 0);
    const auto cursor_x = cursorXForTimelinePosition(
        transport.current_position, state.visible_timeline, track_content.getWidth());
    REQUIRE(cursor_x.has_value());
    const float cursor_position = cursor_x.value_or(0.0f);
    REQUIRE(
        cursor_position >=
        static_cast<float>(viewport.getViewPositionX() + viewport.getViewWidth()));

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = 1.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    const auto zoomed_cursor_x = cursorXForTimelinePosition(
        transport.current_position, state.visible_timeline, track_content.getWidth());
    REQUIRE(zoomed_cursor_x.has_value());
    const double zoomed_cursor_position = static_cast<double>(zoomed_cursor_x.value_or(0.0f));
    const double zoomed_screen_x =
        zoomed_cursor_position - static_cast<double>(viewport.getViewPositionX());
    CHECK(
        zoomed_screen_x ==
        Catch::Approx(static_cast<double>(viewport.getViewWidth()) / 2.0).margin(1.0));
}

// Verifies Ctrl-click keeps the free timeline placement path.
TEST_CASE("EditorView Ctrl-click forwards free timeline position", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    view.setBounds(0, 0, 1600, 1000);
    view.setState(
        core::EditorViewState{
            .open_enabled = true,
            .import_enabled = true,
            .save_enabled = true,
            .save_as_enabled = true,
            .publish_enabled = true,
            .suggested_publish_file = std::filesystem::path{"song.rock"},
            .close_enabled = true,
            .project_loaded = true,
            .save_requires_destination = false,
            .transport =
                core::TransportViewState{
                    .play_pause_enabled = true,
                    .stop_enabled = false,
                    .play_pause_shows_pause_icon = false,
                },
            .audio_devices_available = false,
            .visible_timeline =
                common::core::TimeRange{
                    .start = common::core::TimePosition{},
                    .end = common::core::TimePosition{4.0},
                },
            .arrangement = makeArrangementState(std::filesystem::path{"mix.wav"}),
            .signal_chain =
                core::SignalChainViewState{
                    .plugins = {},
                },
            .unsaved_changes_prompt = std::nullopt,
            .save_as_prompt = std::nullopt,
            .busy = std::nullopt,
        });

    auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    CHECK(cursor_overlay.isVisible());
    REQUIRE(cursor_overlay.getWidth() > 0);
    const float click_x = std::floor(static_cast<float>(cursor_overlay.getWidth()) * 0.25f) + 0.5f;
    const auto click_y = static_cast<float>(cursor_overlay.getHeight() - 20);
    REQUIRE(click_y > static_cast<float>(arrangement_view.getBottom()));
    REQUIRE(click_y < static_cast<float>(cursor_overlay.getHeight()));
    cursor_overlay.mouseDown(makeMouseDownEvent(
        cursor_overlay,
        click_x,
        click_y,
        juce::ModifierKeys{
            juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier
        }));

    CHECK(controller.timeline_seek_count == 1);
    const auto last_seek_position = controller.last_seek_position;
    REQUIRE(last_seek_position.has_value());
    // Free placement keeps the sub-pixel click point's time over the 4-second visible range.
    const double max_column = static_cast<double>(cursor_overlay.getWidth() - 1);
    const double expected_seconds =
        std::clamp(static_cast<double>(click_x), 0.0, max_column) / max_column * 4.0;
    CHECK(last_seek_position->seconds == Catch::Approx(expected_seconds));
}

// Verifies unmodified timeline clicks snap to the nearest tempo-grid line.
TEST_CASE("EditorView timeline click snaps to nearest grid line", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1600, 1000);
    auto state = makeLoadedEditorState(4.0);
    state.tempo_map = makeOneMeasureTempoMap(4.0);
    view.setState(state);

    auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    CHECK(cursor_overlay.isVisible());
    REQUIRE(cursor_overlay.getWidth() > 1);

    const auto grid_x = cursorXForTimelinePosition(
        common::core::TimePosition{1.0}, state.visible_timeline, cursor_overlay.getWidth());
    REQUIRE(grid_x.has_value());

    const int expected_grid_x = static_cast<int>(std::round(*grid_x));
    const float click_x = static_cast<float>(expected_grid_x + 20);
    const auto click_y = static_cast<float>(cursor_overlay.getHeight() - 20);
    REQUIRE(click_y > static_cast<float>(arrangement_view.getBottom()));
    REQUIRE(click_y < static_cast<float>(cursor_overlay.getHeight()));
    cursor_overlay.mouseDown(makeMouseDownEvent(cursor_overlay, click_x, click_y));

    CHECK(controller.timeline_seek_count == 1);
    const auto last_seek_position = controller.last_seek_position;
    REQUIRE(last_seek_position.has_value());
    // The snapped seek is the exact beat time, not a value quantized to the beat's pixel column.
    CHECK(last_seek_position->seconds == Catch::Approx(1.0));
}

// Verifies ruler clicks use the same grid-snapped placement as timeline-content clicks.
TEST_CASE("EditorView ruler click snaps to nearest grid line", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1600, 1000);
    auto state = makeLoadedEditorState(4.0);
    state.tempo_map = makeOneMeasureTempoMap(4.0);
    view.setState(state);

    auto& timeline_ruler = findRequiredDescendant<TimelineRuler>(view, "timeline_ruler");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    REQUIRE(track_content.getWidth() > 1);

    const auto grid_x = cursorXForTimelinePosition(
        common::core::TimePosition{1.0}, state.visible_timeline, track_content.getWidth());
    REQUIRE(grid_x.has_value());

    const int expected_grid_x = static_cast<int>(std::round(*grid_x));
    const float click_x = static_cast<float>(expected_grid_x + 20);
    timeline_ruler.mouseDown(makeMouseDownEvent(timeline_ruler, click_x, 10.0f));

    CHECK(controller.timeline_seek_count == 1);
    const auto last_seek_position = controller.last_seek_position;
    REQUIRE(last_seek_position.has_value());
    // The snapped seek is the exact beat time, not a value quantized to the beat's pixel column.
    CHECK(last_seek_position->seconds == Catch::Approx(1.0));
}

// Verifies the track grid, overlay snapping, and ruler snapping all use the state grid spacing.
TEST_CASE("EditorView subdivision grid and snapping share spacing", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1600, 1000);
    auto state = makeLoadedEditorState(4.0);
    state.tempo_map = makeOneMeasureTempoMap(4.0);
    state.grid_spacing_beats = common::core::Fraction{1, 2};
    view.setState(state);

    auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    auto& timeline_ruler = findRequiredDescendant<TimelineRuler>(view, "timeline_ruler");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    REQUIRE(track_content.getWidth() > 1);

    // The half-beat subdivision at 0.5s draws a grid column in the lower track area.
    const auto subdivision_x = cursorXForTimelinePosition(
        common::core::TimePosition{0.5}, state.visible_timeline, track_content.getWidth());
    REQUIRE(subdivision_x.has_value());
    const int line_x = static_cast<int>(std::round(*subdivision_x));
    const int background_x = std::min(track_content.getWidth() - 1, line_x + 12);
    const int lower_track_y = gridDotYAtOrAfter(arrangement_view.getBottom() + 20);
    const juce::Image image = track_content.createComponentSnapshot(track_content.getLocalBounds());
    CHECK(
        std::abs(
            image.getPixelAt(line_x, lower_track_y).getBrightness() -
            image.getPixelAt(background_x, lower_track_y).getBrightness()) > 0.01f);

    // Overlay clicks near the subdivision snap to its exact time.
    const float click_x = static_cast<float>(line_x + 10);
    const auto click_y = static_cast<float>(cursor_overlay.getHeight() - 20);
    REQUIRE(click_y > static_cast<float>(arrangement_view.getBottom()));
    REQUIRE(click_y < static_cast<float>(cursor_overlay.getHeight()));
    cursor_overlay.mouseDown(makeMouseDownEvent(cursor_overlay, click_x, click_y));
    CHECK(controller.timeline_seek_count == 1);
    REQUIRE(controller.last_seek_position.has_value());
    CHECK(controller.last_seek_position->seconds == Catch::Approx(0.5));

    // Ruler clicks resolve through the same subdivision grid.
    timeline_ruler.mouseDown(makeMouseDownEvent(timeline_ruler, click_x, 10.0f));
    CHECK(controller.timeline_seek_count == 2);
    REQUIRE(controller.last_seek_position.has_value());
    CHECK(controller.last_seek_position->seconds == Catch::Approx(0.5));
}

// Verifies Ctrl-clicking the ruler keeps free cursor placement.
TEST_CASE("EditorView ruler Ctrl-click forwards free position", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1600, 1000);
    const auto state = makeLoadedEditorState(4.0);
    view.setState(state);

    auto& timeline_ruler = findRequiredDescendant<TimelineRuler>(view, "timeline_ruler");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    REQUIRE(track_content.getWidth() > 0);

    const float click_x = std::floor(static_cast<float>(track_content.getWidth()) * 0.25f) + 0.5f;
    timeline_ruler.mouseDown(makeMouseDownEvent(
        timeline_ruler,
        click_x,
        10.0f,
        juce::ModifierKeys{
            juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier
        }));

    CHECK(controller.timeline_seek_count == 1);
    const auto last_seek_position = controller.last_seek_position;
    REQUIRE(last_seek_position.has_value());
    // Free placement keeps the sub-pixel click point's time over the 4-second visible range.
    const double max_column = static_cast<double>(track_content.getWidth() - 1);
    const double expected_seconds =
        std::clamp(static_cast<double>(click_x), 0.0, max_column) / max_column * 4.0;
    CHECK(last_seek_position->seconds == Catch::Approx(expected_seconds));
}

// Verifies non-primary (right-button) clicks never trigger a timeline seek.
TEST_CASE("EditorView right-click does not seek the timeline", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1600, 1000);
    auto state = makeLoadedEditorState(4.0);
    state.tempo_map = makeOneMeasureTempoMap(4.0);
    view.setState(state);

    auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    auto& timeline_ruler = findRequiredDescendant<TimelineRuler>(view, "timeline_ruler");
    REQUIRE(cursor_overlay.getWidth() > 1);

    const float click_x = static_cast<float>(cursor_overlay.getWidth()) * 0.25f;
    const juce::ModifierKeys right_button{juce::ModifierKeys::rightButtonModifier};
    cursor_overlay.mouseDown(makeMouseDownEvent(cursor_overlay, click_x, 10.0f, right_button));
    timeline_ruler.mouseDown(makeMouseDownEvent(timeline_ruler, click_x, 10.0f, right_button));

    CHECK(controller.timeline_seek_count == 0);
    CHECK_FALSE(controller.last_seek_position.has_value());
}

// Verifies the focusable editor root maps keyboard play/pause to the transport intent.
TEST_CASE("EditorView forwards space key to the controller", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    CHECK(view.getWantsKeyboardFocus());
    CHECK(view.keyPressed(juce::KeyPress{juce::KeyPress::spaceKey}));
    CHECK(controller.play_pause_press_count == 1);
}

// Verifies cursor geometry uses a pushed visible range plus a separately read position.
TEST_CASE("EditorView cursor geometry maps position through visible range", "[ui][editor-view]")
{
    const auto midpoint_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{5.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{10.0}
        },
        201);
    REQUIRE(midpoint_cursor.has_value());
    CHECK(optionalValueForApprox(midpoint_cursor) == Catch::Approx(100.0f));

    const auto offset_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{12.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{10.0}, .end = common::core::TimePosition{14.0}
        },
        101);
    REQUIRE(offset_cursor.has_value());
    CHECK(optionalValueForApprox(offset_cursor) == Catch::Approx(50.0f));

    const auto fractional_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{1.25},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{4.0}
        },
        101);
    REQUIRE(fractional_cursor.has_value());
    CHECK(optionalValueForApprox(fractional_cursor) == Catch::Approx(31.25f));

    const auto before_start_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{-1.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{4.0}
        },
        101);
    REQUIRE(before_start_cursor.has_value());
    CHECK(optionalValueForApprox(before_start_cursor) == Catch::Approx(0.0f));

    const auto after_end_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{9.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{4.0}
        },
        101);
    REQUIRE(after_end_cursor.has_value());
    CHECK(optionalValueForApprox(after_end_cursor) == Catch::Approx(100.0f));

    CHECK_FALSE(cursorXForTimelinePosition(
                    common::core::TimePosition{1.0},
                    common::core::TimeRange{
                        .start = common::core::TimePosition{}, .end = common::core::TimePosition{}
                    },
                    101)
                    .has_value());
}

} // namespace rock_hero::editor::ui
