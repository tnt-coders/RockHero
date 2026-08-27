#include <algorithm>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <optional>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The chart-editing fixture wired for the arpeggio hold verb: the shared six-string chart opened
// through the controller's normal route, with the quarter-note grid the lane geometry assumes.
struct SilentHoldFixture
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

    // The shared six-string chart by default; a scenario whose narrative needs a stream the
    // fixture does not carry — a legato claim to break, say — passes its own.
    explicit SilentHoldFixture(common::core::Chart chart = makeTestChart())
    {
        controller.attachView(view);
        REQUIRE(loadChartArrangement(controller, project_services, audio, {}, std::move(chart)));
    }
};

// One silently-held stop: a note with no onset at all, which is the whole of how the chart states
// a stop the hand takes without sounding it.
[[nodiscard]] common::core::ChartNote holdNote(
    const common::core::GridPosition& position, const int string, const int fret)
{
    common::core::ChartNote note = makeTestNote(position, string, fret);
    note.attack = common::core::NoteAttack::None;
    note.sustain = common::core::Fraction{};
    return note;
}

// A chart whose string-3 note at measure 2 beat 2 claims a connection back to the ringing note at
// beat 1 — the claim the arpeggio hold's convert case breaks by silencing that predecessor.
[[nodiscard]] common::core::Chart makeClaimedChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    // The string-1 note is what the conversion's product then states a shape WITH: a stop the hand
    // takes alone states nothing, so the verb would refuse the press rather than break the claim,
    // and this scenario is about the claim.
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 3, common::core::Fraction{1}),
        makeTestNote({.measure = 2, .beat = 1}, 3, 5, common::core::Fraction{1}),
        makeTestNote({.measure = 2, .beat = 2}, 3, 7),
    };
    chart.notes[2].attack = common::core::NoteAttack::Legato;
    return chart;
}

// A held two-string shape with one silently-held member on string 3, re-picked once inside the
// span, and the same shape struck again after it. Whether that re-pick is read as the same hand is
// what decides whether this derives ONE span or two, and the held stop's own fret is what the
// derivation asks — which is the composition the bracket-fret edit rides.
[[nodiscard]] common::core::Chart makeHeldShapeChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    // The first pair rings past the beat-2 re-pick and stops short of the beat-3 restrike, so no
    // overlap repair is owed and the stream is already the one every edit re-normalizes to.
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 3, common::core::Fraction{3, 2}),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5, common::core::Fraction{3, 2}),
        holdNote({.measure = 2, .beat = 1}, 3, 9),
        makeTestNote({.measure = 2, .beat = 2}, 3, 9),
        makeTestNote({.measure = 2, .beat = 3}, 1, 3, common::core::Fraction{1}),
        makeTestNote({.measure = 2, .beat = 3}, 2, 5, common::core::Fraction{1}),
    };
    std::ranges::sort(chart.notes, common::core::chartNoteOrderLess);
    return chart;
}

// The chart-editing overlays the controller last published. Bound through the harness's own
// state accessor so a scenario that never pushed reads as a failed REQUIRE rather than a crash.
[[nodiscard]] const ChartEditViewState& chartEditState(const FakeEditorView& view)
{
    const EditorViewState* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    return state->chart_edit;
}

// The tab projection the controller last published — where a held stop's derived consequences (the
// span it opened, the bracket that draws it) are actually observable.
[[nodiscard]] const common::core::ChartViewState& tabProjection(const FakeEditorView& view)
{
    const EditorViewState* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tab != nullptr);
    return *state->tab;
}

// How many silently-held stops the live chart carries, which is what almost every case here is
// counting rather than the whole stream's length.
[[nodiscard]] std::size_t heldStops(const common::core::Chart& chart)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(chart.notes, [](const common::core::ChartNote& note) {
            return common::core::silentHold(note.attack);
        }));
}

} // namespace

// The empty-slot case of the caret-anchored verb: the slot gains a hold at the OPEN string, which
// is the same neutral value the Alt+click placement plants, and the charter states the real stop by
// typing it. The hold becomes the selection, so the armed-caret invariant — the selection is
// exactly what sits under the caret — holds through the press.
TEST_CASE("Arpeggio hold authors a held stop at an empty caret slot", "[core][chart]")
{
    SilentHoldFixture fixture;

    // String 3 at measure 2 beat 1: empty, and the caret arms on the click.
    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartSilentHoldToggleRequested();

    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 4);
    REQUIRE(heldStops(*chart) == 1);
    // String 3 sorts after the fixture's own string-1 and string-2 notes at that onset.
    const common::core::ChartNote& held = chart->notes[2];
    CHECK(held.attack == common::core::NoteAttack::None);
    CHECK(held.position == common::core::GridPosition{.measure = 2, .beat = 1, .offset = {}});
    CHECK(held.string == 3);
    CHECK(held.fret == 0);
    CHECK(held.sustain == common::core::Fraction{});
    CHECK(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{2});
}

// The verb is a true two-press toggle on an empty slot: the second press takes the hold back out,
// and does it by REVERSING the entry the first press pushed, so the pair leaves no history trace at
// all (the same window every technique toggle runs under).
TEST_CASE("Arpeggio hold removes the stop it authored and leaves no trace", "[core][chart]")
{
    SilentHoldFixture fixture;

    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartSilentHoldToggleRequested();
    REQUIRE(heldStops(*chartOrNull(fixture.controller)) == 1);

    fixture.controller.onChartSilentHoldToggleRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(heldStops(*chart) == 0);
    CHECK(chart->notes.size() == 3);
    // No entry survives the pair, so undo reaches PAST it — to the loaded state, which the
    // history has nothing before. A do/undo pair would have left one.
    const EditorViewState* const state = stateOrNull(fixture.view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->undo_enabled);
}

// The convert case: the note at the caret's slot keeps its slot and its FRET and loses everything
// its new attack cannot state. That is the only fret-carrying path there is, since note insertion
// is the editor's only fret-stating flow — place, then promote.
TEST_CASE("Arpeggio hold converts a note into a held stop", "[core][chart]")
{
    SilentHoldFixture fixture;

    // The string-1 note at measure 2 beat 1 carries fret 3 in the shared fixture.
    click(fixture.controller, 40.0f, 220.0f);
    const common::core::ChartNote original = chartOrNull(fixture.controller)->notes[0];
    REQUIRE(original.string == 1);

    fixture.controller.onChartSilentHoldToggleRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    const common::core::ChartNote& held = chart->notes[0];
    CHECK(held.attack == common::core::NoteAttack::None);
    CHECK(held.position == original.position);
    CHECK(held.string == original.string);
    CHECK(held.fret == original.fret);
    CHECK(held.sustain == common::core::Fraction{});
    // The object stayed under the caret, so the armed-caret invariant reads the same after it.
    CHECK(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{0});

    // One entry: a single undo puts the note back field-for-field.
    fixture.controller.onUndoRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[0] == original);

    // And redo re-applies it.
    fixture.controller.onRedoRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::None);
}

// The conversion's second press is what restores the note WHOLE, and it can only be the window's
// exact reversal: a held stop carries no ring and no technique, so no forward law could rebuild
// what the conversion stripped.
TEST_CASE("Arpeggio hold restores the converted note on a second press", "[core][chart]")
{
    SilentHoldFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartSustainAdjustRequested(1);
    const common::core::ChartNote original = chartOrNull(fixture.controller)->notes[0];

    fixture.controller.onChartSilentHoldToggleRequested();
    REQUIRE(heldStops(*chartOrNull(fixture.controller)) == 1);

    fixture.controller.onChartSilentHoldToggleRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(heldStops(*chart) == 0);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[0] == original);

    // No trace: the next undo reaches past the pair to the sustain adjust that preceded it.
    fixture.controller.onUndoRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].sustain == g_fixture_sustain);
}

// Once the window is dead — any other edit, a caret move, undo/redo — the verb's forward meaning
// takes over: pressing it on a held stop SOUNDS that stop again, as a plain pick at the session's
// grid step. It is the honest forward inverse, and the note the conversion took whole is Ctrl+Z's
// business by then rather than the verb's.
TEST_CASE("Arpeggio hold outside its window sounds the stop again", "[core][chart]")
{
    SilentHoldFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartSilentHoldToggleRequested();
    REQUIRE(heldStops(*chartOrNull(fixture.controller)) == 1);

    // A caret move is a commit point: it ends the window.
    click(fixture.controller, 80.0f, 220.0f);
    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartSilentHoldToggleRequested();

    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(heldStops(*chart) == 0);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Pick);
    CHECK(chart->notes[0].fret == 3);
    CHECK(chart->notes[0].sustain.numerator > 0);
}

// A press whose product would state nothing is refused WHOLE, and the note it was asked to hold is
// left exactly as it was. This is the safety property the settle owes the verb: without it the
// press would convert the note and the settle would then delete it, so the charter's own note would
// vanish for pressing a key on it.
TEST_CASE("Arpeggio hold refuses a press that would state nothing", "[core][chart]")
{
    common::core::Chart lone;
    lone.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    lone.notes = {makeTestNote({.measure = 2, .beat = 1}, 1, 3)};
    SilentHoldFixture fixture{lone};
    const common::core::Chart before = *chartOrNull(fixture.controller);

    // Converting the only note at its slot: one member is no shape, so there is nothing for the
    // stop to belong to.
    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartSilentHoldToggleRequested();
    CHECK(*chartOrNull(fixture.controller) == before);

    // And authoring one at an empty slot no shape reaches is the same nothing. Deliberately at
    // another ONSET: a stop authored beside the note, at the slot it sounds on, would be its
    // second member and would state a shape — which is the case above this one.
    click(fixture.controller, 200.0f, 60.0f);
    fixture.controller.onChartSilentHoldToggleRequested();
    CHECK(*chartOrNull(fixture.controller) == before);
    const EditorViewState* const state = stateOrNull(fixture.view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->undo_enabled);
}

// The scope is the selection, so a whole chord converts in ONE press and one entry — and the whole
// PLAN is what the settle judges: these stops are legal together and illegal one at a time, so
// nothing here could be decided note by note.
TEST_CASE("Arpeggio hold converts a whole chord in one entry", "[core][chart]")
{
    common::core::Chart fronted;
    fronted.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    fronted.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 3),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5),
        // The content the pair is stated in front of: a note arriving on a claimed string at
        // exactly that claim's stop. Without it the span law dissolves the shape and the press is
        // refused, which the section below this proves.
        makeTestNote({.measure = 3, .beat = 1}, 1, 3),
    };
    SilentHoldFixture fixture{fronted};
    const common::core::Chart authored = *chartOrNull(fixture.controller);

    doubleClick(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartSilentHoldToggleRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(heldStops(*chart) == 2);
    REQUIRE(chart->notes.size() == 3);

    // One entry for the pair.
    fixture.controller.onUndoRequested();
    CHECK(*chartOrNull(fixture.controller) == authored);

    // Take the content away and the same press states nothing at all, so it refuses whole.
    fronted.notes.pop_back();
    SilentHoldFixture unfronted{fronted};
    const common::core::Chart alone = *chartOrNull(unfronted.controller);
    doubleClick(unfronted.controller, 40.0f, 220.0f);
    unfronted.controller.onChartSilentHoldToggleRequested();
    CHECK(*chartOrNull(unfronted.controller) == alone);
}

// The cascade, in the entry that caused it: an edit that leaves a held stop belonging to no shape
// takes it along, because a stop that states nothing is a note the charter can neither see nor
// select. One undo restores the pair, so the cascade is never a second entry to walk back.
TEST_CASE("An edit that strands a held stop takes it in the same entry", "[core][chart]")
{
    common::core::Chart shaped;
    shaped.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    shaped.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 3),
        holdNote({.measure = 2, .beat = 1}, 2, 5),
    };
    SilentHoldFixture fixture{shaped};
    const common::core::Chart authored = *chartOrNull(fixture.controller);
    REQUIRE(heldStops(authored) == 1);

    // Delete the note the stop was a member WITH: one member is no shape, so the stop goes too.
    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onSelectionDeleteRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes.empty());

    fixture.controller.onUndoRequested();
    CHECK(*chartOrNull(fixture.controller) == authored);
}

// The verb has no operand while the marker is passive and nothing is selected. Silent, not an
// error: pressing a chart verb with no scope is an ordinary thing to do.
TEST_CASE("Arpeggio hold does nothing while the caret is passive", "[core][chart]")
{
    SilentHoldFixture fixture;

    fixture.controller.onChartSilentHoldToggleRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(heldStops(*chart) == 0);
    CHECK(chart->notes.size() == 3);
}

// A held stop is a note, so the editor-wide Delete removes it with no case of its own.
TEST_CASE("Delete removes a selected held stop", "[core][chart]")
{
    SilentHoldFixture fixture;

    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartSilentHoldToggleRequested();
    REQUIRE(heldStops(*chartOrNull(fixture.controller)) == 1);

    fixture.controller.onSelectionDeleteRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(heldStops(*chart) == 0);
    CHECK(chart->notes.size() == 3);
}

// And it rides the move verb like any other note, which is what keeps a hold from being left
// behind on a slot no span reaches any more.
TEST_CASE("A selected held stop moves with the selection", "[core][chart]")
{
    SilentHoldFixture fixture;

    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartSilentHoldToggleRequested();
    REQUIRE(heldStops(*chartOrNull(fixture.controller)) == 1);

    // Up one string: the hold is the selection, so the move acts on it alone.
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Up);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(heldStops(*chart) == 1);
    REQUIRE(chart->notes.size() == 4);
    CHECK(chart->notes[2].attack == common::core::NoteAttack::None);
    CHECK(chart->notes[2].string == 4);

    // The caret rode along with its own object, exactly as it does for a sounding note. Read
    // through the harness's nullable-pointer accessor, the project's standard shape for an
    // optional under a Catch2 assertion: a bare `REQUIRE(...has_value())` is a separate expression
    // from the access after it, which the optional-access analysis cannot tie to the same object.
    const ChartCaretViewState* const caret = caretOrNull(chartEditState(fixture.view));
    REQUIRE(caret != nullptr);
    CHECK(caret->string == 4);
}

// Slot uniqueness is one rule over one stream, so a move onto a slot a held stop occupies is
// refused exactly like a move onto a sounding note's — silently, leaving the selection put.
TEST_CASE("A move onto a held stop's slot is refused", "[core][chart]")
{
    SilentHoldFixture fixture;

    // Author a hold on string 2 at measure 3 beat 1 (x = 80), then select the string-1 note there
    // and try to push it up onto that slot.
    click(fixture.controller, 80.0f, 180.0f);
    fixture.controller.onChartSilentHoldToggleRequested();
    REQUIRE(heldStops(*chartOrNull(fixture.controller)) == 1);

    click(fixture.controller, 80.0f, 220.0f);
    const common::core::Chart before = *chartOrNull(fixture.controller);
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Up);
    CHECK(*chartOrNull(fixture.controller) == before);
}

// The double click's unit is the onset group, and the group is the HAND's at that instant: a stop
// the charter stated silently is a member of the shape the strum takes, so it comes with the chord
// rather than being left behind by every verb the group feeds. It needs no rule of its own now —
// the group is one equal_range over one stream.
TEST_CASE("Double-clicking an onset takes its held stop with the chord", "[core][chart]")
{
    SilentHoldFixture fixture;

    // String 3 at measure 2 beat 1 — the same onset the fixture's two-note chord sits on.
    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartSilentHoldToggleRequested();
    REQUIRE(heldStops(*chartOrNull(fixture.controller)) == 1);

    doubleClick(fixture.controller, 40.0f, 220.0f);
    CHECK(chartEditState(fixture.view).selected_notes == (std::vector<std::size_t>{0, 1, 2}));

    // The later onset's group is its own: nothing from measure 2 rides along.
    doubleClick(fixture.controller, 80.0f, 220.0f);
    CHECK(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{3});
}

// One gesture over the group is one undo entry, holds included.
TEST_CASE("Deleting a group clears its held stop in one entry", "[core][chart]")
{
    SilentHoldFixture fixture;

    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartSilentHoldToggleRequested();
    const common::core::Chart authored = *chartOrNull(fixture.controller);
    REQUIRE(heldStops(authored) == 1);

    doubleClick(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onSelectionDeleteRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    // Only measure 3's note survives the onset's group delete.
    REQUIRE(chart->notes.size() == 1);
    CHECK(chart->notes.front().position == common::core::GridPosition{.measure = 3, .beat = 1});

    // One entry: a single undo restores the whole group.
    fixture.controller.onUndoRequested();
    CHECK(*chartOrNull(fixture.controller) == authored);
}

// A conversion that breaks a legato claim is settled by the sweep at the next commit point, and
// the sweep FOLDS its flatten into the conversion's own entry — replacing it. The replacement has
// to describe the whole burst, so one undo takes both back together.
TEST_CASE("A conversion that breaks a claim survives the settle sweep", "[core][chart]")
{
    SilentHoldFixture fixture{makeClaimedChart()};
    const common::core::Chart authored = *chartOrNull(fixture.controller);

    // The ringing string-3 note at measure 2 beat 1 becomes a held stop; the claim at beat 2 now
    // has nothing to connect to.
    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartSilentHoldToggleRequested();
    REQUIRE(heldStops(*chartOrNull(fixture.controller)) == 1);

    // A caret move is a commit point, so the sweep runs here.
    click(fixture.controller, 200.0f, 60.0f);

    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[1].attack == common::core::NoteAttack::None);
    CHECK(chart->notes[1].fret == 5);
    // The flatten landed too: the claim reads as the pick it plays as.
    CHECK(chart->notes[2].attack == common::core::NoteAttack::Pick);

    // One entry describes the conversion AND the flatten, so one undo restores the whole thing.
    fixture.controller.onUndoRequested();
    CHECK(*chartOrNull(fixture.controller) == authored);
}

// The insert affordances read the same slot authority the caret does, so an Alt+click over a held
// stop never plants a note on top of one — slot uniqueness holds without a second guard.
TEST_CASE("Alt+click refuses to plant a note on a held stop's slot", "[core][chart]")
{
    SilentHoldFixture fixture;

    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartSilentHoldToggleRequested();
    REQUIRE(heldStops(*chartOrNull(fixture.controller)) == 1);

    click(fixture.controller, 200.0f, 60.0f);
    click(fixture.controller, 40.0f, 140.0f, ChartPointerModifiers{.alt = true});

    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes.size() == 4);
    CHECK(heldStops(*chart) == 1);
}

// The reported sighting, end to end. Converting one member of a two-note chord used to make the
// shape evaporate: the remaining note was suddenly a lone onset, no span opened, and the fret the
// conversion had just kept had nowhere to be drawn. Under the member rule (2026-08-27) the sound
// and the held finger are two members, the span opens, and BOTH stops reach the posture — which is
// also what makes the hold visible at all, since it has no head of its own.
TEST_CASE("A converted chord member keeps its fret in the span's posture", "[core][chart]")
{
    SilentHoldFixture fixture;

    // The string-1 note at measure 2 beat 1 (fret 3), beside the string-2 note (fret 5).
    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartSilentHoldToggleRequested();

    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    // The convert path keeps the note's own fret, which is what there is to derive.
    CHECK(chart->notes[0].attack == common::core::NoteAttack::None);
    CHECK(chart->notes[0].fret == 3);

    const common::core::ChartViewState& tab = tabProjection(fixture.view);
    REQUIRE(tab.shapes.size() == 1);
    CHECK(tab.shapes.front().arpeggio);
    CHECK(
        tab.shapes.front().strings == std::vector<common::core::ShapeStringViewState>{
                                          {.string = 1, .fret = 3}, {.string = 2, .fret = 5}
                                      });
    // And the hold draws: its face is the span's start, 2.0s under the fixture's tempo map.
    // Bound to a name before it is read, so the guard and the access are provably one object.
    REQUIRE(tab.notes.size() == 3);
    const std::optional<double>& bracket = tab.notes.front().bracket_seconds;
    REQUIRE(bracket.has_value());
    if (bracket.has_value())
    {
        CHECK_THAT(*bracket, Catch::Matchers::WithinAbs(2.0, 1e-9));
    }
}

// A selected bracket takes a typed fret exactly as a selected head does, through the same
// pending-entry model — a held stop is a note and its fret is a fret. Nothing else in the span
// moves, because a fret verb edits exactly the selected notes' own stops.
TEST_CASE("Typing a digit on a selected bracket states its stop", "[core][chart]")
{
    SilentHoldFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartSilentHoldToggleRequested();
    REQUIRE(chartOrNull(fixture.controller)->notes[0].fret == 3);
    const common::core::ChartNote neighbour = chartOrNull(fixture.controller)->notes[1];

    // The conversion leaves the hold selected, so the digit lands on it. 7 cannot be widened at
    // the 24-fret cap, so it settles in the same keystroke.
    fixture.controller.onChartFretDigitTyped(7);

    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::None);
    CHECK(chart->notes[0].fret == 7);
    // Only the bracket moved: the note beside it in the span keeps its own fret.
    CHECK(chart->notes[1] == neighbour);

    // The posture follows through the ordinary derivation — nothing writes it twice.
    const common::core::ChartViewState& tab = tabProjection(fixture.view);
    REQUIRE(tab.shapes.size() == 1);
    CHECK(
        tab.shapes.front().strings == std::vector<common::core::ShapeStringViewState>{
                                          {.string = 1, .fret = 7}, {.string = 2, .fret = 5}
                                      });

    // One undo entry, named like any other typed fret, and it puts the authored stop back.
    const EditorViewState* const state = stateOrNull(fixture.view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->undo_label == std::optional<std::string>{"Set Fret 7"});
    fixture.controller.onUndoRequested();
    CHECK(chartOrNull(fixture.controller)->notes[0].fret == 3);
}

// The coherence half, and it is a COMPOSITION rather than a rule: a bracket whose stop now
// contradicts the note that re-picks its string is no longer the same hand, side ruling (ii)
// declines to carry the span across that re-pick, and the shape splits in two. Nothing in the fret
// verb knows about spans; the derivation answers on its own.
TEST_CASE("A bracket retyped against its span's own note splits the span", "[core][chart]")
{
    SilentHoldFixture fixture{makeHeldShapeChart()};

    // One span to start with: the claim states fret 9 and the beat-2 re-pick sounds fret 9, so the
    // hand never leaves the shape and the beat-3 restrike merges into the same span.
    REQUIRE(tabProjection(fixture.view).shapes.size() == 1);
    CHECK(
        tabProjection(fixture.view).shapes.front().strings ==
        std::vector<common::core::ShapeStringViewState>{
            {.string = 1, .fret = 3}, {.string = 2, .fret = 5}, {.string = 3, .fret = 9}
        });

    // Select the bracket itself — string 3 at the span's start, where no note sounds — and state a
    // stop the beat-2 note contradicts.
    click(fixture.controller, 40.0f, 140.0f);
    REQUIRE(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{2});
    fixture.controller.onChartFretDigitTyped(7);

    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[2].attack == common::core::NoteAttack::None);
    CHECK(chart->notes[2].fret == 7);
    // The other notes are untouched, which the fret-verb law states outright.
    CHECK(chart->notes[3].fret == 9);

    // And the shape splits rather than printing a stop the notes inside it disagree with.
    const common::core::ChartViewState& tab = tabProjection(fixture.view);
    REQUIRE(tab.shapes.size() == 2);
    CHECK(
        tab.shapes.front().strings ==
        std::vector<common::core::ShapeStringViewState>{
            {.string = 1, .fret = 3}, {.string = 2, .fret = 5}, {.string = 3, .fret = 7}
        });
}

} // namespace rock_hero::editor::core
