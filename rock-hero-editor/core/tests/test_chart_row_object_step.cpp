#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// A controller with a chart loaded through the normal open route, at the default 120 BPM 4/4 map
// (measure N's downbeat at (N - 1) * 2 seconds) and the quarter-note grid.
struct RowObjectStepFixture
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
        },
    };
    FakeEditorView view;

    explicit RowObjectStepFixture(common::core::Chart chart)
    {
        controller.attachView(view);
        const bool loaded =
            loadChartArrangement(controller, project_services, audio, {}, std::move(chart));
        REQUIRE(loaded);
    }

    // Parks the passive cursor and arms the caret there on the remembered string.
    void armAt(double seconds)
    {
        controller.onTimelineSeekRequested(common::core::TimePosition{seconds});
        controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    }

    // Walks the armed caret up the given number of strings.
    void walkUp(int strings)
    {
        for (int step = 0; step < strings; ++step)
        {
            controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
        }
    }

    // What the last published selection names, for the walk's own landings: a step lands on the
    // OBJECT it reached, which at a shared instant is the only thing that tells the two apart.
    [[nodiscard]] std::vector<ChartKeyframeRef> selectedKeyframes() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        REQUIRE(state != nullptr);
        if (state == nullptr)
        {
            throw std::logic_error("no state was published");
        }
        return state->chart_edit.selected_keyframes;
    }

    [[nodiscard]] std::vector<std::size_t> selectedNotes() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        REQUIRE(state != nullptr);
        if (state == nullptr)
        {
            throw std::logic_error("no state was published");
        }
        return state->chart_edit.selected_notes;
    }

    // The armed string caret as last published.
    [[nodiscard]] const ChartCaretViewState& caret() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        REQUIRE(state != nullptr);
        const ChartCaretViewState* const armed =
            state != nullptr ? caretOrNull(state->chart_edit) : nullptr;
        REQUIRE(armed != nullptr);
        if (armed == nullptr)
        {
            throw std::logic_error("no string caret is armed");
        }
        return *armed;
    }
};

// The glide chart plus a later note on the glide's string: string 3 holds a note at 2.0s, its
// keyframe at 4.0s, and a note at 8.0s, and nothing else.
[[nodiscard]] common::core::Chart makeKeyframedStringChart()
{
    common::core::Chart chart = makeGlideChart();
    chart.notes.push_back(makeTestNote({.measure = 5, .beat = 1}, 3, 7));
    return chart;
}

} // namespace

// Tab steps a string's objects with the grid ignored — its notes and the keyframes along their
// rings — and Ctrl+Tab steps its notes alone, over the keyframes. Past the last object the press is
// inert, and the caret keeps its string throughout.
TEST_CASE("EditorController steps the caret to a string's objects", "[core][chart]")
{
    RowObjectStepFixture fixture{makeKeyframedStringChart()};
    fixture.armAt(0.0);
    fixture.walkUp(2);
    REQUIRE(fixture.caret().string == 3);

    fixture.controller.onRowObjectStepRequested(true, false);
    CHECK(fixture.caret().seconds == Catch::Approx(2.0));
    fixture.controller.onRowObjectStepRequested(true, false);
    CHECK(fixture.caret().seconds == Catch::Approx(4.0));
    fixture.controller.onRowObjectStepRequested(true, false);
    CHECK(fixture.caret().seconds == Catch::Approx(8.0));
    fixture.controller.onRowObjectStepRequested(true, false);
    CHECK(fixture.caret().seconds == Catch::Approx(8.0));

    fixture.controller.onRowObjectStepRequested(false, false);
    CHECK(fixture.caret().seconds == Catch::Approx(4.0));
    fixture.controller.onRowObjectStepRequested(false, true);
    CHECK(fixture.caret().seconds == Catch::Approx(2.0));
    fixture.controller.onRowObjectStepRequested(true, true);
    CHECK(fixture.caret().seconds == Catch::Approx(8.0));
    CHECK(fixture.caret().string == 3);
}

// AT A SHARED INSTANT THE WALK STEPS THE ENDING RING'S STATEMENT BEFORE THE HEAD — time order,
// the instant belonging to the head — and it lands on the OBJECT, which is the whole of how the
// keyboard reaches a statement no landing addresses. A second press leaves the slot.
TEST_CASE("EditorController steps a ring's end statement before the head", "[core][chart]")
{
    RowObjectStepFixture fixture{makeAbuttingStringChart(true)};
    fixture.armAt(0.0);
    fixture.walkUp(2);
    REQUIRE(fixture.caret().string == 3);

    // The glide's head, then its junction, then the two objects sharing 6.0s.
    fixture.controller.onRowObjectStepRequested(true, false);
    CHECK(fixture.caret().seconds == Catch::Approx(2.0));
    fixture.controller.onRowObjectStepRequested(true, false);
    CHECK(fixture.caret().seconds == Catch::Approx(4.0));
    fixture.controller.onRowObjectStepRequested(true, false);
    CHECK(fixture.caret().seconds == Catch::Approx(6.0));
    CHECK(
        fixture.selectedKeyframes() ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 1}}));
    fixture.controller.onRowObjectStepRequested(true, false);
    CHECK(fixture.caret().seconds == Catch::Approx(6.0));
    CHECK(fixture.selectedKeyframes().empty());
    CHECK(fixture.selectedNotes() == (std::vector<std::size_t>{1}));

    // Backward from that head the statement comes first, and the press after it leaves the slot.
    fixture.controller.onRowObjectStepRequested(false, false);
    CHECK(fixture.caret().seconds == Catch::Approx(6.0));
    CHECK(
        fixture.selectedKeyframes() ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 1}}));
    fixture.controller.onRowObjectStepRequested(false, false);
    CHECK(fixture.caret().seconds == Catch::Approx(4.0));
}

// AN EMPTY END IS NOT AN OBJECT: where the ring simply stops on the next head, the only thing at
// that instant is the head, and the press before it reaches the junction.
TEST_CASE("EditorController steps over a ring's empty end", "[core][chart]")
{
    RowObjectStepFixture fixture{makeAbuttingStringChart(false)};
    fixture.armAt(0.0);
    fixture.walkUp(2);
    REQUIRE(fixture.caret().string == 3);

    fixture.controller.onRowObjectStepRequested(true, false);
    fixture.controller.onRowObjectStepRequested(true, false);
    REQUIRE(fixture.caret().seconds == Catch::Approx(4.0));
    fixture.controller.onRowObjectStepRequested(true, false);
    CHECK(fixture.caret().seconds == Catch::Approx(6.0));
    CHECK(fixture.selectedKeyframes().empty());
    CHECK(fixture.selectedNotes() == (std::vector<std::size_t>{1}));

    fixture.controller.onRowObjectStepRequested(false, false);
    CHECK(fixture.caret().seconds == Catch::Approx(4.0));
}

// From the passive marker the first press arms in place, exactly as an arrow's first press does,
// rather than leaping to an object the charter cannot yet see the caret beside.
TEST_CASE("EditorController arms in place on the first object step", "[core][chart]")
{
    RowObjectStepFixture fixture{makeKeyframedStringChart()};
    fixture.controller.onTimelineSeekRequested(common::core::TimePosition{1.0});

    fixture.controller.onRowObjectStepRequested(true, false);
    CHECK(fixture.caret().seconds == Catch::Approx(1.0));
    CHECK(fixture.caret().string == 1);
}

// A tap holding the grip the chord states beneath it is no object beyond its head: an arrow
// arriving from the right lands on the head like every arrival, and so does Tab.
TEST_CASE("EditorController lands an arrival on a tap's head", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 3, common::core::Fraction{2}),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5, common::core::Fraction{2}),
        makeTestNote({.measure = 2, .beat = 2}, 3, 12, common::core::Fraction{1, 2}),
    };
    chart.notes[2].attack = common::core::NoteAttack::Tap;
    RowObjectStepFixture fixture{std::move(chart)};

    // The tap sits at measure 2 beat 2 (2.5s); the caret starts on its string one beat later.
    fixture.armAt(3.0);
    fixture.walkUp(2);
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Left, false);
    REQUIRE(fixture.caret().seconds == Catch::Approx(2.5));
    REQUIRE(fixture.caret().face == ChartCaretFace::Mark);

    // A seek demotes the caret and remembers its string, so the next arming returns to string 3.
    fixture.armAt(3.0);
    REQUIRE(fixture.caret().string == 3);
    fixture.controller.onRowObjectStepRequested(false, false);
    CHECK(fixture.caret().seconds == Catch::Approx(2.5));
    CHECK(fixture.caret().face == ChartCaretFace::Mark);
}

} // namespace rock_hero::editor::core
