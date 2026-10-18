#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/testing/tuning_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::Fraction;
using common::core::GridPosition;
using common::core::ShapeHold;

// A place at a measure, beat and beat fraction.
[[nodiscard]] GridPosition at(const int measure, const int beat, const Fraction offset = {})
{
    return GridPosition{.measure = measure, .beat = beat, .offset = offset};
}

// An unnamed hold over [start, end).
[[nodiscard]] ShapeHold unnamedHold(const GridPosition& start, const GridPosition& end)
{
    return ShapeHold{.start = start, .end = end, .grip = common::core::UnnamedGrip{}};
}

// A none hold over [start, end).
[[nodiscard]] ShapeHold noneHold(const GridPosition& start, const GridPosition& end)
{
    return ShapeHold{.start = start, .end = end, .grip = common::core::NoneGrip{}};
}

// A two-string chord, frets `low` and `high` on strings 1 and 2, ringing `ring`.
[[nodiscard]] std::vector<common::core::ChartNote> chordAt(
    const GridPosition& place, const int low, const int high, const Fraction ring = Fraction{1, 4})
{
    return {makeTestNote(place, 1, low, ring), makeTestNote(place, 2, high, ring)};
}

// An arpeggio over measure `measure`: strings 1, 2 and 3 struck on its first three beats, every
// ring running to the next downbeat, so the walk derives one arpeggio span over the measure.
[[nodiscard]] std::vector<common::core::ChartNote> arpeggioIn(const int measure)
{
    return {
        makeTestNote(at(measure, 1), 1, 5, Fraction{3, 4}),
        makeTestNote(at(measure, 2), 2, 7, Fraction{1, 2}),
        makeTestNote(at(measure, 3), 3, 7, Fraction{1, 4}),
    };
}

// A six-string chart over the note groups, in the order given, holding `holds`.
[[nodiscard]] common::core::Chart chartOf(
    const std::vector<std::vector<common::core::ChartNote>>& groups,
    std::vector<ShapeHold> holds = {})
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    for (const std::vector<common::core::ChartNote>& group : groups)
    {
        chart.notes.insert(chart.notes.end(), group.begin(), group.end());
    }
    chart.shape_holds = std::move(holds);
    return chart;
}

// Owns the fakes and a controller with one chart loaded over the default map: 120 BPM in 4/4, so a
// measure lasts two seconds and a beat half of one.
struct HoldEditor
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

    explicit HoldEditor(common::core::Chart chart)
    {
        controller.attachView(view);
        const bool loaded =
            loadChartArrangement(controller, project_services, audio, {}, std::move(chart));
        REQUIRE(loaded);
    }

    [[nodiscard]] const EditorViewState& state() const
    {
        const EditorViewState* const published = stateOrNull(view.last_state);
        REQUIRE(published != nullptr);
        if (published == nullptr)
        {
            throw std::logic_error("editor pushed no view state");
        }
        return *published;
    }

    // The loaded chart as it now stands.
    [[nodiscard]] const common::core::Chart& chart() const
    {
        const common::core::Chart* const loaded = chartOrNull(controller);
        REQUIRE(loaded != nullptr);
        if (loaded == nullptr)
        {
            throw std::logic_error("no chart is loaded");
        }
        return *loaded;
    }

    // The spans the tab projection draws.
    [[nodiscard]] std::size_t spanCount() const
    {
        const std::shared_ptr<const common::core::ChartViewState>& tab = state().tab;
        return tab == nullptr ? 0 : tab->shapes.size();
    }

    // Parks the passive cursor at a time: a bare cursor, nothing selected.
    void park(const double seconds)
    {
        controller.onTimelineSeekRequested(common::core::TimePosition{seconds});
    }

    // Parks the cursor and lands on the time-signature row there, the row above the span rows.
    void onSignatureRowAt(const double seconds)
    {
        park(seconds);
        controller.onFocusRowJumpRequested(FocusRowJump::TimeSignature);
        REQUIRE(state().selected_time_signature_measure.has_value());
    }

    // One step of the vertical walk.
    void step(const ChartStepDirection direction)
    {
        controller.onChartCaretStepRequested(direction, false);
    }

    // Selects what the walk reaches `rows` rows below the time-signature row at a time: the hold
    // there first, then the derived span, then the position row.
    void walkDownAt(const double seconds, const int rows)
    {
        onSignatureRowAt(seconds);
        for (int row = 0; row < rows; ++row)
        {
            step(ChartStepDirection::Down);
        }
    }

    // The selected hold's index in the chart's holds, or nothing.
    [[nodiscard]] std::optional<std::size_t> selectedHold() const
    {
        return state().chart_edit.selected_shape_hold;
    }

    // The selected derived span's index in the projection's spans, or nothing.
    [[nodiscard]] std::optional<std::size_t> selectedSpan() const
    {
        return state().chart_edit.selected_hand_span;
    }
};

} // namespace

// The hold row lists the chart's holds and the span row the spans the walk derives where no unnamed
// hold states one, each only where a marker holds the column. Down from the signature row lands on
// the hold, or on the derived span, and past them on the position row.
TEST_CASE(
    "The walk reaches the hold and span rows only where they hold the column", "[core][holds]")
{
    // An arpeggio over measure 1, a chord box at 3:1, and an unnamed hold over the box.
    HoldEditor editor{chartOf(
        {arpeggioIn(1), chordAt(at(3, 1), 3, 5)}, {unnamedHold(at(3, 1), at(3, 2))})};
    REQUIRE(editor.spanCount() == 2);

    SECTION("inside a derived span the walk selects it")
    {
        editor.walkDownAt(0.75, 1);
        CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});
        CHECK_FALSE(editor.selectedHold().has_value());
        CHECK(editor.state().selection_present);
        CHECK(editor.state().selection_start_seconds == std::optional{0.0});
        CHECK(editor.transport.position().seconds == Catch::Approx(0.75));

        editor.step(ChartStepDirection::Down);
        CHECK_FALSE(editor.selectedSpan().has_value());
        CHECK(editor.state().chart_edit.selected_fret_hand_position.has_value());
    }

    SECTION("inside a hold the walk selects the hold, never its span")
    {
        editor.walkDownAt(4.25, 1);
        CHECK(editor.selectedHold() == std::optional<std::size_t>{0});
        CHECK_FALSE(editor.selectedSpan().has_value());
        CHECK(editor.state().selection_present);

        editor.step(ChartStepDirection::Down);
        CHECK_FALSE(editor.selectedHold().has_value());
        CHECK(editor.state().chart_edit.selected_fret_hand_position.has_value());

        editor.step(ChartStepDirection::Up);
        CHECK(editor.selectedHold() == std::optional<std::size_t>{0});
    }

    SECTION("in a gap the walk passes from the signature row to the position row")
    {
        editor.walkDownAt(2.5, 1);
        CHECK_FALSE(editor.selectedHold().has_value());
        CHECK_FALSE(editor.selectedSpan().has_value());
        CHECK(editor.state().chart_edit.selected_fret_hand_position.has_value());
    }
}

// A none hold keeps the chord box struck inside it, so both rows hold that column: the walk down
// lands on the none hold first and on the box next.
TEST_CASE("The walk reaches a none hold and then the box it encloses", "[core][holds]")
{
    HoldEditor editor{chartOf({chordAt(at(1, 2), 3, 5)}, {noneHold(at(1, 1), at(2, 1))})};
    REQUIRE(editor.spanCount() == 1);

    editor.walkDownAt(0.6, 1);
    CHECK(editor.selectedHold() == std::optional<std::size_t>{0});
    CHECK_FALSE(editor.selectedSpan().has_value());

    editor.step(ChartStepDirection::Down);
    CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});
    CHECK_FALSE(editor.selectedHold().has_value());
}

// Tab on the span row steps from span to span across the gap, selecting each with the cursor on
// its start; Shift+Tab steps back.
TEST_CASE("Tab steps the span row from span to span", "[core][holds]")
{
    HoldEditor editor{chartOf({arpeggioIn(1), chordAt(at(3, 1), 3, 5)})};
    REQUIRE(editor.spanCount() == 2);
    editor.walkDownAt(0.75, 1);
    REQUIRE(editor.selectedSpan() == std::optional<std::size_t>{0});

    editor.controller.onRowObjectStepRequested(true, false);
    CHECK(editor.selectedSpan() == std::optional<std::size_t>{1});
    CHECK(editor.transport.position().seconds == Catch::Approx(4.0));

    editor.controller.onRowObjectStepRequested(false, false);
    CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});
    CHECK(editor.transport.position().seconds == Catch::Approx(0.0));
}

} // namespace rock_hero::editor::core
