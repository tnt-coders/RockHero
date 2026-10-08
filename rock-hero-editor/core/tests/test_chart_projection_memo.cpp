#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/testing/tuning_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// A downbeat of the default 4/4 map the fixture loads.
[[nodiscard]] common::core::GridPosition downbeatOf(const int measure)
{
    return common::core::GridPosition{.measure = measure, .beat = 1, .offset = {}};
}

// The controller every memo scenario runs, with a chart loaded through the normal open route at the
// quarter-note grid. Its scheduler is the immediate one, so a typed digit no further digit can
// widen settles inside its own keystroke.
struct MemoFixture
{
    FakeTransport transport{};
    ConfigurableSongAudio audio{};
    FakeProjectServices project_services{};
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view{};

    MemoFixture()
    {
        controller.attachView(view);
    }

    [[nodiscard]] bool load(common::core::Chart chart)
    {
        return loadChartArrangement(controller, project_services, audio, {}, std::move(chart));
    }

    // Whether the published tab projection is exactly the scene a fresh derivation of the stored
    // chart builds, which is what a memo seeded or refreshed anywhere along the way must leave.
    // Assertion-free, so callers read it inside their own CHECK.
    [[nodiscard]] bool publishesFreshScene() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        const common::core::Arrangement* const arrangement =
            controller.session().currentArrangement();
        if (state == nullptr || state->tab == nullptr || arrangement == nullptr ||
            state->chart_edit.pending_fret.has_value())
        {
            return false;
        }
        return *state->tab == common::core::makeChartViewState(
                                  *arrangement, controller.session().song().tempo_map);
    }

    // Clicks the hand chip fronting at a position.
    void clickChipAt(const common::core::GridPosition& position)
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        REQUIRE(state != nullptr);
        REQUIRE(state->tab != nullptr);
        const std::optional<std::size_t> chip = fretHandChipAt(*state->tab, position);
        REQUIRE(chip.has_value());
        if (chip.has_value())
        {
            controller.onFretHandPositionSelected(*chip);
        }
    }

    // The stored chart's authored positions, empty where no chart is loaded.
    [[nodiscard]] std::vector<common::core::FretHandPosition> positions() const
    {
        const common::core::Chart* const chart = chartOrNull(controller);
        return chart != nullptr ? chart->fret_hand_positions
                                : std::vector<common::core::FretHandPosition>{};
    }
};

// The shared six-string chart with one authored position on the measure-2 chord, so the scene's
// hand layer has an authored chip as well as derived ones.
[[nodiscard]] common::core::Chart makeHandedChart()
{
    common::core::Chart chart = makeTestChart();
    chart.fret_hand_positions = {
        common::core::FretHandPosition{.position = downbeatOf(2), .fret = 1, .end_fret = {}},
    };
    return chart;
}

} // namespace

// The controller keeps the chart's notes layer and hand layer memoized, and a judged edit seeds the
// notes layer with the gate's own derivation instead of deriving it again. Whichever path wrote the
// chart — a verb, a gesture's replayed step, a fret entry's settle, an undo — the scene published
// afterwards must be the one a fresh derivation of the stored chart builds.
TEST_CASE("The chart memo publishes the scene a fresh derivation builds", "[core][chart]")
{
    MemoFixture fixture;
    REQUIRE(fixture.load(makeHandedChart()));
    REQUIRE(fixture.publishesFreshScene());
    const common::core::Chart* const loaded = chartOrNull(fixture.controller);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->notes.size() == 3);

    SECTION("a plain verb")
    {
        // The measure-3 note's head (makeGeometry): measure 3 on string 1.
        click(fixture.controller, 80.0f, 220.0f);
        fixture.controller.onChartFretShiftRequested(1);
        const common::core::Chart* const chart = chartOrNull(fixture.controller);
        REQUIRE(chart != nullptr);
        CHECK(chart->notes[2].fret == 8);
        CHECK(fixture.publishesFreshScene());
    }

    SECTION("a gesture's second step, which walks back and replays")
    {
        click(fixture.controller, 80.0f, 220.0f);
        fixture.controller.onChartSustainAdjustRequested(1);
        CHECK(fixture.publishesFreshScene());
        fixture.controller.onChartSustainAdjustRequested(1);
        const common::core::Chart* const chart = chartOrNull(fixture.controller);
        REQUIRE(chart != nullptr);
        CHECK(chart->notes[2].sustain == common::core::Fraction{1});
        CHECK(fixture.publishesFreshScene());
    }

    SECTION("a fret entry's settle")
    {
        // The measure-2 chord's string-1 head; no fret above 24 begins with 9, so the digit
        // settles in its own keystroke.
        click(fixture.controller, 40.0f, 220.0f);
        fixture.controller.onChartFretDigitTyped(9);
        const common::core::Chart* const chart = chartOrNull(fixture.controller);
        REQUIRE(chart != nullptr);
        CHECK(chart->notes[0].fret == 9);
        CHECK(fixture.publishesFreshScene());
    }

    SECTION("an undo, which seeds nothing")
    {
        click(fixture.controller, 80.0f, 220.0f);
        fixture.controller.onChartFretShiftRequested(1);
        fixture.controller.onUndoRequested();
        const common::core::Chart* const chart = chartOrNull(fixture.controller);
        REQUIRE(chart != nullptr);
        CHECK(chart->notes[2].fret == 7);
        CHECK(fixture.publishesFreshScene());
    }
}

// The position gate reads the memo's notes layer, so the layer must be the one the notes now
// derive. A window of frets 5 to 8 cannot move inside a ring gripping fret 12; once that note is
// retyped to 5 — a commit that seeds the layer — the same move must land. A gate reading the layer
// the retype replaced would refuse it again.
TEST_CASE("The position gate reads the notes layer the last edit left", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = {makeTestNote(downbeatOf(2), 1, 12, common::core::Fraction{1})};
    chart.fret_hand_positions = {
        common::core::FretHandPosition{.position = downbeatOf(3), .fret = 5, .end_fret = 8},
    };
    MemoFixture fixture;
    REQUIRE(fixture.load(chart));
    const std::vector<common::core::FretHandPosition> authored = fixture.positions();
    REQUIRE(authored == chart.fret_hand_positions);

    // One quantum left is measure 2 beat 4, inside the ring gripping fret 12.
    fixture.clickChipAt(downbeatOf(3));
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(fixture.positions() == authored);

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartFretDigitTyped(5);
    const common::core::Chart* const retyped = chartOrNull(fixture.controller);
    REQUIRE(retyped != nullptr);
    REQUIRE(retyped->notes.size() == 1);
    CHECK(retyped->notes.front().fret == 5);

    fixture.clickChipAt(downbeatOf(3));
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(
        fixture.positions() ==
        std::vector{common::core::FretHandPosition{
            .position = common::core::GridPosition{.measure = 2, .beat = 4, .offset = {}},
            .fret = 5,
            .end_fret = 8,
        }});
    CHECK(fixture.publishesFreshScene());
}

} // namespace rock_hero::editor::core
