#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <rock_hero/editor/core/timeline/timeline_geometry.h>
#include <vector>

namespace rock_hero::editor::core
{

// A glyph click selects the note and never seeks the transport.
TEST_CASE("EditorController selects a chart note on glyph click", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    const int seek_baseline = transport.seek_call_count;

    // The containment hierarchy: a single click selects the individual note, a double click its
    // whole onset group.
    click(controller, 40.0f, 220.0f);

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const ChartEditViewState& edit = state->chart_edit;
    CHECK(edit.selected_notes == std::vector<std::size_t>{0});
    CHECK(transport.seek_call_count == seek_baseline);

    doubleClick(controller, 40.0f, 220.0f);
    CHECK(state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 1}));

    // A sustain-tail click (right of the measure-3 head, inside its one-second tail) selects
    // NOTHING: heads are targets, tails are testimony. The click moves the caret to the empty slot
    // under the pointer, which is what a click in this lane means everywhere a mark is not —
    // selecting a note whose onset is elsewhere would be the one place it meant something else.
    click(controller, 97.0f, 220.0f);
    CHECK(state->chart_edit.selected_notes.empty());
    CHECK(state->chart_edit.caret.has_value());
}

// Ctrl toggles individual membership; plain clicks select individual notes per the containment
// hierarchy.
TEST_CASE("EditorController toggles and extends the chart selection", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));

    // The plain click selects one note and arms the caret on it; Ctrl adds another
    // individually AND dissolves the caret into a cursor in its place (a multi-select gesture;
    // the paused seek carries the transport to the former caret's 2.0s slot).
    click(controller, 40.0f, 220.0f);
    click(controller, 40.0f, 180.0f, ChartPointerModifiers{.ctrl = true});
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 1}));
    CHECK_FALSE(state->chart_edit.caret.has_value());
    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{2.0}});

    // Toggling the same note again removes it.
    click(controller, 40.0f, 180.0f, ChartPointerModifiers{.ctrl = true});
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0});

    // Ctrl on NOTHING is inert: an empty slot has no member to toggle, so a misclick made while
    // holding the membership key neither arms a caret there nor clears what was picked.
    click(controller, 120.0f, 220.0f, ChartPointerModifiers{.ctrl = true});
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0});
    CHECK_FALSE(state->chart_edit.caret.has_value());

    // Shift on a note behaves as a plain click and replaces the selection with the clicked note;
    // on this lane Shift extends only a marquee box.
    click(controller, 80.0f, 220.0f, ChartPointerModifiers{.shift = true});
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{2});

    // A plain click on one note of a multi-selection collapses the selection to it.
    click(controller, 40.0f, 220.0f, ChartPointerModifiers{.ctrl = true});
    CHECK(state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 2}));
    click(controller, 40.0f, 220.0f);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0});
}

// One selection exists editor-wide: selecting on another surface structurally replaces the chart
// selection, cross-surface selection changes never touch the marker, and the unified Delete intent
// deletes whatever kind the selection holds.
TEST_CASE("EditorController keeps one selection across surfaces", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));

    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0});
    CHECK(state->chart_edit.caret.has_value());

    // Selecting an automation point (another surface) replaces the chart selection, and the
    // marker follows the click onto the lane row (clicks arm on both surfaces): the chart-row
    // caret unpublishes while the caret rides the lane.
    controller.onToneAutomationPointSelectRequested(
        "instance-x", "gain", common::core::GridPosition{.measure = 1, .beat = 1, .offset = {}});
    CHECK(state->chart_edit.selected_notes.empty());
    CHECK_FALSE(state->chart_edit.caret.has_value());

    // Delete on the (stale — no such plugin) automation selection removes nothing.
    const auto* chart = chartOrNull(controller);
    REQUIRE(chart != nullptr);
    const std::size_t notes_before = chart->notes.size();
    controller.onSelectionDeleteRequested();
    CHECK(chartOrNull(controller)->notes.size() == notes_before);

    // A fresh chart selection then deletes through the very same intent.
    click(controller, 40.0f, 220.0f);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0});
    controller.onSelectionDeleteRequested();
    CHECK(chartOrNull(controller)->notes.size() == notes_before - 1);
    CHECK(state->chart_edit.selected_notes.empty());
}

// A CLICK NEVER CREATES, whatever modifiers it carries: the pointer's whole job on this lane is to
// say where the next digit lands. So a press on an empty slot arms the caret and authors nothing,
// bare or under Alt, and a press on an occupied one selects what is there.
TEST_CASE("EditorController arms the caret on a click and creates nothing", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    const std::size_t notes_before = chartOrNull(controller)->notes.size();

    // x = 200 is 10.0s (measure 6, later than every fixture note) on the empty string-4 lane.
    click(controller, 200.0f, 100.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(chartOrNull(controller)->notes.size() == notes_before);
    CHECK(state->chart_edit.selected_notes.empty());
    const ChartCaretViewState* caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(10.0));
    CHECK(caret->string == 4);

    // Alt is the reveal modifier here and nothing else: the same empty slot, the same arm.
    click(controller, 200.0f, 100.0f, ChartPointerModifiers{.alt = true});
    CHECK(chartOrNull(controller)->notes.size() == notes_before);
    CHECK(state->chart_edit.selected_notes.empty());

    // Alt on an OCCUPIED slot selects what is there, exactly as a plain press does.
    click(controller, 40.0f, 220.0f, ChartPointerModifiers{.alt = true});
    CHECK(chartOrNull(controller)->notes.size() == notes_before);
    CHECK(state->chart_edit.selected_notes.size() == 1);
    // The selected note (2.0s) is what a verb acts on, not the cursor the dissolved caret left at
    // 10.0s.
    CHECK(state->selection_start_seconds == std::optional{2.0});
}

// With no create gesture left on the pointer, Alt has nothing to suppress: a drag from an empty
// slot is the ordinary marquee whether or not Alt is held, and it plants nothing.
TEST_CASE("EditorController boxes a marquee on an Alt+drag", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    const std::size_t notes_before = chartOrNull(controller)->notes.size();

    controller.onChartPointerDown(pointerEvent(200.0f, 100.0f, ChartPointerModifiers{.alt = true}));
    controller.onChartPointerDrag(pointerEvent(240.0f, 100.0f, ChartPointerModifiers{.alt = true}));
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->chart_edit.marquee.has_value());

    controller.onChartPointerUp(pointerEvent(240.0f, 100.0f, ChartPointerModifiers{.alt = true}));
    CHECK(chartOrNull(controller)->notes.size() == notes_before);
}

// Typed digits set every selected note to the typed value; Alt+Shift+wheel's fret-shift intent
// moves the whole selection by one, shape-preserving, refusing (never clamping) at fret zero
// and at the fret cap.
TEST_CASE("EditorController sets frets by typing and shifts them by wheel", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));

    // The measure-2 chord is frets 3 and 5; double-clicking selects the whole chord and
    // typing 9 sets BOTH members to 9.
    doubleClick(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 1}));
    controller.onChartFretDigitTyped(9);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 9);
    CHECK(chart->notes[1].fret == 9);
    CHECK(state->undo_label == std::optional<std::string>{"Set Fret 9"});

    // One undo restores both members in one step.
    controller.onUndoRequested();
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 3);
    CHECK(chart->notes[1].fret == 5);

    // The fret shift moves the shape as a unit.
    controller.onChartFretShiftRequested(1);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 4);
    CHECK(chart->notes[1].fret == 6);

    // Shifting down stops when the lowest fret reaches zero: four downward ticks land on 0/2
    // and the fifth is refused.
    for (int step = 0; step < 5; ++step)
    {
        controller.onChartFretShiftRequested(-1);
    }
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 0);
    CHECK(chart->notes[1].fret == 2);

    // Shifting up stops when the highest fret reaches the cap (24): 22 upward ticks land on
    // 22/24 and every further tick is refused.
    for (int step = 0; step < 29; ++step)
    {
        controller.onChartFretShiftRequested(1);
    }
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 22);
    CHECK(chart->notes[1].fret == 24);
}

// An empty-lane click places the caret at the snapped slot on the clicked string — never a
// transport seek (the caret model: play-from-caret makes the caret the seek).
TEST_CASE("EditorController places the caret on empty click", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    click(controller, 40.0f, 220.0f);
    const int seek_baseline = transport.seek_call_count;

    // x = 200 is 10.0s (a grid beat at 120 BPM); y = 100 is the string-4 lane.
    click(controller, 200.0f, 100.0f);

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const ChartEditViewState& edit = state->chart_edit;
    CHECK(edit.selected_notes.empty());
    const ChartCaretViewState* caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(10.0));
    CHECK(caret->string == 4);
    CHECK(transport.seek_call_count == seek_baseline);
}

// An empty-lane drag past the click threshold marquees instead of seeking; release selects the
// boxed notes and Shift extends the box into the existing selection.
TEST_CASE("EditorController marquee selects boxed chart notes", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    const int seek_baseline = transport.seek_call_count;

    controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const ChartMarqueeViewState* marquee = marqueeOrNull(state->chart_edit);
    REQUIRE(marquee != nullptr);
    CHECK(marquee->start_seconds == Catch::Approx(1.0));
    CHECK(marquee->end_seconds == Catch::Approx(3.0));

    controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));
    CHECK(state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 1}));
    CHECK_FALSE(state->chart_edit.marquee.has_value());
    CHECK(transport.seek_call_count == seek_baseline);

    // Shift-marquee over the measure-3 note extends the selection.
    controller.onChartPointerDown(
        pointerEvent(70.0f, 200.0f, ChartPointerModifiers{.shift = true}));
    controller.onChartPointerDrag(
        pointerEvent(95.0f, 239.0f, ChartPointerModifiers{.shift = true}));
    controller.onChartPointerUp(pointerEvent(95.0f, 239.0f, ChartPointerModifiers{.shift = true}));
    CHECK(state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 1, 2}));

    // Ctrl-marquee toggles the boxed set as one unit — the box form of Ctrl+click. The measure-3
    // note is already in, so the box takes it out; the same box again brings it back, and the
    // rest of the selection stands throughout.
    const auto ctrl_box_measure_3 = [&controller] {
        const ChartPointerModifiers ctrl{.ctrl = true};
        controller.onChartPointerDown(pointerEvent(70.0f, 200.0f, ctrl));
        controller.onChartPointerDrag(pointerEvent(95.0f, 239.0f, ctrl));
        controller.onChartPointerUp(pointerEvent(95.0f, 239.0f, ctrl));
    };
    ctrl_box_measure_3();
    CHECK(state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 1}));
    ctrl_box_measure_3();
    CHECK(state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 1, 2}));

    // Dissolution is a rule over outcomes: a marquee whose box catches NOTHING is a complete
    // no-op — an armed caret survives with no dissolution seek — while a box that catches
    // notes dissolves the caret to a cursor in its place.
    click(controller, 240.0f, 100.0f);
    REQUIRE(state->chart_edit.caret.has_value());
    const int armed_seek_baseline = transport.seek_call_count;
    controller.onChartPointerDown(pointerEvent(300.0f, 60.0f));
    controller.onChartPointerDrag(pointerEvent(340.0f, 140.0f));
    controller.onChartPointerUp(pointerEvent(340.0f, 140.0f));
    const ChartCaretViewState* caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(12.0));
    CHECK(transport.seek_call_count == armed_seek_baseline);
    controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));
    CHECK(state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 1}));
    CHECK_FALSE(state->chart_edit.caret.has_value());
    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{12.0}});
}

// While playing, lane clicks are plain seeks (the marker model): there is no caret to place
// and no selection to build, so the lane behaves like the waveform around it.
TEST_CASE("EditorController seeks on lane clicks while playing", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    transport.setStateAndNotify(common::audio::TransportState{.playing = true});

    // x = 60 maps to 3.0s, exactly on the quarter grid; the click seeks there and neither
    // arms the caret nor selects the note under the pointer.
    click(controller, 60.0f, 220.0f);

    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{3.0}});
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->chart_edit.selected_notes.empty());
    CHECK_FALSE(state->chart_edit.caret.has_value());
}

// Loading a different song clears the selection so keys never leak across charts.
TEST_CASE("EditorController clears chart selection on project load", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE_FALSE(state->chart_edit.selected_notes.empty());

    REQUIRE(loadChartArrangement(controller, project_services, audio));
    CHECK(state->chart_edit.selected_notes.empty());
}

// The lane is handed ONE projection that carries both lengths of every note — the ink it draws
// and the controller hit-tests against, and the stored ring the Alt reveal draws to — so a chart
// edit rebuilds them together under the one memo key. A reveal reading a length the rebuild left
// stale would show a wrong picture of exactly the ring the verb in the user's hand is changing.
TEST_CASE("EditorController publishes the ink and the ring in one projection", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tab != nullptr);
    REQUIRE(state->tab->notes.size() == 3);

    // The measure-2 pair is a chug — an eighth of a beat each — so presentation drops both tails
    // and the lane draws a bare head at 2.0s, while the ring the reveal draws is still stored.
    CHECK_THAT(
        state->tab->notes[0].ink_end_seconds,
        Catch::Matchers::WithinULP(state->tab->notes[0].start_seconds, 0));
    CHECK(state->tab->notes[0].ring_end_seconds == Catch::Approx(2.0625));

    // A chart edit rebuilds the projection: one grid step of the sustain verb moves the clicked
    // note's ring END onto the next grid line — the chug's eighth of a beat ends between lines, so
    // the step snaps it to the beat at 2.5s rather than adding half a second to it — and the
    // reveal must show that immediately.
    const std::shared_ptr<const common::core::ChartViewState> before = state->tab;
    click(controller, 40.0f, 220.0f);
    controller.onChartSustainAdjustRequested(1);
    REQUIRE(state->tab != nullptr);
    CHECK(state->tab != before);
    CHECK(state->tab->notes[0].ring_end_seconds == Catch::Approx(2.5));
}

// The quantum-versus-grid-value distinction, on one verb: with snap off a placement's POSITION
// lands on the tick lattice, while the ring it authors still reaches the next line of the session
// GRID. A quantum-long default would be a tick of sound, which is the mistake this pins against.
TEST_CASE("Grid snap moves the insert position but never the insert's ring", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    const std::size_t notes_before = chartOrNull(controller)->notes.size();

    // The fixture pins a quarter-note grid, which is one beat at 120 BPM 4/4 — the ring every
    // placement below must author whatever snap says.
    constexpr common::core::Fraction grid_step_beats{1};

    // x = 203 inverts to a time BETWEEN quarter lines, so the two lattices give different answers
    // and the assertions can tell which one the placement took.
    constexpr float off_grid_x = 203.0f;
    const common::core::TempoMap& tempo_map = controller.session().song().tempo_map;
    const common::ui::TabLaneGeometry geometry = makeGeometry();
    const std::optional<common::core::TimePosition> clicked = timelinePositionForX(
        off_grid_x, geometry.visible_timeline, static_cast<int>(geometry.bounds_width));
    REQUIRE(clicked.has_value());
    if (!clicked.has_value())
    {
        return;
    }
    const common::core::GridPosition grid_slot =
        nearestTempoGridPosition(tempo_map, common::core::Fraction{1, 4}, *clicked);
    const common::core::GridPosition tick_slot =
        nearestTempoGridPosition(tempo_map, common::core::g_tick_quantum_note_value, *clicked);
    REQUIRE(grid_slot != tick_slot);

    // Snap on: the click arms on the grid line and the typed head lands there, ringing one grid
    // step. A digit is the only way to place a head, so the placement rule is read through it.
    click(controller, off_grid_x, 100.0f);
    controller.onChartFretDigitTyped(3);
    const common::core::Chart* chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == notes_before + 1);
    CHECK(chart->notes.back().position == grid_slot);
    CHECK(chart->notes.back().sustain == grid_step_beats);

    // Snap off: the same pixel arms on the TICK lattice — and the ring still reaches the next
    // QUARTER line, the rest of a grid step from between two lines, because a duration reads the
    // grid value, never the quantum.
    turnGridSnapOff(controller);
    click(controller, off_grid_x, 140.0f);
    controller.onChartFretDigitTyped(3);
    chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == notes_before + 2);
    const common::core::ChartNote& off_grid_note = chart->notes.back();
    CHECK(off_grid_note.position == tick_slot);
    CHECK(
        common::core::sustainEndPosition(tempo_map, off_grid_note) ==
        adjacentTempoGridPosition(tempo_map, common::core::Fraction{1, 4}, tick_slot, true));
}

// Undo and redo are never blind: a note the transition writes back becomes the selection with the
// caret on it, and one it takes away leaves the caret on the emptied slot — wherever the caret
// stood when the transition ran. Each such transition is counted for the view to keep in sight.
TEST_CASE("EditorController brings an undone chart change into focus", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    const EditorViewState* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    // The armed caret's time, read through one named binding so the optional's guard and its read
    // are provably the same object.
    const auto caret_seconds = [state]() -> std::optional<double> {
        const std::optional<ChartCaretViewState>& caret = state->chart_edit.caret;
        if (caret.has_value())
        {
            return caret->seconds;
        }
        return std::nullopt;
    };

    click(controller, 40.0f, 220.0f);
    REQUIRE(state->chart_edit.selected_notes == std::vector<std::size_t>{0});
    REQUIRE(caret_seconds().has_value());
    const double note_seconds = caret_seconds().value_or(-1.0);
    const std::size_t notes = chartOrNull(controller)->notes.size();
    controller.onSelectionDeleteRequested();

    // The caret leaves for an empty slot far from the deleted note before the history moves.
    click(controller, 120.0f, 220.0f);
    REQUIRE(caret_seconds().has_value());
    REQUIRE(caret_seconds().value_or(note_seconds) > note_seconds + 1.0);
    const std::uint64_t focused_before = state->transition_focus_count;

    controller.onUndoRequested();
    REQUIRE(chartOrNull(controller)->notes.size() == notes);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0});
    REQUIRE(caret_seconds().has_value());
    CHECK(caret_seconds().value_or(-1.0) == Catch::Approx(note_seconds));
    CHECK(state->transition_focus_count == focused_before + 1);

    // The redo takes the note away again: nothing left to select, the caret on its emptied slot.
    controller.onRedoRequested();
    REQUIRE(chartOrNull(controller)->notes.size() == notes - 1);
    CHECK(state->chart_edit.selected_notes.empty());
    REQUIRE(caret_seconds().has_value());
    CHECK(caret_seconds().value_or(-1.0) == Catch::Approx(note_seconds));
    CHECK(state->transition_focus_count == focused_before + 2);
}

// Several notes cannot all sit under one caret, so a transition writing several back selects them
// all and brings the passive cursor to the first — and a transition off the timeline (the output
// gain) focuses nothing and leaves both the selection and the count alone.
TEST_CASE("EditorController focuses a chord an undo restores with the cursor", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    const EditorViewState* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);

    // Marquee both measure-2 chord members and delete them, then park the caret elsewhere.
    controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));
    REQUIRE(state->chart_edit.selected_notes.size() == 2);
    REQUIRE(state->selection_start_seconds.has_value());
    const double chord_seconds = state->selection_start_seconds.value_or(0.0);
    controller.onSelectionDeleteRequested();
    click(controller, 120.0f, 220.0f);

    controller.onUndoRequested();
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0, 1});
    CHECK_FALSE(state->chart_edit.caret.has_value());
    CHECK(transport.position().seconds == Catch::Approx(chord_seconds));
    const std::uint64_t focused = state->transition_focus_count;

    controller.onOutputGainChanged(-12.0);
    controller.onUndoRequested();
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0, 1});
    CHECK(state->transition_focus_count == focused);
}

} // namespace rock_hero::editor::core
