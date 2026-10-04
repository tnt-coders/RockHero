#include "log_capture.h"

#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

// A plan refused whole names the selection it was planned over: two objects by their count and
// their first and last, beside the planner's own reason.
TEST_CASE("A refused chart plan logs the selection it refused", "[core][chart][refusal]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));

    // The measure-2 chord is frets 3 and 5: three downward ticks land on 0/2, and the fourth would
    // take the lower one below zero.
    doubleClick(controller, 40.0f, 220.0f);
    for (int step = 0; step < 3; ++step)
    {
        controller.onChartFretShiftRequested(-1);
    }
    const testing::LogCapture capture;
    controller.onChartFretShiftRequested(-1);

    const std::string contents = capture.text();
    INFO(contents);
    CHECK(
        contents.find(
            R"(editor.chart: Refused chart edit )"
            R"(subject="2 objects: 2:1 string 1 .. 2:1 string 2" )"
            R"(detail="note is out of range at 2:1")") != std::string::npos);
}

// A per-note verb that applied to the rest names each note it refused by its slot.
TEST_CASE("A per-note chart refusal logs the note it refused", "[core][chart][refusal]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    common::core::Chart chart = makeTestChart();
    chart.notes[0].vibrato = common::core::VibratoState::Narrow;
    const bool loaded =
        loadChartArrangement(controller, project_services, audio, {}, std::move(chart));
    REQUIRE(loaded);

    // A dead note sounds no pitch, so X over the chord mutes the plain member and refuses the
    // vibrato one.
    doubleClick(controller, 40.0f, 220.0f);
    const testing::LogCapture capture;
    controller.onChartTechniqueToggleRequested(ChartTechnique::Dead);

    const std::string contents = capture.text();
    INFO(contents);
    CHECK(
        contents.find(
            R"(editor.chart: Refused chart edit subject="2:1 string 1" )"
            R"(detail="a dead note sounds no pitch, )"
            R"(so its bend or vibrato was dropped at 2:1")") != std::string::npos);
}

} // namespace rock_hero::editor::core
