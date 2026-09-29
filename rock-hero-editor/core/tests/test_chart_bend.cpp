#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/ui/tab/tab_layout_manifest.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The glide chart's lane at 20 px per second: its onset (2.0s), a covered slot two beats in (3.0s,
// on the travel toward the junction), the junction (4.0s), the ring's end (6.0s), and an empty slot
// past it (8.0s), all on string 3.
constexpr float g_onset_x{40.0f};
constexpr float g_covered_x{60.0f};
constexpr float g_ring_end_x{120.0f};
constexpr float g_empty_x{160.0f};
constexpr float g_string_3_y{140.0f};

// Rest to three whole steps in quarter steps: thirteen amounts.
constexpr std::size_t g_amount_rows{13};

// The glide's junction (4.0s).
constexpr float g_junction_x{80.0f};

// The chart-editing state the controller last published. The pointer is bound ONCE and the guard
// rides that name, which the CI-only optional-access checker can follow where a REQUIRE is opaque.
[[nodiscard]] ChartEditViewState chartEdit(const FakeEditorView& view)
{
    const EditorViewState* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    if (state == nullptr)
    {
        return {};
    }
    return state->chart_edit;
}

// The armed caret as the controller last published it.
[[nodiscard]] ChartCaretViewState caretOf(const FakeEditorView& view)
{
    const std::optional<ChartCaretViewState> caret = chartEdit(view).caret;
    REQUIRE(caret.has_value());
    if (!caret.has_value())
    {
        return {};
    }
    return *caret;
}

// The onset chip's click box of the one note in `fixture`'s chart, from the published projection.
[[nodiscard]] std::optional<common::ui::TabLayoutRect> onsetChipOf(
    const FakeEditorView& view, const std::size_t index)
{
    const EditorViewState* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    if (state == nullptr)
    {
        return std::nullopt;
    }
    const std::shared_ptr<const common::core::ChartViewState>& tab = state->tab;
    REQUIRE(tab != nullptr);
    REQUIRE(index < tab->notes.size());
    return common::ui::tabNoteLayout(makeGeometry(), tab->notes[index]).bend_chip;
}

// What Enter would do right now.
[[nodiscard]] RestateTarget restateOf(const FakeEditorView& view)
{
    const EditorViewState* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    if (state == nullptr)
    {
        return {};
    }
    return state->restate_target;
}

struct BendFixture
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

    explicit BendFixture(common::core::Chart chart = makeGlideChart())
    {
        controller.attachView(view);
        // Hoisted out of the assertion: a Catch2 macro mentions its expression a second time, and
        // the never-run mention reads a moved-from operand that CI's use-after-move check sees.
        const bool loaded =
            loadChartArrangement(controller, project_services, audio, {}, std::move(chart));
        REQUIRE(loaded);
    }

    // The LAST question the view was handed, or nothing when no press asked one.
    [[nodiscard]] std::optional<ChartBendPicker> lastPicker() const
    {
        return view.shown_bend_pickers.empty()
                   ? std::nullopt
                   : std::optional<ChartBendPicker>{view.shown_bend_pickers.back()};
    }

    [[nodiscard]] std::size_t undoEntries() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state == nullptr ? 0 : state->undo_history.labels.size();
    }
};

// The amount a row states, or nothing for the clear row.
[[nodiscard]] std::optional<double> rowAmount(const ChartBendChoice& choice)
{
    const auto* const amount = std::get_if<ChartBendAmountChoice>(&choice);
    return amount != nullptr ? std::optional<double>{amount->semitones} : std::nullopt;
}

} // namespace

// A BARE `B` ON A COVERED SLOT ASKS ABOUT THE RING THERE. A bend can create no note and split no
// ring, so on a slot a ring covers the key has one meaning: a point on that ring. The question
// names the instant, commits nothing, and opens on rest; the answer plants the point with
// the amount alone — no fret, so the glide it sits on is untouched — as one entry, and selects it.
TEST_CASE("A bare bend on a covered slot plants a bend point on the ring", "[core][chart]")
{
    BendFixture fixture;
    click(fixture.controller, g_covered_x, g_string_3_y);
    const std::size_t entries_before = fixture.undoEntries();

    fixture.controller.onChartBendRequested();
    const std::optional<ChartBendPicker> picker = fixture.lastPicker();
    REQUIRE(picker.has_value());
    if (picker.has_value())
    {
        CHECK_THAT(picker->anchor.seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));
        CHECK(picker->anchor.string == 3);
        // Nothing stated there yet: every amount and no clear, so Return takes the first row,
        // rest.
        REQUIRE(picker->choices.size() == g_amount_rows);
        const std::optional<double> opening = rowAmount(picker->choices.front());
        REQUIRE(opening.has_value());
        if (opening.has_value())
        {
            CHECK_THAT(*opening, Catch::Matchers::WithinULP(0.0, 0));
        }
    }
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].keyframes.size() == 1);
    CHECK(fixture.undoEntries() == entries_before);

    fixture.controller.onChartBendChosen(std::optional{1.0});
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes[0].keyframes.size() == 2);
    const common::core::Keyframe& point = chart->notes[0].keyframes[0];
    CHECK(point.offset == common::core::Fraction{2});
    CHECK_FALSE(point.fret.has_value());
    const std::optional<double>& bend = point.bend;
    REQUIRE(bend.has_value());
    if (bend.has_value())
    {
        CHECK_THAT(*bend, Catch::Matchers::WithinULP(1.0, 0));
    }
    CHECK(fixture.undoEntries() == entries_before + 1);
    // The planted point is the selection, so it wears its ring and the keys act on it next.
    const EditorViewState* const state = stateOrNull(fixture.view.last_state);
    REQUIRE(state != nullptr);
    CHECK(
        state->chart_edit.selected_keyframes ==
        std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}});

    fixture.controller.onUndoRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].keyframes.size() == 1);
}

// Return on a bare ring takes rest, which the ring already holds: the point it plants says nothing,
// so the answer leaves no entry, and the point is silent authoring state that dissolves once focus
// leaves the note.
TEST_CASE("Rest on a covered slot writes nothing", "[core][chart]")
{
    BendFixture fixture;
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    const common::core::Chart original = *chart;
    click(fixture.controller, g_covered_x, g_string_3_y);
    const std::size_t entries_before = fixture.undoEntries();

    fixture.controller.onChartBendRequested();
    fixture.controller.onChartBendChosen(std::optional{0.0});
    CHECK(fixture.undoEntries() == entries_before);

    click(fixture.controller, g_empty_x, g_string_3_y);
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(*chart == original);
    CHECK(fixture.undoEntries() == entries_before);
}

// On an empty slot there is no ring and nothing to bend, so the key asks nothing.
TEST_CASE("A bend on an empty slot is inert", "[core][chart]")
{
    BendFixture fixture;
    click(fixture.controller, g_empty_x, g_string_3_y);
    fixture.controller.onChartBendRequested();
    CHECK(fixture.view.shown_bend_pickers.empty());
}

// A SELECTED HEAD'S ANCHOR IS ITS ONSET, whose value is the pre-bend: the question ticks the rest
// it states, and the answer states the pre-bend.
TEST_CASE("A bend on a selected head states its pre-bend", "[core][chart]")
{
    BendFixture fixture;
    click(fixture.controller, g_onset_x, g_string_3_y);
    fixture.controller.onChartBendRequested();
    const std::optional<ChartBendPicker> picker = fixture.lastPicker();
    REQUIRE(picker.has_value());
    if (picker.has_value())
    {
        CHECK_THAT(picker->anchor.seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));
        REQUIRE(picker->choices.size() == g_amount_rows);
        const auto* const rest = std::get_if<ChartBendAmountChoice>(&picker->choices.front());
        REQUIRE(rest != nullptr);
        if (rest != nullptr)
        {
            CHECK(rest->current);
        }
    }

    fixture.controller.onChartBendChosen(std::optional{3.0});
    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK_THAT(chart->notes[0].bend, Catch::Matchers::WithinULP(3.0, 0));
}

// A POINT STATING A BEND can have the statement taken away, so its question leads with the clear —
// the row Return takes — and the clear leaves the point's other channels alone.
TEST_CASE("A bend point's question offers the clear", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].keyframes[0].bend = 1.0;
    BendFixture fixture{std::move(chart)};
    // The junction carries the bend beside its fret; the caret walks onto it and selects it.
    click(fixture.controller, g_onset_x, g_string_3_y);
    fixture.controller.onRowObjectStepRequested(true, false);
    fixture.controller.onChartBendRequested();
    const std::optional<ChartBendPicker> picker = fixture.lastPicker();
    REQUIRE(picker.has_value());
    if (picker.has_value())
    {
        REQUIRE(picker->choices.size() == g_amount_rows + 1);
        CHECK(std::holds_alternative<ChartBendClearChoice>(picker->choices.front()));
        const std::optional<double> stated = rowAmount(picker->choices[3]);
        REQUIRE(stated.has_value());
        if (stated.has_value())
        {
            CHECK_THAT(*stated, Catch::Matchers::WithinULP(1.0, 0));
        }
        const auto* const ticked = std::get_if<ChartBendAmountChoice>(&picker->choices[3]);
        REQUIRE(ticked != nullptr);
        if (ticked != nullptr)
        {
            CHECK(ticked->current);
        }
    }

    fixture.controller.onChartBendChosen(std::nullopt);
    const common::core::Chart* const edited = chartOrNull(fixture.controller);
    REQUIRE(edited != nullptr);
    REQUIRE(edited->notes[0].keyframes.size() == 1);
    CHECK_FALSE(edited->notes[0].keyframes[0].bend.has_value());
    CHECK(edited->notes[0].keyframes[0].fret == 9);
}

// THE TWO PLANES PART ONLY WHERE TWO ANCHORS COLLIDE: a ring's end on which the next head of its
// string is struck. With that head selected under the caret, the bare key bends the head and `Alt`
// bends the ring that ends there — its final value, at the end.
TEST_CASE("Alt bend reaches the ring ending on the selected head", "[core][chart]")
{
    SECTION("the ring plane states the ending ring's final bend")
    {
        BendFixture fixture{makeAbuttingStringChart(false)};
        click(fixture.controller, g_ring_end_x, g_string_3_y);
        fixture.controller.onChartRingBendRequested();
        REQUIRE(fixture.lastPicker().has_value());
        fixture.controller.onChartBendChosen(std::optional{1.0});
        const common::core::Chart* const chart = chartOrNull(fixture.controller);
        REQUIRE(chart != nullptr);
        REQUIRE(chart->notes.size() == 2);
        const common::core::Keyframe& end = chart->notes[0].keyframes.back();
        CHECK(end.offset == chart->notes[0].sustain);
        CHECK(end.bend.has_value());
        CHECK_THAT(chart->notes[1].bend, Catch::Matchers::WithinULP(0.0, 0));
    }
    SECTION("the bare key states the head's pre-bend")
    {
        BendFixture fixture{makeAbuttingStringChart(false)};
        click(fixture.controller, g_ring_end_x, g_string_3_y);
        fixture.controller.onChartBendRequested();
        fixture.controller.onChartBendChosen(std::optional{1.0});
        const common::core::Chart* const chart = chartOrNull(fixture.controller);
        REQUIRE(chart != nullptr);
        REQUIRE(chart->notes.size() == 2);
        CHECK_THAT(chart->notes[1].bend, Catch::Matchers::WithinULP(1.0, 0));
        CHECK(chart->notes[0].keyframes.size() == 1);
    }
}

// The rules gate the answer as they gate every write: a dead note sounds no pitch to bend, so the
// answer changes nothing and records no entry.
TEST_CASE("A bend answer the rules refuse changes nothing", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].keyframes.clear();
    chart.notes[0].dead = true;
    BendFixture fixture{std::move(chart)};
    click(fixture.controller, g_onset_x, g_string_3_y);
    const std::size_t entries_before = fixture.undoEntries();
    fixture.controller.onChartBendRequested();
    fixture.controller.onChartBendChosen(std::optional{2.0});
    const common::core::Chart* const edited = chartOrNull(fixture.controller);
    REQUIRE(edited != nullptr);
    CHECK_THAT(edited->notes[0].bend, Catch::Matchers::WithinULP(0.0, 0));
    CHECK(fixture.undoEntries() == entries_before);
}

// A bend chip is a FACE of the object whose bend it prints: `Up` from the mark stands on it and
// `Down` returns, the caret keeping its slot and the selection, so only the face changes; `Up`
// from the chip leaves the column like any step. On the chip, `Enter` restates the bend.
TEST_CASE("The caret steps between a bent point's mark and its chip", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].keyframes[0].bend = 1.0;
    BendFixture fixture{std::move(chart)};
    click(fixture.controller, g_junction_x, g_string_3_y);
    REQUIRE(caretOf(fixture.view).face == ChartCaretFace::Mark);
    CHECK(std::holds_alternative<std::monostate>(restateOf(fixture.view)));

    const std::vector<ChartKeyframeRef> junction{
        ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}
    };
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
    const ChartCaretViewState on_chip = caretOf(fixture.view);
    CHECK(on_chip.face == ChartCaretFace::BendChip);
    CHECK(on_chip.string == 3);
    CHECK_THAT(on_chip.seconds, Catch::Matchers::WithinAbs(4.0, 1e-9));
    CHECK(chartEdit(fixture.view).selected_keyframes == junction);
    CHECK(std::holds_alternative<OpenBendPickerTarget>(restateOf(fixture.view)));

    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    CHECK(caretOf(fixture.view).face == ChartCaretFace::Mark);
    CHECK(caretOf(fixture.view).string == 3);
    CHECK(chartEdit(fixture.view).selected_keyframes == junction);

    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
    CHECK(caretOf(fixture.view).face == ChartCaretFace::Mark);
    CHECK(caretOf(fixture.view).string == 4);
}

// With the caret on a chip, `Delete` takes the BEND the chip prints and leaves everything else:
// the picker's "No bend" through its own planner. A point left saying nothing goes, and a note's
// onset, which always states its bend, is left at rest.
TEST_CASE("Delete on a bend chip takes the bend", "[core][chart]")
{
    SECTION("a point keeps its fret, and the caret returns to its mark")
    {
        common::core::Chart chart = makeGlideChart();
        chart.notes[0].keyframes[0].bend = 1.0;
        BendFixture fixture{std::move(chart)};
        click(fixture.controller, g_junction_x, g_string_3_y);
        fixture.controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
        REQUIRE(caretOf(fixture.view).face == ChartCaretFace::BendChip);

        fixture.controller.onSelectionDeleteRequested();
        const common::core::Chart* const edited = chartOrNull(fixture.controller);
        REQUIRE(edited != nullptr);
        REQUIRE(edited->notes.size() == 1);
        REQUIRE(edited->notes[0].keyframes.size() == 1);
        CHECK(edited->notes[0].keyframes[0].fret == 9);
        CHECK_FALSE(edited->notes[0].keyframes[0].bend.has_value());
        // The chip went with the bend, so the caret stands on the mark it wears.
        CHECK(caretOf(fixture.view).face == ChartCaretFace::Mark);
        CHECK(
            chartEdit(fixture.view).selected_keyframes ==
            (std::vector<ChartKeyframeRef>{
                ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}
            }));

        // Undo puts the bend back and the caret back on its chip.
        fixture.controller.onUndoRequested();
        const common::core::Chart* const restored = chartOrNull(fixture.controller);
        REQUIRE(restored != nullptr);
        REQUIRE(restored->notes[0].keyframes.size() == 1);
        const std::optional<double>& bend = restored->notes[0].keyframes[0].bend;
        REQUIRE(bend.has_value());
        if (bend.has_value())
        {
            CHECK_THAT(*bend, Catch::Matchers::WithinULP(1.0, 0));
        }
        CHECK(caretOf(fixture.view).face == ChartCaretFace::BendChip);
    }

    SECTION("a point stating only its bend goes whole")
    {
        common::core::Chart chart = makeGlideChart();
        chart.notes[0].keyframes = {
            common::core::Keyframe{.offset = common::core::Fraction{4}, .fret = {}, .bend = 1.0}
        };
        BendFixture fixture{std::move(chart)};
        click(fixture.controller, g_junction_x, g_string_3_y);
        REQUIRE(chartEdit(fixture.view).selected_keyframes.size() == 1);
        fixture.controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
        REQUIRE(caretOf(fixture.view).face == ChartCaretFace::BendChip);

        fixture.controller.onSelectionDeleteRequested();
        const common::core::Chart* const edited = chartOrNull(fixture.controller);
        REQUIRE(edited != nullptr);
        REQUIRE(edited->notes.size() == 1);
        CHECK(edited->notes[0].keyframes.empty());
        CHECK(chartEdit(fixture.view).selected_keyframes.empty());
    }

    SECTION("a note's pre-bend goes to rest and the note stays selected")
    {
        common::core::Chart chart = makeGlideChart();
        chart.notes[0].bend = 1.0;
        BendFixture fixture{std::move(chart)};
        click(fixture.controller, g_onset_x, g_string_3_y);
        fixture.controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
        REQUIRE(caretOf(fixture.view).face == ChartCaretFace::BendChip);

        fixture.controller.onSelectionDeleteRequested();
        const common::core::Chart* const edited = chartOrNull(fixture.controller);
        REQUIRE(edited != nullptr);
        REQUIRE(edited->notes.size() == 1);
        CHECK_THAT(edited->notes[0].bend, Catch::Matchers::WithinULP(0.0, 0));
        CHECK(edited->notes[0].keyframes.size() == 1);
        CHECK(chartEdit(fixture.view).selected_notes == std::vector<std::size_t>{0});
    }
}

// A press on a chip puts the caret on that face at once, the pointer twin of `Up` from the mark.
TEST_CASE("A click on a bend chip puts the caret on it", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].bend = 1.0;
    BendFixture fixture{std::move(chart)};
    const EditorViewState* const state = stateOrNull(fixture.view.last_state);
    REQUIRE(state != nullptr);
    if (state == nullptr)
    {
        return;
    }
    const std::shared_ptr<const common::core::ChartViewState>& tab = state->tab;
    REQUIRE(tab != nullptr);
    REQUIRE(tab->notes.size() == 1);
    const std::optional<common::ui::TabLayoutRect> chip =
        common::ui::tabNoteLayout(makeGeometry(), tab->notes[0]).bend_chip;
    REQUIRE(chip.has_value());
    if (!chip.has_value())
    {
        return;
    }

    click(fixture.controller, chip->x + chip->width / 2.0f, chip->y + chip->height / 2.0f);
    CHECK(caretOf(fixture.view).face == ChartCaretFace::BendChip);
    CHECK(chartEdit(fixture.view).selected_notes == std::vector<std::size_t>{0});
    // The head, clicked, moves the caret back onto the mark.
    click(fixture.controller, g_onset_x, g_string_3_y);
    CHECK(caretOf(fixture.view).face == ChartCaretFace::Mark);
}

// A click on the chip of a member of a selected chord selects that note ALONE on its chip, exactly
// as a click on its head selects it alone: a face is one object's (user ruling 2026-09-29). The
// chord's bends are the letter verb's: `B` over the chord.
TEST_CASE("A click on a chord member's bend chip collapses to that note", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].bend = 1.0;
    chart.notes.push_back(makeTestNote({.measure = 2, .beat = 1}, 4, 7));
    BendFixture fixture{std::move(chart)};
    doubleClick(fixture.controller, g_onset_x, g_string_3_y);
    REQUIRE(chartEdit(fixture.view).selected_notes.size() == 2);
    REQUIRE_FALSE(chartEdit(fixture.view).caret.has_value());

    const std::optional<common::ui::TabLayoutRect> chip = onsetChipOf(fixture.view, 0);
    REQUIRE(chip.has_value());
    if (!chip.has_value())
    {
        return;
    }
    click(fixture.controller, chip->x + chip->width / 2.0f, chip->y + chip->height / 2.0f);
    CHECK(chartEdit(fixture.view).selected_notes == std::vector<std::size_t>{0});
    CHECK(caretOf(fixture.view).face == ChartCaretFace::BendChip);
}

// A double click on a bend chip restates the bend it prints: its note alone on the chip, with the
// bend picker open over it (user ruling 2026-09-29). The chord stays a double click on the heads.
TEST_CASE("A double click on a bend chip opens the bend picker", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].bend = 1.0;
    BendFixture fixture{std::move(chart)};
    const std::optional<common::ui::TabLayoutRect> chip = onsetChipOf(fixture.view, 0);
    REQUIRE(chip.has_value());
    if (!chip.has_value())
    {
        return;
    }
    REQUIRE_FALSE(fixture.lastPicker().has_value());

    doubleClick(fixture.controller, chip->x + chip->width / 2.0f, chip->y + chip->height / 2.0f);
    CHECK(fixture.lastPicker().has_value());
    CHECK(chartEdit(fixture.view).selected_notes == std::vector<std::size_t>{0});
    CHECK(caretOf(fixture.view).face == ChartCaretFace::BendChip);
}

} // namespace rock_hero::editor::core
