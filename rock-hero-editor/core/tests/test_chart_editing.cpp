#include <catch2/catch_approx.hpp>
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

// Loads the chart-bearing song fixture through the controller's normal open route.
[[nodiscard]] bool loadChartArrangement(
    EditorController& controller, FakeProjectServices& project_services,
    ConfigurableSongAudio& audio)
{
    const common::core::TimeRange timeline_range = loadedTimelineRange(30.0);
    audio.next_prepared_audio_duration = timeline_range.duration();
    audio.next_set_active_arrangement_result = true;
    common::core::Song song = makeSong(std::filesystem::path{"a.wav"}, timeline_range);
    // The default-constructed TempoMap's terminal anchor sits at 2.0s and time queries clamp
    // there; cover the whole fixture timeline the way real imports do.
    song.tempo_map = common::core::TempoMap::defaultMap(timeline_range.duration());
    song.arrangements.front().chart = makeTestChart();
    project_services.next_song = std::move(song);
    controller.onOpenRequested(std::filesystem::path{"loaded.rhp"});
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

    // The containment hierarchy: a single click selects the individual note, a double click
    // its whole onset group (settled 2026-07-17).
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
// hierarchy (2026-07-17).
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

// One selection exists editor-wide (2026-07-18): selecting on another surface structurally
// replaces the chart selection, cross-surface selection changes never touch the marker, and the
// unified Delete intent deletes whatever kind the selection holds.
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
    const bool caret_before = state->chart_edit.caret.has_value();

    // Selecting an automation point (another surface) replaces the chart selection while the
    // marker keeps its state — selection and position are separate concepts (§9a/§9b).
    controller.onToneAutomationPointSelected(
        "instance-x", "gain", common::core::GridPosition{.measure = 1, .beat = 1, .offset = {}});
    CHECK(state->chart_edit.selected_notes.empty());
    CHECK(state->chart_edit.caret.has_value() == caret_before);

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

// Insert is the neutral-create verb (2026-07-18): a fret-0 note at an armed empty string slot;
// occupied slots (whose arming selects the note) and the passive marker are no-ops.
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

// Typed digits set every selected note to the typed value; Alt+Shift+wheel's fret-shift intent
// moves the whole selection by one, shape-preserving, refusing (never clamping) at fret zero
// and at the fret cap (settled 2026-07-17).
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

    // A double click selects the whole measure-2 chord (both strings).
    doubleClick(controller, 40.0f, 220.0f);
    controller.onChartSelectionDeleteRequested();

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
    controller.onChartSustainAdjustRequested(1);
    controller.onChartFretDigitTyped(3);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 3);
}

// Sustain growth clamps to the minimum-note-distance margin — 1/16 whole note (a quarter beat
// in 4/4) — before the next onset on ANY string (settled 2026-07-18); shrinking floors at
// zero.
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
    controller.onChartSustainAdjustRequested(1);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes[0].sustain == common::core::Fraction{1, 1});

    // Five more quarter-note steps would reach 6 beats, but the measure-3 note sits 4 beats
    // later: growth clamps a quarter-beat margin before it, at 15/4 beats.
    for (int step = 0; step < 5; ++step)
    {
        controller.onChartSustainAdjustRequested(1);
    }
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].sustain == common::core::Fraction{15, 4});

    // The margin binds across strings too: the string-2 chord member has no same-string
    // successor at all, but the measure-3 string-1 note still stops its tail at 15/4 beats.
    click(controller, 40.0f, 180.0f);
    for (int step = 0; step < 6; ++step)
    {
        controller.onChartSustainAdjustRequested(1);
    }
    chart = chartOrNull(controller);
    CHECK(chart->notes[1].sustain == common::core::Fraction{15, 4});

    // Shrinking floors at zero.
    click(controller, 40.0f, 220.0f);
    for (int step = 0; step < 6; ++step)
    {
        controller.onChartSustainAdjustRequested(-1);
    }
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].sustain == common::core::Fraction{});
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

// The Ctrl fine tier on notes (settled 2026-07-18): Alt+Ctrl+Left/Right moves the selection by
// one 1/960-beat step, off the grid; grid steps stay relative afterwards, so the offset rides
// along, and the fine step back returns to the exact lattice slot.
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
