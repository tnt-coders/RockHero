/*!
\file chart_editing_fixture.h
\brief The chart-editing controller tests' shared fixture: the six-string chart, its open route,
the lane geometry, the pointer gestures, and the pending-entry harness.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <rock_hero/editor/core/testing/chart_fixture.h>
#include <rock_hero/editor/core/testing/deferring_message_thread_scheduler.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

// Loads the chart-bearing song fixture through the controller's normal open route. A scenario
// needing a different note stream passes its own chart; the shared fixture is the default.
[[nodiscard]] inline bool loadChartArrangement(
    EditorController& controller, FakeProjectServices& project_services,
    ConfigurableSongAudio& audio, std::vector<common::core::SongSection> sections = {},
    common::core::Chart chart = makeTestChart())
{
    const common::core::TimeRange timeline_range = loadedTimelineRange(30.0);
    audio.next_prepared_audio_duration = timeline_range.duration();
    audio.next_set_active_arrangement_result = true;
    common::core::Song song = makeSong(std::filesystem::path{"a.wav"}, timeline_range);
    // The default-constructed TempoMap's terminal anchor sits at 2.0s and time queries clamp
    // there; cover the whole fixture timeline the way real imports do.
    song.tempo_map = common::core::TempoMap::defaultMap(timeline_range.duration());
    song.sections = std::move(sections);
    song.arrangements.front().chart = std::move(chart);
    project_services.next_song = std::move(song);
    controller.onOpenRequested(std::filesystem::path{"loaded.rhp"});
    // Every scenario in this file states its times in quarter-note grid steps (0.5s at the
    // default 120 BPM 4/4), so pin that grid explicitly instead of riding the editor default.
    controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});
    return controller.session().currentArrangement() != nullptr;
}

// 20-second window across a 400x240 six-lane band: 20 px/s, 40px lanes, 25px heads.
// Note anchors: measure 2 = (40, 220) on string 1 and (40, 180) on string 2; measure 3 = (80,
// 220) with a one-second tail to x = 100.
[[nodiscard]] inline common::ui::TabLaneGeometry makeGeometry()
{
    return common::ui::makeTabLaneGeometry(
        0.0f,
        0.0f,
        400.0f,
        240.0f,
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        },
        6,
        6);
}

[[nodiscard]] inline ChartPointerEvent pointerEvent(
    float x, float y, ChartPointerModifiers modifiers = {}, int clicks = 1)
{
    return ChartPointerEvent{
        .geometry = makeGeometry(), .x = x, .y = y, .modifiers = modifiers, .clicks = clicks
    };
}

// Presses and releases at the same point, the plain click gesture.
inline void click(
    EditorController& controller, float x, float y, ChartPointerModifiers modifiers = {})
{
    controller.onChartPointerDown(pointerEvent(x, y, modifiers));
    controller.onChartPointerUp(pointerEvent(x, y, modifiers));
}

// Two click gestures with the second pair reporting a consecutive-click count of two, matching
// how JUCE delivers a double click.
inline void doubleClick(EditorController& controller, float x, float y)
{
    click(controller, x, y);
    controller.onChartPointerDown(pointerEvent(x, y, {}, 2));
    controller.onChartPointerUp(pointerEvent(x, y, {}, 2));
}

// Chart typing fixture: the deferring scheduler holds the pending window's wake for explicit
// pumping, and the injected clock pins the combine window, so a provisional value stays pending
// exactly until the test settles it. Under the immediate scheduler the wake settles inside the
// arming keystroke — production-correct, but useless for pinning the pending state itself, so
// every test whose narrative needs a value to STAY pending builds its controller over this.
// Declare it before the controller: the services lambda reads the clock through `this`.
struct PendingEntryHarness
{
    testing::DeferringMessageThreadScheduler scheduler{};
    std::uint32_t now_ms{1000};

    // Builds the controller services over this harness's scheduler and clock.
    [[nodiscard]] EditorController::Services services()
    {
        EditorController::Services services =
            controllerServices(nullEditorSettings(), immediateTaskRunner(), scheduler);
        services.now_milliseconds = [this] { return now_ms; };
        return services;
    }
};

} // namespace rock_hero::editor::core
