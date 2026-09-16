#include <catch2/catch_approx.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The rows of the LAST picker request, or an empty list when no press has asked for one. The
// REQUEST is the whole published surface: the fork lives in the controller, which decides it
// against the chart the press lands on, so what the view was handed is the only record that a
// press meant "the charter chooses".
[[nodiscard]] std::vector<ChartHarmonicNodeChoice> lastPickerChoices(const FakeEditorView& view)
{
    return view.shown_harmonic_pickers.empty() ? std::vector<ChartHarmonicNodeChoice>{}
                                               : view.shown_harmonic_pickers.back().choices;
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

    // The rows the view was last asked to open its popup on.
    [[nodiscard]] std::vector<ChartHarmonicNodeChoice> choices() const
    {
        return lastPickerChoices(view);
    }

    // The note the last request named — the member the rows were read from, and the head the popup
    // anchors on. Empty when no press has asked for one.
    [[nodiscard]] std::optional<std::size_t> pickerNote() const
    {
        return view.shown_harmonic_pickers.empty()
                   ? std::nullopt
                   : std::optional<std::size_t>{view.shown_harmonic_pickers.back().note};
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
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);

    // Exactly one request, its rows ascending by partial. That order IS the contract: the view
    // preselects the first row, and the same row is what the planner's own default would write.
    CHECK(fixture.pickerRequests() == 1);
    const std::vector<ChartHarmonicNodeChoice> choices = fixture.choices();
    REQUIRE(choices.size() == 4);
    if (choices.size() == 4)
    {
        CHECK(choices[0].partial == 6);
        CHECK(choices[0].node == Catch::Approx(3.1564).margin(0.001));
        CHECK(choices[1].partial == 7);
        CHECK(choices[1].node == Catch::Approx(2.6687).margin(0.001));
        CHECK(choices[2].partial == 11);
        CHECK(choices[2].node == Catch::Approx(3.4741).margin(0.001));
        CHECK(choices[3].partial == 13);
        CHECK(choices[3].node == Catch::Approx(2.8921).margin(0.001));
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
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);
    REQUIRE(fixture.pickerRequests() == 1);
    const std::size_t entries_before = fixture.undoEntries();

    fixture.controller.onChartHarmonicNodeRequested(7);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].fret == 0);
    const std::optional<double>& node = chart->notes[0].harmonic_node;
    REQUIRE(node.has_value());
    if (node.has_value())
    {
        CHECK(*node == Catch::Approx(2.6687).margin(0.001));
    }
    CHECK(fixture.undoEntries() == entries_before + 1);

    fixture.controller.onUndoRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].fret == 3);
    CHECK_FALSE(chart->notes[0].harmonic_node.has_value());
}

// A fret naming ONE node has nothing to choose, so the press is an ordinary technique toggle that
// writes in the same keystroke: 12 names the octave and nothing else inside the partial bound, and
// the charter is never shown a menu with a single row on it.
TEST_CASE("A label naming one node writes with no request", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = {makeTestNote({.measure = 2, .beat = 1}, 1, 12)};
    HarmonicPickerFixture fixture{std::move(chart)};

    click(fixture.controller, 40.0f, 220.0f);
    const std::size_t entries_before = fixture.undoEntries();
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);

    const common::core::Chart* const live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 0);
    const std::optional<double>& node = live->notes[0].harmonic_node;
    REQUIRE(node.has_value());
    if (node.has_value())
    {
        CHECK(*node == Catch::Approx(12.0).margin(0.001));
    }
    CHECK(fixture.undoEntries() == entries_before + 1);
    CHECK(fixture.view.shown_harmonic_pickers.empty());
}

// UNIFORM SCOPE OVER A CHORD: one press, ONE request, and the rows come off the FIRST ambiguous
// member — the fret-3 note's. One member speaks for the scope because a row carries a PARTIAL,
// and that partial binds every member whose own label offers it; where it lands is each note's
// own arithmetic, so one partial prints two numbers. A member whose label does not name the chosen
// partial is not skipped: it takes its own first row, what a choiceless press would have written.
TEST_CASE("A harmonic press over a chord requests one picker for the whole scope", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    fixture.controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    fixture.controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    fixture.controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    const std::size_t entries_before = fixture.undoEntries();
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);

    CHECK(fixture.pickerRequests() == 1);
    // The request names the member the rows came from — here the fret-3 note, projection index 0,
    // which is also the earliest selected. The next case separates the two.
    const std::optional<std::size_t> anchor = fixture.pickerNote();
    REQUIRE(anchor.has_value());
    if (anchor.has_value())
    {
        CHECK(*anchor == 0);
    }
    const std::vector<ChartHarmonicNodeChoice> choices = fixture.choices();
    REQUIRE_FALSE(choices.empty());
    if (!choices.empty())
    {
        CHECK(choices.front().partial == 6);
        CHECK(choices.front().node == Catch::Approx(3.1564).margin(0.001));
    }

    // Nothing lands until a row is chosen — not even on the member whose label named one node.
    const common::core::Chart* const asked = chartOrNull(fixture.controller);
    REQUIRE(asked != nullptr);
    CHECK(asked->notes[0].fret == 3);
    CHECK_FALSE(asked->notes[0].harmonic_node.has_value());
    CHECK(asked->notes[1].fret == 5);
    CHECK_FALSE(asked->notes[1].harmonic_node.has_value());
    CHECK(fixture.undoEntries() == entries_before);

    SECTION("a partial both labels name lands on both, in one entry")
    {
        fixture.controller.onChartHarmonicNodeRequested(13);
        const common::core::Chart* const chart = chartOrNull(fixture.controller);
        REQUIRE(chart != nullptr);
        const std::optional<double>& ambiguous = chart->notes[0].harmonic_node;
        REQUIRE(ambiguous.has_value());
        if (ambiguous.has_value())
        {
            CHECK(*ambiguous == Catch::Approx(2.8921).margin(0.001));
        }
        const std::optional<double>& other = chart->notes[1].harmonic_node;
        REQUIRE(other.has_value());
        if (other.has_value())
        {
            CHECK(*other == Catch::Approx(4.5421).margin(0.001));
        }
        CHECK(fixture.undoEntries() == entries_before + 1);
    }

    SECTION("a partial only one label names leaves the other on its default")
    {
        // Fret 5 names the 4th, 13th and 15th partials and no 7th, so it takes its own first row.
        fixture.controller.onChartHarmonicNodeRequested(7);
        const common::core::Chart* const chart = chartOrNull(fixture.controller);
        REQUIRE(chart != nullptr);
        const std::optional<double>& ambiguous = chart->notes[0].harmonic_node;
        REQUIRE(ambiguous.has_value());
        if (ambiguous.has_value())
        {
            CHECK(*ambiguous == Catch::Approx(2.6687).margin(0.001));
        }
        const std::optional<double>& other = chart->notes[1].harmonic_node;
        REQUIRE(other.has_value());
        if (other.has_value())
        {
            CHECK(*other == Catch::Approx(4.9804).margin(0.001));
        }
        CHECK(fixture.undoEntries() == entries_before + 1);
    }
}

// THE ANCHOR IS THE MEMBER THE ROWS CAME FROM, which need not be the earliest selected note. The
// rows are read off the first AMBIGUOUS member, so over a chord whose earlier note names ONE node
// and whose later one names three, the request names the LATER note — and the popup has to open on
// that head, because the fret-5 rows hanging under the fret-7 head would describe a note they do
// not touch.
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

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);

    CHECK(fixture.pickerRequests() == 1);
    const std::optional<std::size_t> anchor = fixture.pickerNote();
    REQUIRE(anchor.has_value());
    if (anchor.has_value())
    {
        CHECK(*anchor == 1);
    }

    // Fret 5's own three rows, the first of them 4.98 rather than the 7's single 7.02: the rows and
    // the named note are one answer, read off the same member.
    const std::vector<ChartHarmonicNodeChoice> choices = fixture.choices();
    REQUIRE(choices.size() == 3);
    if (choices.size() == 3)
    {
        CHECK(choices[0].partial == 4);
        CHECK(choices[0].node == Catch::Approx(4.9804).margin(0.001));
        CHECK(choices[1].partial == 13);
        CHECK(choices[2].partial == 15);
    }
}

// THE REVERSAL A CLEAR MUST STILL GET. An imported artificial harmonic keeps the stop its fretting
// hand presses, and that fret is a label like any other — but a press over a selection that
// ALREADY carries the technique means REMOVE, so there is nothing to choose and no menu to open.
// The clear's own arithmetic gives back only the fret; the node it dropped is a payload it cannot
// reconstruct, so the second press must REVERSE that entry rather than re-derive anything — and
// the pair must leave the history exactly as it found it.
TEST_CASE("A harmonic press that cleared reverses exactly on the next press", "[core][chart]")
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

    // A preceding entry the pair must not disturb, so "no trace" can be read off the history.
    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartSustainAdjustRequested(1);
    const std::size_t entries_before = fixture.undoEntries();

    // The first press CLEARS: the node goes, the pressed stop stays, and nothing was asked.
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);
    const common::core::Chart* live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 5);
    CHECK_FALSE(live->notes[0].harmonic_node.has_value());
    CHECK(fixture.pickerRequests() == 0);
    CHECK(fixture.undoEntries() == entries_before + 1);

    // The second press reverses that entry exactly: the node is the one the import authored, not
    // one the standing fret happens to name — and it still opens no picker.
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);
    live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 5);
    const std::optional<double>& restored = live->notes[0].harmonic_node;
    REQUIRE(restored.has_value());
    if (restored.has_value())
    {
        CHECK(*restored == Catch::Approx(17.0).margin(0.001));
    }
    CHECK(fixture.pickerRequests() == 0);

    // No trace: the reversal consumed the clear's own entry, so the history holds what it held
    // before the pair and the next undo reaches past it to the sustain adjust.
    CHECK(fixture.undoEntries() == entries_before);
    fixture.controller.onUndoRequested();
    live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].sustain == g_fixture_sustain);
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
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);
    const common::core::Chart* const live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 1);
    CHECK_FALSE(live->notes[0].harmonic_node.has_value());

    CHECK(fixture.pickerRequests() == 1);
    const std::vector<ChartHarmonicNodeChoice> choices = fixture.choices();
    REQUIRE(choices.size() == 4);
    if (choices.size() == 4)
    {
        CHECK(choices[0].partial == 13);
        CHECK(choices[0].node == Catch::Approx(1.3861).margin(0.001));
        CHECK(choices[1].partial == 14);
        CHECK(choices[1].node == Catch::Approx(1.2836).margin(0.001));
        CHECK(choices[2].partial == 15);
        CHECK(choices[2].node == Catch::Approx(1.1943).margin(0.001));
        CHECK(choices[3].partial == 16);
        CHECK(choices[3].node == Catch::Approx(1.1173).margin(0.001));
    }

    // ONE entry, the settle's; the press that only asked authored none, and nothing is left
    // pending behind it. Read off the state the TOGGLE pushed, not the one the digit did.
    CHECK(fixture.undoEntries() == entries_before + 1);
    const EditorViewState* const settled = stateOrNull(fixture.view.last_state);
    REQUIRE(settled != nullptr);
    if (settled != nullptr)
    {
        CHECK_FALSE(settled->chart_edit.pending_fret.has_value());
    }
}

// The skips are silent: an open string states no position — its offset is zero, which is not a
// touch — and a pinch's node belongs to the other hand, so neither names a row. With nothing to
// choose the press falls through to the verb, which finds nothing it can state either: no request,
// no entry, and the pinch keeps the node it came in with. (The counted-skip reason waits on the
// non-modal refusal channel, exactly as the legato verb's does.)
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
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);

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
        CHECK(*untouched == Catch::Approx(17.0).margin(0.001));
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
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);

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

// A PRESS THAT ONLY ASKED STILL DISARMS. The verb window belongs to the last press, whichever verb
// it was, and `H` consumes it on the way past even when it commits nothing — otherwise the window
// another verb armed would outlive the press that interrupted it, and that verb's next press would
// reverse an entry the charter had already moved on from.
TEST_CASE("A harmonic press that only asked disarms another verb's window", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    const std::size_t entries_before = fixture.undoEntries();

    // Vibrato on, which arms the window on Vibrato.
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    const common::core::Chart* live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].vibrato == common::core::VibratoState::Narrow);
    CHECK(fixture.undoEntries() == entries_before + 1);

    // `H` asks and writes nothing — but the window it walked past is gone.
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);
    REQUIRE(fixture.pickerRequests() == 1);
    CHECK(fixture.undoEntries() == entries_before + 1);

    // So the next vibrato press is a fresh CLEAR — its own entry on top — rather than the reversal
    // the armed window would have made, which would have taken the history back to entries_before.
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].vibrato == common::core::VibratoState::Off);
    CHECK(fixture.undoEntries() == entries_before + 2);
}

} // namespace rock_hero::editor::core
