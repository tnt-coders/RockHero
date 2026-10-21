#include "log_capture.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/testing/tuning_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::Fraction;
using common::core::GridPosition;
using common::core::ShapeHold;
using testing::LogCapture;

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
// ring running to its fourth, so the walk derives one arpeggio span over those three beats.
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
        // A grid step is a beat, so a time selection is stated in beats.
        controller.onGridNoteValueChangeRequested(Fraction{1, 4});
        // Every case starts from holds that fit, as a loaded chart's do.
        REQUIRE(holdsFit());
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

    // The published selection's span, or nothing, read as the lane reads it.
    [[nodiscard]] const common::core::ShapeViewState* publishedSpan() const
    {
        const std::optional<std::size_t> index = state().chart_edit.selected_span;
        const std::shared_ptr<const common::core::ChartViewState>& tab = state().tab;
        if (!index.has_value() || tab == nullptr || *index >= tab->shapes.size())
        {
            return nullptr;
        }
        return &tab->shapes[*index];
    }

    // The index in the chart's holds of the hold the selection names: a selected none hold, or the
    // unnamed hold stating a selected held span, which starts where its span does. Nothing where
    // neither is selected.
    [[nodiscard]] std::optional<std::size_t> selectedHold() const
    {
        const std::vector<ShapeHold> standing = holds();
        const common::core::TempoMap& tempo_map = controller.session().song().tempo_map;
        const auto index_of = [&standing](const auto& names) -> std::optional<std::size_t> {
            for (std::size_t index = 0; index < standing.size(); ++index)
            {
                if (names(standing[index]))
                {
                    return index;
                }
            }
            return std::nullopt;
        };
        if (const std::optional<std::size_t> none = state().chart_edit.selected_none_hold;
            none.has_value())
        {
            const GridPosition start = state().tab->none_holds.at(*none).start;
            return index_of([&start](const ShapeHold& hold) { return hold.start == start; });
        }
        const common::core::ShapeViewState* const span = publishedSpan();
        if (span == nullptr || !span->held)
        {
            return std::nullopt;
        }
        const double start_seconds = span->start_seconds;
        return index_of([&tempo_map, start_seconds](const ShapeHold& hold) {
            return tempo_map.secondsAt(hold.start) == Catch::Approx(start_seconds);
        });
    }

    // The selected derived span's index in the projection's spans, or nothing.
    [[nodiscard]] std::optional<std::size_t> selectedSpan() const
    {
        const common::core::ShapeViewState* const span = publishedSpan();
        if (span == nullptr || span->held)
        {
            return std::nullopt;
        }
        return state().chart_edit.selected_span;
    }

    // The chart's holds as they now stand.
    [[nodiscard]] std::vector<ShapeHold> holds() const
    {
        return chart().shape_holds;
    }

    // Whether every hold fits its notes as the chart now stands: the normal form rewrites nothing.
    [[nodiscard]] bool holdsFit() const
    {
        const common::core::Chart& loaded = chart();
        return common::core::normalizeShapeHolds(
                   loaded.notes,
                   loaded.shape_holds,
                   loaded.tuning.capo,
                   controller.session().song().tempo_map)
            .conversions.empty();
    }

    // The chart's holds as text, each its token and a none hold's grip, for a failure to name them.
    [[nodiscard]] std::string holdsText() const
    {
        std::string text;
        for (const ShapeHold& hold : holds())
        {
            const bool none = std::holds_alternative<common::core::NoneGrip>(hold.grip);
            text += common::core::formatShapeHoldToken(hold) + (none ? " (none); " : "; ");
        }
        return text;
    }

    // How many entries the history holds, so a refusal reads as no new entry.
    [[nodiscard]] std::size_t entries() const
    {
        return state().undo_history.labels.size();
    }

    // The label of the history's latest entry.
    [[nodiscard]] std::string lastLabel() const
    {
        const std::vector<std::string>& labels = state().undo_history.labels;
        REQUIRE_FALSE(labels.empty());
        return labels.empty() ? std::string{} : labels.back();
    }

    // Parks the cursor and extends a time selection `beats` grid steps to the right.
    void selectTime(const double from_seconds, const int beats)
    {
        park(from_seconds);
        for (int beat = 0; beat < beats; ++beat)
        {
            controller.onTimeSelectionExtendRequested(
                TimeSelectionExtent::Grid, ChartStepDirection::Right);
        }
    }

    // Parks the cursor and arms the caret there on string 1, selecting the note standing there.
    void armAt(const double seconds)
    {
        park(seconds);
        step(ChartStepDirection::Right);
    }

    // Ctrl+H, answering the picker it opens with `grip`, or empty for Auto.
    void chooseHold(const std::optional<common::core::HoldGrip>& grip)
    {
        const std::size_t asked = view.shown_hold_pickers.size();
        controller.onHoldRequested();
        if (view.shown_hold_pickers.size() > asked)
        {
            controller.onHoldChoiceRequested(grip);
        }
    }

    // Ctrl+H, choosing Shape.
    void holdShape()
    {
        chooseHold(common::core::UnnamedGrip{});
    }

    // The labels of the last hold picker Ctrl+H opened, in the order it shows them.
    [[nodiscard]] std::vector<std::string> offeredHoldChoices() const
    {
        REQUIRE_FALSE(view.shown_hold_pickers.empty());
        std::vector<std::string> labels;
        if (!view.shown_hold_pickers.empty())
        {
            for (const ChartHoldChoice& choice : view.shown_hold_pickers.back().choices)
            {
                labels.push_back(choice.label);
            }
        }
        return labels;
    }

    // Alt+Right or Alt+Left: the start key.
    void startKey(const bool later)
    {
        controller.onSelectionMoveRequested(
            later ? ChartStepDirection::Right : ChartStepDirection::Left);
    }

    // Alt+Shift+Right or Alt+Shift+Left: the end key.
    void endKey(const bool later)
    {
        controller.onChartSustainAdjustRequested(later ? 1 : -1);
    }

    // Delete.
    void deleteSelection()
    {
        controller.onSelectionDeleteRequested();
    }

    // A press on the lane's rails or a ruler chip landing on these marks.
    void press(const std::optional<std::size_t> span, const std::optional<std::size_t> none_hold)
    {
        controller.onSpanRowMarksPressed(SpanRowMarks{.span = span, .none_hold = none_hold});
    }
};

// Four strums of one chord on strings 1 and 2, frets 3 and 5, on the beats of measure 3, the last
// ringing a half note to 4:2: one derived span, [3:1, 4:2), which the holds below cut in two.
[[nodiscard]] std::vector<std::vector<common::core::ChartNote>> strummedMeasure()
{
    return {
        chordAt(at(3, 1), 3, 5),
        chordAt(at(3, 2), 3, 5),
        chordAt(at(3, 3), 3, 5),
        chordAt(at(3, 4), 3, 5, Fraction{1, 2}),
    };
}

// The strummed measure under two touching holds, [3:1, 3:3) and [3:3, 4:2).
[[nodiscard]] common::core::Chart twoHeldHalves()
{
    return chartOf(
        strummedMeasure(), {unnamedHold(at(3, 1), at(3, 3)), unnamedHold(at(3, 3), at(4, 2))});
}

} // namespace

// The span row lists every span, derived and held alike, each only where it holds the column. Down
// from the signature row lands on the span there, and past it on the position row.
TEST_CASE("The walk reaches the span row only where a span holds the column", "[core][holds]")
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

    SECTION("inside a held span the walk selects the span, its hold the one the verbs edit")
    {
        editor.walkDownAt(4.25, 1);
        CHECK(editor.state().chart_edit.selected_span == std::optional<std::size_t>{1});
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

// A none hold keeps the chord box struck inside it, so the span row and the none-hold row both
// hold that column, the none hold nearer the strings: the walk down from the ruler lands on the
// box first and on the none hold next, and the walk up from the strings meets them the other way
// round, the hold before the chord inside it, as a lane press does.
TEST_CASE("The walk reaches a none hold and the box it encloses", "[core][holds]")
{
    HoldEditor editor{chartOf({chordAt(at(1, 2), 3, 5)}, {noneHold(at(1, 1), at(2, 1))})};
    REQUIRE(editor.spanCount() == 1);

    SECTION("down from the ruler")
    {
        editor.walkDownAt(0.6, 1);
        CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});
        CHECK_FALSE(editor.selectedHold().has_value());

        editor.step(ChartStepDirection::Down);
        CHECK(editor.state().chart_edit.selected_none_hold == std::optional<std::size_t>{0});
        CHECK(editor.selectedHold() == std::optional<std::size_t>{0});
        CHECK_FALSE(editor.state().chart_edit.selected_span.has_value());
    }

    SECTION("up from a note")
    {
        editor.armAt(0.5);
        REQUIRE_FALSE(editor.state().chart_edit.selected_notes.empty());
        for (int press = 0;
             press < 8 && !editor.state().chart_edit.selected_fret_hand_position.has_value();
             ++press)
        {
            editor.step(ChartStepDirection::Up);
        }
        REQUIRE(editor.state().chart_edit.selected_fret_hand_position.has_value());

        editor.step(ChartStepDirection::Up);
        CHECK(editor.state().chart_edit.selected_none_hold == std::optional<std::size_t>{0});
        CHECK_FALSE(editor.state().chart_edit.selected_span.has_value());

        editor.step(ChartStepDirection::Up);
        CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});
        CHECK_FALSE(editor.state().chart_edit.selected_none_hold.has_value());
    }
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

// Ctrl+H over a derived span states an unnamed hold exactly over it, which leaves the spans as
// they were and the new hold selected, as one undo entry that undo takes back to the span.
TEST_CASE("Ctrl+H over a derived span holds exactly its extent", "[core][holds]")
{
    HoldEditor editor{chartOf({arpeggioIn(1), chordAt(at(3, 1), 3, 5)})};
    REQUIRE(editor.spanCount() == 2);
    editor.walkDownAt(0.75, 1);
    REQUIRE(editor.selectedSpan() == std::optional<std::size_t>{0});
    const std::size_t entries = editor.entries();

    editor.holdShape();
    INFO(editor.holdsText());
    CHECK(editor.holds() == std::vector{unnamedHold(at(1, 1), at(1, 4))});
    CHECK(editor.spanCount() == 2);
    CHECK(editor.selectedHold() == std::optional<std::size_t>{0});
    CHECK(editor.entries() == entries + 1);
    CHECK(editor.lastLabel() == "Hold Shape");

    editor.controller.onUndoRequested();
    CHECK(editor.holds().empty());
    CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});

    editor.controller.onRedoRequested();
    CHECK(editor.holds() == std::vector{unnamedHold(at(1, 1), at(1, 4))});
    CHECK(editor.selectedHold() == std::optional<std::size_t>{0});
}

// Ctrl+H over a time selection states a hold over exactly the selection: over silence it fits,
// and one ending where a ring still sounds and nothing closes is refused with the flash.
TEST_CASE("Ctrl+H over a time selection holds exactly the selection", "[core][holds]")
{
    HoldEditor editor{chartOf({{makeTestNote(at(3, 1), 1, 5, Fraction{1})}})};

    SECTION("over silence")
    {
        editor.selectTime(8.0, 4);
        editor.holdShape();
        CHECK(editor.holds() == std::vector{unnamedHold(at(5, 1), at(6, 1))});
        CHECK(editor.selectedHold() == std::optional<std::size_t>{0});
    }

    SECTION("over a whole ring")
    {
        editor.selectTime(4.0, 4);
        editor.holdShape();
        CHECK(editor.holds() == std::vector{unnamedHold(at(3, 1), at(4, 1))});
    }

    SECTION("ending in the middle of a ring")
    {
        const std::size_t entries = editor.entries();
        const std::size_t flashes = editor.view.refusal_flashes.size();
        editor.selectTime(4.0, 2);
        editor.holdShape();
        CHECK(editor.holds().empty());
        CHECK(editor.entries() == entries);
        CHECK(editor.view.refusal_flashes.size() == flashes + 1);
        CHECK(editor.state().time_selection.has_value());
    }
}

// A bar of the box at 1:1 (frets 5 and 7) and fret 12 on string 3 at 1:3, each a quarter, played
// twice, with an authored position at each 12 on fret 10, which covers it. A position is downstream
// of the notes and the holds, so an edit that leaves the 1:3 one uncovering lands, and the position
// is dropped into the edit's own undo entry and logged with every stop it missed, while the 2:3 one
// stays: a hold over [1:1, 1:4) carries the box through 1:3, and the 1:3 ring lengthened to 2:2
// carries into the 2:1 box, fronting one span at 1:3 that grips the box's frets from there.
TEST_CASE("An edit drops the authored position it leaves uncovering", "[core][holds]")
{
    const LogCapture capture;
    const auto floor_ten = [](const GridPosition& place) {
        return common::core::FretHandPosition{
            .position = place, .fret = 10, .end_fret = std::nullopt
        };
    };
    common::core::Chart chart = chartOf(
        {chordAt(at(1, 1), 5, 7),
         {makeTestNote(at(1, 3), 3, 12, Fraction{1, 4})},
         chordAt(at(2, 1), 5, 7),
         {makeTestNote(at(2, 3), 3, 12, Fraction{1, 4})}});
    chart.fret_hand_positions = {floor_ten(at(1, 3)), floor_ten(at(2, 3))};
    HoldEditor editor{chart};
    const std::size_t entries = editor.entries();
    const std::size_t flashes = editor.view.refusal_flashes.size();
    std::string dropped_line;

    SECTION("a hold carrying the box through the position")
    {
        editor.selectTime(0.0, 3);
        editor.holdShape();
        CHECK(editor.holds() == std::vector{unnamedHold(at(1, 1), at(1, 4))});
        dropped_line = "dropped at hand position 1:3 (frets 5 and 7 gripped from 1:1)";
    }

    SECTION("a ring lengthened through the next downbeat")
    {
        // The 1:3 head on string 3 (makeGeometry: 20 px a second, string 3's lane at y = 140).
        click(editor.controller, 20.0f, 140.0f);
        REQUIRE(editor.state().chart_edit.selected_notes == std::vector<std::size_t>{2});
        editor.controller.onChartSustainAdjustRequested(1);
        editor.controller.onChartSustainAdjustRequested(1);
        CHECK(editor.chart().notes[2].sustain == Fraction{3, 4});
        // The run reports what it rewrote once it ends, which a caret move does.
        editor.armAt(8.0);
        dropped_line = "dropped at hand position 1:3 (frets 5 and 7 gripped from 1:3)";
    }

    CHECK(editor.chart().fret_hand_positions == std::vector{floor_ten(at(2, 3))});
    CHECK(editor.entries() == entries + 1);
    CHECK(editor.view.refusal_flashes.size() == flashes);
    const std::string contents = capture.text();
    INFO(contents);
    CHECK(contents.find(dropped_line) != std::string::npos);
    CHECK(contents.find("hand position 2:3") == std::string::npos);

    editor.controller.onUndoRequested();
    CHECK(editor.chart().fret_hand_positions == chart.fret_hand_positions);
    CHECK(editor.chart().notes == chart.notes);
    CHECK(editor.holds().empty());
}

// Ctrl+H over selected notes runs from the first onset to the last ring's end.
TEST_CASE("Ctrl+H over a note selection holds from its onset to its ring's end", "[core][holds]")
{
    HoldEditor editor{chartOf({chordAt(at(3, 1), 3, 5)})};
    editor.armAt(4.0);
    REQUIRE_FALSE(editor.state().chart_edit.selected_notes.empty());

    editor.holdShape();
    CHECK(editor.holds() == std::vector{unnamedHold(at(3, 1), at(3, 2))});
    CHECK(editor.selectedHold() == std::optional<std::size_t>{0});
    CHECK(caretOrNull(editor.state().chart_edit) == nullptr);
}

// A bare caret covers nothing, so Ctrl+H is refused and writes nothing.
TEST_CASE("Ctrl+H on a bare caret is refused", "[core][holds]")
{
    HoldEditor editor{chartOf({chordAt(at(3, 1), 3, 5)})};
    editor.armAt(8.0);
    REQUIRE(editor.state().chart_edit.selected_notes.empty());
    const std::size_t entries = editor.entries();

    editor.holdShape();
    CHECK(editor.holds().empty());
    CHECK(editor.entries() == entries);
}

// A new hold carves its range out of every hold it overlaps: one it straddles is cropped, one it
// lies inside is split around it, one it covers is gone. A surviving part that would not fit its
// notes refuses the whole verb.
TEST_CASE("A new hold carves the holds it overlaps", "[core][holds]")
{
    SECTION("a hold it lies inside is split")
    {
        HoldEditor editor{chartOf({}, {unnamedHold(at(5, 1), at(9, 1))})};
        editor.selectTime(10.0, 4);
        editor.holdShape();
        CHECK(
            editor.holds() == std::vector{
                                  unnamedHold(at(5, 1), at(6, 1)),
                                  unnamedHold(at(6, 1), at(7, 1)),
                                  unnamedHold(at(7, 1), at(9, 1)),
                              });
        CHECK(editor.selectedHold() == std::optional<std::size_t>{1});

        editor.controller.onUndoRequested();
        CHECK(editor.holds() == std::vector{unnamedHold(at(5, 1), at(9, 1))});
    }

    SECTION("a hold it straddles is cropped")
    {
        HoldEditor editor{chartOf({}, {noneHold(at(5, 1), at(7, 1))})};
        editor.selectTime(10.0, 8);
        editor.holdShape();
        CHECK(
            editor.holds() == std::vector{
                                  noneHold(at(5, 1), at(6, 1)),
                                  unnamedHold(at(6, 1), at(8, 1)),
                              });
    }

    SECTION("a hold it covers is gone")
    {
        HoldEditor editor{chartOf({}, {unnamedHold(at(6, 1), at(7, 1))})};
        editor.selectTime(8.0, 12);
        editor.holdShape();
        CHECK(editor.holds() == std::vector{unnamedHold(at(5, 1), at(8, 1))});
    }

    // String 1 states fret 5 over the old hold, so frets 7 and 6 above it prove nothing. Cut at
    // 1:3, the part left after the new hold states fret 7 there, and fret 6 below it at 2:1 is that
    // finger lifting: a hand conflict that would crop the part. The refusal lights the stretch the
    // crop would cut, [2:1, 2:3), where the 2:1 chord sounds, not the selected [1:1, 1:3).
    SECTION("a part that would not fit refuses the verb")
    {
        const LogCapture capture;
        HoldEditor editor{chartOf(
            {chordAt(at(1, 1), 5, 7, Fraction{1, 2}),
             chordAt(at(1, 3), 7, 7, Fraction{1, 2}),
             chordAt(at(2, 1), 6, 7, Fraction{1, 2})},
            {unnamedHold(at(1, 1), at(2, 3))})};
        const std::size_t entries = editor.entries();
        const std::size_t flashes = editor.view.refusal_flashes.size();
        editor.selectTime(0.0, 2);
        editor.holdShape();
        INFO(editor.holdsText());
        CHECK(editor.holds() == std::vector{unnamedHold(at(1, 1), at(2, 3))});
        CHECK(editor.entries() == entries);
        const std::string contents = capture.text();
        INFO(contents);
        CHECK(
            contents.find(
                "shape hold 1:3 to 2:3 does not fit its notes: its shape closes at 2:1") !=
            std::string::npos);
        REQUIRE(editor.view.refusal_flashes.size() == flashes + 1);
        CHECK(editor.view.refusal_flashes.back().notes == std::vector<std::size_t>{4, 5});
        CHECK(editor.view.refusal_flashes.back().fret_hand_positions.empty());
    }
}

// The start key steps a derived span's start to the next candidate — here the 1:2 onset, a grid
// line too — converting the span to a hold, and the end key steps its end likewise; a step is
// refused only where no candidate that way leaves a hold that fits its notes.
TEST_CASE("The edge keys convert a derived span to a hold", "[core][holds]")
{
    HoldEditor editor{chartOf({arpeggioIn(1)})};
    REQUIRE(editor.spanCount() == 1);
    editor.walkDownAt(0.75, 1);
    REQUIRE(editor.selectedSpan() == std::optional<std::size_t>{0});
    const std::size_t entries = editor.entries();

    SECTION("the start key")
    {
        editor.startKey(true);
        INFO(editor.holdsText());
        CHECK(editor.holds() == std::vector{unnamedHold(at(1, 2), at(1, 4))});
        CHECK(editor.selectedHold() == std::optional<std::size_t>{0});
        CHECK(editor.entries() == entries + 1);
        CHECK(editor.lastLabel() == "Move Hold Start");

        editor.controller.onUndoRequested();
        CHECK(editor.holds().empty());
        CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});
    }

    SECTION("a run stepped back to where it began leaves the span derived")
    {
        editor.startKey(true);
        REQUIRE(editor.selectedHold() == std::optional<std::size_t>{0});
        editor.startKey(false);
        INFO(editor.holdsText());
        CHECK(editor.holds().empty());
        CHECK(editor.entries() == entries);
        CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});
    }

    SECTION("the end key, refused where no earlier candidate lets the hold fit its notes")
    {
        // Every candidate earlier than the span's close falls inside its rings, so the hold would
        // run on past each of them: none fits, and the press is refused with the NEAREST
        // candidate's own refusal — the hold stated to 1:3 closes at 1:4 — lighting the stretch it
        // overruns, [1:3, 1:4), where the third note is struck and the first two still ring.
        const std::size_t flashes = editor.view.refusal_flashes.size();
        const LogCapture capture;
        editor.endKey(false);
        const std::string contents = capture.text();
        INFO(contents);
        CHECK(
            contents.find(
                "shape hold 1:1 to 1:3 does not fit its notes: its shape closes at 1:4") !=
            std::string::npos);
        CHECK(editor.holds().empty());
        CHECK(editor.entries() == entries);
        REQUIRE(editor.view.refusal_flashes.size() == flashes + 1);
        CHECK(editor.view.refusal_flashes.back().notes == std::vector<std::size_t>{0, 1, 2});
        CHECK(editor.view.refusal_flashes.back().spans == std::vector<std::size_t>{0});
        CHECK(editor.view.refusal_flashes.back().fret_hand_positions.empty());
        CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});
    }
}

// An edge key lands on the nearest candidate where the hold fits, skipping the candidates the gate
// refuses. A box whose statement a re-pick renews cannot end at a grace note's onset inside that
// renewed ring, where nothing ends and the span runs on, so the end key stepping earlier passes it
// to the boundary where the re-picked string's first ring ends, and the run stepping back passes it
// again to land where it began.
TEST_CASE("The end key skips boundaries where the hold would not fit", "[core][holds]")
{
    // Strings 1 and 2 struck at 1:1, string 1 for a beat and string 2 to 1:4; string 1 re-picked at
    // 1:2 to 1:4; a grace note on string 4 at 1:3, an eighth long: one span, [1:1, 1:3+1/2).
    HoldEditor editor{chartOf({
        {makeTestNote(at(1, 1), 1, 5, Fraction{1, 4}),
         makeTestNote(at(1, 1), 2, 7, Fraction{3, 4})},
        {makeTestNote(at(1, 2), 1, 5, Fraction{1, 2})},
        {makeTestNote(at(1, 3), 4, 3, Fraction{1, 8})},
    })};
    REQUIRE(editor.spanCount() == 1);
    editor.walkDownAt(0.25, 1);
    REQUIRE(editor.selectedSpan() == std::optional<std::size_t>{0});
    REQUIRE(editor.publishedSpan() != nullptr);
    REQUIRE(editor.publishedSpan()->close_seconds == Catch::Approx(1.25));
    const std::size_t entries = editor.entries();

    editor.endKey(false);
    INFO(editor.holdsText());
    CHECK(editor.holds() == std::vector{unnamedHold(at(1, 1), at(1, 2))});
    CHECK(editor.selectedHold() == std::optional<std::size_t>{0});
    CHECK(editor.entries() == entries + 1);

    editor.endKey(true);
    INFO(editor.holdsText());
    CHECK(editor.holds().empty());
    CHECK(editor.entries() == entries);
    CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});
}

// The end key moved into the next hold pushes that hold's start along with it, and a step back in
// the same run restores it, the run being one undo entry; a press whose every candidate would push
// the neighbour to nothing is refused. A step back in a later run moves the edge alone, leaving a
// gap.
TEST_CASE("The end key pushes the next hold and a step back restores it", "[core][holds]")
{
    HoldEditor editor{twoHeldHalves()};
    editor.walkDownAt(4.25, 1);
    REQUIRE(editor.selectedHold() == std::optional<std::size_t>{0});
    const std::size_t entries = editor.entries();

    editor.endKey(true);
    INFO(editor.holdsText());
    CHECK(
        editor.holds() ==
        std::vector{unnamedHold(at(3, 1), at(3, 4)), unnamedHold(at(3, 4), at(4, 2))});
    CHECK(editor.selectedHold() == std::optional<std::size_t>{0});

    SECTION("a step back in the run restores the pushed hold")
    {
        editor.endKey(false);
        CHECK(editor.holds() == twoHeldHalves().shape_holds);
        CHECK(editor.entries() == entries);
    }

    SECTION("a neighbour pushed to nothing at every later candidate refuses the press")
    {
        // The 4:1 grid line is the nearer candidate: the 3:4 chord ringing to 4:2 crosses it, a
        // member the pushed neighbour carries, so the hold fits there.
        editor.endKey(true);
        CHECK(
            editor.holds() ==
            std::vector{unnamedHold(at(3, 1), at(4, 1)), unnamedHold(at(4, 1), at(4, 2))});

        // Every candidate later than 4:1 lies at or past the neighbour's end, so each would push it
        // to nothing: none fits, and the press is refused with the nearest candidate's own
        // refusal, which names the neighbour — as it stood when the run began, since every press
        // replays from the run's start chart. The neighbour no longer starts there, so what lights
        // is the selected hold the press was on.
        const std::size_t flashes = editor.view.refusal_flashes.size();
        const LogCapture capture;
        editor.endKey(true);
        const std::string contents = capture.text();
        INFO(contents);
        CHECK(
            contents.find("the edge would push shape hold 3:3 to 4:2 to nothing") !=
            std::string::npos);
        REQUIRE(editor.view.refusal_flashes.size() == flashes + 1);
        CHECK(editor.view.refusal_flashes.back().spans == std::vector<std::size_t>{0});
        CHECK(editor.view.refusal_flashes.back().notes.empty());
        CHECK(
            editor.holds() ==
            std::vector{unnamedHold(at(3, 1), at(4, 1)), unnamedHold(at(4, 1), at(4, 2))});
        CHECK(editor.entries() == entries + 1);
    }

    SECTION("once the run ends, a step back leaves the pushed hold where it is")
    {
        editor.controller.onRowObjectStepRequested(true, false);
        REQUIRE(editor.selectedHold() == std::optional<std::size_t>{1});
        editor.controller.onRowObjectStepRequested(false, false);
        REQUIRE(editor.selectedHold() == std::optional<std::size_t>{0});
        editor.endKey(false);
        INFO(editor.holdsText());
        CHECK(
            editor.holds() ==
            std::vector{unnamedHold(at(3, 1), at(3, 3)), unnamedHold(at(3, 4), at(4, 2))});
        CHECK(editor.entries() == entries + 2);

        editor.controller.onUndoRequested();
        CHECK(
            editor.holds() ==
            std::vector{unnamedHold(at(3, 1), at(3, 4)), unnamedHold(at(3, 4), at(4, 2))});
        editor.controller.onUndoRequested();
        CHECK(editor.holds() == twoHeldHalves().shape_holds);
    }
}

// The start key moved into the previous hold pushes that hold's end back with it. A press whose
// every candidate would push the neighbour to nothing is refused with the nearest one's refusal,
// which names the neighbour as the run began: 3:1, where the neighbour starts, is the nearest, and
// every grid line before it pushes the neighbour out as well. The flash lights the neighbour's span
// and the selected one the press was on.
TEST_CASE("The start key pushes the previous hold", "[core][holds]")
{
    HoldEditor editor{twoHeldHalves()};
    editor.walkDownAt(5.25, 1);
    REQUIRE(editor.selectedHold() == std::optional<std::size_t>{1});

    editor.startKey(false);
    INFO(editor.holdsText());
    CHECK(
        editor.holds() ==
        std::vector{unnamedHold(at(3, 1), at(3, 2)), unnamedHold(at(3, 2), at(4, 2))});
    CHECK(editor.selectedHold() == std::optional<std::size_t>{1});

    const std::size_t flashes = editor.view.refusal_flashes.size();
    const LogCapture capture;
    editor.startKey(false);
    CHECK(
        editor.holds() ==
        std::vector{unnamedHold(at(3, 1), at(3, 2)), unnamedHold(at(3, 2), at(4, 2))});
    const std::string contents = capture.text();
    INFO(contents);
    CHECK(
        contents.find("the edge would push shape hold 3:1 to 3:3 to nothing") != std::string::npos);
    REQUIRE(editor.view.refusal_flashes.size() == flashes + 1);
    CHECK(editor.view.refusal_flashes.back().spans == std::vector<std::size_t>{0, 1});
    CHECK(editor.view.refusal_flashes.back().notes.empty());

    editor.controller.onUndoRequested();
    CHECK(editor.holds() == twoHeldHalves().shape_holds);
}

// A single press pushing the next hold to nothing at every later candidate flashes the moved hold
// and the neighbour standing in its way, as the spans they state rather than the notes under them.
TEST_CASE("A push to nothing flashes the moved hold and the neighbour", "[core][holds]")
{
    // Two E-string notes under the first hold, and a short hold over silence touching its end:
    // every candidate later than 1:3 lies at or past the neighbour's end at 1:4.
    HoldEditor editor{chartOf(
        {{makeTestNote(at(1, 1), 1, 5, Fraction{1, 4}),
          makeTestNote(at(1, 2), 1, 7, Fraction{1, 4})}},
        {unnamedHold(at(1, 1), at(1, 3)), unnamedHold(at(1, 3), at(1, 4))})};
    REQUIRE(editor.spanCount() == 2);
    editor.press(0, std::nullopt);
    REQUIRE(editor.selectedHold() == std::optional<std::size_t>{0});
    const std::size_t entries = editor.entries();
    const std::size_t flashes = editor.view.refusal_flashes.size();

    const LogCapture capture;
    editor.endKey(true);
    const std::string contents = capture.text();
    INFO(contents);
    CHECK(
        contents.find("the edge would push shape hold 1:3 to 1:4 to nothing") != std::string::npos);
    CHECK(editor.entries() == entries);
    REQUIRE(editor.view.refusal_flashes.size() == flashes + 1);
    CHECK(editor.view.refusal_flashes.back().spans == std::vector<std::size_t>{0, 1});
    CHECK(editor.view.refusal_flashes.back().notes.empty());
}

// Where no note lies before a span, its start steps back over the silence a grid line at a time,
// each press accepted: the hold opens over an empty grip and the strike at 2:1 founds the one span,
// which fronts at the hold's start. The run is one undo entry.
TEST_CASE("The start key steps over silence by grid lines", "[core][holds]")
{
    HoldEditor editor{chartOf({chordAt(at(2, 1), 5, 7)})};
    editor.walkDownAt(2.25, 1);
    REQUIRE(editor.selectedSpan() == std::optional<std::size_t>{0});
    const std::size_t entries = editor.entries();

    editor.startKey(false);
    INFO(editor.holdsText());
    CHECK(editor.holds() == std::vector{unnamedHold(at(1, 4), at(2, 2))});

    editor.startKey(false);
    CHECK(editor.holds() == std::vector{unnamedHold(at(1, 3), at(2, 2))});
    CHECK(editor.entries() == entries + 1);
    REQUIRE(editor.spanCount() == 1);
    REQUIRE(editor.publishedSpan() != nullptr);
    CHECK(editor.publishedSpan()->held);
    CHECK(editor.publishedSpan()->start_seconds == Catch::Approx(1.0));
}

// The 2:1 box's start steps back to 1:4, where the 1:3 note's ring ends on a grid line: the hold
// opens over the silence after that ring, so the authored position at 1:3 still stands
// and the derived window for the box's frets 5 and 7 fronts at the hold's start.
TEST_CASE("The start key moves the derived window with the hold", "[core][holds]")
{
    common::core::Chart chart = chartOf(
        {chordAt(at(1, 1), 5, 7),
         {makeTestNote(at(1, 3), 3, 12, Fraction{1, 4})},
         chordAt(at(2, 1), 5, 7)});
    chart.fret_hand_positions = {
        common::core::FretHandPosition{.position = at(1, 3), .fret = 10, .end_fret = std::nullopt}
    };
    HoldEditor editor{std::move(chart)};
    editor.walkDownAt(2.25, 1);
    REQUIRE(editor.selectedSpan().has_value());
    const std::size_t entries = editor.entries();

    editor.startKey(false);
    INFO(editor.holdsText());
    CHECK(editor.holds() == std::vector{unnamedHold(at(1, 4), at(2, 2))});
    CHECK(editor.entries() == entries + 1);

    REQUIRE(editor.state().tab != nullptr);
    const std::vector<common::core::FhpViewState>& chips = editor.state().tab->fret_hand_positions;
    const auto chip_at = [&chips](const GridPosition& place) -> const common::core::FhpViewState* {
        for (const common::core::FhpViewState& chip : chips)
        {
            if (chip.position == place)
            {
                return &chip;
            }
        }
        return nullptr;
    };
    const common::core::FhpViewState* const derived = chip_at(at(1, 4));
    REQUIRE(derived != nullptr);
    CHECK(derived->fret == 5);
    CHECK(derived->authored == common::core::FhpAuthored::Nothing);
    const common::core::FhpViewState* const authored = chip_at(at(1, 3));
    REQUIRE(authored != nullptr);
    CHECK(authored->authored == common::core::FhpAuthored::Fret);
}

// A start stepped into the ring of the figure before it lands on the grid line there, not only at
// an onset: the 1:3 box on strings 3 and 4 ringing to 2:1 crosses the hold's start at 1:4, so it
// is a member the hold carries, and the derived span before it closes where the hold begins.
TEST_CASE("The start key steps into a ring the hold then carries", "[core][holds]")
{
    HoldEditor editor{chartOf(
        {{makeTestNote(at(1, 3), 3, 7, Fraction{1, 2}),
          makeTestNote(at(1, 3), 4, 7, Fraction{1, 2})},
         chordAt(at(2, 1), 5, 7)})};
    editor.walkDownAt(2.25, 1);
    REQUIRE(editor.selectedSpan().has_value());

    editor.startKey(false);
    INFO(editor.holdsText());
    CHECK(editor.holds() == std::vector{unnamedHold(at(1, 4), at(2, 2))});
    REQUIRE(editor.state().tab != nullptr);
    const std::vector<common::core::ShapeViewState>& spans = editor.state().tab->shapes;
    REQUIRE(spans.size() == 2);
    CHECK_FALSE(spans[0].held);
    CHECK(spans[0].close_seconds == Catch::Approx(1.5));
    CHECK(spans[1].held);
    CHECK(spans[1].start_seconds == Catch::Approx(1.5));
    CHECK(
        std::ranges::any_of(spans[1].strings, [](const common::core::ShapeStringViewState& entry) {
            return entry.string == 3;
        }));
}

// String 1 states fret 7 at 1:3 and fret 6 at 2:1, a finger lifting: a hold opening at 1:4 or at
// 1:3 carries the fret 7 into that release and is refused, so the start key passes both — the grid
// line and the onset alike — and lands at 1:2, where the hold carries the 1:1 box's fret 5 and the
// frets above it prove nothing.
TEST_CASE("The start key skips candidates where the hand would release a stop", "[core][holds]")
{
    HoldEditor editor{chartOf(
        {chordAt(at(1, 1), 5, 7, Fraction{1, 2}),
         chordAt(at(1, 3), 7, 7, Fraction{1, 2}),
         chordAt(at(2, 1), 6, 7, Fraction{1, 2})},
        {unnamedHold(at(2, 1), at(2, 3))})};
    editor.walkDownAt(2.25, 1);
    REQUIRE(editor.selectedHold() == std::optional<std::size_t>{0});

    editor.startKey(false);
    INFO(editor.holdsText());
    CHECK(editor.holds() == std::vector{unnamedHold(at(1, 2), at(2, 3))});
}

// The grid line the edge keys step to is the placement quantum's: the grid with snap on, and with
// snap off the next tick, 1/960 of a beat.
TEST_CASE("The edge keys step by the placement quantum", "[core][holds]")
{
    HoldEditor editor{chartOf({chordAt(at(2, 1), 5, 7)})};
    editor.walkDownAt(2.25, 1);
    REQUIRE(editor.selectedSpan() == std::optional<std::size_t>{0});

    SECTION("with snap on, a beat")
    {
        editor.startKey(false);
        INFO(editor.holdsText());
        CHECK(editor.holds() == std::vector{unnamedHold(at(1, 4), at(2, 2))});
    }

    SECTION("with snap off, a tick")
    {
        turnGridSnapOff(editor.controller);
        editor.startKey(false);
        INFO(editor.holdsText());
        CHECK(editor.holds() == std::vector{unnamedHold(at(1, 4, Fraction{959, 960}), at(2, 2))});
    }

    SECTION("with snap off, the end key a tick")
    {
        turnGridSnapOff(editor.controller);
        editor.endKey(true);
        INFO(editor.holdsText());
        CHECK(editor.holds() == std::vector{unnamedHold(at(2, 1), at(2, 2, Fraction{1, 960}))});
    }
}

// A coarse grid never steps past a note boundary: with a whole-note grid the box's end, at 1:2,
// steps to the 1:3 onset, nearer than the 2:1 grid line.
TEST_CASE("A coarse grid never hides a note boundary", "[core][holds]")
{
    HoldEditor editor{chartOf(
        {chordAt(at(1, 1), 5, 7), {makeTestNote(at(1, 3), 3, 7, Fraction{1, 4})}})};
    editor.controller.onGridNoteValueChangeRequested(Fraction{1, 1});
    editor.walkDownAt(0.25, 1);
    REQUIRE(editor.selectedSpan() == std::optional<std::size_t>{0});
    REQUIRE(editor.publishedSpan() != nullptr);
    REQUIRE(editor.publishedSpan()->close_seconds == Catch::Approx(0.5));

    editor.endKey(true);
    INFO(editor.holdsText());
    CHECK(editor.holds() == std::vector{unnamedHold(at(1, 1), at(1, 3))});
}

// A refused grid line never blocks an accepted note boundary beyond it: the box re-struck at 1:2
// rings to 1:3 and a half, so a hold ending at the 1:3 grid line stops in the middle of that ring
// and is refused, and the end key passes it to land where the ring ends. With snap off the first
// tick past 1:2 is refused the same way and falls through to the same boundary.
TEST_CASE("The end key skips a refused grid line to the boundary beyond", "[core][holds]")
{
    HoldEditor editor{chartOf(
        {chordAt(at(1, 1), 5, 7), chordAt(at(1, 2), 5, 7, Fraction{3, 8})},
        {unnamedHold(at(1, 1), at(1, 2))})};
    editor.walkDownAt(0.25, 1);
    REQUIRE(editor.selectedHold() == std::optional<std::size_t>{0});

    SECTION("with snap on")
    {
        editor.endKey(true);
        INFO(editor.holdsText());
        CHECK(editor.holds() == std::vector{unnamedHold(at(1, 1), at(1, 3, Fraction{1, 2}))});
    }

    SECTION("with snap off")
    {
        turnGridSnapOff(editor.controller);
        editor.endKey(true);
        INFO(editor.holdsText());
        CHECK(editor.holds() == std::vector{unnamedHold(at(1, 1), at(1, 3, Fraction{1, 2}))});
    }
}

// Delete on a held span removes its hold and the derivation takes its stretch back, the span it
// leaves derived there staying selected; undo restores the hold and selects its span.
TEST_CASE("Delete removes a selected span's hold", "[core][holds]")
{
    HoldEditor editor{twoHeldHalves()};
    editor.walkDownAt(4.25, 1);
    REQUIRE(editor.selectedHold() == std::optional<std::size_t>{0});
    const std::size_t entries = editor.entries();

    editor.deleteSelection();
    INFO(editor.holdsText());
    CHECK(editor.holds() == std::vector{unnamedHold(at(3, 3), at(4, 2))});
    CHECK(editor.entries() == entries + 1);
    CHECK(editor.lastLabel() == "Delete Hold");
    CHECK(editor.spanCount() == 2);
    CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});

    editor.controller.onUndoRequested();
    CHECK(editor.holds() == twoHeldHalves().shape_holds);
    CHECK(editor.selectedHold() == std::optional<std::size_t>{0});
}

// A hold over silence states the one span its notes never would, so deleting it leaves no span at
// its start and none before it: nothing is left selected, rather than a span that is not there.
TEST_CASE("Delete on a hold over silence leaves nothing selected", "[core][holds]")
{
    HoldEditor editor{chartOf({}, {unnamedHold(at(5, 1), at(6, 1))})};
    REQUIRE(editor.spanCount() == 1);
    editor.press(0, std::nullopt);
    REQUIRE(editor.selectedHold() == std::optional<std::size_t>{0});

    editor.deleteSelection();
    CHECK(editor.holds().empty());
    CHECK(editor.spanCount() == 0);
    CHECK_FALSE(editor.state().selection_present);
}

// The second hold's start is where the first hold's span closes, so removing it would leave the
// first ending in the middle of the chord's ring, where nothing closes: the delete is refused.
TEST_CASE("Delete refuses a hold whose removal unfits its neighbour", "[core][holds]")
{
    const LogCapture capture;
    const std::vector<ShapeHold> holds{
        unnamedHold(at(3, 1), at(3, 3)), unnamedHold(at(3, 3), at(4, 1))
    };
    HoldEditor editor{chartOf({chordAt(at(3, 1), 3, 5, Fraction{1})}, holds)};
    editor.walkDownAt(5.25, 1);
    REQUIRE(editor.selectedHold() == std::optional<std::size_t>{1});
    const std::size_t entries = editor.entries();

    editor.deleteSelection();
    CHECK(editor.holds() == holds);
    CHECK(editor.entries() == entries);
    CHECK(editor.selectedHold() == std::optional<std::size_t>{1});
    const std::string contents = capture.text();
    INFO(contents);
    CHECK(contents.find("shape hold 3:1 to 3:3 does not fit its notes") != std::string::npos);
}

// Delete on a derived span adds a none hold over it, left selected, whatever the span's class: an
// arpeggio inside it is hidden, and a chord box the notes derive there still stands. A second
// Delete removes the none hold and the derivation returns.
TEST_CASE("Delete adds a none hold over a derived arpeggio or chord box", "[core][holds]")
{
    HoldEditor editor{chartOf({arpeggioIn(1), chordAt(at(3, 1), 3, 5)})};
    REQUIRE(editor.spanCount() == 2);
    const std::size_t entries = editor.entries();

    SECTION("an arpeggio")
    {
        editor.walkDownAt(0.75, 1);
        REQUIRE(editor.selectedSpan() == std::optional<std::size_t>{0});
        editor.deleteSelection();
        INFO(editor.holdsText());
        CHECK(editor.holds() == std::vector{noneHold(at(1, 1), at(1, 4))});
        CHECK(editor.spanCount() == 1);
        CHECK(editor.selectedHold() == std::optional<std::size_t>{0});
        CHECK(editor.lastLabel() == "Hold None");

        // The none hold is a hold like any other: Delete removes it, and the arpeggio returns
        // selected, one level down.
        editor.deleteSelection();
        CHECK(editor.holds().empty());
        CHECK(editor.spanCount() == 2);
        CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});

        editor.controller.onUndoRequested();
        editor.controller.onUndoRequested();
        CHECK(editor.holds().empty());
        CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});
    }

    SECTION("a chord box")
    {
        editor.walkDownAt(4.25, 1);
        REQUIRE(editor.selectedSpan() == std::optional<std::size_t>{1});
        editor.deleteSelection();
        INFO(editor.holdsText());
        CHECK(editor.holds() == std::vector{noneHold(at(3, 1), at(3, 2))});
        CHECK(editor.spanCount() == 2);
        CHECK(editor.state().chart_edit.selected_none_hold == std::optional<std::size_t>{0});
        CHECK(editor.entries() == entries + 1);
        CHECK(editor.lastLabel() == "Hold None");

        editor.deleteSelection();
        CHECK(editor.holds().empty());
        CHECK(editor.spanCount() == 2);
    }
}

// Delete steps an authored chord down one level per press: first to the chord the notes derive,
// then to a none hold over it with the chord still standing, then back to the derivation.
TEST_CASE(
    "Delete steps an authored chord down to its derived chord, then a none hold", "[core][holds]")
{
    HoldEditor editor{chartOf({chordAt(at(3, 1), 3, 5)}, {unnamedHold(at(3, 1), at(3, 2))})};
    editor.walkDownAt(4.25, 1);
    REQUIRE(editor.selectedHold() == std::optional<std::size_t>{0});

    editor.deleteSelection();
    INFO(editor.holdsText());
    CHECK(editor.holds().empty());
    CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});

    editor.deleteSelection();
    CHECK(editor.holds() == std::vector{noneHold(at(3, 1), at(3, 2))});
    CHECK(editor.spanCount() == 1);
    CHECK(editor.selectedHold() == std::optional<std::size_t>{0});

    editor.deleteSelection();
    CHECK(editor.holds().empty());
    CHECK(editor.spanCount() == 1);
}

// None holds that meet are one: Delete on an arpeggio beside a none hold grows that hold over it
// and selects the grown hold, and Delete on it gives the whole stretch back to the derivation.
TEST_CASE(
    "Delete beside a none hold grows it, and deleting it restores the stretch", "[core][holds]")
{
    HoldEditor editor{chartOf({arpeggioIn(1), arpeggioIn(2)}, {noneHold(at(1, 1), at(2, 1))})};
    REQUIRE(editor.spanCount() == 1);
    editor.walkDownAt(2.75, 1);
    REQUIRE(editor.selectedSpan() == std::optional<std::size_t>{0});

    editor.deleteSelection();
    INFO(editor.holdsText());
    CHECK(editor.holds() == std::vector{noneHold(at(1, 1), at(2, 4))});
    CHECK(editor.spanCount() == 0);
    CHECK(editor.state().chart_edit.selected_none_hold == std::optional<std::size_t>{0});

    editor.deleteSelection();
    CHECK(editor.holds().empty());
    CHECK(editor.spanCount() == 2);
}

// A chord standing inside a none hold has nothing to add: the none hold over it merges back into
// the one already there, so Delete on it changes nothing and records nothing.
TEST_CASE("Delete on a chord inside a none hold changes nothing", "[core][holds]")
{
    HoldEditor editor{chartOf({chordAt(at(1, 2), 3, 5)}, {noneHold(at(1, 1), at(2, 1))})};
    editor.walkDownAt(0.6, 1);
    REQUIRE(editor.selectedSpan() == std::optional<std::size_t>{0});
    const std::size_t entries = editor.entries();

    editor.deleteSelection();
    CHECK(editor.holds() == std::vector{noneHold(at(1, 1), at(2, 1))});
    CHECK(editor.entries() == entries);
}

// A press on a span's rails or chip selects that span, held or derived, by its index in the
// projection, and a press on a none hold's hatch selects the hold by its index among the none
// holds; neither seeks, and an index naming nothing selects nothing.
TEST_CASE("A span press and a none-hold press select what they name", "[core][holds]")
{
    // A derived arpeggio over measure 1, a held box at 3:1, and a none hold over measure 5.
    HoldEditor editor{chartOf(
        {arpeggioIn(1), chordAt(at(3, 1), 3, 5)},
        {unnamedHold(at(3, 1), at(3, 2)), noneHold(at(5, 1), at(6, 1))})};
    REQUIRE(editor.spanCount() == 2);
    editor.park(12.5);

    editor.press(0, std::nullopt);
    CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});
    CHECK_FALSE(editor.selectedHold().has_value());

    editor.press(1, std::nullopt);
    CHECK(editor.state().chart_edit.selected_span == std::optional<std::size_t>{1});
    CHECK(editor.selectedHold() == std::optional<std::size_t>{0});

    editor.press(std::nullopt, 0);
    CHECK(editor.state().chart_edit.selected_none_hold == std::optional<std::size_t>{0});
    CHECK_FALSE(editor.state().chart_edit.selected_span.has_value());
    CHECK(editor.selectedHold() == std::optional<std::size_t>{1});
    CHECK(editor.transport.position().seconds == Catch::Approx(12.5));

    editor.press(2, std::nullopt);
    CHECK(editor.state().chart_edit.selected_none_hold == std::optional<std::size_t>{0});
}

// A press landing on a none hold and the chord standing inside it selects the none hold, the one
// the walk up from the strings meets first, as the hold jump does.
TEST_CASE("A press on a none hold and the chord inside it selects the hold", "[core][holds]")
{
    HoldEditor editor{chartOf({chordAt(at(1, 2), 3, 5)}, {noneHold(at(1, 1), at(2, 1))})};
    REQUIRE(editor.spanCount() == 1);

    editor.press(0, 0);
    CHECK(editor.state().chart_edit.selected_none_hold == std::optional<std::size_t>{0});
    CHECK_FALSE(editor.state().chart_edit.selected_span.has_value());
}

// Ctrl+H on a none hold states an unnamed hold over it, restoring the shape the Delete denied, and
// leaves the span it states selected.
TEST_CASE("Ctrl+H on a none hold restores the arpeggio", "[core][holds]")
{
    HoldEditor editor{chartOf({arpeggioIn(1)}, {noneHold(at(1, 1), at(1, 4))})};
    REQUIRE(editor.spanCount() == 0);
    editor.press(std::nullopt, 0);
    REQUIRE(editor.selectedHold() == std::optional<std::size_t>{0});

    editor.holdShape();
    INFO(editor.holdsText());
    CHECK(editor.holds() == std::vector{unnamedHold(at(1, 1), at(1, 4))});
    CHECK(editor.spanCount() == 1);
    CHECK(editor.state().chart_edit.selected_span == std::optional<std::size_t>{0});
    CHECK(editor.selectedHold() == std::optional<std::size_t>{0});
}

// A lane press over a chord standing inside a none hold selects the hold, so the chord's ruler chip
// is its pointer path: the chip's press selects the span by its index, the hold left untouched.
TEST_CASE("A span press selects a chord standing inside a none hold", "[core][holds]")
{
    HoldEditor editor{chartOf({chordAt(at(1, 2), 3, 5)}, {noneHold(at(1, 1), at(2, 1))})};
    REQUIRE(editor.spanCount() == 1);

    editor.press(0, std::nullopt);
    CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});
    CHECK_FALSE(editor.state().chart_edit.selected_none_hold.has_value());
    CHECK(editor.holds() == std::vector{noneHold(at(1, 1), at(2, 1))});
}

// Ctrl+Shift+H selects the hold holding the cursor, the row the walk up from the strings meets
// first: the none hold over a chord standing in it, else the span there, and nothing in an empty
// stretch.
TEST_CASE("The hold jump selects the none hold before the chord inside it", "[core][holds]")
{
    HoldEditor editor{chartOf(
        {chordAt(at(1, 2), 3, 5), chordAt(at(3, 1), 3, 5)}, {noneHold(at(1, 1), at(2, 1))})};
    REQUIRE(editor.spanCount() == 2);
    const auto jump = [&editor](const double seconds) {
        editor.park(seconds);
        editor.controller.onFocusRowJumpRequested(FocusRowJump::Hold);
    };

    SECTION("inside a none hold over a chord")
    {
        jump(0.6);
        CHECK(editor.state().chart_edit.selected_none_hold == std::optional<std::size_t>{0});
        CHECK_FALSE(editor.state().chart_edit.selected_span.has_value());
    }

    SECTION("on a bare span")
    {
        jump(4.25);
        CHECK(editor.selectedSpan() == std::optional<std::size_t>{1});
    }

    SECTION("in an empty stretch")
    {
        jump(6.5);
        CHECK_FALSE(editor.state().chart_edit.selected_none_hold.has_value());
        CHECK_FALSE(editor.state().chart_edit.selected_span.has_value());
        CHECK_FALSE(editor.state().selection_present);
    }
}

// The hold picker offers only the choices that change the extent, each with its tooltip: Auto is
// left out where no hold touches it, Shape where an unnamed hold already states it, and None
// where a none hold already covers it.
TEST_CASE("The hold picker leaves out the choices that would change nothing", "[core][holds]")
{
    HoldEditor editor{chartOf(
        {arpeggioIn(1), chordAt(at(3, 1), 3, 5)},
        {unnamedHold(at(3, 1), at(3, 2)), noneHold(at(5, 1), at(6, 1))})};

    SECTION("a derived span")
    {
        editor.walkDownAt(0.75, 1);
        editor.controller.onHoldRequested();
        CHECK(editor.offeredHoldChoices() == std::vector<std::string>{"Shape", "None"});
        const ChartHoldPicker& picker = editor.view.shown_hold_pickers.back();
        CHECK(picker.choices[0].tooltip == "Hold one shape over the whole range");
        CHECK(picker.choices[1].tooltip == "Hide arpeggios here; chords still stand");
    }

    SECTION("a held span")
    {
        editor.walkDownAt(4.25, 1);
        editor.controller.onHoldRequested();
        CHECK(editor.offeredHoldChoices() == std::vector<std::string>{"None", "Auto"});
        CHECK(
            editor.view.shown_hold_pickers.back().choices[1].tooltip ==
            "Clear holds here and let the notes decide the spans");
    }

    SECTION("a none hold")
    {
        editor.press(std::nullopt, 0);
        editor.controller.onHoldRequested();
        CHECK(editor.offeredHoldChoices() == std::vector<std::string>{"Shape", "Auto"});
    }

    SECTION("a time selection over silence")
    {
        editor.selectTime(6.0, 4);
        editor.controller.onHoldRequested();
        CHECK(editor.offeredHoldChoices() == std::vector<std::string>{"Shape", "None"});
    }
}

// None over a time selection holding a chord box: the none hold covers the whole selection and the
// box the notes derive inside it still stands, the none hold left selected.
TEST_CASE("Ctrl+H None over a time selection keeps the chord inside it", "[core][holds]")
{
    HoldEditor editor{chartOf({chordAt(at(3, 1), 3, 5)})};
    editor.selectTime(4.0, 4);
    editor.chooseHold(common::core::NoneGrip{});
    INFO(editor.holdsText());
    CHECK(editor.holds() == std::vector{noneHold(at(3, 1), at(4, 1))});
    CHECK(editor.spanCount() == 1);
    CHECK(editor.state().chart_edit.selected_none_hold == std::optional<std::size_t>{0});
    CHECK(editor.lastLabel() == "Hold None");
}

// Auto over the middle of a merged none hold carves only that stretch out: the hold keeps its
// pieces on either side, and the arpeggio in the middle is derived fresh.
TEST_CASE("Ctrl+H Auto over the middle of a none hold leaves its two ends", "[core][holds]")
{
    HoldEditor editor{chartOf(
        {arpeggioIn(1), arpeggioIn(2), arpeggioIn(3)}, {noneHold(at(1, 1), at(4, 1))})};
    REQUIRE(editor.spanCount() == 0);
    editor.selectTime(2.0, 4);
    editor.chooseHold(std::nullopt);
    INFO(editor.holdsText());
    CHECK(
        editor.holds() == std::vector{noneHold(at(1, 1), at(2, 1)), noneHold(at(3, 1), at(4, 1))});
    CHECK(editor.spanCount() == 1);
    CHECK(editor.lastLabel() == "Hold Auto");
}

// Auto over a held span takes its hold away, and the span the notes derive there stays selected.
TEST_CASE("Ctrl+H Auto over a held span gives it back to the derivation", "[core][holds]")
{
    HoldEditor editor{chartOf({chordAt(at(3, 1), 3, 5)}, {unnamedHold(at(3, 1), at(3, 2))})};
    editor.walkDownAt(4.25, 1);
    REQUIRE(editor.selectedHold() == std::optional<std::size_t>{0});
    editor.chooseHold(std::nullopt);
    CHECK(editor.holds().empty());
    CHECK(editor.spanCount() == 1);
    CHECK(editor.selectedSpan() == std::optional<std::size_t>{0});
}

} // namespace rock_hero::editor::core
