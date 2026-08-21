#include <catch2/catch_approx.hpp>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>

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
    // the sustained note the tail belongs to.
    click(controller, 97.0f, 220.0f);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{2});
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

    // Shift behaves as plain until plan 52's time-range selection lands: it replaces the
    // selection with the clicked note.
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

// Insert is the neutral-create verb: a fret-0 note at an armed empty string slot; occupied slots
// (whose arming selects the note) and the passive marker are no-ops.
TEST_CASE("EditorController inserts a fret-0 note on the Insert verb", "[core][chart]")
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

    // Passive marker: Insert is inert.
    const auto* chart = chartOrNull(controller);
    REQUIRE(chart != nullptr);
    const std::size_t notes_before = chart->notes.size();
    controller.onNeutralInsertRequested();
    CHECK(chartOrNull(controller)->notes.size() == notes_before);

    // Click empty space (x 200 = 10.0s on the string-4 lane, the established empty slot) to
    // arm the caret there, then Insert creates a fret-0 note and selects it.
    click(controller, 200.0f, 100.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->chart_edit.caret.has_value());
    CHECK(state->chart_edit.selected_notes.empty());
    controller.onNeutralInsertRequested();
    REQUIRE(chartOrNull(controller)->notes.size() == notes_before + 1);
    CHECK(state->chart_edit.selected_notes.size() == 1);

    // The caret now sits on the created note (arming re-derived the selection), so a second
    // Insert is a no-op: Insert never mutates existing objects.
    controller.onNeutralInsertRequested();
    CHECK(chartOrNull(controller)->notes.size() == notes_before + 1);
}

// Alt+click is the mouse form of the Insert verb: a press-release on an empty slot plants a fret-0
// note there, selects it, and arms the caret on it, so the very next typed digit retypes it —
// "place, then correct the value".
TEST_CASE("EditorController plants a fret-0 note on Alt+click", "[core][chart]")
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
    click(controller, 200.0f, 100.0f, ChartPointerModifiers{.alt = true});

    const auto* chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == notes_before + 1);
    // The planted note sorts last (its measure is beyond the fixture's) and carries fret 0.
    CHECK(chart->notes.back().string == 4);
    CHECK(chart->notes.back().fret == 0);

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->chart_edit.selected_notes.size() == 1);
    const ChartCaretViewState* caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(10.0));
    CHECK(caret->string == 4);

    // Muscle-memory correction: the note lands selected, so a digit retypes it in place — no
    // second note, the string-4 note is now fret 7.
    controller.onChartFretDigitTyped(7);
    chart = chartOrNull(controller);
    CHECK(chart->notes.size() == notes_before + 1);
    CHECK(chart->notes.back().fret == 7);

    // Alt+click on an existing note keeps its plain select meaning — Insert refuses occupied
    // slots, so Alt+click never duplicates and is never destructive.
    click(controller, 40.0f, 220.0f, ChartPointerModifiers{.alt = true});
    CHECK(chartOrNull(controller)->notes.size() == notes_before + 1);
    CHECK(state->chart_edit.selected_notes.size() == 1);
}

// The Alt-hover insert ghost is published only where an Alt+click would actually land: present
// over an insertable empty slot with Alt held, absent without Alt, absent over an occupied slot,
// and cleared when the pointer leaves the lane (§7, no lying affordance).
TEST_CASE("EditorController publishes the Alt insert ghost honestly", "[core][chart]")
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

    // Alt over the empty string-4 slot at 10.0s: the ring appears exactly there.
    controller.onChartPointerMove(pointerEvent(200.0f, 100.0f, ChartPointerModifiers{.alt = true}));
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const ChartSlotViewState* insert_ghost = insertGhostOrNull(state->chart_edit);
    REQUIRE(insert_ghost != nullptr);
    CHECK(insert_ghost->seconds == Catch::Approx(10.0));
    CHECK(insert_ghost->string == 4);

    // Drop Alt over the same slot: no ring — Alt is the create gate, a plain hover shows none.
    controller.onChartPointerMove(pointerEvent(200.0f, 100.0f));
    CHECK_FALSE(state->chart_edit.insert_ghost.has_value());

    // Alt over the occupied string-1 slot at 2.0s: no ring — an Alt+click there would not insert.
    controller.onChartPointerMove(pointerEvent(40.0f, 220.0f, ChartPointerModifiers{.alt = true}));
    CHECK_FALSE(state->chart_edit.insert_ghost.has_value());

    // Alt back over the empty slot, then the pointer leaves the lane: Exit clears the ring.
    controller.onChartPointerMove(pointerEvent(200.0f, 100.0f, ChartPointerModifiers{.alt = true}));
    REQUIRE(state->chart_edit.insert_ghost.has_value());
    controller.onChartPointerExit();
    CHECK_FALSE(state->chart_edit.insert_ghost.has_value());
}

// Alt+drag from an empty slot is the neutral-create gesture, not a marquee: no selection box
// forms, the ghost follows the pointer, and the release plants one note at the release slot.
TEST_CASE("EditorController Alt+drag plants a note and suppresses the marquee", "[core][chart]")
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

    // Press on the empty string-4 slot and drag past the click threshold, Alt held throughout.
    controller.onChartPointerDown(pointerEvent(200.0f, 100.0f, ChartPointerModifiers{.alt = true}));
    controller.onChartPointerDrag(pointerEvent(240.0f, 100.0f, ChartPointerModifiers{.alt = true}));
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    // No marquee under Alt, and the ring follows the drag.
    CHECK_FALSE(state->chart_edit.marquee.has_value());
    CHECK(state->chart_edit.insert_ghost.has_value());

    // Release plants exactly one fret-0 note at the release slot
    // (240 px = 12.0s), clearing the ring.
    controller.onChartPointerUp(pointerEvent(240.0f, 100.0f, ChartPointerModifiers{.alt = true}));
    REQUIRE(chartOrNull(controller)->notes.size() == notes_before + 1);
    CHECK(chartOrNull(controller)->notes.back().string == 4);
    CHECK(chartOrNull(controller)->notes.back().fret == 0);
    CHECK_FALSE(state->chart_edit.insert_ghost.has_value());
    CHECK(state->chart_edit.selected_notes.size() == 1);
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

} // namespace rock_hero::editor::core
