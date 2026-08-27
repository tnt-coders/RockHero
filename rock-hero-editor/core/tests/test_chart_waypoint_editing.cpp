#include <optional>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The chart every scenario here opens: one pitched glide on string 3, a lane the shared fixture
// leaves empty so a probe can only land on the gesture under test. Measure 2 beat 1 is 2.0s
// (x = 40 at the fixture geometry's 20 px/s, y = 140 on string 3), the ring runs eight beats to
// 6.0s, and the junction it arrives at four beats in draws its linked head at 4.0s (x = 80) —
// far enough from the onset head that the two boxes cannot overlap.
[[nodiscard]] common::core::Chart makeGlideChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote glide =
        makeTestNote({.measure = 2, .beat = 1}, 3, 5, common::core::Fraction{8});
    glide.waypoints = {common::core::Waypoint{.offset = common::core::Fraction{4}, .fret = 9}};
    chart.notes = {std::move(glide)};
    return chart;
}

// The chart-editing fixture wired for the waypoint verbs: the glide chart opened through the
// controller's normal route, with the quarter-note grid the lane geometry assumes.
struct WaypointFixture
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

    explicit WaypointFixture(common::core::Chart chart = makeGlideChart())
    {
        controller.attachView(view);
        REQUIRE(loadChartArrangement(controller, project_services, audio, {}, std::move(chart)));
    }
};

[[nodiscard]] const EditorViewState& publishedState(const FakeEditorView& view)
{
    const EditorViewState* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    return *state;
}

// The chart as it stands, copied so a scenario can compare a later chart against it field for
// field — which is what an undo round trip has to prove.
[[nodiscard]] common::core::Chart currentChart(const EditorController& controller)
{
    const common::core::Chart* const chart = chartOrNull(controller);
    REQUIRE(chart != nullptr);
    return *chart;
}

// Lane-local pixels of the fixture's two clickable marks.
constexpr float g_onset_x{40.0f};
constexpr float g_junction_x{80.0f};
constexpr float g_string_3_y{140.0f};

} // namespace

// A waypoint is a selection citizen: the linked head the lane draws at a junction is clickable,
// and clicking it selects the waypoint alone. It occupies no slot, so nothing arms a caret for it
// — the marker demotes to a cursor in place, exactly as every multi-select gesture leaves it.
TEST_CASE("A click selects the waypoint under it", "[core][chart]")
{
    WaypointFixture fixture;

    // Arm the caret on the note first, so the demotion below is a state CHANGE and not the
    // absence a fresh controller would report anyway.
    click(fixture.controller, g_onset_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.caret.has_value());
    REQUIRE(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{0});

    click(fixture.controller, g_junction_x, g_string_3_y);
    const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
    CHECK(
        edit.selected_waypoints ==
        (std::vector<ChartWaypointRef>{ChartWaypointRef{.note_index = 0, .waypoint_index = 0}}));
    CHECK(edit.selected_notes.empty());
    CHECK_FALSE(edit.caret.has_value());
    // The selection is not empty, which is what makes the selection-scoped verbs reachable.
    CHECK(publishedState(fixture.view).selection_present);
}

// The vibrato channel's second authoring scope: with a waypoint selected, `V` states the shake AT
// that waypoint and leaves the note's onset statement alone. The second press inside the verb
// window takes it back — and takes the statement out entirely rather than writing a false one,
// because a statement that restates the state already in force changes neither the path nor the
// state (the generalized dissolve law).
TEST_CASE("The vibrato verb states the shake at a selected waypoint", "[core][chart]")
{
    WaypointFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);

    const common::core::Chart shaking = currentChart(fixture.controller);
    REQUIRE(shaking.notes.size() == 1);
    REQUIRE(shaking.notes[0].waypoints.size() == 1);
    CHECK(shaking.notes[0].waypoints[0].vibrato == true);
    // The onset is untouched: the ring opens still and shakes from the junction on.
    CHECK_FALSE(shaking.notes[0].vibrato);
    // And the position channel rides along unchanged — one record, so the coupling needs no copy.
    CHECK(shaking.notes[0].waypoints[0].fret == 9);

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    const common::core::Chart cleared = currentChart(fixture.controller);
    CHECK(cleared == original);
    // A statement saying what was already true is no statement, so the waypoint keeps only its
    // fret rather than gaining a stated `false`.
    REQUIRE(cleared.notes[0].waypoints.size() == 1);
    CHECK_FALSE(cleared.notes[0].waypoints[0].vibrato.has_value());
    // The pair reversed its own entry, so it leaves no history trace at all.
    CHECK_FALSE(publishedState(fixture.view).undo_enabled);
}

// The same clear a press LATER, with the toggle window closed behind a selection change, so the
// dissolve law itself does the work rather than the window's exact reversal: the press writes a
// statement, the statement says what was already true, and a waypoint the drop empties would go
// with it. Here the point states a fret too, so what dissolves is the statement alone — and this
// is a NEW undo entry, not a reversal, which is what the round trip below proves.
TEST_CASE("Clearing the shake at a waypoint dissolves the statement it wrote", "[core][chart]")
{
    WaypointFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    const common::core::Chart shaking = currentChart(fixture.controller);
    REQUIRE(shaking.notes[0].waypoints[0].vibrato == true);

    // A selection change commits that entry and closes the verb window, so the press below runs
    // the verb's ordinary law instead of reversing anything.
    click(fixture.controller, g_onset_x, g_string_3_y);
    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);

    const common::core::Chart cleared = currentChart(fixture.controller);
    CHECK(cleared == original);
    REQUIRE(cleared.notes[0].waypoints.size() == 1);
    CHECK_FALSE(cleared.notes[0].waypoints[0].vibrato.has_value());
    CHECK(cleared.notes[0].waypoints[0].fret == 9);

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == shaking);
    fixture.controller.onRedoRequested();
    CHECK(currentChart(fixture.controller) == cleared);
    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == shaking);
}

// Delete reaches waypoints exactly as it reaches notes and hold markers: it takes every statement
// the selected waypoint makes, and a waypoint stating nothing is no record at all, so the point
// goes with them. The round trip is field-exact in both directions.
TEST_CASE("Delete takes the selected waypoint and undo puts it back", "[core][chart]")
{
    WaypointFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onSelectionDeleteRequested();

    const common::core::Chart cut = currentChart(fixture.controller);
    REQUIRE(cut.notes.size() == 1);
    CHECK(cut.notes[0].waypoints.empty());
    // The note it rode is untouched — deleting a waypoint is not deleting the gesture.
    CHECK(cut.notes[0].fret == 5);
    CHECK(cut.notes[0].sustain == common::core::Fraction{8});

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
    fixture.controller.onRedoRequested();
    CHECK(currentChart(fixture.controller) == cut);
    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// `Shift+L` on a selected waypoint severs the gesture there (W10's 2026-08-26 addendum): the
// note's path ends at the junction and a new head takes the remainder. One compound undo entry
// spanning both products, reversed exactly.
TEST_CASE("The disconnect verb severs the gesture at a selected waypoint", "[core][chart]")
{
    WaypointFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartWaypointDisconnectRequested();

    const common::core::Chart severed = currentChart(fixture.controller);
    REQUIRE(severed.notes.size() == 2);
    CHECK(severed.notes[0].position == original.notes[0].position);
    CHECK(severed.notes[0].fret == 5);
    CHECK(severed.notes[0].sustain == common::core::Fraction{4});
    // The origin keeps the waypoint it travels to: the leg the user split at is real travel, and
    // the junction is now an equal-fret handover to the new head. The arrival lands the
    // glide-into-a-landing margin before that head (a quarter beat in 4/4) — a fret-stating
    // waypoint may not sit on a later onset of its own string — while the RING still runs to it.
    REQUIRE(severed.notes[0].waypoints.size() == 1);
    CHECK(severed.notes[0].waypoints[0].fret == 9);
    CHECK(severed.notes[0].waypoints[0].offset == common::core::Fraction{15, 4});

    CHECK(
        severed.notes[1].position ==
        common::core::GridPosition{.measure = 3, .beat = 1, .offset = {}});
    CHECK(severed.notes[1].string == 3);
    CHECK(severed.notes[1].fret == 9);
    CHECK(severed.notes[1].sustain == common::core::Fraction{4});
    CHECK(severed.notes[1].waypoints.empty());
    // W10's signed store for a split head. The addendum's proposed UNSTRUCK-tie reading needs
    // LegatoMotion::Continuation, which is unbuilt — the default is a proposal, not a ruling.
    CHECK(severed.notes[1].attack == common::core::NoteAttack::Legato);

    // The SPLIT PRODUCT becomes the selection — it is the one record the plan inserted at a new
    // key, which the follow rule already calls "the edit's own product". So the next verb acts on
    // the new head, and the origin (rewritten in place, and never selected) does not join. The
    // junction key itself resolves to nothing now: its arrival retreated off the offset it named,
    // which is the linger, and nothing wears a ring for it.
    CHECK(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{1});
    CHECK(publishedState(fixture.view).chart_edit.selected_waypoints.empty());

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
    fixture.controller.onRedoRequested();
    CHECK(currentChart(fixture.controller) == severed);
    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// The verb is selection-scoped like every technique verb beside it, so a selection holding no
// waypoint is simply no operand — pressing it is inert, not an error, and leaves no entry.
TEST_CASE("The disconnect verb is inert without a waypoint selected", "[core][chart]")
{
    WaypointFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    fixture.controller.onChartWaypointDisconnectRequested();
    CHECK(currentChart(fixture.controller) == original);

    click(fixture.controller, g_onset_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{0});
    fixture.controller.onChartWaypointDisconnectRequested();
    CHECK(currentChart(fixture.controller) == original);
    CHECK_FALSE(publishedState(fixture.view).undo_enabled);
}

// The arrow move's operand is the two SLOT-keyed arrays, so a waypoint-only selection is no
// operand and the press is inert. This is not a policy preference: the verb reads a selected slot
// to answer the meter question, so a selection carrying no slot has no front to read at all — the
// widening made `selection is not empty` and `this verb has an operand` two different questions,
// and the guard has to ask the second.
TEST_CASE("The arrow move is inert with only a waypoint selected", "[core][chart]")
{
    WaypointFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_waypoints.size() == 1);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_notes.empty());
    REQUIRE(publishedState(fixture.view).chart_edit.selected_hold_markers.empty());

    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(currentChart(fixture.controller) == original);
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(currentChart(fixture.controller) == original);
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Up);
    CHECK(currentChart(fixture.controller) == original);
    CHECK_FALSE(publishedState(fixture.view).undo_enabled);

    // And the guard does not over-reach: a selection that DOES hold a slot still moves, waypoint
    // riding along in the selection or not.
    click(fixture.controller, g_onset_x, g_string_3_y, ChartPointerModifiers{.ctrl = true});
    REQUIRE(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{0});
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    const common::core::Chart moved = currentChart(fixture.controller);
    REQUIRE(moved.notes.size() == 1);
    CHECK(
        moved.notes[0].position ==
        common::core::GridPosition{.measure = 2, .beat = 2, .offset = {}});
}

} // namespace rock_hero::editor::core
