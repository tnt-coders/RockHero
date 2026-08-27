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
struct HoldMarkerFixture
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
    explicit HoldMarkerFixture(common::core::Chart chart = makeTestChart())
    {
        controller.attachView(view);
        REQUIRE(loadChartArrangement(controller, project_services, audio, {}, std::move(chart)));
    }
};

// A chart whose string-3 note at measure 2 beat 2 claims a connection back to the ringing note at
// beat 1 — the claim the arpeggio hold's convert case breaks by taking that predecessor out of the
// stream.
[[nodiscard]] common::core::Chart makeClaimedChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 3, 5, common::core::Fraction{1}),
        makeTestNote({.measure = 2, .beat = 2}, 3, 7),
    };
    chart.notes[1].attack = common::core::NoteAttack::Legato;
    return chart;
}

// A held two-string shape with one silently-held member on string 3, re-picked once inside the
// span, and the same shape struck again after it. Whether that re-pick is read as the same hand
// is what decides whether this derives ONE span or two, and the marker's own stop is what the
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
        makeTestNote({.measure = 2, .beat = 2}, 3, 9),
        makeTestNote({.measure = 2, .beat = 3}, 1, 3, common::core::Fraction{1}),
        makeTestNote({.measure = 2, .beat = 3}, 2, 5, common::core::Fraction{1}),
    };
    chart.hold_markers = {
        common::core::ChartHoldMarker{
            .position = common::core::GridPosition{.measure = 2, .beat = 1, .offset = {}},
            .string = 3,
            .fret = {},
        },
    };
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

// The tab projection the controller last published — where a marker's derived consequences (the
// span it opened, the bracket that draws it) are actually observable.
[[nodiscard]] const common::core::ChartViewState& tabProjection(const FakeEditorView& view)
{
    const EditorViewState* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tab != nullptr);
    return *state->tab;
}

} // namespace

// Case 1 of the caret-anchored verb: an empty armed slot gains a marker that states NO fret,
// because a later in-span note on that string is what supplies it (the reported case authors a
// WHEN, never a WHAT). The marker becomes the selection, so the armed-caret invariant — the
// selection is exactly what sits under the caret — reads the same for a marker as for a note.
TEST_CASE("Arpeggio hold authors a fret-less marker at an empty caret slot", "[core][chart]")
{
    HoldMarkerFixture fixture;

    // String 3 at measure 2 beat 1: empty, and the caret arms on the click.
    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartHoldMarkerToggleRequested();

    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->hold_markers.size() == 1);
    const common::core::ChartHoldMarker& marker = chart->hold_markers.front();
    CHECK(marker.position == common::core::GridPosition{.measure = 2, .beat = 1, .offset = {}});
    CHECK(marker.string == 3);
    CHECK_FALSE(marker.fret.has_value());
    // The note stream is untouched: a marker is not a note by another name.
    CHECK(chart->notes.size() == 3);
    CHECK(chartEditState(fixture.view).selected_hold_markers == std::vector<std::size_t>{0});
    CHECK(chartEditState(fixture.view).selected_notes.empty());
}

// The verb is a true two-press toggle on an empty slot: the second press takes the marker back
// out, and does it by REVERSING the entry the first press pushed, so the pair leaves no history
// trace at all (the same window every technique toggle runs under).
TEST_CASE("Arpeggio hold removes the marker it authored and leaves no trace", "[core][chart]")
{
    HoldMarkerFixture fixture;

    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartHoldMarkerToggleRequested();
    REQUIRE(chartOrNull(fixture.controller)->hold_markers.size() == 1);

    fixture.controller.onChartHoldMarkerToggleRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->hold_markers.empty());
    // No entry survives the pair, so undo reaches PAST it — to the loaded state, which the
    // history has nothing before. A do/undo pair would have left one.
    const EditorViewState* const state = stateOrNull(fixture.view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->undo_enabled);
}

// Case 2: a note at the caret's slot is CONVERTED. The note leaves the stream and the marker
// carries its fret — the only fret-carrying path there is, since note insertion is the editor's
// only fret-stating flow. The gesture crosses both authored arrays and is ONE undo entry, which
// is the whole reason the plan spans them.
TEST_CASE("Arpeggio hold converts a note into a fret-carrying marker", "[core][chart]")
{
    HoldMarkerFixture fixture;

    // The string-1 note at measure 2 beat 1 carries fret 3 in the shared fixture.
    click(fixture.controller, 40.0f, 220.0f);
    const common::core::ChartNote original = chartOrNull(fixture.controller)->notes[0];
    REQUIRE(original.string == 1);

    fixture.controller.onChartHoldMarkerToggleRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes.size() == 2);
    REQUIRE(chart->hold_markers.size() == 1);
    const common::core::ChartHoldMarker& marker = chart->hold_markers.front();
    CHECK(marker.position == original.position);
    CHECK(marker.string == original.string);
    CHECK(marker.fret == std::optional{original.fret});
    // The selection followed the object ACROSS the arrays, so the caret still sits on what is
    // selected.
    CHECK(chartEditState(fixture.view).selected_notes.empty());
    CHECK(chartEditState(fixture.view).selected_hold_markers == std::vector<std::size_t>{0});

    // One entry: a single undo puts the note back field-for-field AND takes the marker away.
    fixture.controller.onUndoRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->hold_markers.empty());
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[0] == original);

    // And redo re-applies both halves together.
    fixture.controller.onRedoRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes.size() == 2);
    CHECK(chart->hold_markers.size() == 1);
}

// The conversion's second press is what restores the note, and it can only be the window's exact
// reversal: the marker stores a fret and nothing else, so no clear law could rebuild the ring,
// attack and techniques a note carried.
TEST_CASE("Arpeggio hold restores the converted note on a second press", "[core][chart]")
{
    HoldMarkerFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartSustainAdjustRequested(1);
    const common::core::ChartNote original = chartOrNull(fixture.controller)->notes[0];

    fixture.controller.onChartHoldMarkerToggleRequested();
    REQUIRE(chartOrNull(fixture.controller)->hold_markers.size() == 1);

    fixture.controller.onChartHoldMarkerToggleRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->hold_markers.empty());
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[0] == original);

    // No trace: the next undo reaches past the pair to the sustain adjust that preceded it.
    fixture.controller.onUndoRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].sustain == g_fixture_sustain);
}

// Once the window is dead — any other edit, a caret move, undo/redo — pressing the verb on a
// marker simply removes it, which is the plain inverse of authoring one. The note a conversion
// took is Ctrl+Z's business by then, not the verb's.
TEST_CASE("Arpeggio hold on a marker outside its window just removes it", "[core][chart]")
{
    HoldMarkerFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartHoldMarkerToggleRequested();
    REQUIRE(chartOrNull(fixture.controller)->hold_markers.size() == 1);

    // A caret move is a commit point: it ends the window.
    click(fixture.controller, 80.0f, 220.0f);
    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartHoldMarkerToggleRequested();

    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->hold_markers.empty());
    // The slot is EMPTY, not re-noted: removing a marker outside the window restores nothing.
    CHECK(chart->notes.size() == 2);
}

// The verb is caret-anchored, so the passive state gives it nothing to act on. Silent, not an
// error: pressing a chart verb with no caret armed is an ordinary thing to do.
TEST_CASE("Arpeggio hold does nothing while the caret is passive", "[core][chart]")
{
    HoldMarkerFixture fixture;

    fixture.controller.onChartHoldMarkerToggleRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->hold_markers.empty());
    CHECK(chart->notes.size() == 3);
}

// Markers are selectable objects, so the editor-wide Delete removes them exactly like notes.
TEST_CASE("Delete removes a selected hold marker", "[core][chart]")
{
    HoldMarkerFixture fixture;

    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartHoldMarkerToggleRequested();
    REQUIRE(chartOrNull(fixture.controller)->hold_markers.size() == 1);

    fixture.controller.onSelectionDeleteRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->hold_markers.empty());
    CHECK(chart->notes.size() == 3);
}

// A marker rides the move verb with the notes rather than staying put: a marker left behind by a
// moved chord opens under no span and goes silently inert, which loses the hand fact without
// making anything false — the quietest failure and therefore the one worth designing out.
TEST_CASE("A selected hold marker moves with the selection", "[core][chart]")
{
    HoldMarkerFixture fixture;

    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartHoldMarkerToggleRequested();
    REQUIRE(chartOrNull(fixture.controller)->hold_markers.size() == 1);

    // Up one string: the marker is the selection, so the move acts on it alone.
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Up);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->hold_markers.size() == 1);
    CHECK(chart->hold_markers.front().string == 4);
    CHECK(chart->notes.size() == 3);

    // The caret rode along with its own object, exactly as it does for a lone note. Read through
    // the harness's nullable-pointer accessor, the project's standard shape for an optional under
    // a Catch2 assertion: a bare `REQUIRE(...has_value())` is a separate expression from the
    // access after it, which the optional-access analysis cannot tie to the same object.
    const ChartCaretViewState* const caret = caretOrNull(chartEditState(fixture.view));
    REQUIRE(caret != nullptr);
    CHECK(caret->string == 4);
}

// Disjointness is one rule over one slot space, so a move onto a slot a marker occupies is
// refused exactly like a move onto an occupied note slot — silently, leaving the selection put.
TEST_CASE("A move onto a hold marker's slot is refused", "[core][chart]")
{
    HoldMarkerFixture fixture;

    // Author a marker on string 2 at measure 3 beat 1 (x = 80), then select the string-1 note
    // there and try to push it up onto that slot.
    click(fixture.controller, 80.0f, 180.0f);
    fixture.controller.onChartHoldMarkerToggleRequested();
    REQUIRE(chartOrNull(fixture.controller)->hold_markers.size() == 1);

    click(fixture.controller, 80.0f, 220.0f);
    const common::core::Chart before = *chartOrNull(fixture.controller);
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Up);
    CHECK(*chartOrNull(fixture.controller) == before);
}

// The marker is drawn as its own mark and hit-tests on that mark, so clicking one selects it —
// the affordance and the pixels are the same rectangle by construction.
TEST_CASE("Clicking a hold marker's mark selects it", "[core][chart]")
{
    HoldMarkerFixture fixture;

    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartHoldMarkerToggleRequested();
    REQUIRE(chartOrNull(fixture.controller)->hold_markers.size() == 1);

    // Leave the mark, then come back to it by pointer alone.
    click(fixture.controller, 200.0f, 60.0f);
    CHECK(chartEditState(fixture.view).selected_hold_markers.empty());

    click(fixture.controller, 40.0f, 140.0f);
    CHECK(chartEditState(fixture.view).selected_hold_markers == std::vector<std::size_t>{0});
}

// The double click's unit is the onset group, and the group is the HAND's at that instant: a stop
// the charter marked silently is a member of the shape the strum takes, so it comes with the
// chord rather than being left behind by every verb the group feeds.
TEST_CASE("Double-clicking an onset takes its hold marker with the chord", "[core][chart]")
{
    HoldMarkerFixture fixture;

    // String 3 at measure 2 beat 1 — the same onset the fixture's two-note chord sits on.
    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartHoldMarkerToggleRequested();
    REQUIRE(chartOrNull(fixture.controller)->hold_markers.size() == 1);

    doubleClick(fixture.controller, 40.0f, 220.0f);
    CHECK(chartEditState(fixture.view).selected_notes == (std::vector<std::size_t>{0, 1}));
    CHECK(chartEditState(fixture.view).selected_hold_markers == std::vector<std::size_t>{0});

    // The later onset's group is its own: nothing from measure 2 rides along.
    doubleClick(fixture.controller, 80.0f, 220.0f);
    CHECK(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{2});
    CHECK(chartEditState(fixture.view).selected_hold_markers.empty());
}

// One gesture over a mixed selection is one undo entry across both arrays, which is the whole
// reason the plan was widened rather than composed: a composite would need an order between its
// halves, and the round-trip would depend on two sides agreeing about it by hand.
TEST_CASE("Deleting a mixed selection clears both arrays in one entry", "[core][chart]")
{
    HoldMarkerFixture fixture;

    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartHoldMarkerToggleRequested();
    const common::core::Chart authored = *chartOrNull(fixture.controller);
    REQUIRE(authored.hold_markers.size() == 1);

    doubleClick(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onSelectionDeleteRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->hold_markers.empty());
    // Only measure 3's note survives the onset's group delete.
    REQUIRE(chart->notes.size() == 1);
    CHECK(chart->notes.front().position == common::core::GridPosition{.measure = 3, .beat = 1});

    // One entry: a single undo restores both arrays at once.
    fixture.controller.onUndoRequested();
    CHECK(*chartOrNull(fixture.controller) == authored);
}

// A conversion that breaks a legato claim is settled by the sweep at the next commit point, and
// the sweep FOLDS its flatten into the conversion's own entry — replacing it. The replacement has
// to describe the whole burst: the note the conversion took and the marker it authored, not just
// the claim the sweep flattened. A note-only fold would walk the chart back through the
// conversion's reversal and never re-apply the marker half, leaving the note deleted and the mark
// gone in one silent step.
TEST_CASE("A conversion that breaks a claim survives the settle sweep", "[core][chart]")
{
    HoldMarkerFixture fixture{makeClaimedChart()};
    const common::core::Chart authored = *chartOrNull(fixture.controller);

    // The ringing string-3 note at measure 2 beat 1 becomes the marker; the claim at beat 2 now
    // has nothing to connect to.
    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartHoldMarkerToggleRequested();
    REQUIRE(chartOrNull(fixture.controller)->hold_markers.size() == 1);

    // A caret move is a commit point, so the sweep runs here.
    click(fixture.controller, 200.0f, 60.0f);

    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->hold_markers.size() == 1);
    const common::core::ChartHoldMarker& marker = chart->hold_markers.front();
    CHECK(marker.position == common::core::GridPosition{.measure = 2, .beat = 1, .offset = {}});
    CHECK(marker.string == 3);
    CHECK(marker.fret == std::optional{5});
    // The flatten landed too: the claim reads as the pick it plays as.
    REQUIRE(chart->notes.size() == 1);
    CHECK(chart->notes.front().attack == common::core::NoteAttack::Pick);

    // One entry describes the conversion AND the flatten, so one undo restores the whole thing.
    fixture.controller.onUndoRequested();
    CHECK(*chartOrNull(fixture.controller) == authored);
}

// The insert affordances read the same slot authority the caret does, so an Alt+click over a
// marker never plants a note on top of one — disjointness holds without a second guard.
TEST_CASE("Alt+click refuses to plant a note on a hold marker's slot", "[core][chart]")
{
    HoldMarkerFixture fixture;

    click(fixture.controller, 40.0f, 140.0f);
    fixture.controller.onChartHoldMarkerToggleRequested();
    REQUIRE(chartOrNull(fixture.controller)->hold_markers.size() == 1);

    click(fixture.controller, 200.0f, 60.0f);
    click(fixture.controller, 40.0f, 140.0f, ChartPointerModifiers{.alt = true});

    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes.size() == 3);
    CHECK(chart->hold_markers.size() == 1);
}

// The reported sighting, end to end. Converting one member of a two-note chord used to make the
// shape evaporate: the remaining note was suddenly a lone onset, no span opened, and the fret the
// conversion had just copied onto the marker had nowhere to be drawn. Under the member rule
// (2026-08-27) the sound and the held finger are two members, the span opens, and BOTH stops
// reach the posture — which is also what makes the marker visible at all now that it has no mark
// of its own.
TEST_CASE("A converted chord member keeps its fret in the span's posture", "[core][chart]")
{
    HoldMarkerFixture fixture;

    // The string-1 note at measure 2 beat 1 (fret 3), beside the string-2 note (fret 5).
    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartHoldMarkerToggleRequested();

    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->hold_markers.size() == 1);
    // Ruling 2: the convert path stores the note's own fret, which is what there is to derive.
    CHECK(chart->hold_markers.front().fret == std::optional{3});

    const common::core::ChartViewState& tab = tabProjection(fixture.view);
    REQUIRE(tab.shapes.size() == 1);
    CHECK(tab.shapes.front().arpeggio);
    CHECK(
        tab.shapes.front().strings == std::vector<common::core::ShapeStringViewState>{
                                          {.string = 1, .fret = 3}, {.string = 2, .fret = 5}
                                      });
    // And the marker draws: its bracket is the span's start, 2.0s under the fixture's tempo map.
    // Bound to a name before it is read, so the guard and the access are provably one object.
    REQUIRE(tab.hold_markers.size() == 1);
    const std::optional<double>& bracket = tab.hold_markers.front().bracket_seconds;
    REQUIRE(bracket.has_value());
    if (bracket.has_value())
    {
        CHECK_THAT(*bracket, Catch::Matchers::WithinAbs(2.0, 1e-9));
    }
}

// Ruling 4: a selected bracket takes a typed fret exactly as a selected head does, through the
// same pending-entry model. The bracket is the marker's only mark, so this is the one way its
// stop is authored after the toggle states it — and nothing else in the span moves.
TEST_CASE("Typing a digit on a selected bracket states its stop", "[core][chart]")
{
    HoldMarkerFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartHoldMarkerToggleRequested();
    REQUIRE(chartOrNull(fixture.controller)->hold_markers.front().fret == std::optional{3});
    const std::vector<common::core::ChartNote> notes_before =
        chartOrNull(fixture.controller)->notes;

    // The conversion leaves the marker selected, so the digit lands on it. 7 cannot be widened
    // at the 24-fret cap, so it settles in the same keystroke.
    fixture.controller.onChartFretDigitTyped(7);

    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->hold_markers.size() == 1);
    CHECK(chart->hold_markers.front().fret == std::optional{7});
    // Only the bracket moved: the note beside it in the span keeps its own fret.
    CHECK(chart->notes == notes_before);

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
    CHECK(chartOrNull(fixture.controller)->hold_markers.front().fret == std::optional{3});
}

// The coherence half of ruling 4, and it is a COMPOSITION rather than a rule: a bracket whose
// stop now contradicts the note that re-picks its string is no longer the same hand, side ruling
// (ii) declines to carry the span across that re-pick, and the shape splits in two. Nothing in
// the fret verb knows about spans; the derivation answers on its own.
TEST_CASE("A bracket retyped against its span's own note splits the span", "[core][chart]")
{
    HoldMarkerFixture fixture{makeHeldShapeChart()};
    const std::vector<common::core::ChartNote> notes_before =
        chartOrNull(fixture.controller)->notes;

    // One span to start with: the fret-less claim takes its stop from the beat-2 re-pick, so the
    // hand never leaves the shape and the beat-3 restrike merges into the same span.
    REQUIRE(tabProjection(fixture.view).shapes.size() == 1);
    CHECK(
        tabProjection(fixture.view).shapes.front().strings ==
        std::vector<common::core::ShapeStringViewState>{
            {.string = 1, .fret = 3}, {.string = 2, .fret = 5}, {.string = 3, .fret = 9}
        });

    // Select the bracket itself — string 3 at the span's start, where no note sounds — and state
    // a stop the beat-2 note contradicts.
    click(fixture.controller, 40.0f, 140.0f);
    REQUIRE(chartEditState(fixture.view).selected_hold_markers == std::vector<std::size_t>{0});
    fixture.controller.onChartFretDigitTyped(7);

    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->hold_markers.front().fret == std::optional{7});
    // The other notes are untouched, which the ruling states outright.
    CHECK(chart->notes == notes_before);

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
