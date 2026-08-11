#include <catch2/catch_approx.hpp>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>

namespace rock_hero::editor::core
{

namespace
{

// Six-string chart with two simultaneous notes at measure 2 and a sustained note at measure 3.
// Default tempo map (120 BPM, 4/4): measure 2 beat 1 = 2.0s, measure 3 beat 1 = 4.0s.
[[nodiscard]] common::core::Chart makeTestChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 2,
            .fret = 5,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 7,
            .sustain = common::core::Fraction{2, 1},
            .bend = {},
            .slides = {},
        },
    };
    return chart;
}

// Loads the chart-bearing song fixture through the controller's normal open route. A scenario
// needing a different note stream passes its own chart; the shared fixture is the default.
[[nodiscard]] bool loadChartArrangement(
    EditorController& controller, FakeProjectServices& project_services,
    ConfigurableSongAudio& audio, std::vector<common::core::SongSection> sections = {},
    common::core::Chart chart = makeTestChart())
{
    const common::core::TimeRange timeline_range = loadedTimelineRange(30.0);
    audio.next_prepared_audio_duration = timeline_range.duration();
    audio.next_set_active_arrangement_result = true;
    common::core::Song song = makeSong(std::filesystem::path{"a.wav"}, timeline_range);
    // The default-constructed TempoMap's terminal anchor sits at 2.0s and time queries clamp
    // there; cover the whole fixture timeline the way real imports do.
    song.tempo_map = common::core::TempoMap::defaultMap(timeline_range.duration());
    song.sections = std::move(sections);
    song.arrangements.front().chart = std::move(chart);
    project_services.next_song = std::move(song);
    controller.onOpenRequested(std::filesystem::path{"loaded.rhp"});
    // Every scenario in this file states its times in quarter-note grid steps (0.5s at the
    // default 120 BPM 4/4), so pin that grid explicitly instead of riding the editor default.
    controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});
    return controller.session().currentArrangement() != nullptr;
}

// 20-second window across a 400x240 six-lane band: 20 px/s, 40px lanes, 25px heads.
// Note anchors: measure 2 = (40, 220) on string 1 and (40, 180) on string 2; measure 3 = (80,
// 220) with a one-second tail to x = 100.
[[nodiscard]] common::ui::TabLaneGeometry makeGeometry()
{
    return common::ui::makeTabLaneGeometry(
        0.0f,
        0.0f,
        400.0f,
        240.0f,
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        },
        6,
        6);
}

[[nodiscard]] ChartPointerEvent pointerEvent(
    float x, float y, ChartPointerModifiers modifiers = {}, int clicks = 1)
{
    return ChartPointerEvent{
        .geometry = makeGeometry(), .x = x, .y = y, .modifiers = modifiers, .clicks = clicks
    };
}

// Presses and releases at the same point, the plain click gesture.
void click(EditorController& controller, float x, float y, ChartPointerModifiers modifiers = {})
{
    controller.onChartPointerDown(pointerEvent(x, y, modifiers));
    controller.onChartPointerUp(pointerEvent(x, y, modifiers));
}

// Two click gestures with the second pair reporting a consecutive-click count of two, matching
// how JUCE delivers a double click.
void doubleClick(EditorController& controller, float x, float y)
{
    click(controller, x, y);
    controller.onChartPointerDown(pointerEvent(x, y, {}, 2));
    controller.onChartPointerUp(pointerEvent(x, y, {}, 2));
}

} // namespace

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
    const ChartInsertGhostViewState* insert_ghost = insertGhostOrNull(state->chart_edit);
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

    // Shifting up stops when the highest fret reaches the cap (30): 28 upward ticks land on
    // 28/30 and the next is refused.
    for (int step = 0; step < 29; ++step)
    {
        controller.onChartFretShiftRequested(1);
    }
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 28);
    CHECK(chart->notes[1].fret == 30);
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

// Arrows move the caret: Left/Right by one grid step on its string, Up/Down across strings,
// and the modifier jumps measures (the Guitar Pro jump); the selection re-derives from what
// sits under the caret.
TEST_CASE("EditorController steps the caret along the grid and strings", "[core][chart]")
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

    // Park the caret at measure 4 beat 1 (6.0s) on string 1 via an empty click. The caret
    // itself never seeks the transport.
    const int seek_baseline = transport.seek_call_count;
    click(controller, 120.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(transport.seek_call_count == seek_baseline);
    const ChartCaretViewState* caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(6.0));
    CHECK(caret->string == 1);
    // The caret publishes its measure bounds for the keep-in-view glide: measure 4 spans
    // 6.0s..8.0s at the default 120 BPM 4/4.
    CHECK(caret->measure_start_seconds == Catch::Approx(6.0));
    CHECK(caret->measure_end_seconds == Catch::Approx(8.0));

    // Right by one quarter-note grid step: 6.0s -> 6.5s at 120 BPM; Left steps back.
    controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(6.5));
    controller.onChartCaretStepRequested(ChartStepDirection::Left, false);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(6.0));

    // Up/Down move across strings, clamped at the neck.
    controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->string == 2);
    controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->string == 1);

    // The measure jump: Right to measure 5 (8.0s); Left back to measure 4, then measure 3.
    controller.onChartCaretStepRequested(ChartStepDirection::Right, true);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(8.0));
    controller.onChartCaretStepRequested(ChartStepDirection::Left, true);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(6.0));

    // Stepping onto a note selects it and keeps the caret published there — the square rides
    // the selection highlight so the caret stays visible through a single selection: measure 3
    // beat 1 (4.0s) holds the sustained string-1 note.
    controller.onChartCaretStepRequested(ChartStepDirection::Left, true);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{2});
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(4.0));
    CHECK(caret->string == 1);
}

// Home and End leap the caret to the chart's bounds — measure 1 beat 1 and the tempo map's
// terminal downbeat — on the first press, keeping the caret's string because bounds are
// horizontal reach.
TEST_CASE("EditorController jumps the caret to the chart bounds", "[core][chart]")
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

    // Arm mid-song at measure 4 (6.0s) on string 1.
    click(controller, 120.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const ChartCaretViewState* caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(6.0));
    CHECK(caret->string == 1);

    // Home -> chart start (measure 1 beat 1 = 0.0s), string preserved.
    controller.onChartCaretJumpRequested(ChartCaretJump::ChartStart);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(0.0));
    CHECK(caret->string == 1);

    // End -> the tempo map's terminal downbeat, the chart's closing barline past the last measure.
    const common::core::TempoMap& tempo_map = controller.session().song().tempo_map;
    const auto [end_measure, end_beat] =
        tempo_map.beatAtGlobalIndex(tempo_map.terminalGlobalBeatIndex());
    const double end_seconds = tempo_map.secondsAtBeat(end_measure, end_beat);
    controller.onChartCaretJumpRequested(ChartCaretJump::ChartEnd);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(end_seconds > 6.0);
    CHECK(caret->seconds == Catch::Approx(end_seconds));
    CHECK(caret->string == 1);
}

// PageUp/PageDown leap the caret to the previous/next song section; a jump with no section in that
// direction is refused, not clamped (the caret stays put), matching every other refused move.
TEST_CASE("EditorController jumps the caret between sections", "[core][chart]")
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
    // Sections at measure 3 (4.0s) and measure 7 (12.0s) on the 120 BPM 4/4 fixture.
    const std::vector<common::core::SongSection> sections{
        common::core::SongSection{
            .position = {.measure = 3, .beat = 1, .offset = {}}, .name = "Verse"
        },
        common::core::SongSection{
            .position = {.measure = 7, .beat = 1, .offset = {}}, .name = "Chorus"
        },
    };
    REQUIRE(loadChartArrangement(controller, project_services, audio, sections));

    // Arm mid-song at measure 4 (6.0s).
    click(controller, 120.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const ChartCaretViewState* caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(6.0));

    // PageUp -> the previous section (measure 3 = 4.0s).
    controller.onChartCaretJumpRequested(ChartCaretJump::PreviousSection);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(4.0));

    // PageUp again -> no section before measure 3: refused, caret stays at 4.0s.
    controller.onChartCaretJumpRequested(ChartCaretJump::PreviousSection);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(4.0));

    // PageDown -> the next section (measure 7 = 12.0s).
    controller.onChartCaretJumpRequested(ChartCaretJump::NextSection);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(12.0));

    // PageDown again -> no section after measure 7: refused, caret stays at 12.0s.
    controller.onChartCaretJumpRequested(ChartCaretJump::NextSection);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(12.0));
}

// Shift+arrows build and extend the grid-locked time selection from the marker: a first press
// anchors at the paused cursor (measure 1 beat 1 = 0.0s) and extends one unit, later presses grow
// the focus edge, and Ctrl reaches the measure while Shift+End reaches the chart bound. A refused
// extend (no section that way) creates no range.
TEST_CASE("EditorController builds and extends the time selection", "[core][chart]")
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

    // A refused first extend (no previous section from the chart start) creates no range.
    controller.onTimeSelectionExtendRequested(
        TimeSelectionExtent::Section, ChartStepDirection::Left);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->time_selection.has_value());

    // First Shift+Right anchors at the paused cursor (measure 1 beat 1 = 0.0s) and extends one
    // quarter-note grid step (0.5s at 120 BPM 4/4).
    controller.onTimeSelectionExtendRequested(TimeSelectionExtent::Grid, ChartStepDirection::Right);
    const common::core::TimeRange* time_selection = timeSelectionOrNull(*state);
    REQUIRE(time_selection != nullptr);
    CHECK(time_selection->start.seconds == Catch::Approx(0.0));
    CHECK(time_selection->end.seconds == Catch::Approx(0.5));
    CHECK(state->selection_present);

    // A second grid extend grows the range to a half note (1.0s), keeping the anchor.
    controller.onTimeSelectionExtendRequested(TimeSelectionExtent::Grid, ChartStepDirection::Right);
    time_selection = timeSelectionOrNull(*state);
    REQUIRE(time_selection != nullptr);
    CHECK(time_selection->start.seconds == Catch::Approx(0.0));
    CHECK(time_selection->end.seconds == Catch::Approx(1.0));

    // Ctrl reaches the measure unit: extend to the next measure downbeat (measure 2 = 2.0s).
    controller.onTimeSelectionExtendRequested(
        TimeSelectionExtent::Measure, ChartStepDirection::Right);
    CHECK(time_selection->end.seconds == Catch::Approx(2.0));

    // Extending Left shrinks the focus back one grid step toward the anchor (measure 1 beat 4 =
    // 1.5s).
    controller.onTimeSelectionExtendRequested(TimeSelectionExtent::Grid, ChartStepDirection::Left);
    CHECK(time_selection->start.seconds == Catch::Approx(0.0));
    CHECK(time_selection->end.seconds == Catch::Approx(1.5));

    // Shift+End extends the focus to the chart's terminal downbeat; the anchor stays at 0.0.
    const common::core::TempoMap& tempo_map = controller.session().song().tempo_map;
    const auto [end_measure, end_beat] =
        tempo_map.beatAtGlobalIndex(tempo_map.terminalGlobalBeatIndex());
    const double chart_end = tempo_map.secondsAtBeat(end_measure, end_beat);
    controller.onTimeSelectionExtendRequested(
        TimeSelectionExtent::ChartBound, ChartStepDirection::Right);
    CHECK(time_selection->start.seconds == Catch::Approx(0.0));
    CHECK(time_selection->end.seconds == Catch::Approx(chart_end));
}

// Building a time selection dissolves the caret (decision D), and a plain arrow then clears the
// range — the settled "a plain arrow clears it" rule — arming a caret again.
TEST_CASE(
    "EditorController time selection dissolves the caret and yields to arrows", "[core][chart]")
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

    // Arm a caret mid-song at measure 4 (6.0s) on string 1.
    click(controller, 120.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(caretOrNull(state->chart_edit) != nullptr);

    // Shift+Right builds a range anchored at the caret's grid slot (6.0s -> 6.5s) and demotes the
    // marker to passive: the range dissolves the caret.
    controller.onTimeSelectionExtendRequested(TimeSelectionExtent::Grid, ChartStepDirection::Right);
    const common::core::TimeRange* time_selection = timeSelectionOrNull(*state);
    REQUIRE(time_selection != nullptr);
    CHECK(time_selection->start.seconds == Catch::Approx(6.0));
    CHECK(time_selection->end.seconds == Catch::Approx(6.5));
    CHECK(caretOrNull(state->chart_edit) == nullptr);

    // A plain arrow clears the range and arms a caret again (object selection evicts the range).
    // The caret reappears at the range's anchor (6.0s), not a stale transport position: building
    // the range seeked the transport to the caret, so the marker's passive time is the caret's.
    controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    CHECK_FALSE(state->time_selection.has_value());
    const ChartCaretViewState* rearmed = caretOrNull(state->chart_edit);
    REQUIRE(rearmed != nullptr);
    CHECK(rearmed->seconds == Catch::Approx(6.0));
}

// Stepping the focus exactly back onto the anchor collapses the range to nothing rather than
// holding a zero-width span; the transport rests at the anchor, so a further extend continues from
// there.
TEST_CASE("EditorController collapses a shrunk-to-zero time selection", "[core][chart]")
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

    // Arm mid-song at measure 4 (6.0s) so the anchor is not the origin (which would clamp).
    click(controller, 120.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);

    // Shift+Right builds [6.0, 6.5] anchored at 6.0.
    controller.onTimeSelectionExtendRequested(TimeSelectionExtent::Grid, ChartStepDirection::Right);
    const common::core::TimeRange* time_selection = timeSelectionOrNull(*state);
    REQUIRE(time_selection != nullptr);
    CHECK(time_selection->end.seconds == Catch::Approx(6.5));

    // Shift+Left steps the focus back onto the anchor: the range clears rather than lingering as a
    // zero-width span, and selection_present drops.
    controller.onTimeSelectionExtendRequested(TimeSelectionExtent::Grid, ChartStepDirection::Left);
    CHECK_FALSE(state->time_selection.has_value());
    CHECK_FALSE(state->selection_present);

    // A further Shift+Left re-anchors at 6.0 (the transport rested there) and extends left to
    // measure 3 beat 4 (5.5s).
    controller.onTimeSelectionExtendRequested(TimeSelectionExtent::Grid, ChartStepDirection::Left);
    time_selection = timeSelectionOrNull(*state);
    REQUIRE(time_selection != nullptr);
    CHECK(time_selection->start.seconds == Catch::Approx(5.5));
    CHECK(time_selection->end.seconds == Catch::Approx(6.0));
}

// Shift+PageUp/PageDown extend the range by whole sections; an extend with no section in that
// direction is refused, leaving the range unchanged.
TEST_CASE("EditorController extends the time selection by section", "[core][chart]")
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
    // Sections at measure 3 (4.0s) and measure 7 (12.0s) on the 120 BPM 4/4 fixture.
    const std::vector<common::core::SongSection> sections{
        common::core::SongSection{
            .position = {.measure = 3, .beat = 1, .offset = {}}, .name = "Verse"
        },
        common::core::SongSection{
            .position = {.measure = 7, .beat = 1, .offset = {}}, .name = "Chorus"
        },
    };
    REQUIRE(loadChartArrangement(controller, project_services, audio, sections));

    // First Shift+PageDown anchors at measure 1 beat 1 (0.0s) and extends to the first section
    // after it (measure 3 = 4.0s).
    controller.onTimeSelectionExtendRequested(
        TimeSelectionExtent::Section, ChartStepDirection::Right);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const common::core::TimeRange* time_selection = timeSelectionOrNull(*state);
    REQUIRE(time_selection != nullptr);
    CHECK(time_selection->start.seconds == Catch::Approx(0.0));
    CHECK(time_selection->end.seconds == Catch::Approx(4.0));

    // Again extends to the next section (measure 7 = 12.0s).
    controller.onTimeSelectionExtendRequested(
        TimeSelectionExtent::Section, ChartStepDirection::Right);
    CHECK(time_selection->end.seconds == Catch::Approx(12.0));

    // Again -> no section past measure 7: refused, the range stays put.
    controller.onTimeSelectionExtendRequested(
        TimeSelectionExtent::Section, ChartStepDirection::Right);
    time_selection = timeSelectionOrNull(*state);
    REQUIRE(time_selection != nullptr);
    CHECK(time_selection->end.seconds == Catch::Approx(12.0));
}

// Playback dissolves the marker's armed state (the marker model): play clears the note
// selection and demotes the caret to the passive cursor; pause rests passive at the raw stop
// point — typing is inert there — and the first arrow re-arms at the nearest grid line on the
// remembered string.
TEST_CASE("EditorController dissolves the caret while playing", "[core][chart]")
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

    // Select the measure-2 string-1 note; the caret arms on it (and publishes there — the
    // square rides the selection highlight).
    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0});
    CHECK(state->chart_edit.caret.has_value());

    // Play: the selection clears and the marker demotes immediately; the playing pushes
    // publish no caret.
    controller.onPlayPausePressed();
    CHECK(state->chart_edit.selected_notes.empty());
    CHECK_FALSE(state->chart_edit.caret.has_value());
    transport.setStateAndNotify(common::audio::TransportState{.playing = true});
    CHECK(state->chart_edit.selected_notes.empty());
    CHECK_FALSE(state->chart_edit.caret.has_value());

    // Pause at 10.2s: the marker rests passive at the raw stop point — no caret arms and no
    // grid snap happens (the paused cursor line at 10.2s is the position).
    transport.current_position = common::core::TimePosition{10.2};
    controller.onPlayPausePressed();
    CHECK(transport.pause_call_count == 1);
    transport.setStateAndNotify(common::audio::TransportState{.playing = false});
    CHECK_FALSE(state->chart_edit.caret.has_value());

    // Typing while passive is inert: a stray digit after listening authors nothing.
    controller.onChartFretDigitTyped(5);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes.size() == 3);

    // The first arrow arms the caret at the paused cursor: nearest grid line (10.0s at the
    // default 120 BPM quarter grid) on the remembered string, without stepping.
    controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    const ChartCaretViewState* caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(10.0));
    CHECK(caret->string == 1);
}

// Esc steps the marker ladder down one rung at a time: an armed caret dissolves to the passive
// cursor in its place (a paused seek carries the transport there), keeping the selection; the
// next Esc clears the selection.
TEST_CASE("EditorController steps the Esc ladder down", "[core][chart]")
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

    // Arm on the measure-2 string-1 note (2.0s); its singleton selection derives.
    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->chart_edit.caret.has_value());
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0});

    // First Esc: disarm in place — the cursor takes the caret's spot via a paused seek — and
    // the selection stays.
    controller.onChartEscapePressed();
    CHECK_FALSE(state->chart_edit.caret.has_value());
    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{2.0}});
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0});

    // Second Esc: the selection clears; the marker stays passive.
    controller.onChartEscapePressed();
    CHECK(state->chart_edit.selected_notes.empty());
    CHECK_FALSE(state->chart_edit.caret.has_value());
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

// Typing a digit on the empty caret INSERTS a note there with the typed fret (the caret
// model): a second digit inside the window widens the SAME insert to the combined fret, and
// one undo removes the note entirely.
TEST_CASE("EditorController inserts a note by typing at the caret", "[core][chart]")
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

    // Place the caret at measure 4 beat 1 (x = 120 is 6.0s) on string 1 and type 1 then 2:
    // ONE inserted note at fret 12, selected, as ONE undo entry.
    click(controller, 120.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();
    controller.onChartFretDigitTyped(1);
    controller.onChartFretDigitTyped(2);

    const auto* chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == 4);
    CHECK(chart->notes[3].position == common::core::GridPosition{.measure = 4, .beat = 1});
    CHECK(chart->notes[3].string == 1);
    CHECK(chart->notes[3].fret == 12);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{3});
    CHECK(state->undo_history.labels.size() == entries_before + 1);

    // ONE undo removes the whole typed insert (never stranding a fret-1 note).
    controller.onUndoRequested();
    chart = chartOrNull(controller);
    CHECK(chart->notes.size() == 3);
    CHECK(state->chart_edit.selected_notes.empty());

    controller.onRedoRequested();
    chart = chartOrNull(controller);
    CHECK(chart->notes.size() == 4);
}

// Typing at a caret inside an earlier sustain truncates it in the same undo entry (40-Q2-B).
TEST_CASE("EditorController insert truncates the overlapped sustain", "[core][chart]")
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

    // The target slot (measure 3 beat 2, 4.5s) sits inside the measure-3 note's two-beat
    // sustain, so clicking there would hit the tail and select the note instead of placing
    // the caret. Reach it via the empty string-2 lane and an arrow down.
    click(controller, 90.0f, 180.0f);
    controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    controller.onChartFretDigitTyped(5);

    const auto* chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == 4);
    CHECK(chart->notes[2].sustain == common::core::Fraction{1, 1});
    CHECK(chart->notes[3].position == (common::core::GridPosition{.measure = 3, .beat = 2}));

    // One undo restores both the removed note and the original sustain.
    controller.onUndoRequested();
    chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[2].sustain == common::core::Fraction{2, 1});
}

// Delete removes the whole selection as one entry and undo restores it.
TEST_CASE("EditorController deletes the chart selection undoably", "[core][chart]")
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

    // A double click selects the whole measure-2 chord (both strings); the unified Delete
    // dispatch removes it (the chart branch of the one selection-delete intent).
    doubleClick(controller, 40.0f, 220.0f);
    controller.onSelectionDeleteRequested();

    const auto* chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == 1);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->chart_edit.selected_notes.empty());
    CHECK(state->undo_label == std::optional<std::string>{"Delete 2 Notes"});

    controller.onUndoRequested();
    chart = chartOrNull(controller);
    CHECK(chart->notes.size() == 3);
}

// Typed digits retype the selection's fret, combining inside the multi-digit window.
TEST_CASE("EditorController fret digits combine inside the entry window", "[core][chart]")
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

    // A plain click selects just the string-1 note (containment hierarchy).
    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();

    controller.onChartFretDigitTyped(1);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 1);

    // The second digit inside the window widens the SAME undo entry: one action, fret 12.
    controller.onChartFretDigitTyped(2);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 12);
    CHECK(state->undo_history.labels.size() == entries_before + 1);
    CHECK(state->undo_label == std::optional<std::string>{"Set Fret 12"});

    // The selection stays on the retyped note under its unchanged key.
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0});

    // One undo restores the original fret 3 in one step.
    controller.onUndoRequested();
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 3);

    // An interleaved edit kills the window: the next digit starts a fresh value.
    controller.onChartFretDigitTyped(2);
    controller.onChartSustainAdjustRequested(1, false);
    controller.onChartFretDigitTyped(3);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 3);
}

// Two-digit entry across a note another note connects to. Nothing is repaired mid-burst any more —
// the claim is authored data and stays put — so what this pins is that the widen still reconstructs
// the pre-entry stream by reversing its own plan, and that the connection is pure re-projection:
// the direction it reads back as follows each typed value with no stored field to fall out of step.
TEST_CASE("EditorController re-projects a claim through a widened fret entry", "[core][chart]")
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

    // String 1: fret 9 at measure 2 held to the margin before a legato note at fret 5 in measure 3
    // (four beats on, minus the quarter-beat margin), so the claim resolves as a pull-off.
    common::core::Chart chart_with_claim;
    chart_with_claim.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart_with_claim.notes = {
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 9,
            .sustain = common::core::Fraction{15, 4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 5,
            .attack = common::core::NoteAttack::Legato,
            .bend = {},
            .slides = {},
        },
    };
    REQUIRE(loadChartArrangement(controller, project_services, audio, {}, chart_with_claim));

    const auto resolution = [&](const std::size_t index) {
        const common::core::Chart& current = *controller.session().currentArrangement()->chart;
        return common::core::chartResolutions(
                   current.notes, current.shapes, controller.session().song().tempo_map)
            .legato[index];
    };

    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();
    // The claim's stored form, captured whole: every step below compares against this, because
    // "pure re-projection" is a claim about BYTES and an attack check alone would not catch a
    // repair that rewrote the note's tail or node.
    const common::core::ChartNote claim_bytes = chartOrNull(controller)->notes[1];

    SECTION("two digits inside the window are one entry, and the mark follows each value")
    {
        // The 5-to-3-to-2 story: retyping the predecessor under the claim RE-PROJECTS it. The
        // note's bytes never change — the claim is still exactly the claim the author wrote — and
        // only the direction it reads back as moves, from a pull-off to a hammer-on.
        controller.onChartFretDigitTyped(1);
        const auto* chart = chartOrNull(controller);
        REQUIRE(chart->notes.size() == 2);
        CHECK(chart->notes[0].fret == 1);
        CHECK(chart->notes[1] == claim_bytes);
        CHECK(resolution(1) == common::core::LegatoMotion::Hammer);
        // The armed caret's selection is still exactly the note under it, which is what keeps the
        // window open for the next digit.
        CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0});

        // The second digit widens the SAME entry to fret 12, replanning from the reversed pre-entry
        // stream, and the claim reads back as the pull-off again.
        controller.onChartFretDigitTyped(2);
        chart = chartOrNull(controller);
        CHECK(chart->notes[0].fret == 12);
        CHECK(chart->notes[1] == claim_bytes);
        CHECK(resolution(1) == common::core::LegatoMotion::Pull);
        CHECK(state->undo_history.labels.size() == entries_before + 1);
        CHECK(state->undo_label == std::optional<std::string>{"Set Fret 12"});

        // One undo restores the whole typed number in one step.
        controller.onUndoRequested();
        chart = chartOrNull(controller);
        CHECK(chart->notes[0].fret == 9);
        CHECK(chart->notes[1] == claim_bytes);
        CHECK(resolution(1) == common::core::LegatoMotion::Pull);
    }

    // The same story told in separate revisions rather than one widened entry, including the value
    // that justifies NOTHING. That middle state is the one the old stored-direction model could not
    // hold: there was no attack to name it, so an edit passing through had to repair the note or
    // refuse. Here it is simply a claim displaying as the pick it plays as, and stepping back out
    // of it needs no repair — the mark returns because the fret did.
    SECTION("each wheel tick is its own revision, unjustifiable ones included")
    {
        controller.onChartFretShiftRequested(-1);
        controller.onChartFretShiftRequested(-1);
        controller.onChartFretShiftRequested(-1);
        controller.onChartFretShiftRequested(-1);
        const auto* chart = chartOrNull(controller);
        CHECK(chart->notes[0].fret == 5);
        CHECK(chart->notes[1] == claim_bytes);
        // Equal released fret: nothing to hammer or pull, so the claim resolves to nothing and the
        // lane draws a plain head.
        CHECK(resolution(1) == common::core::LegatoMotion::Unjustified);
        CHECK(state->undo_history.labels.size() == entries_before + 4);

        // One tick further and the direction has crossed over.
        controller.onChartFretShiftRequested(-1);
        chart = chartOrNull(controller);
        CHECK(chart->notes[0].fret == 4);
        CHECK(resolution(1) == common::core::LegatoMotion::Hammer);

        // Each undo is bit-exact, and the mark it restores is whatever that revision's frets
        // justify — walked all the way back, the pull-off is there again with no re-authoring.
        for (const int expected_fret : {5, 6, 7, 8, 9})
        {
            controller.onUndoRequested();
            chart = chartOrNull(controller);
            CAPTURE(expected_fret);
            CHECK(chart->notes[0].fret == expected_fret);
            CHECK(chart->notes[1] == claim_bytes);
        }
        CHECK(resolution(1) == common::core::LegatoMotion::Pull);
    }
}

// Sustain growth clamps to the minimum-sustain-distance margin — the shared 1/16-whole-note
// constant (a quarter beat in 4/4) — before the next onset on ANY string;
// shrinking floors at zero.
TEST_CASE("EditorController grows and clamps sustains on the grid", "[core][chart]")
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

    // A plain click selects just the string-1 note (containment hierarchy).
    click(controller, 40.0f, 220.0f);
    controller.onChartSustainAdjustRequested(1, false);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes[0].sustain == common::core::Fraction{1, 1});

    // Five more quarter-note steps would reach 6 beats, but the measure-3 note sits 4 beats
    // later: growth clamps a quarter-beat margin before it, at 15/4 beats.
    for (int step = 0; step < 5; ++step)
    {
        controller.onChartSustainAdjustRequested(1, false);
    }
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].sustain == common::core::Fraction{15, 4});

    // The margin binds across strings too: the string-2 chord member has no same-string
    // successor at all, but the measure-3 string-1 note still stops its tail at 15/4 beats.
    click(controller, 40.0f, 180.0f);
    for (int step = 0; step < 6; ++step)
    {
        controller.onChartSustainAdjustRequested(1, false);
    }
    chart = chartOrNull(controller);
    CHECK(chart->notes[1].sustain == common::core::Fraction{15, 4});

    // Shrinking floors at zero.
    click(controller, 40.0f, 220.0f);
    for (int step = 0; step < 6; ++step)
    {
        controller.onChartSustainAdjustRequested(-1, false);
    }
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].sustain == common::core::Fraction{});

    // The Ctrl fine tier composes on the extent verb too (the off-grid unification): one fine
    // grow adds exactly 1/960 beat, and the fine shrink returns exactly to zero.
    controller.onChartSustainAdjustRequested(1, true);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].sustain == common::core::Fraction{1, 960});
    controller.onChartSustainAdjustRequested(-1, true);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].sustain == common::core::Fraction{});
}

// The pick-slide toggle authors a scrape from a plain note and back within the session, each
// direction one compound undo entry replaying exactly through the shared edit seam.
TEST_CASE("EditorController toggles pick slides with exact restoration", "[core][chart]")
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

    // Select the string-1 note and give it a real tail first, so the toggle round trip is
    // field-exact (a zero sustain would legitimately gain the minimum gesture window).
    click(controller, 40.0f, 220.0f);
    controller.onChartSustainAdjustRequested(1, false);
    const auto* chart = chartOrNull(controller);
    const common::core::ChartNote original = chart->notes[0];

    controller.onChartPickSlideToggleRequested();
    chart = chartOrNull(controller);
    const common::core::ChartNote& scrape = chart->notes[0];
    CHECK(scrape.attack == common::core::NoteAttack::PickSlide);
    CHECK(scrape.fret == original.fret);
    REQUIRE(scrape.slide_out.has_value());
    if (scrape.slide_out.has_value())
    {
        CHECK(scrape.slide_out->offset == scrape.sustain);
    }

    // Toggling back restores the note field-for-field: the scrape's path clears and nothing
    // else was ever touched.
    controller.onChartPickSlideToggleRequested();
    chart = chartOrNull(controller);
    CHECK(chart->notes[0] == original);

    // Undo replays both directions exactly: first back to the scrape, then to the original.
    controller.onUndoRequested();
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::PickSlide);
    controller.onUndoRequested();
    chart = chartOrNull(controller);
    CHECK(chart->notes[0] == original);
}

// Uniform scope on a mixed selection: any plain note present makes the whole selection become
// scrapes; only an all-scrape selection reverts. Pins the all-of decision an any-of regression
// would silently invert.
TEST_CASE("EditorController pick-slide toggle applies uniform scope", "[core][chart]")
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

    // One note becomes a scrape first, so the marquee selection below is mixed.
    click(controller, 40.0f, 220.0f);
    controller.onChartPickSlideToggleRequested();

    // Marquee both measure-1 chord members: a scrape plus a plain note.
    controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    controller.onChartPickSlideToggleRequested();
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::PickSlide);
    CHECK(chart->notes[1].attack == common::core::NoteAttack::PickSlide);

    // Now all-scrape: the same intent reverts the whole selection in one entry.
    controller.onChartPickSlideToggleRequested();
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Pick);
    CHECK(chart->notes[1].attack == common::core::NoteAttack::Pick);
    CHECK(chart->notes[0].slides.empty());
    CHECK(chart->notes[1].slides.empty());
    CHECK_FALSE(chart->notes[0].slide_out.has_value());
    CHECK_FALSE(chart->notes[1].slide_out.has_value());
}

// A refused first digit still arms the multi-digit entry window, so an in-range two-digit
// value stays typeable on a scrape whose translated path rejects every single-digit target.
TEST_CASE("EditorController fret typing recovers from a refused first digit", "[core][chart]")
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

    // Build a high downward scrape: type the note to fret 17, then toggle — the default path
    // travels to fret 3, so any single typed digit d would translate it to d - 14 < 0.
    click(controller, 40.0f, 220.0f);
    controller.onChartFretDigitTyped(1);
    controller.onChartFretDigitTyped(7);
    controller.onChartPickSlideToggleRequested();
    const auto* chart = chartOrNull(controller);
    const common::core::ChartNote& scrape = chart->notes[0];
    REQUIRE(scrape.attack == common::core::NoteAttack::PickSlide);
    REQUIRE(scrape.fret == 17);
    REQUIRE(scrape.slide_out.has_value());
    if (scrape.slide_out.has_value())
    {
        REQUIRE(scrape.slide_out->fret == 3);
    }

    // "1" refuses (the terminal would leave the neck) but arms the window; "5" widens to 15
    // and the whole path translates with the start.
    controller.onChartFretDigitTyped(1);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 17);
    controller.onChartFretDigitTyped(5);
    chart = chartOrNull(controller);
    const common::core::ChartNote& widened = chart->notes[0];
    CHECK(widened.fret == 15);
    REQUIRE(widened.slide_out.has_value());
    if (widened.slide_out.has_value())
    {
        CHECK(widened.slide_out->fret == 1);
    }
}

// Arrow keys nudge a selection by the grid step; refused moves (occupied slot) change nothing.
TEST_CASE("EditorController nudges the selection and refuses collisions", "[core][chart]")
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

    // A plain click selects just the string-1 note (containment hierarchy).
    click(controller, 40.0f, 220.0f);

    // Plain arrows never mutate: they move the caret (deselecting on the empty slot), leaving
    // the chart untouched; re-clicking the note restores the selection for the move test.
    controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes[0].position == (common::core::GridPosition{.measure = 2, .beat = 1}));
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->chart_edit.selected_notes.empty());
    click(controller, 40.0f, 220.0f);

    // Alt+Up would land on the occupied measure-2 string-2 slot: refused, nothing changes.
    controller.onSelectionMoveRequested(ChartStepDirection::Up, false);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].string == 1);
    CHECK(chart->notes[0].position == (common::core::GridPosition{.measure = 2, .beat = 1}));

    // Alt+Right moves one quarter-note step; the selection follows the moved note.
    controller.onSelectionMoveRequested(ChartStepDirection::Right, false);
    chart = chartOrNull(controller);
    CHECK(chart->notes[1].position == (common::core::GridPosition{.measure = 2, .beat = 2}));
    CHECK(chart->notes[1].string == 1);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{1});
    CHECK(state->undo_label == std::optional<std::string>{"Move Note"});

    controller.onUndoRequested();
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].position == (common::core::GridPosition{.measure = 2, .beat = 1}));
    CHECK(chart->notes[0].string == 1);
}

// The Ctrl fine tier on notes: Alt+Ctrl+Left/Right moves the selection by one 1/960-beat step, off
// the grid; grid steps stay relative afterwards, so the offset rides along, and the fine step back
// returns to the exact lattice slot.
TEST_CASE("EditorController fine-moves the selection by 1/960 beat", "[core][chart]")
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

    // The moved note sorts after its untouched measure-2 chord mate once it carries an offset.
    controller.onSelectionMoveRequested(ChartStepDirection::Right, true);
    const auto* chart = chartOrNull(controller);
    CHECK(
        chart->notes[1].position ==
        (common::core::GridPosition{
            .measure = 2, .beat = 1, .offset = common::core::Fraction{1, 960}
        }));
    CHECK(chart->notes[1].string == 1);

    // A grid step from the off-grid slot stays relative: the 1/960 offset rides along.
    controller.onSelectionMoveRequested(ChartStepDirection::Right, false);
    chart = chartOrNull(controller);
    CHECK(
        chart->notes[1].position ==
        (common::core::GridPosition{
            .measure = 2, .beat = 2, .offset = common::core::Fraction{1, 960}
        }));

    // The fine step back lands exactly on the lattice again — no residue.
    controller.onSelectionMoveRequested(ChartStepDirection::Left, true);
    chart = chartOrNull(controller);
    CHECK(chart->notes[1].position == (common::core::GridPosition{.measure = 2, .beat = 2}));
}

// Off-grid notes are first-class caret stops (the union stop set): plain arrows step to the nearer
// of the adjacent grid line and the row's next note, so a fine-placed note stays reachable from
// the keyboard — and landing on it arms onto it.
TEST_CASE("EditorController steps the caret onto off-grid notes", "[core][chart]")
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

    // Slide the (2,1) string-1 note one fine step off the grid; the caret (armed on the note
    // by the click) rides the fine move with it.
    click(controller, 40.0f, 220.0f);
    controller.onSelectionMoveRequested(ChartStepDirection::Right, true);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{1});

    // Plain Left leaves the note onto the adjacent (2,1) grid line — never jumping past it —
    // and the empty slot clears the selection.
    controller.onChartCaretStepRequested(ChartStepDirection::Left, false);
    CHECK(state->chart_edit.selected_notes.empty());

    // Plain Right lands back ON the off-grid note (nearer than the (2,2) grid line): the
    // caret arms onto it, selecting it.
    controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{1});

    // The next step continues to the (2,2) grid line (an empty slot clears the selection);
    // stepping back stops on the note again before the line.
    controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    CHECK(state->chart_edit.selected_notes.empty());
    controller.onChartCaretStepRequested(ChartStepDirection::Left, false);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{1});
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

// The H toggle law measures what the PLAN does (W5, ruled): a selection holding a note nothing can
// justify must still round-trip — apply on the first press, clear on the second. The old
// whole-selection test left such a selection stuck in apply mode forever. And the clear targets
// only the stored claims, so a rider keeps its own attack instead of being flattened by the clear.
TEST_CASE("EditorController legato toggle round-trips a mixed selection", "[core][chart]")
{
    // String 1 carries a resolvable pair (the predecessor's tail reaches the note's onset, so the
    // hold test passes); string 2 an open string with nothing before it on its own string, so no
    // claim is ever justified there; string 3 a two-hand tap, a picking-hand rider the verb skips
    // in both directions.
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 5,
            .sustain = common::core::Fraction{4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 2,
            .fret = 0,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 3,
            .fret = 6,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 7,
            .bend = {},
            .slides = {},
        },
    };
    chart.notes[2].attack = common::core::NoteAttack::Tap;

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
    REQUIRE(loadChartArrangement(controller, project_services, audio, {}, std::move(chart)));

    const auto note_attack = [&](const std::size_t index) {
        return controller.session().currentArrangement()->chart->notes[index].attack;
    };

    SECTION("an unjustifiable note no longer wedges the toggle in apply mode")
    {
        // Select the resolvable note (string 1, measure 3) plus the open string.
        click(controller, 80.0f, 220.0f);
        click(controller, 40.0f, 180.0f, ChartPointerModifiers{.ctrl = true});

        controller.onChartLegatoToggleRequested();
        CHECK(note_attack(3) == common::core::NoteAttack::Legato);
        CHECK(note_attack(1) == common::core::NoteAttack::Pick);

        // The second press reverses — this used to be the stuck press that re-applied forever.
        controller.onChartLegatoToggleRequested();
        CHECK(note_attack(3) == common::core::NoteAttack::Pick);
        CHECK(note_attack(1) == common::core::NoteAttack::Pick);

        // And the toggle keeps round-tripping.
        controller.onChartLegatoToggleRequested();
        CHECK(note_attack(3) == common::core::NoteAttack::Legato);
    }

    SECTION("the clear leaves a picking-hand rider's own attack alone")
    {
        click(controller, 80.0f, 220.0f);
        click(controller, 40.0f, 140.0f, ChartPointerModifiers{.ctrl = true});

        controller.onChartLegatoToggleRequested();
        CHECK(note_attack(3) == common::core::NoteAttack::Legato);
        CHECK(note_attack(2) == common::core::NoteAttack::Tap);

        controller.onChartLegatoToggleRequested();
        CHECK(note_attack(3) == common::core::NoteAttack::Pick);
        CHECK(note_attack(2) == common::core::NoteAttack::Tap);
    }

    SECTION("a left-hand tap survives the clear, because Ctrl+H is its sole author")
    {
        // The clear flattens stored claims only. A tap is the one attack plain H must never
        // destroy: the toggle cannot re-create it, so flattening it would lose authored intent no
        // press could bring back.
        click(controller, 80.0f, 220.0f);
        controller.onChartLegatoToggleRequested();
        CHECK(note_attack(3) == common::core::NoteAttack::Legato);

        // The rider becomes a deliberate tap of its own. Nothing precedes it on its string, so no
        // press can ever justify a claim there.
        click(controller, 40.0f, 140.0f);
        controller.onChartLeftTapRequested();
        CHECK(note_attack(2) == common::core::NoteAttack::LeftTap);

        // With both selected there is nothing left to claim — the connection already stands and the
        // tap cannot be justified — so the press clears, and clears the claim only.
        click(controller, 80.0f, 220.0f, ChartPointerModifiers{.ctrl = true});
        controller.onChartLegatoToggleRequested();
        CHECK(note_attack(3) == common::core::NoteAttack::Pick);
        CHECK(note_attack(2) == common::core::NoteAttack::LeftTap);
    }

    SECTION("a press that only skipped is silent, like every other verb that applies nothing")
    {
        // Nothing precedes the open string on its own string, so this press can neither claim nor
        // clear. It stays SILENT: selecting a phrase's first note and pressing H is the commonest
        // press there is, and the only reporting seam the view offers is a modal error box. The
        // count and the dominant reason still travel on planSetLegato's own return (pinned in
        // test_chart_edits.cpp) and surface once a non-modal refusal channel exists.
        click(controller, 40.0f, 180.0f);
        controller.onChartLegatoToggleRequested();
        CHECK(note_attack(1) == common::core::NoteAttack::Pick);
        CHECK(view.shown_errors.empty());

        // Nor does a press that applies report anything: the marks it moved are the feedback.
        click(controller, 80.0f, 220.0f);
        controller.onChartLegatoToggleRequested();
        CHECK(note_attack(3) == common::core::NoteAttack::Legato);
        CHECK(view.shown_errors.empty());
    }
}

TEST_CASE("EditorController Ctrl+H states the left-hand tap", "[core][chart]")
{
    // String 1 carries a resolvable descending pair (released 7 over fret 5, tail reaching the
    // onset, so plain H claims a connection that reads as a pull-off); string 2 the open string
    // with no node — the verb's sole matrix-grounds refusal; string 3 a fret-0 tap harmonic whose
    // strike point the tap verb re-hands; string 4 a stopped pinch whose node survives as the
    // tapped-harmonic gesture; string 5 an open-string pinch whose bridge-side graze can never
    // become a strike point.
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 7,
            .sustain = common::core::Fraction{4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 2,
            .fret = 0,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 3,
            .fret = 0,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 4,
            .fret = 5,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 5,
            .fret = 0,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 5,
            .bend = {},
            .slides = {},
        },
    };
    chart.notes[2].attack = common::core::NoteAttack::Tap;
    chart.notes[2].harmonic_node = 12.0;
    chart.notes[3].attack = common::core::NoteAttack::Pinch;
    chart.notes[3].harmonic_node = 17.0;
    chart.notes[4].attack = common::core::NoteAttack::Pinch;
    chart.notes[4].harmonic_node = 24.0;

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
    REQUIRE(loadChartArrangement(controller, project_services, audio, {}, std::move(chart)));

    const auto note = [&](const std::size_t index) -> const common::core::ChartNote& {
        return controller.session().currentArrangement()->chart->notes[index];
    };

    SECTION("it overrides a standing claim, and plain H claims the connection back")
    {
        click(controller, 80.0f, 220.0f);
        controller.onChartLegatoToggleRequested();
        CHECK(note(5).attack == common::core::NoteAttack::Legato);

        // Only the author knows the predecessor was damped, so the tap overrides the claim.
        controller.onChartLeftTapRequested();
        CHECK(note(5).attack == common::core::NoteAttack::LeftTap);

        // The signed symmetry between the stating verb and the inferring one: where the chart DOES
        // justify a connection, plain H writes it again.
        controller.onChartLegatoToggleRequested();
        CHECK(note(5).attack == common::core::NoteAttack::Legato);
    }

    SECTION("it skips the open string with no node and converts the rest")
    {
        click(controller, 80.0f, 220.0f);
        click(controller, 40.0f, 180.0f, ChartPointerModifiers{.ctrl = true});

        controller.onChartLeftTapRequested();
        CHECK(note(5).attack == common::core::NoteAttack::LeftTap);
        CHECK(note(1).attack == common::core::NoteAttack::Pick);
    }

    SECTION("it re-hands a tap harmonic's strike point into the left-hand form")
    {
        click(controller, 40.0f, 140.0f);

        controller.onChartLeftTapRequested();
        CHECK(note(2).attack == common::core::NoteAttack::LeftTap);
        REQUIRE(note(2).harmonic_node.has_value());
        if (note(2).harmonic_node.has_value())
        {
            CHECK(*note(2).harmonic_node == Catch::Approx(12.0));
        }
    }

    SECTION("it keeps a stopped pinch's node as the tapped-harmonic gesture")
    {
        click(controller, 40.0f, 100.0f);

        controller.onChartLeftTapRequested();
        CHECK(note(3).attack == common::core::NoteAttack::LeftTap);
        REQUIRE(note(3).harmonic_node.has_value());
        if (note(3).harmonic_node.has_value())
        {
            CHECK(*note(3).harmonic_node == Catch::Approx(17.0));
        }
    }

    SECTION("it refuses to re-hand an open-string pinch's bridge-side graze")
    {
        click(controller, 40.0f, 60.0f);

        controller.onChartLeftTapRequested();
        CHECK(note(4).attack == common::core::NoteAttack::Pinch);
        REQUIRE(note(4).harmonic_node.has_value());
        if (note(4).harmonic_node.has_value())
        {
            CHECK(*note(4).harmonic_node == Catch::Approx(24.0));
        }
    }
}

TEST_CASE("EditorController legato toggle window and the connection assist", "[core][chart]")
{
    // String 1 carries a bare predecessor four beats before its note — past the kept-sustain
    // bound, so no claim resolves until the assist authors the connection; string 2 an
    // open string for changing the selection. String 3 repeats the string-1 shape with a SCRAPE as
    // the predecessor — the same missing hold, but a tail the assist is forbidden to spend — and
    // sits later in the stream so the indices above stay put.
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 5,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 2,
            .fret = 0,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 7,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 4, .beat = 1, .offset = {}},
            .string = 3,
            .fret = 9,
            .sustain = common::core::Fraction{1},
            .attack = common::core::NoteAttack::PickSlide,
            .bend = {},
            .slides = {common::core::SlideWaypoint{
                .offset = common::core::Fraction{1, 2},
                .fret = 3,
            }},
            .slide_out = common::core::SlideOut{.offset = common::core::Fraction{1}, .fret = 12},
        },
        common::core::ChartNote{
            .position = {.measure = 5, .beat = 1, .offset = {}},
            .string = 3,
            .fret = 7,
            .bend = {},
            .slides = {},
        },
    };

    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio, {}, std::move(chart)));

    const auto note = [&](const std::size_t index) -> const common::core::ChartNote& {
        return controller.session().currentArrangement()->chart->notes[index];
    };

    SECTION("H authors the connection and H again reverses it exactly")
    {
        click(controller, 80.0f, 220.0f);

        // The assist: the hold was the only thing missing, so the press grows the predecessor's
        // tail to the margin point and claims the connection in one entry.
        controller.onChartLegatoToggleRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Legato);
        CHECK(note(0).sustain == common::core::Fraction{15, 4});

        // The window (ruling 4): the second press reverses that entry exactly — the grown tail
        // included, which the clear law could never restore — and leaves no history entry.
        controller.onChartLegatoToggleRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
        CHECK(note(0).sustain == common::core::Fraction{});
        const EditorViewState* state = stateOrNull(view.last_state);
        REQUIRE(state != nullptr);
        CHECK(state->undo_label != std::optional<std::string>{"Legato"});

        // And the pair of presses keeps cycling.
        controller.onChartLegatoToggleRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Legato);
        CHECK(note(0).sustain == common::core::Fraction{15, 4});
    }

    SECTION("a selection change closes the window: the clear stands and the tail stays")
    {
        click(controller, 80.0f, 220.0f);
        controller.onChartLegatoToggleRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Legato);

        // Extending the selection kills the window's proof, so the next press means the
        // ordinary law — the clear — and the grown tail stays (Ctrl+Z is the revert once the window
        // is gone).
        click(controller, 40.0f, 180.0f, ChartPointerModifiers{.ctrl = true});
        controller.onChartLegatoToggleRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
        CHECK(note(0).sustain == common::core::Fraction{15, 4});

        // And Ctrl+Z is exact where the window would have been genuine: the clear is its own entry,
        // so the first undo restores the claim with the grown tail still standing, and the second
        // undoes the assist that grew it. Two steps rather than the toggle's zero, which is the
        // price of the context switch, not a loss.
        controller.onUndoRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Legato);
        CHECK(note(0).sustain == common::core::Fraction{15, 4});
        controller.onUndoRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
        CHECK(note(0).sustain == common::core::Fraction{});
    }

    // The assist writes a tail, and a tail is sometimes a gesture's own authored window. Where it
    // is, the verb declines rather than reshaping it: a scrape's travel and a trail-off's exit are
    // data the author placed, and spending them to buy a connection would silently rewrite the
    // sound. The connection itself stays authorable — the resolver reads the released fret, so a
    // pull off a scrape is legal — but only by dragging the tail out by hand first.
    SECTION("the assist never spends a gesture carrier's tail")
    {
        click(controller, 160.0f, 140.0f);
        controller.onChartLegatoToggleRequested();

        // Nothing changed at all: no claim, no growth, and the scrape's own geometry intact.
        CHECK(note(4).attack == common::core::NoteAttack::Pick);
        CHECK(note(3).sustain == common::core::Fraction{1});
        CHECK(note(3).attack == common::core::NoteAttack::PickSlide);

        // An all-skipped press leaves nothing behind at all: no undo entry, and no dialog either
        // (the skip count travels on the planner's return, not through the error seam).
        CHECK(view.shown_errors.empty());
        const EditorViewState* state = stateOrNull(view.last_state);
        REQUIRE(state != nullptr);
        CHECK(state->undo_label != std::optional<std::string>{"Legato"});
    }

    SECTION("undo after the pair is a no-op on the chart: the entry is gone")
    {
        click(controller, 80.0f, 220.0f);
        controller.onChartLegatoToggleRequested();
        controller.onChartLegatoToggleRequested();

        controller.onUndoRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
        CHECK(note(0).sustain == common::core::Fraction{});
    }

    SECTION("a save between the presses reverses by pushing the exact inverse")
    {
        click(controller, 80.0f, 220.0f);
        controller.onChartLegatoToggleRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Legato);

        // The save makes that entry the file's clean state, so dropping it would make "return to
        // clean" restore content the file does not hold. The reversal proceeds anyway — the toggle
        // stays genuine and the grown tail comes back — as its own inverse entry, which leaves the
        // session correctly dirty.
        controller.onSaveRequested();
        REQUIRE(project_services.save_call_count == 1);
        const EditorViewState* state = stateOrNull(view.last_state);
        REQUIRE(state != nullptr);
        const std::size_t entries_after_save = state->undo_history.labels.size();
        // The saved position IS the clean one, which is what the drop would have had to erase.
        CHECK(state->undo_history.clean_position == std::optional{state->undo_history.position});

        controller.onChartLegatoToggleRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
        CHECK(note(0).sustain == common::core::Fraction{});
        CHECK(state->undo_history.labels.size() == entries_after_save + 1);
        CHECK(state->undo_history.clean_position != std::optional{state->undo_history.position});

        // And it is exactly undoable: one Ctrl+Z puts the connection and the tail back.
        controller.onUndoRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Legato);
        CHECK(note(0).sustain == common::core::Fraction{15, 4});
    }
}

// The settle sweep's commit shape (red-team D1): on top of history the flatten FOLDS into the
// burst's own entry, so one undo restores the edit and the claim together; at a mid-stack point it
// DEFERS, because touching bytes there would either truncate a live redo branch or rewrite an entry
// the cursor is not on.
TEST_CASE("EditorController settles a broken claim at the burst's end", "[core][chart]")
{
    // String 1: fret 9 held exactly to the margin before a legato note at fret 5 four beats later,
    // so the claim resolves as a pull-off until the tail is shortened.
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 9,
            .sustain = common::core::Fraction{15, 4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 5,
            .attack = common::core::NoteAttack::Legato,
            .bend = {},
            .slides = {},
        },
    };

    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio, {}, std::move(chart)));

    const auto note = [&](const std::size_t index) -> const common::core::ChartNote& {
        return controller.session().currentArrangement()->chart->notes[index];
    };

    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();

    SECTION("Esc folds the flatten into the burst's entry, and one undo restores both")
    {
        // One shrink disconnects the tail. Mid-burst the claim is untouched — it simply plays as
        // the pick it sounds like.
        controller.onChartSustainAdjustRequested(-1, false);
        CHECK(note(0).sustain == common::core::Fraction{11, 4});
        CHECK(note(1).attack == common::core::NoteAttack::Legato);
        CHECK(state->undo_history.labels.size() == entries_before + 1);

        // Esc ends the burst: the flatten folds into the shrink's own entry rather than stacking a
        // second step.
        controller.onChartEscapePressed();
        CHECK(note(1).attack == common::core::NoteAttack::Pick);
        CHECK(state->undo_history.labels.size() == entries_before + 1);
        REQUIRE_FALSE(state->undo_history.labels.empty());
        CHECK(state->undo_history.labels.back() == "Shrink Sustain");

        // One undo restores the tail and the claim together.
        controller.onUndoRequested();
        CHECK(note(0).sustain == common::core::Fraction{15, 4});
        CHECK(note(1).attack == common::core::NoteAttack::Legato);
    }

    SECTION("a settle at a mid-stack resting point defers instead of rewriting history")
    {
        // Two shrinks, each its own entry (the sustain verb does not coalesce), so undo lands
        // between them.
        controller.onChartSustainAdjustRequested(-1, false);
        controller.onChartSustainAdjustRequested(-1, false);
        CHECK(note(0).sustain == common::core::Fraction{7, 4});
        const std::size_t entries_after_edits = state->undo_history.labels.size();

        // Undo steps the cursor off the top, and the state it lands on still holds the claim.
        controller.onUndoRequested();
        CHECK(note(0).sustain == common::core::Fraction{11, 4});
        CHECK(note(1).attack == common::core::NoteAttack::Legato);

        // Esc there settles nothing: the bytes stay, the redo branch survives, and the claim simply
        // displays as the pick it plays as.
        controller.onChartEscapePressed();
        CHECK(note(1).attack == common::core::NoteAttack::Legato);
        CHECK(state->undo_history.labels.size() == entries_after_edits);
        CHECK(state->redo_label.has_value());

        // Back on top, the same press commits.
        controller.onRedoRequested();
        controller.onChartEscapePressed();
        CHECK(note(1).attack == common::core::NoteAttack::Pick);
    }

    SECTION("a save settles before it writes, so memory never lags the file")
    {
        // The file is resolved either way — the document writer serializes the resolved form — so
        // what the write verb's settle adds is that MEMORY matches the bytes it just produced.
        controller.onChartSustainAdjustRequested(-1, false);
        CHECK(note(1).attack == common::core::NoteAttack::Legato);

        controller.onSaveRequested();
        REQUIRE(project_services.save_call_count == 1);
        CHECK(note(1).attack == common::core::NoteAttack::Pick);
        // Folded into the shrink rather than stacked, exactly as at any other settle event.
        CHECK(state->undo_history.labels.size() == entries_before + 1);
    }
}

// Orphaning: deleting the note a claim connects to is not an edit to the claim. The statement stays
// exactly as the author wrote it and only its resolution changes, which is what makes putting the
// predecessor back a restoration rather than a re-authoring — under a stored direction the delete
// would have had to rewrite or refuse. A left-hand tap rides through untouched, because its
// statement was never relational to begin with.
TEST_CASE("EditorController orphans a claim without rewriting it", "[core][chart]")
{
    // String 1: fret 9 held to the margin before a legato note at fret 5, so the claim resolves as
    // a pull-off. String 2: a left-hand tap with nothing before it on its own string at all.
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 9,
            .sustain = common::core::Fraction{15, 4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 5,
            .attack = common::core::NoteAttack::Legato,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 2,
            .fret = 7,
            .attack = common::core::NoteAttack::LeftTap,
            .bend = {},
            .slides = {},
        },
    };

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
    REQUIRE(loadChartArrangement(controller, project_services, audio, {}, std::move(chart)));

    const auto note = [&](const std::size_t index) -> const common::core::ChartNote& {
        return controller.session().currentArrangement()->chart->notes[index];
    };
    const auto resolution = [&](const std::size_t index) {
        const common::core::Chart& current = *controller.session().currentArrangement()->chart;
        return common::core::chartResolutions(
                   current.notes, current.shapes, controller.session().song().tempo_map)
            .legato[index];
    };

    const common::core::ChartNote claim_bytes = note(1);
    const common::core::ChartNote tap_bytes = note(2);
    CHECK(resolution(1) == common::core::LegatoMotion::Pull);
    CHECK(resolution(2) == common::core::LegatoMotion::Hammer);

    // Select the predecessor and remove it.
    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();

    SECTION("mid-burst the orphaned claim is byte-identical and simply resolves to nothing")
    {
        controller.onSelectionDeleteRequested();
        REQUIRE(controller.session().currentArrangement()->chart->notes.size() == 2);
        CHECK(note(0) == claim_bytes);
        CHECK(resolution(0) == common::core::LegatoMotion::Unjustified);
        CHECK(note(1) == tap_bytes);
        CHECK(resolution(1) == common::core::LegatoMotion::Hammer);

        // Undo puts the predecessor back and the mark returns with it — nothing re-authored it,
        // because nothing had unwritten it.
        controller.onUndoRequested();
        REQUIRE(controller.session().currentArrangement()->chart->notes.size() == 3);
        CHECK(note(1) == claim_bytes);
        CHECK(resolution(1) == common::core::LegatoMotion::Pull);
        CHECK(note(2) == tap_bytes);
    }

    SECTION("the settle flattens the orphan and leaves the tap exactly as authored")
    {
        controller.onSelectionDeleteRequested();
        controller.onChartEscapePressed();
        REQUIRE(controller.session().currentArrangement()->chart->notes.size() == 2);
        CHECK(note(0).attack == common::core::NoteAttack::Pick);
        CHECK(note(1) == tap_bytes);

        // Folded into the delete, so one undo restores the predecessor and the claim together.
        CHECK(state->undo_history.labels.size() == entries_before + 1);
        REQUIRE_FALSE(state->undo_history.labels.empty());
        CHECK(state->undo_history.labels.back() == "Delete Note");
        controller.onUndoRequested();
        REQUIRE(controller.session().currentArrangement()->chart->notes.size() == 3);
        CHECK(note(1) == claim_bytes);
        CHECK(resolution(1) == common::core::LegatoMotion::Pull);
    }
}

// Red-team D3: a sweep that COMMITS closes both coalescing windows. A fold rewrites the top entry's
// content without moving the history position, so every proof an armed window checks still passes —
// and reversing or widening against a plan that no longer describes that entry would either
// resurrect the claim the sweep just flattened or reconstruct the wrong pre-burst stream.
TEST_CASE("EditorController closes its coalescing windows on a committing settle", "[core][chart]")
{
    // String 1 climbs 3 -> 7 -> 5, each a measure apart. The first two both hold to the margin, so
    // the third note's claim resolves as a pull-off and a claim on the middle note would resolve as
    // a hammer-on.
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        common::core::ChartNote{
            .position = {.measure = 1, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 3,
            .sustain = common::core::Fraction{15, 4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 7,
            .sustain = common::core::Fraction{15, 4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 5,
            .attack = common::core::NoteAttack::Legato,
            .bend = {},
            .slides = {},
        },
    };

    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio, {}, std::move(chart)));

    const auto note = [&](const std::size_t index) -> const common::core::ChartNote& {
        return controller.session().currentArrangement()->chart->notes[index];
    };

    // The middle note is the one every scenario below edits; the claim it carries is on the note
    // after it.
    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();

    SECTION("the toggle window: the next press means the ordinary law, not a reversal")
    {
        // Shrinking the middle note's tail breaks the claim after it, and the selection never
        // changes, so no settle has run yet when the next press lands.
        controller.onChartSustainAdjustRequested(-1, false);
        CHECK(note(2).attack == common::core::NoteAttack::Legato);

        // `H` claims the middle note's own connection and arms the toggle window.
        controller.onChartLegatoToggleRequested();
        CHECK(note(1).attack == common::core::NoteAttack::Legato);
        CHECK(note(2).attack == common::core::NoteAttack::Legato);

        // The write verb settles with the selection untouched and the window still armed — the one
        // event that reaches the sweep that way. The flatten folds into the `H` entry.
        controller.onSaveRequested();
        REQUIRE(project_services.save_call_count == 1);
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
        const std::size_t entries_after_settle = state->undo_history.labels.size();

        // The press that follows is the ordinary law: it CLEARS the claim it can no longer set.
        // Were the window still armed it would reverse the folded entry instead, resurrecting the
        // claim the sweep just flattened.
        controller.onChartLegatoToggleRequested();
        CHECK(note(1).attack == common::core::NoteAttack::Pick);
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
        CHECK(state->undo_history.labels.size() == entries_after_settle + 1);
        REQUIRE_FALSE(state->undo_history.labels.empty());
        CHECK(state->undo_history.labels.back() == "Remove Legato");
    }

    SECTION("the fret entry window: the next digit starts a fresh value")
    {
        // Retyping the middle note to the claim's own fret leaves nothing to hammer or pull, so the
        // claim breaks — and the digit arms the multi-digit window.
        controller.onChartFretDigitTyped(5);
        CHECK(note(1).fret == 5);
        CHECK(note(2).attack == common::core::NoteAttack::Legato);
        CHECK(state->undo_history.labels.size() == entries_before + 1);

        // Esc's caret rung keeps the selection, so the digit below still has somewhere to land.
        controller.onChartEscapePressed();
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
        CHECK(state->undo_history.labels.size() == entries_before + 1);
        REQUIRE_FALSE(state->undo_history.labels.empty());
        CHECK(state->undo_history.labels.back() == "Set Fret 5");

        // A surviving window would have combined the digits into fret 54 — refused by the cap — or,
        // worse, replanned the widened entry from a pre-burst stream the fold had already replaced.
        controller.onChartFretDigitTyped(4);
        CHECK(note(1).fret == 4);
        CHECK(state->undo_history.labels.size() == entries_before + 2);

        // Two undos, and the fold restores the retype and the claim in one step.
        controller.onUndoRequested();
        CHECK(note(1).fret == 5);
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
        controller.onUndoRequested();
        CHECK(note(1).fret == 7);
        CHECK(note(2).attack == common::core::NoteAttack::Legato);
    }
}

namespace
{

// String 1 climbs 3 -> 7 -> 2, a measure apart, the first two held to the margin: the third note's
// claim resolves as a pull-off, and shrinking the middle note's tail breaks it. The claim's fret is
// 2 rather than 5 so a single typed digit can also break the claim by making the frets equal while
// still leaving room under the fret cap for a second digit — which is what the multi-digit window
// needs to be armed at all.
[[nodiscard]] common::core::Chart makeBreakableClaimChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        common::core::ChartNote{
            .position = {.measure = 1, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 3,
            .sustain = common::core::Fraction{15, 4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 7,
            .sustain = common::core::Fraction{15, 4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 2,
            .attack = common::core::NoteAttack::Legato,
            .bend = {},
            .slides = {},
        },
    };
    return chart;
}

} // namespace

// The settle-event set, at the five events that reach the sweep through their OWN call site rather
// than through setSelection: a caret move (armChartCaret, the funnel behind pointer, arrow and
// jump), Ctrl+click, double-click, a marquee release that caught notes, and playback start. Each
// rewrites the selection without passing setSelection, so each needs its own settle — and each was
// a live hole: reverting any one left the sweep un-run at that event with nothing to notice.
TEST_CASE("EditorController settles at every ruled selection event", "[core][chart]")
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
    REQUIRE(
        loadChartArrangement(controller, project_services, audio, {}, makeBreakableClaimChart()));

    const auto note = [&](const std::size_t index) -> const common::core::ChartNote& {
        return controller.session().currentArrangement()->chart->notes[index];
    };

    // Select the middle note and shrink its tail: the claim after it is now unjustified, and no
    // settle has run yet because the selection never changed.
    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();
    controller.onChartSustainAdjustRequested(-1, false);
    REQUIRE(note(2).attack == common::core::NoteAttack::Legato);
    REQUIRE(state->undo_history.labels.size() == entries_before + 1);

    SECTION("a caret move settles")
    {
        controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
    }

    SECTION("Ctrl+click settles")
    {
        click(controller, 80.0f, 220.0f, ChartPointerModifiers{.ctrl = true});
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
    }

    SECTION("a double-click settles")
    {
        // Delivered as JUCE delivers the second press of a double click — the leading plain press
        // is deliberately not replayed here, because its own arming would settle first and hide
        // this site.
        controller.onChartPointerDown(pointerEvent(80.0f, 220.0f, {}, 2));
        controller.onChartPointerUp(pointerEvent(80.0f, 220.0f, {}, 2));
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
    }

    SECTION("a marquee release that caught notes settles")
    {
        controller.onChartPointerDown(pointerEvent(20.0f, 200.0f));
        controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
        controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));
        REQUIRE_FALSE(state->chart_edit.selected_notes.empty());
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
    }

    SECTION("playback start settles")
    {
        // Authoring is over for now, so a claim the burst broke must not be heard as something it
        // is not. On this path the selection clear is the proximate settler and the play site's own
        // sweep is the belt for an already-empty selection; what is pinned is the outcome, which is
        // what a regression in either one would break.
        controller.onPlayPausePressed();
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
    }

    // Whichever event ran it, a committing sweep FOLDS: the flatten rides the shrink's own entry,
    // so one Ctrl+Z restores the tail and the claim together.
    CHECK(state->undo_history.labels.size() == entries_before + 1);
}

// A transport seek is a settle event that does NOT touch the selection or the caret's fret entry:
// disarmChartMarker leaves the multi-digit window armed and the seek keeps the note selection, so
// the sweep is reached with the window live and its history position unmoved — the one sequence
// where closing that window is the sweep's own job.
TEST_CASE("EditorController closes the fret-entry window on a settling seek", "[core][chart]")
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
    REQUIRE(
        loadChartArrangement(controller, project_services, audio, {}, makeBreakableClaimChart()));

    const auto note = [&](const std::size_t index) -> const common::core::ChartNote& {
        return controller.session().currentArrangement()->chart->notes[index];
    };

    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();

    // Typing 2 makes the middle note's fret equal the claim's, so nothing connects any more — and
    // arms the window, because a second digit still fits under the fret cap.
    controller.onChartFretDigitTyped(2);
    CHECK(note(1).fret == 2);
    CHECK(note(2).attack == common::core::NoteAttack::Legato);
    REQUIRE(state->undo_history.labels.size() == entries_before + 1);

    // The seek settles. The fold rewrites the retype's entry in place, so the history POSITION is
    // unchanged and every proof the armed window checks still passes.
    controller.onTimelineSeekRequested(common::core::TimePosition{1.0});
    CHECK(note(2).attack == common::core::NoteAttack::Pick);
    CHECK(state->undo_history.labels.size() == entries_before + 1);
    REQUIRE_FALSE(state->chart_edit.selected_notes.empty());

    // So the next digit must start a FRESH value: fret 3, not the widened 23. A surviving window
    // would replan the whole entry from a pre-entry stream the fold has already replaced.
    controller.onChartFretDigitTyped(3);
    CHECK(note(1).fret == 3);
    CHECK(state->undo_history.labels.size() == entries_before + 2);

    // And the two entries undo cleanly in order, the fold restoring retype and claim as one step.
    controller.onUndoRequested();
    CHECK(note(1).fret == 2);
    CHECK(note(2).attack == common::core::NoteAttack::Pick);
    controller.onUndoRequested();
    CHECK(note(1).fret == 7);
    CHECK(note(2).attack == common::core::NoteAttack::Legato);
}

} // namespace rock_hero::editor::core
