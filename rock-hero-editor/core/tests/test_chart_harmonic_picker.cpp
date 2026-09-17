#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The NODE rows of a request, in the order it published them. A request is one list of variant rows
// under a shape contract — nodes ascending by partial, then at most one clear row LAST — so reading
// the nodes out is also where that shape is checked: a non-node row anywhere but the end is the
// contract broken rather than a row to hand back.
[[nodiscard]] std::vector<ChartHarmonicNodeChoice> nodeRows(const ChartHarmonicNodePicker& picker)
{
    std::vector<ChartHarmonicNodeChoice> rows;
    rows.reserve(picker.choices.size());
    for (std::size_t index = 0; index < picker.choices.size(); ++index)
    {
        const auto* const node = std::get_if<ChartHarmonicNodeChoice>(&picker.choices[index]);
        if (node == nullptr)
        {
            CHECK(index + 1 == picker.choices.size());
            break;
        }
        rows.push_back(*node);
    }
    return rows;
}

// Whether the request ends with the "No harmonic" row, which the controller appends only when a
// clear over the live selection would change something.
[[nodiscard]] bool offersClear(const ChartHarmonicNodePicker& picker)
{
    return !picker.choices.empty() &&
           std::holds_alternative<ChartHarmonicClearChoice>(picker.choices.back());
}

// The controller wired for the node picker's scenarios: the shared six-string chart opened through
// the normal route, over the DEFERRING scheduler and its injected clock so the one scenario that
// needs a fret entry to still be pending when `H` arrives can have it — under the immediate
// scheduler the entry's combine window would meet its wake inside the digit keystroke itself.
// Declare the harness before the controller: its services() lambda reads the clock through `this`.
struct HarmonicPickerFixture
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    PendingEntryHarness pending;
    EditorController controller{
        audioPorts(transport, audio),
        pending.services(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            // Wired so one scenario can put a real commit point — a save — in the middle of a
            // harmonic run; every other scenario here never asks for one.
            .save_function = project_services.saveFunction(),
        }
    };
    FakeEditorView view;

    // The shared six-string chart by default; a scenario needing a different stream passes its own.
    explicit HarmonicPickerFixture(common::core::Chart chart = makeTestChart())
    {
        controller.attachView(view);
        // Hoisted out of the assertion: a Catch2 macro mentions its expression a second time, and
        // the never-run mention reads a moved-from operand that CI's use-after-move check sees.
        const bool loaded =
            loadChartArrangement(controller, project_services, audio, {}, std::move(chart));
        REQUIRE(loaded);
        // The open route's own delayed work, drained here so a later pump delivers only the wake a
        // test actually armed.
        static_cast<void>(pending.scheduler.runDelayed());
    }

    // The LAST request the view was handed, or nothing when no press has asked for one. The REQUEST
    // is the whole published surface — the rows, the note they were read from, and the row Return
    // takes — because the fork lives in the controller, which decides it against the chart the
    // press lands on: what the view was handed is the only record that a press meant "the charter
    // chooses".
    [[nodiscard]] std::optional<ChartHarmonicNodePicker> lastPicker() const
    {
        return view.shown_harmonic_pickers.empty()
                   ? std::nullopt
                   : std::optional<ChartHarmonicNodePicker>{view.shown_harmonic_pickers.back()};
    }

    // How many presses handed the choice to the charter instead of answering it themselves.
    [[nodiscard]] std::size_t pickerRequests() const
    {
        return view.shown_harmonic_pickers.size();
    }

    // How many entries the history holds, so a scenario can count what one press authored.
    [[nodiscard]] std::size_t undoEntries() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state == nullptr ? 0 : state->undo_history.labels.size();
    }

    // Ends the harmonic run without changing what the chart holds: a history move is a context
    // switch, so it retires the burst record every coalescing window rests on and the next choice
    // opens a fresh entry. The same "undo then redo" the technique-toggle suite closes its window
    // with, named here because the harmonic scenarios lean on it repeatedly.
    void endRun()
    {
        controller.onUndoRequested();
        controller.onRedoRequested();
    }
};

} // namespace

// `H` OVER AN AMBIGUOUS FRET ASKS RATHER THAN WRITES. The fixture's string-1 note carries fret 3,
// which under the editor's partial bound names four nodes, and which one the finger touches is the
// charter's to say — so the press hands the rows to the view and commits NOTHING: no node, no
// entry, and the fret the touch would have consumed still standing.
TEST_CASE("A harmonic press over an ambiguous fret requests the picker", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    const std::size_t entries_before = fixture.undoEntries();
    fixture.controller.onChartHarmonicRequested();

    // Exactly one request, its rows ascending by partial. That order IS the contract: the view
    // preselects the first row, and the same row is what the planner's own default would write.
    CHECK(fixture.pickerRequests() == 1);
    const std::optional<ChartHarmonicNodePicker> picker = fixture.lastPicker();
    REQUIRE(picker.has_value());
    if (picker.has_value())
    {
        const std::vector<ChartHarmonicNodeChoice> choices = nodeRows(*picker);
        REQUIRE(choices.size() == 4);
        if (choices.size() == 4)
        {
            CHECK(choices[0].partial == 6);
            CHECK_THAT(choices[0].node, Catch::Matchers::WithinAbs(3.1564, 0.001));
            CHECK(choices[1].partial == 7);
            CHECK_THAT(choices[1].node, Catch::Matchers::WithinAbs(2.6687, 0.001));
            CHECK(choices[2].partial == 11);
            CHECK_THAT(choices[2].node, Catch::Matchers::WithinAbs(3.4741, 0.001));
            CHECK(choices[3].partial == 13);
            CHECK_THAT(choices[3].node, Catch::Matchers::WithinAbs(2.8921, 0.001));
            // Nothing is ticked: the finger is on the fret, not on any of the nodes it names.
            CHECK_FALSE(choices[0].current);
            CHECK_FALSE(choices[1].current);
            CHECK_FALSE(choices[2].current);
            CHECK_FALSE(choices[3].current);
        }

        // No harmonic to remove, so the rows end at the last node and Return takes the first of
        // them: every row here changes something, so the lowest partial is what a toggle meant.
        CHECK_FALSE(offersClear(*picker));
        CHECK(picker->preselected == 0);
    }

    // The chart is exactly as the press found it, and the history never grew.
    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].fret == 3);
    CHECK_FALSE(chart->notes[0].harmonic_node.has_value());
    CHECK(fixture.undoEntries() == entries_before);
}

// A ROW IS ALREADY A DELIBERATE CHOICE, so the chosen partial applies at once through the same
// planner a choiceless press runs: one entry for the whole press, and the undo gives back the fret
// the touch consumed.
TEST_CASE("A chosen picker row writes the harmonic at once", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartHarmonicRequested();
    REQUIRE(fixture.pickerRequests() == 1);
    const std::size_t entries_before = fixture.undoEntries();

    fixture.controller.onChartHarmonicNodeRequested(std::optional{7});
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].fret == 0);
    const std::optional<double>& node = chart->notes[0].harmonic_node;
    REQUIRE(node.has_value());
    if (node.has_value())
    {
        CHECK_THAT(*node, Catch::Matchers::WithinAbs(2.6687, 0.001));
    }
    CHECK(fixture.undoEntries() == entries_before + 1);

    fixture.controller.onUndoRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].fret == 3);
    CHECK_FALSE(chart->notes[0].harmonic_node.has_value());
}

// A fret naming ONE node has nothing to choose, so the press answers itself in the same keystroke:
// 12 names the octave and nothing else inside the partial bound, and the charter is never shown a
// menu with a single row on it.
TEST_CASE("A label naming one node writes with no request", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = {makeTestNote({.measure = 2, .beat = 1}, 1, 12)};
    HarmonicPickerFixture fixture{std::move(chart)};

    click(fixture.controller, 40.0f, 220.0f);
    const std::size_t entries_before = fixture.undoEntries();
    fixture.controller.onChartHarmonicRequested();

    const common::core::Chart* const live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 0);
    const std::optional<double>& node = live->notes[0].harmonic_node;
    REQUIRE(node.has_value());
    if (node.has_value())
    {
        CHECK_THAT(*node, Catch::Matchers::WithinAbs(12.0, 0.001));
    }
    CHECK(fixture.undoEntries() == entries_before + 1);
    CHECK(fixture.pickerRequests() == 0);
}

// THE CARRIER WITH NOTHING ELSE ON OFFER CLEARS, and whether that clear costs an entry is the
// RUN's question rather than the press's. A 12 already touching its one node has exactly one change
// left — removal — so the press takes it without asking; inside the run that set it, removing walks
// the chart back to where the run began, so the entry describing the run RETIRES rather than a
// second one being pushed on top. Once a history move has ended the run, the same press is an
// ordinary edit with an entry of its own.
TEST_CASE("A carrier with one node clears, and the run decides what that costs", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = {makeTestNote({.measure = 2, .beat = 1}, 1, 12)};
    HarmonicPickerFixture fixture{std::move(chart)};

    click(fixture.controller, 40.0f, 220.0f);
    const std::size_t entries_before = fixture.undoEntries();

    // The set half, which opens the run.
    fixture.controller.onChartHarmonicRequested();
    REQUIRE(fixture.undoEntries() == entries_before + 1);
    REQUIRE(fixture.pickerRequests() == 0);

    SECTION("a second press inside the run retires the entry it pushed")
    {
        fixture.controller.onChartHarmonicRequested();

        const common::core::Chart* const live = chartOrNull(fixture.controller);
        REQUIRE(live != nullptr);
        CHECK(live->notes[0].fret == 12);
        CHECK_FALSE(live->notes[0].harmonic_node.has_value());
        // Still no menu — the carrier's one row is the one it is standing on, so "remove" is the
        // only change there is — and no trace in the history either.
        CHECK(fixture.pickerRequests() == 0);
        CHECK(fixture.undoEntries() == entries_before);
    }

    SECTION("with the run ended the clear is an entry of its own")
    {
        fixture.endRun();
        const std::size_t entries_after_run = fixture.undoEntries();

        fixture.controller.onChartHarmonicRequested();

        const common::core::Chart* const live = chartOrNull(fixture.controller);
        REQUIRE(live != nullptr);
        CHECK(live->notes[0].fret == 12);
        CHECK_FALSE(live->notes[0].harmonic_node.has_value());
        CHECK(fixture.pickerRequests() == 0);
        CHECK(fixture.undoEntries() == entries_after_run + 1);
    }
}

// THE CHANGES ARE COUNTED BY PLANNING, so a member that names nothing adds no row and no change: a
// 12 already touching its one node, selected beside an open string, still has exactly ONE change on
// offer — the clear — and the press takes it in the same keystroke. Counting the label's rows
// instead would have made this two (a node row plus the clear) and opened a menu whose first row
// does nothing.
TEST_CASE("A one-node carrier beside a note naming nothing clears at once", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 12),
        makeTestNote({.measure = 2, .beat = 1}, 2, 0),
    };
    HarmonicPickerFixture fixture{std::move(chart)};

    // The string-1 note becomes the carrier through the verb, so its node is exactly the one the
    // rows name; the run is then ended so the clear below is an ordinary edit rather than a retire.
    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartHarmonicRequested();
    REQUIRE(fixture.pickerRequests() == 0);
    fixture.endRun();

    fixture.controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    fixture.controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    fixture.controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    // Both members are in scope, which is what makes the count below a statement about PLANNING
    // rather than about a selection that quietly lost its second note.
    const EditorViewState* const selected = stateOrNull(fixture.view.last_state);
    REQUIRE(selected != nullptr);
    if (selected != nullptr)
    {
        CHECK(selected->chart_edit.selected_notes.size() == 2);
    }

    const std::size_t entries_before = fixture.undoEntries();
    fixture.controller.onChartHarmonicRequested();

    // No menu: one change, applied at once.
    CHECK(fixture.pickerRequests() == 0);
    const common::core::Chart* const live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 12);
    CHECK_FALSE(live->notes[0].harmonic_node.has_value());
    // The open string had nothing to clear, so the press passed over it.
    CHECK(live->notes[1].fret == 0);
    CHECK_FALSE(live->notes[1].harmonic_node.has_value());
    CHECK(fixture.undoEntries() == entries_before + 1);
}

// A CARRIER'S LABEL IS THE FRET ITS NODE LIES AT, so a finger touching 4.98 is offered the OTHER
// nodes of a 5 — the 13th and the 15th — with the row it is standing on ticked rather than dropped,
// because the menu's job is to show where the hand is before it is moved. Every member carries a
// harmonic, so the clear row is the preselected one: `H` then Return still removes, exactly as a
// toggle would have.
TEST_CASE("A harmonic press over a carrier offers the label's other nodes", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    // The string-2 note at measure 2 beat 1 carries fret 5. Touched through the verb itself rather
    // than authored by hand, so the stored node is bit-for-bit the one the rows name and the ticked
    // row is decided by the arithmetic instead of by a literal.
    click(fixture.controller, 40.0f, 180.0f);
    fixture.controller.onChartHarmonicRequested();
    fixture.controller.onChartHarmonicNodeRequested(std::optional{4});
    fixture.endRun();
    const std::size_t entries_before = fixture.undoEntries();
    const std::size_t requests_before = fixture.pickerRequests();

    fixture.controller.onChartHarmonicRequested();
    CHECK(fixture.pickerRequests() == requests_before + 1);

    const std::optional<ChartHarmonicNodePicker> picker = fixture.lastPicker();
    REQUIRE(picker.has_value());
    if (picker.has_value())
    {
        const std::vector<ChartHarmonicNodeChoice> choices = nodeRows(*picker);
        REQUIRE(choices.size() == 3);
        if (choices.size() == 3)
        {
            CHECK(choices[0].partial == 4);
            CHECK_THAT(choices[0].node, Catch::Matchers::WithinAbs(4.9804, 0.001));
            CHECK(choices[0].current);
            CHECK(choices[1].partial == 13);
            CHECK_THAT(choices[1].node, Catch::Matchers::WithinAbs(4.5421, 0.001));
            CHECK_FALSE(choices[1].current);
            CHECK(choices[2].partial == 15);
            CHECK_THAT(choices[2].node, Catch::Matchers::WithinAbs(5.3695, 0.001));
            CHECK_FALSE(choices[2].current);
        }

        // Four rows, the clear last, and Return takes it: every selected note carries a harmonic,
        // which is exactly when a toggle would have removed one.
        CHECK(offersClear(*picker));
        CHECK(picker->choices.size() == 4);
        CHECK(picker->preselected == 3);
    }

    SECTION("the clear row presses the finger back onto the fret it was touching")
    {
        fixture.controller.onChartHarmonicNodeRequested(std::nullopt);

        const common::core::Chart* const live = chartOrNull(fixture.controller);
        REQUIRE(live != nullptr);
        CHECK(live->notes[1].fret == 5);
        CHECK_FALSE(live->notes[1].harmonic_node.has_value());
        CHECK(fixture.undoEntries() == entries_before + 1);
    }

    SECTION("another row moves the touch along the same label")
    {
        fixture.controller.onChartHarmonicNodeRequested(std::optional{13});

        const common::core::Chart* const live = chartOrNull(fixture.controller);
        REQUIRE(live != nullptr);
        CHECK(live->notes[1].fret == 0);
        const std::optional<double>& node = live->notes[1].harmonic_node;
        REQUIRE(node.has_value());
        if (node.has_value())
        {
            CHECK_THAT(*node, Catch::Matchers::WithinAbs(4.5421, 0.001));
        }
        CHECK(fixture.undoEntries() == entries_before + 1);
    }
}

// THE RUN FOLDS LIKE A GESTURE, NOT LIKE A TOGGLE. Consecutive choices on one selection are one
// edit — the second replaces the first's entry rather than stacking on it — so a charter trying the
// 13th and settling on the 15th leaves ONE thing to undo, and that undo lands on the state the run
// began at rather than on an intermediate node nobody asked to keep. A history move then ends the
// run, and the next choice opens a fresh entry the way the first one did.
TEST_CASE("Consecutive harmonic choices fold into one history entry", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    click(fixture.controller, 40.0f, 180.0f);
    const std::size_t entries_before = fixture.undoEntries();

    fixture.controller.onChartHarmonicRequested();
    fixture.controller.onChartHarmonicNodeRequested(std::optional{13});
    const std::size_t entries_after_first = fixture.undoEntries();
    CHECK(entries_after_first == entries_before + 1);

    // The second press reads the chart the first choice left, so the 13th is the ticked row.
    fixture.controller.onChartHarmonicRequested();
    CHECK(fixture.pickerRequests() == 2);
    const std::optional<ChartHarmonicNodePicker> picker = fixture.lastPicker();
    REQUIRE(picker.has_value());
    if (picker.has_value())
    {
        const std::vector<ChartHarmonicNodeChoice> choices = nodeRows(*picker);
        REQUIRE(choices.size() == 3);
        if (choices.size() == 3)
        {
            CHECK(choices[1].partial == 13);
            CHECK(choices[1].current);
        }
        CHECK(offersClear(*picker));
        CHECK(picker->choices.size() == 4);
        CHECK(picker->preselected == 3);
    }

    fixture.controller.onChartHarmonicNodeRequested(std::optional{15});
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    const std::optional<double>& node = chart->notes[1].harmonic_node;
    REQUIRE(node.has_value());
    if (node.has_value())
    {
        CHECK_THAT(*node, Catch::Matchers::WithinAbs(5.3695, 0.001));
    }
    // The fold: the second choice rewrote the run's entry instead of pushing a second one.
    CHECK(fixture.undoEntries() == entries_after_first);

    // And the one entry describes start → now, so a single undo is the whole run undone.
    fixture.controller.onUndoRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[1].fret == 5);
    CHECK_FALSE(chart->notes[1].harmonic_node.has_value());

    // The redo restores the touch AND ends the run, so the clear below is an ordinary edit with an
    // entry of its own rather than the retire a third choice inside the run would have been.
    fixture.controller.onRedoRequested();
    const std::size_t entries_after_redo = fixture.undoEntries();
    fixture.controller.onChartHarmonicRequested();
    fixture.controller.onChartHarmonicNodeRequested(std::nullopt);
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[1].fret == 5);
    CHECK_FALSE(chart->notes[1].harmonic_node.has_value());
    CHECK(fixture.undoEntries() == entries_after_redo + 1);
}

// A SAVE IS A COMMIT POINT LIKE ANY OTHER, so the run ends there. The saved entry now describes
// what the file holds, and rewriting it would make "return to clean" restore content the file does
// not have — so the next choice PUSHES rather than replaces, and the charter keeps one Ctrl+Z back
// to the saved state and another back to where the run began.
TEST_CASE("A save mid-run starts a fresh entry on the next choice", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    click(fixture.controller, 40.0f, 180.0f);
    const std::size_t entries_before = fixture.undoEntries();

    fixture.controller.onChartHarmonicRequested();
    fixture.controller.onChartHarmonicNodeRequested(std::optional{13});
    CHECK(fixture.undoEntries() == entries_before + 1);

    fixture.controller.onSaveRequested();
    REQUIRE(fixture.project_services.save_call_count == 1);

    fixture.controller.onChartHarmonicRequested();
    fixture.controller.onChartHarmonicNodeRequested(std::optional{15});

    const common::core::Chart* const live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[1].fret == 0);
    const std::optional<double>& node = live->notes[1].harmonic_node;
    REQUIRE(node.has_value());
    if (node.has_value())
    {
        CHECK_THAT(*node, Catch::Matchers::WithinAbs(5.3695, 0.001));
    }
    // Two entries, not one rewritten: the saved 13th and this choice's 15th.
    CHECK(fixture.undoEntries() == entries_before + 2);
}

// A RUN THAT CHOOSES ITS WAY HOME LEAVES NOTHING BEHIND. Setting a node and then removing it inside
// the same run describes no edit at all, so the entry the first choice pushed is RETIRED rather
// than joined by a second: an entry describing nothing is a dead Ctrl+Z on a document reported
// modified that is byte-identical to the one on disk. No reversal of its own is needed for this —
// the replan against the run's start is what notices, which is why the verb needs no second-press
// rule the way the technique toggle does.
TEST_CASE("A harmonic run that returns to its start leaves no trace", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    // A preceding entry the run must not disturb, so "no trace" can be read off the history.
    click(fixture.controller, 40.0f, 180.0f);
    fixture.controller.onChartSustainAdjustRequested(1);
    const std::size_t entries_before = fixture.undoEntries();

    fixture.controller.onChartHarmonicRequested();
    fixture.controller.onChartHarmonicNodeRequested(std::optional{4});
    CHECK(fixture.undoEntries() == entries_before + 1);

    fixture.controller.onChartHarmonicRequested();
    CHECK(fixture.pickerRequests() == 2);
    fixture.controller.onChartHarmonicNodeRequested(std::nullopt);

    const common::core::Chart* live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[1].fret == 5);
    CHECK_FALSE(live->notes[1].harmonic_node.has_value());
    CHECK(fixture.undoEntries() == entries_before);

    // The next undo reaches past the retired run to the sustain adjust that preceded it.
    fixture.controller.onUndoRequested();
    live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[1].sustain == g_fixture_sustain);
}

// UNIFORM SCOPE OVER A MIXED CHORD: one press, ONE request, and the rows come off the member whose
// label names the MOST nodes — here the carrier at 4.98, whose label is the 5 its node lies at, not
// the plain 7 that leads it in chart order. One member speaks for the scope because a row carries a
// PARTIAL, and that partial binds every member whose own label offers it; where it lands is each
// note's own arithmetic. Only some members carry a harmonic, so the clear is OFFERED rather than
// preselected: Return still takes the first node row.
TEST_CASE("A harmonic press over a mixed chord reads one member for the scope", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    // Chart order is (position, string), so the plain 7 leads and the ambiguous 5 follows it.
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 7),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5),
    };
    HarmonicPickerFixture fixture{std::move(chart)};

    // Make the string-2 note a carrier through the verb, so its node is exactly a row's value.
    click(fixture.controller, 40.0f, 180.0f);
    fixture.controller.onChartHarmonicRequested();
    fixture.controller.onChartHarmonicNodeRequested(std::optional{4});

    // Marquee both members: a carrier plus a plain note whose label names one node.
    fixture.controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    fixture.controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    fixture.controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    const std::size_t entries_before = fixture.undoEntries();
    const std::size_t requests_before = fixture.pickerRequests();
    fixture.controller.onChartHarmonicRequested();
    CHECK(fixture.pickerRequests() == requests_before + 1);

    // The request names the member the rows came from — the carrier at projection index 1 — because
    // the 5's rows hanging under the 7's head would describe a note they do not touch.
    const std::optional<ChartHarmonicNodePicker> picker = fixture.lastPicker();
    REQUIRE(picker.has_value());
    if (picker.has_value())
    {
        CHECK(picker->note == 1);
        const std::vector<ChartHarmonicNodeChoice> choices = nodeRows(*picker);
        REQUIRE(choices.size() == 3);
        if (choices.size() == 3)
        {
            CHECK(choices[0].partial == 4);
            CHECK_THAT(choices[0].node, Catch::Matchers::WithinAbs(4.9804, 0.001));
            CHECK(choices[1].partial == 13);
            CHECK(choices[2].partial == 15);
        }
        // The clear is on offer, because one member carries — but Return does NOT take it: the
        // plain 7 carries nothing, so what a toggle would have done here is state the lowest
        // partial, and that row changes something (the 7 takes its own single node).
        CHECK(offersClear(*picker));
        CHECK(picker->choices.size() == 4);
        CHECK(picker->preselected == 0);
    }

    SECTION("a chosen partial binds the member that names it and defaults the one that does not")
    {
        fixture.controller.onChartHarmonicNodeRequested(std::optional{4});

        const common::core::Chart* const live = chartOrNull(fixture.controller);
        REQUIRE(live != nullptr);
        // Fret 7 names only the 3rd partial, so it takes its own first row rather than being
        // skipped: one partial, two numbers.
        CHECK(live->notes[0].fret == 0);
        const std::optional<double>& other = live->notes[0].harmonic_node;
        REQUIRE(other.has_value());
        if (other.has_value())
        {
            CHECK_THAT(*other, Catch::Matchers::WithinAbs(7.0196, 0.001));
        }
        // The carrier was already standing on the 4th partial's node and stays there.
        CHECK(live->notes[1].fret == 0);
        const std::optional<double>& carried = live->notes[1].harmonic_node;
        REQUIRE(carried.has_value());
        if (carried.has_value())
        {
            CHECK_THAT(*carried, Catch::Matchers::WithinAbs(4.9804, 0.001));
        }
        CHECK(fixture.undoEntries() == entries_before + 1);
    }

    SECTION("the clear touches only the members that carry a harmonic")
    {
        fixture.controller.onChartHarmonicNodeRequested(std::nullopt);

        const common::core::Chart* const live = chartOrNull(fixture.controller);
        REQUIRE(live != nullptr);
        // The plain member has nothing to remove, so the clear passes over it untouched.
        CHECK(live->notes[0].fret == 7);
        CHECK_FALSE(live->notes[0].harmonic_node.has_value());
        CHECK(live->notes[1].fret == 5);
        CHECK_FALSE(live->notes[1].harmonic_node.has_value());
        CHECK(fixture.undoEntries() == entries_before + 1);
    }
}

// THE PINCH IS THE ONE NODE THIS VERB DOES NOT OWN. A thumb's graze belongs to the picking hand
// and the `Shift+H` row clears it, so "No harmonic" here removes the fret-hand carrier's node and
// leaves the pinch exactly as it was — a shared clear once reached both, and stripped a pinch the
// charter had only selected in passing. The pinch is no carrier either, which is why Return does
// NOT take the clear: not every selected note carries, so what a toggle would have done is state a
// node — and the row the anchor already stands on is skipped for the first one that changes
// something.
TEST_CASE("The harmonic clear leaves a pinch beside a carrier alone", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    common::core::ChartNote pinch = makeTestNote({.measure = 2, .beat = 1}, 2, 5);
    pinch.attack = common::core::NoteAttack::Pinch;
    pinch.harmonic_node = 17.0;
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 5),
        std::move(pinch),
    };
    HarmonicPickerFixture fixture{std::move(chart)};

    // The string-1 five becomes a carrier through the verb, and the run is ended so the clear below
    // is an ordinary edit.
    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartHarmonicRequested();
    REQUIRE(fixture.pickerRequests() == 1);
    fixture.controller.onChartHarmonicNodeRequested(std::optional{4});
    fixture.endRun();

    fixture.controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    fixture.controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    fixture.controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    const std::size_t entries_before = fixture.undoEntries();
    const std::size_t requests_before = fixture.pickerRequests();
    fixture.controller.onChartHarmonicRequested();
    CHECK(fixture.pickerRequests() == requests_before + 1);

    const std::optional<ChartHarmonicNodePicker> picker = fixture.lastPicker();
    REQUIRE(picker.has_value());
    if (picker.has_value())
    {
        // The rows are the carrier's label's — the pinch names none at all — so the anchor is the
        // string-1 note at projection index 0.
        CHECK(picker->note == 0);
        const std::vector<ChartHarmonicNodeChoice> choices = nodeRows(*picker);
        REQUIRE(choices.size() == 3);
        if (choices.size() == 3)
        {
            CHECK(choices[0].partial == 4);
            CHECK_THAT(choices[0].node, Catch::Matchers::WithinAbs(4.9804, 0.001));
            CHECK(choices[0].current);
            CHECK(choices[1].partial == 13);
            CHECK_THAT(choices[1].node, Catch::Matchers::WithinAbs(4.5421, 0.001));
            CHECK(choices[2].partial == 15);
        }
        // Four rows, the clear last because the carrier gives it something to do — and Return takes
        // the 13th at index 1, since the ticked 4th would change nothing.
        CHECK(offersClear(*picker));
        CHECK(picker->choices.size() == 4);
        CHECK(picker->preselected == 1);
    }

    fixture.controller.onChartHarmonicNodeRequested(std::nullopt);

    const common::core::Chart* const live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 5);
    CHECK_FALSE(live->notes[0].harmonic_node.has_value());
    // The pinch is untouched: its attack, its fret and the thumb's node all stand.
    CHECK(live->notes[1].attack == common::core::NoteAttack::Pinch);
    CHECK(live->notes[1].fret == 5);
    const std::optional<double>& grazed = live->notes[1].harmonic_node;
    REQUIRE(grazed.has_value());
    if (grazed.has_value())
    {
        CHECK_THAT(*grazed, Catch::Matchers::WithinAbs(17.0, 0.001));
    }
    CHECK(fixture.undoEntries() == entries_before + 1);
}

// A TICKED ROW MAY CHANGE NOTHING, and it is shown anyway: the tick is how the menu says where the
// finger is before it is moved. What it must not be is the row Return takes — the charter would
// press `H`, press Return, and watch nothing happen — so the preselection skips it for the first
// row that actually plans a change. Choosing the dead row explicitly is still allowed and is still
// a no-op: it plans NoChange, so no entry is pushed and the chart stands.
TEST_CASE("A ticked row that changes nothing is shown but not preselected", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 5),
        makeTestNote({.measure = 2, .beat = 1}, 2, 0),
    };
    HarmonicPickerFixture fixture{std::move(chart)};

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartHarmonicRequested();
    REQUIRE(fixture.pickerRequests() == 1);
    fixture.controller.onChartHarmonicNodeRequested(std::optional{4});
    fixture.endRun();

    fixture.controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    fixture.controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    fixture.controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    const std::size_t entries_before = fixture.undoEntries();
    fixture.controller.onChartHarmonicRequested();
    CHECK(fixture.pickerRequests() == 2);

    const std::optional<ChartHarmonicNodePicker> picker = fixture.lastPicker();
    REQUIRE(picker.has_value());
    if (picker.has_value())
    {
        const std::vector<ChartHarmonicNodeChoice> choices = nodeRows(*picker);
        REQUIRE(choices.size() == 3);
        if (choices.size() == 3)
        {
            // Shown and ticked, and the open string cannot move it either: this row is dead.
            CHECK(choices[0].partial == 4);
            CHECK(choices[0].current);
        }
        CHECK(offersClear(*picker));
        CHECK(picker->choices.size() == 4);
        CHECK(picker->preselected == 1);
    }

    fixture.controller.onChartHarmonicNodeRequested(std::optional{4});

    const common::core::Chart* const live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 0);
    const std::optional<double>& unmoved = live->notes[0].harmonic_node;
    REQUIRE(unmoved.has_value());
    if (unmoved.has_value())
    {
        CHECK_THAT(*unmoved, Catch::Matchers::WithinAbs(4.9804, 0.001));
    }
    CHECK(live->notes[1].fret == 0);
    CHECK_FALSE(live->notes[1].harmonic_node.has_value());
    CHECK(fixture.undoEntries() == entries_before);
}

// THE ANCHOR IS THE MEMBER THE ROWS CAME FROM, which need not be the earliest selected note. The
// rows are read off the member whose label names the most nodes, so over a chord whose earlier note
// names ONE node and whose later one names three, the request names the LATER note — and the popup
// has to open on that head, because the fret-5 rows hanging under the fret-7 head would describe a
// note they do not touch.
TEST_CASE("A harmonic press names the member its rows were read from", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    // Chart order is (position, string), so string 1 leads: the UNAMBIGUOUS 7 — one node, the 3rd
    // partial's 7.02 — is the earliest selected, and the ambiguous 5 comes after it.
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 7),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5),
    };
    HarmonicPickerFixture fixture{std::move(chart)};

    fixture.controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    fixture.controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    fixture.controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    fixture.controller.onChartHarmonicRequested();

    CHECK(fixture.pickerRequests() == 1);
    const std::optional<ChartHarmonicNodePicker> picker = fixture.lastPicker();
    REQUIRE(picker.has_value());
    if (picker.has_value())
    {
        CHECK(picker->note == 1);

        // Fret 5's own three rows, the first of them 4.98 rather than the 7's single 7.02: the rows
        // and the named note are one answer, read off the same member.
        const std::vector<ChartHarmonicNodeChoice> choices = nodeRows(*picker);
        REQUIRE(choices.size() == 3);
        if (choices.size() == 3)
        {
            CHECK(choices[0].partial == 4);
            CHECK_THAT(choices[0].node, Catch::Matchers::WithinAbs(4.9804, 0.001));
            CHECK(choices[1].partial == 13);
            CHECK(choices[2].partial == 15);
        }
        // Neither member carries anything, so there is no clear row and Return takes the first.
        CHECK_FALSE(offersClear(*picker));
        CHECK(picker->preselected == 0);
    }
}

// THE PROLOGUE RUNS BEFORE THE LABEL IS READ. The fork is asked below the settle, so a fret still
// being typed commits FIRST and the rows describe the chart the choice will land on: the freshly
// typed fret's nodes, never the fret the note wore when the digit was pressed.
TEST_CASE("A pending fret entry settles before the harmonic rows are read", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    // Select the fret-3 note and type `1`: provisional, so the chart holds nothing of it yet.
    click(fixture.controller, 40.0f, 220.0f);
    const std::size_t entries_before = fixture.undoEntries();
    fixture.controller.onChartFretDigitTyped(1);
    const EditorViewState* const typed = stateOrNull(fixture.view.last_state);
    REQUIRE(typed != nullptr);
    CHECK(typed->chart_edit.pending_fret.has_value());

    // `H` inside the combine window: the entry settles as its own entry, and what the press then
    // reads is fret 1's four partials.
    fixture.controller.onChartHarmonicRequested();
    const common::core::Chart* const live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 1);
    CHECK_FALSE(live->notes[0].harmonic_node.has_value());

    CHECK(fixture.pickerRequests() == 1);
    const std::optional<ChartHarmonicNodePicker> picker = fixture.lastPicker();
    REQUIRE(picker.has_value());
    if (picker.has_value())
    {
        const std::vector<ChartHarmonicNodeChoice> choices = nodeRows(*picker);
        REQUIRE(choices.size() == 4);
        if (choices.size() == 4)
        {
            CHECK(choices[0].partial == 13);
            CHECK_THAT(choices[0].node, Catch::Matchers::WithinAbs(1.3861, 0.001));
            CHECK(choices[1].partial == 14);
            CHECK_THAT(choices[1].node, Catch::Matchers::WithinAbs(1.2836, 0.001));
            CHECK(choices[2].partial == 15);
            CHECK_THAT(choices[2].node, Catch::Matchers::WithinAbs(1.1943, 0.001));
            CHECK(choices[3].partial == 16);
            CHECK_THAT(choices[3].node, Catch::Matchers::WithinAbs(1.1173, 0.001));
        }
        // The settled fret carries no node, so nothing is on offer to remove.
        CHECK_FALSE(offersClear(*picker));
        CHECK(picker->preselected == 0);
    }

    // ONE entry, the settle's; the press that only asked authored none, and nothing is left
    // pending behind it. Read off the state the HARMONIC press pushed, not the one the digit did.
    CHECK(fixture.undoEntries() == entries_before + 1);
    const EditorViewState* const settled = stateOrNull(fixture.view.last_state);
    REQUIRE(settled != nullptr);
    if (settled != nullptr)
    {
        CHECK_FALSE(settled->chart_edit.pending_fret.has_value());
    }
}

// The skips are silent: an open string states no position — its offset is zero, which is not a
// touch — and a pinch's node belongs to the other hand, so neither names a row. With nothing on
// offer the press asks nothing and writes nothing: no request, no entry, and the pinch keeps the
// node it came in with. (The counted-skip reason waits on the non-modal refusal channel, exactly as
// the legato verb's does.)
TEST_CASE("A harmonic press over notes that name nothing is inert", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    common::core::ChartNote pinch = makeTestNote({.measure = 2, .beat = 1}, 2, 5);
    pinch.attack = common::core::NoteAttack::Pinch;
    pinch.harmonic_node = 17.0;
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 0),
        std::move(pinch),
    };
    HarmonicPickerFixture fixture{std::move(chart)};

    fixture.controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    fixture.controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    fixture.controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    const std::size_t entries_before = fixture.undoEntries();
    fixture.controller.onChartHarmonicRequested();

    CHECK(fixture.pickerRequests() == 0);
    CHECK(fixture.undoEntries() == entries_before);
    const common::core::Chart* const live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 0);
    CHECK_FALSE(live->notes[0].harmonic_node.has_value());
    CHECK(live->notes[1].fret == 5);
    CHECK(live->notes[1].attack == common::core::NoteAttack::Pinch);
    const std::optional<double>& untouched = live->notes[1].harmonic_node;
    REQUIRE(untouched.has_value());
    if (untouched.has_value())
    {
        CHECK_THAT(*untouched, Catch::Matchers::WithinAbs(17.0, 0.001));
    }
}

// WITH NO VIEW TO ASK, THE QUESTION IS DROPPED — like a notice, and never answered behind the
// charter's back. Writing the default here would make `H` mean one thing with a view attached and
// another without, so the press does nothing at all and the fret it would have consumed stands.
TEST_CASE("A harmonic press with no view attached asks and writes nothing", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    const std::size_t entries_before = fixture.undoEntries();

    fixture.controller.detachView();
    fixture.controller.onChartHarmonicRequested();

    const common::core::Chart* const live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 3);
    CHECK_FALSE(live->notes[0].harmonic_node.has_value());
    CHECK(fixture.pickerRequests() == 0);

    // Re-attached only so the history can be read: attachView pushes whatever state the press left
    // behind, and it is the state the press found.
    fixture.controller.attachView(fixture.view);
    CHECK(fixture.undoEntries() == entries_before);
}

// OPENING THE MENU DISARMS NOTHING. A question is not an edit, so a window some other verb staged
// survives an `H` that only asked, and that verb's next press still means what it meant — an
// escaped menu leaves the chart, and that gesture, exactly as it found them.
TEST_CASE("A harmonic press that only asked leaves another verb's window armed", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    click(fixture.controller, 40.0f, 180.0f);
    const std::size_t entries_before = fixture.undoEntries();

    // Vibrato on, which arms the window on Vibrato.
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    const common::core::Chart* live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[1].vibrato == common::core::VibratoState::Narrow);
    CHECK(fixture.undoEntries() == entries_before + 1);

    // `H` asks and writes nothing, so the vibrato window it walked past is untouched.
    fixture.controller.onChartHarmonicRequested();
    REQUIRE(fixture.pickerRequests() == 1);
    CHECK(fixture.undoEntries() == entries_before + 1);

    // The next vibrato press is therefore still the REVERSAL its armed window promised: the entry
    // goes with it, and the history holds what it held before the pair.
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[1].vibrato == common::core::VibratoState::Off);
    CHECK(fixture.undoEntries() == entries_before);
}

// THE CHOICE IS WHAT ENDS IT. The same press that only asks above disarms the foreign window once a
// row WRITES, so the interrupted verb's next press means its ordinary law rather than a reversal of
// an entry the charter has already moved on from. The palm mute stands in for the vibrato here
// deliberately: it says where the picking hand is rather than what the string sounds, so the touch
// does not strip it, and the second press is unambiguously the CLEAR half of the toggle.
TEST_CASE("A harmonic choice ends another verb's window", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    click(fixture.controller, 40.0f, 180.0f);
    const std::size_t entries_before = fixture.undoEntries();

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
    const common::core::Chart* live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[1].palm_mute);
    CHECK(fixture.undoEntries() == entries_before + 1);

    fixture.controller.onChartHarmonicRequested();
    REQUIRE(fixture.pickerRequests() == 1);
    fixture.controller.onChartHarmonicNodeRequested(std::optional{4});
    live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[1].fret == 0);
    CHECK(live->notes[1].palm_mute);
    CHECK(fixture.undoEntries() == entries_before + 2);

    // A fresh clear stacked on top — not the reversal that would have taken the history back to
    // entries_before.
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
    live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK_FALSE(live->notes[1].palm_mute);
    CHECK(fixture.undoEntries() == entries_before + 3);
}

// AN IMPORTED ARTIFICIAL HARMONIC KEEPS THE STOP ITS FRETTING HAND PRESSES, and that fret is a
// label like any other — so the rows are the ones its 5 names, none of them ticked, because the
// node it actually touches is not on the editor's table at all. Every member carries a harmonic, so
// the clear is preselected and Return removes. What the clear cannot do is come back: the node it
// dropped is a payload no arithmetic reconstructs, so a following press is offered the plain fret's
// rows with NO clear on them, and Ctrl+Z is what restores the import.
TEST_CASE("A harmonic press over an imported artificial reads its pressed fret", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    common::core::ChartNote artificial = makeTestNote({.measure = 2, .beat = 1}, 1, 5);
    // Stated rather than left to the default: a plain pick is what makes this the FRETTING hand's
    // artificial harmonic instead of the pinch, whose node belongs to the other hand entirely.
    artificial.attack = common::core::NoteAttack::Pick;
    artificial.harmonic_node = 17.0;
    chart.notes = {std::move(artificial)};
    HarmonicPickerFixture fixture{std::move(chart)};

    click(fixture.controller, 40.0f, 220.0f);
    const std::size_t entries_before = fixture.undoEntries();

    fixture.controller.onChartHarmonicRequested();
    CHECK(fixture.pickerRequests() == 1);
    const std::optional<ChartHarmonicNodePicker> picker = fixture.lastPicker();
    REQUIRE(picker.has_value());
    if (picker.has_value())
    {
        const std::vector<ChartHarmonicNodeChoice> choices = nodeRows(*picker);
        REQUIRE(choices.size() == 3);
        if (choices.size() == 3)
        {
            CHECK(choices[0].partial == 4);
            CHECK_THAT(choices[0].node, Catch::Matchers::WithinAbs(4.9804, 0.001));
            CHECK_FALSE(choices[0].current);
            CHECK(choices[1].partial == 13);
            CHECK_FALSE(choices[1].current);
            CHECK(choices[2].partial == 15);
            CHECK_FALSE(choices[2].current);
        }
        CHECK(offersClear(*picker));
        CHECK(picker->choices.size() == 4);
        CHECK(picker->preselected == 3);
    }

    // The clear gives back the stop and nothing else.
    fixture.controller.onChartHarmonicNodeRequested(std::nullopt);
    const common::core::Chart* live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 5);
    CHECK_FALSE(live->notes[0].harmonic_node.has_value());
    CHECK(fixture.undoEntries() == entries_before + 1);

    // A second press finds a plain 5 and offers its rows with nothing to remove: the verb has no
    // memory of the node it dropped, and never pretends to.
    fixture.controller.onChartHarmonicRequested();
    CHECK(fixture.pickerRequests() == 2);
    const std::optional<ChartHarmonicNodePicker> plain = fixture.lastPicker();
    REQUIRE(plain.has_value());
    if (plain.has_value())
    {
        CHECK_FALSE(offersClear(*plain));
        CHECK(plain->choices.size() == 3);
        CHECK(plain->preselected == 0);
    }

    // Ctrl+Z is the restore, and it gives back the imported node exactly.
    fixture.controller.onUndoRequested();
    live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 5);
    const std::optional<double>& restored = live->notes[0].harmonic_node;
    REQUIRE(restored.has_value());
    if (restored.has_value())
    {
        CHECK_THAT(*restored, Catch::Matchers::WithinAbs(17.0, 0.001));
    }
}

// A PARTIAL BINDS THE WHOLE CHORD AND EACH MEMBER LANDS ON ITS OWN NUMBER, and the clear walks
// every one of them back to the stop its node lies at. The 13th partial names a node on both a 3
// and a 5, so one choice touches both; the second press then reads two carriers, offers the rows of
// the label naming the most (the 3's four), and — every member carrying — preselects the clear. One
// press, one entry, and each finger back on the fret it left.
TEST_CASE("A chord of carriers clears in one step and lands each on its own fret", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 3),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5),
    };
    HarmonicPickerFixture fixture{std::move(chart)};

    // Both members become carriers through the verb, so their nodes are bit-for-bit the values the
    // rows name and the ticked row below is decided by the arithmetic rather than by a literal.
    fixture.controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    fixture.controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    fixture.controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    fixture.controller.onChartHarmonicRequested();
    REQUIRE(fixture.pickerRequests() == 1);
    fixture.controller.onChartHarmonicNodeRequested(std::optional{13});

    const common::core::Chart* live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 0);
    const std::optional<double>& third = live->notes[0].harmonic_node;
    REQUIRE(third.has_value());
    if (third.has_value())
    {
        CHECK_THAT(*third, Catch::Matchers::WithinAbs(2.8921, 0.001));
    }
    CHECK(live->notes[1].fret == 0);
    const std::optional<double>& fifth = live->notes[1].harmonic_node;
    REQUIRE(fifth.has_value());
    if (fifth.has_value())
    {
        CHECK_THAT(*fifth, Catch::Matchers::WithinAbs(4.5421, 0.001));
    }

    fixture.endRun();
    const std::size_t entries_before = fixture.undoEntries();

    fixture.controller.onChartHarmonicRequested();
    CHECK(fixture.pickerRequests() == 2);
    const std::optional<ChartHarmonicNodePicker> picker = fixture.lastPicker();
    REQUIRE(picker.has_value());
    if (picker.has_value())
    {
        // The anchor is the carrier at 2.89, whose label is the 3 its node lies at: four nodes
        // against the other member's three.
        CHECK(picker->note == 0);
        const std::vector<ChartHarmonicNodeChoice> choices = nodeRows(*picker);
        CHECK(choices.size() == 4);
        // Every member carries, so the clear is on offer and Return takes it — exactly what a
        // toggle would have done.
        CHECK(offersClear(*picker));
        CHECK(picker->choices.size() == 5);
        CHECK(picker->preselected == picker->choices.size() - 1);
    }

    fixture.controller.onChartHarmonicNodeRequested(std::nullopt);

    live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 3);
    CHECK_FALSE(live->notes[0].harmonic_node.has_value());
    CHECK(live->notes[1].fret == 5);
    CHECK_FALSE(live->notes[1].harmonic_node.has_value());
    CHECK(fixture.undoEntries() == entries_before + 1);
}

// THE ROUND TRIP IS EXACT. A set followed by its clear is the identity on the note, not merely on
// the two fields the verb writes: the fret comes back because the clear presses the finger onto the
// stop the node lies at, and nothing else the note carries is disturbed on the way through.
TEST_CASE("A set and its clear leave the note as it was", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = {makeTestNote({.measure = 2, .beat = 1}, 1, 5)};
    HarmonicPickerFixture fixture{std::move(chart)};

    const common::core::Chart* live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    const common::core::ChartNote original = live->notes[0];

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartHarmonicRequested();
    REQUIRE(fixture.pickerRequests() == 1);
    fixture.controller.onChartHarmonicNodeRequested(std::optional{4});
    fixture.endRun();

    fixture.controller.onChartHarmonicRequested();
    CHECK(fixture.pickerRequests() == 2);
    const std::optional<ChartHarmonicNodePicker> picker = fixture.lastPicker();
    REQUIRE(picker.has_value());
    if (picker.has_value())
    {
        // The one member carries, so Return is the clear: the press back is what a toggle meant.
        CHECK(offersClear(*picker));
        CHECK(picker->preselected == picker->choices.size() - 1);
    }

    fixture.controller.onChartHarmonicNodeRequested(std::nullopt);

    live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0] == original);
}

} // namespace rock_hero::editor::core
