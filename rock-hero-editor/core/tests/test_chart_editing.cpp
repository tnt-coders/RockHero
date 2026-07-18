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

[[nodiscard]] ChartPointerEvent pointerEvent(float x, float y, ChartPointerModifiers modifiers = {})
{
    return ChartPointerEvent{.geometry = makeGeometry(), .x = x, .y = y, .modifiers = modifiers};
}

// Presses and releases at the same point, the plain click gesture.
void click(EditorController& controller, float x, float y, ChartPointerModifiers modifiers = {})
{
    controller.onChartPointerDown(pointerEvent(x, y, modifiers));
    controller.onChartPointerUp(pointerEvent(x, y, modifiers));
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

    // A plain click selects the whole onset group: the two measure-2 notes form a chord and
    // chords are one cohesive unit (settled 2026-07-17).
    click(controller, 40.0f, 220.0f);

    REQUIRE(view.last_state.has_value());
    const ChartEditViewState& edit = view.last_state->chart_edit;
    CHECK(edit.selected_notes == (std::vector<std::size_t>{0, 1}));
    CHECK(transport.seek_call_count == seek_baseline);

    // A sustain-tail click (right of the measure-3 head, inside its one-second tail) selects
    // the sustained note the tail belongs to.
    click(controller, 97.0f, 220.0f);
    CHECK(view.last_state->chart_edit.selected_notes == std::vector<std::size_t>{2});
}

// Ctrl toggles individual membership while plain clicks act on the chord unit, per the settled
// selection granularity (2026-07-17).
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

    // The plain click takes the whole chord; Ctrl removes one member from it.
    click(controller, 40.0f, 220.0f);
    click(controller, 40.0f, 180.0f, ChartPointerModifiers{.ctrl = true});
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->chart_edit.selected_notes == std::vector<std::size_t>{0});

    // Toggling the same note again re-adds it individually.
    click(controller, 40.0f, 180.0f, ChartPointerModifiers{.ctrl = true});
    CHECK(view.last_state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 1}));

    // Shift behaves as plain until plan 52's time-range selection lands: it replaces the
    // selection with the clicked note's onset group.
    click(controller, 80.0f, 220.0f, ChartPointerModifiers{.shift = true});
    CHECK(view.last_state->chart_edit.selected_notes == std::vector<std::size_t>{2});

    // A plain click on a selected note collapses the selection to that note's onset group.
    click(controller, 40.0f, 220.0f, ChartPointerModifiers{.ctrl = true});
    CHECK(view.last_state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 2}));
    click(controller, 40.0f, 220.0f);
    CHECK(view.last_state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 1}));
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

    // The measure-2 chord is frets 3 and 5; typing 9 sets BOTH members to 9.
    click(controller, 40.0f, 220.0f);
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 1}));
    controller.onChartFretDigitTyped(9);
    const auto* chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[0].fret == 9);
    CHECK(chart->notes[1].fret == 9);
    CHECK(view.last_state->undo_label == std::optional<std::string>{"Set Fret 9"});

    // One undo restores both members in one step.
    controller.onUndoRequested();
    chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[0].fret == 3);
    CHECK(chart->notes[1].fret == 5);

    // The fret shift moves the shape as a unit.
    controller.onChartFretShiftRequested(1);
    chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[0].fret == 4);
    CHECK(chart->notes[1].fret == 6);

    // Shifting down stops when the lowest fret reaches zero: four downward ticks land on 0/2
    // and the fifth is refused.
    for (int step = 0; step < 5; ++step)
    {
        controller.onChartFretShiftRequested(-1);
    }
    chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[0].fret == 0);
    CHECK(chart->notes[1].fret == 2);

    // Shifting up stops when the highest fret reaches the cap (30): 28 upward ticks land on
    // 28/30 and the next is refused.
    for (int step = 0; step < 29; ++step)
    {
        controller.onChartFretShiftRequested(1);
    }
    chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[0].fret == 28);
    CHECK(chart->notes[1].fret == 30);
}

// An empty-lane click seeks the snapped position and clears the selection.
TEST_CASE("EditorController seeks and deselects on empty click", "[core][chart]")
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

    // x = 200 is 10.0s (a grid beat at 120 BPM).
    click(controller, 200.0f, 100.0f);

    REQUIRE(view.last_state.has_value());
    const ChartEditViewState& edit = view.last_state->chart_edit;
    CHECK(edit.selected_notes.empty());
    CHECK(transport.seek_call_count == seek_baseline + 1);
    REQUIRE(transport.last_seek_position.has_value());
    CHECK(transport.last_seek_position->seconds == Catch::Approx(10.0));
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

    REQUIRE(view.last_state.has_value());
    REQUIRE(view.last_state->chart_edit.marquee.has_value());
    CHECK(view.last_state->chart_edit.marquee->start_seconds == Catch::Approx(1.0));
    CHECK(view.last_state->chart_edit.marquee->end_seconds == Catch::Approx(3.0));

    controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));
    CHECK(view.last_state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 1}));
    CHECK_FALSE(view.last_state->chart_edit.marquee.has_value());
    CHECK(transport.seek_call_count == seek_baseline);

    // Shift-marquee over the measure-3 note extends the selection.
    controller.onChartPointerDown(
        pointerEvent(70.0f, 200.0f, ChartPointerModifiers{.shift = true}));
    controller.onChartPointerDrag(
        pointerEvent(95.0f, 239.0f, ChartPointerModifiers{.shift = true}));
    controller.onChartPointerUp(pointerEvent(95.0f, 239.0f, ChartPointerModifiers{.shift = true}));
    CHECK(view.last_state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 1, 2}));
}

// Plain Left/Right step the one timeline cursor along the grid (and the fine grid with the
// precision modifier); vertical arrows have no navigation meaning and are ignored.
TEST_CASE("EditorController steps the timeline cursor along the grid", "[core][chart]")
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

    // Park the cursor at measure 4 beat 1 (6.0s) via an empty click.
    click(controller, 120.0f, 220.0f);
    REQUIRE(transport.last_seek_position.has_value());
    CHECK(transport.last_seek_position->seconds == Catch::Approx(6.0));

    // Right by one quarter-note grid step: 6.0s -> 6.5s at 120 BPM.
    controller.onChartCursorStepRequested(ChartStepDirection::Right, false);
    REQUIRE(transport.last_seek_position.has_value());
    CHECK(transport.last_seek_position->seconds == Catch::Approx(6.5));

    controller.onChartCursorStepRequested(ChartStepDirection::Left, false);
    CHECK(transport.last_seek_position->seconds == Catch::Approx(6.0));

    // Vertical arrows are ignored: no seek fires.
    const int seek_count = transport.seek_call_count;
    controller.onChartCursorStepRequested(ChartStepDirection::Up, false);
    controller.onChartCursorStepRequested(ChartStepDirection::Down, false);
    CHECK(transport.seek_call_count == seek_count);

    // Fine step: one 1/960 beat is 0.5/960 seconds at 120 BPM.
    controller.onChartCursorStepRequested(ChartStepDirection::Right, true);
    CHECK(transport.last_seek_position->seconds == Catch::Approx(6.0 + 0.5 / 960.0).epsilon(1e-9));
}
// Alt+click inserts a note at the snapped point as one undo entry; undo removes it again.
TEST_CASE("EditorController inserts a chart note via the Alt quasimode", "[core][chart]")
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

    // x = 120 is 6.0s = measure 4 beat 1; y = 220 is string 1.
    const ChartPointerModifiers alt{.alt = true};
    click(controller, 120.0f, 220.0f, alt);

    const auto* chart = &*controller.session().currentArrangement()->chart;
    REQUIRE(chart->notes.size() == 4);
    CHECK(chart->notes[3].position == common::core::GridPosition{.measure = 4, .beat = 1});
    CHECK(chart->notes[3].string == 1);
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->chart_edit.selected_notes == std::vector<std::size_t>{3});
    CHECK(view.last_state->undo_label == std::optional<std::string>{"Insert Note"});

    controller.onUndoRequested();
    chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes.size() == 3);
    CHECK(view.last_state->chart_edit.selected_notes.empty());

    controller.onRedoRequested();
    chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes.size() == 4);

    // Placing into an existing stack selects the whole resulting onset group: the string-3
    // insert at measure 2 forms a three-note chord with the fixture's two notes. The session
    // end models a fresh Alt press, so the earlier placement does not accumulate in.
    controller.onChartInsertSessionEnded();
    click(controller, 40.0f, 140.0f, alt);
    chart = &*controller.session().currentArrangement()->chart;
    REQUIRE(chart->notes.size() == 5);
    CHECK(chart->notes[2].string == 3);
    CHECK(view.last_state->chart_edit.selected_notes == (std::vector<std::size_t>{0, 1, 2}));
}

// Alt+digits compose the pending insert fret (published to the ghost, no undo involvement),
// placements carry it, and notes placed during one Alt hold accumulate in the selection until
// the session ends (settled 2026-07-17).
TEST_CASE("EditorController composes the insert fret and accumulates the run", "[core][chart]")
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
    REQUIRE(view.last_state.has_value());
    const std::size_t entries_before = view.last_state->undo_history.labels.size();

    // Composition combines in the multi-digit window and publishes to the view without
    // touching the chart or the undo history.
    controller.onChartInsertFretDigitTyped(1);
    controller.onChartInsertFretDigitTyped(2);
    CHECK(view.last_state->chart_edit.insert_fret == 12);
    CHECK(view.last_state->undo_history.labels.size() == entries_before);

    // Placements carry the composed fret and accumulate while the Alt session runs.
    const ChartPointerModifiers alt{.alt = true};
    click(controller, 120.0f, 220.0f, alt);
    click(controller, 160.0f, 220.0f, alt);
    const auto* chart = &*controller.session().currentArrangement()->chart;
    REQUIRE(chart->notes.size() == 5);
    CHECK(chart->notes[3].fret == 12);
    CHECK(chart->notes[4].fret == 12);
    CHECK(view.last_state->chart_edit.selected_notes == (std::vector<std::size_t>{3, 4}));

    // Releasing Alt ends the session: the next placement starts a fresh selection.
    controller.onChartInsertSessionEnded();
    click(controller, 200.0f, 220.0f, alt);
    chart = &*controller.session().currentArrangement()->chart;
    REQUIRE(chart->notes.size() == 6);
    CHECK(view.last_state->chart_edit.selected_notes == std::vector<std::size_t>{5});
}

// Inserting inside an earlier sustain truncates it in the same undo entry (40-Q2-B).
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

    // x = 90 is 4.5s = measure 3 beat 2, inside the measure-3 note's two-beat sustain.
    click(controller, 90.0f, 220.0f, ChartPointerModifiers{.alt = true});

    const auto* chart = &*controller.session().currentArrangement()->chart;
    REQUIRE(chart->notes.size() == 4);
    CHECK(chart->notes[2].sustain == common::core::Fraction{1, 1});
    CHECK(chart->notes[3].position == (common::core::GridPosition{.measure = 3, .beat = 2}));

    // One undo restores both the removed note and the original sustain.
    controller.onUndoRequested();
    chart = &*controller.session().currentArrangement()->chart;
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

    // One plain click selects the whole measure-2 chord (both strings).
    click(controller, 40.0f, 220.0f);
    controller.onChartSelectionDeleteRequested();

    const auto* chart = &*controller.session().currentArrangement()->chart;
    REQUIRE(chart->notes.size() == 1);
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->chart_edit.selected_notes.empty());
    CHECK(view.last_state->undo_label == std::optional<std::string>{"Delete 2 Notes"});

    controller.onUndoRequested();
    chart = &*controller.session().currentArrangement()->chart;
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

    // Ctrl-click isolates the single string-1 note (a plain click would take the whole chord).
    click(controller, 40.0f, 220.0f, ChartPointerModifiers{.ctrl = true});
    REQUIRE(view.last_state.has_value());
    const std::size_t entries_before = view.last_state->undo_history.labels.size();

    controller.onChartFretDigitTyped(1);
    const auto* chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[0].fret == 1);

    // The second digit inside the window widens the SAME undo entry: one action, fret 12.
    controller.onChartFretDigitTyped(2);
    chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[0].fret == 12);
    CHECK(view.last_state->undo_history.labels.size() == entries_before + 1);
    CHECK(view.last_state->undo_label == std::optional<std::string>{"Set Fret 12"});

    // The selection stays on the retyped note under its unchanged key.
    CHECK(view.last_state->chart_edit.selected_notes == std::vector<std::size_t>{0});

    // One undo restores the original fret 3 in one step.
    controller.onUndoRequested();
    chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[0].fret == 3);

    // An interleaved edit kills the window: the next digit starts a fresh value.
    controller.onChartFretDigitTyped(2);
    controller.onChartSustainAdjustRequested(1, false);
    controller.onChartFretDigitTyped(3);
    chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[0].fret == 3);
}

// Shift+arrow sustain growth clamps against the next same-string onset (40-Q2-B).
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

    // Ctrl-click isolates the single string-1 note (a plain click would take the whole chord).
    click(controller, 40.0f, 220.0f, ChartPointerModifiers{.ctrl = true});
    controller.onChartSustainAdjustRequested(1, false);
    const auto* chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[0].sustain == common::core::Fraction{1, 1});

    // Five more quarter-note steps would reach 6 beats, but the measure-3 note on the same
    // string sits 4 beats later: growth clamps there.
    for (int step = 0; step < 5; ++step)
    {
        controller.onChartSustainAdjustRequested(1, false);
    }
    chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[0].sustain == common::core::Fraction{4, 1});

    // Shrinking floors at zero.
    for (int step = 0; step < 6; ++step)
    {
        controller.onChartSustainAdjustRequested(-1, false);
    }
    chart = &*controller.session().currentArrangement()->chart;
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

    // Ctrl-click isolates the single string-1 note (a plain click would take the whole chord).
    click(controller, 40.0f, 220.0f, ChartPointerModifiers{.ctrl = true});

    // Plain arrows never mutate: with a selection they still step the timeline cursor.
    controller.onChartCursorStepRequested(ChartStepDirection::Right, false);
    const auto* chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[0].position == (common::core::GridPosition{.measure = 2, .beat = 1}));
    REQUIRE(view.last_state.has_value());

    // Alt+Up would land on the occupied measure-2 string-2 slot: refused, nothing changes.
    controller.onChartSelectionMoveRequested(ChartStepDirection::Up, false);
    chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[0].string == 1);
    CHECK(chart->notes[0].position == (common::core::GridPosition{.measure = 2, .beat = 1}));

    // Alt+Right moves one quarter-note step; the selection follows the moved note.
    controller.onChartSelectionMoveRequested(ChartStepDirection::Right, false);
    chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[1].position == (common::core::GridPosition{.measure = 2, .beat = 2}));
    CHECK(chart->notes[1].string == 1);
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->chart_edit.selected_notes == std::vector<std::size_t>{1});
    CHECK(view.last_state->undo_label == std::optional<std::string>{"Move Note"});

    controller.onUndoRequested();
    chart = &*controller.session().currentArrangement()->chart;
    CHECK(chart->notes[0].position == (common::core::GridPosition{.measure = 2, .beat = 1}));
    CHECK(chart->notes[0].string == 1);
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
    REQUIRE(view.last_state.has_value());
    REQUIRE_FALSE(view.last_state->chart_edit.selected_notes.empty());

    REQUIRE(loadChartArrangement(controller, project_services, audio));
    CHECK(view.last_state->chart_edit.selected_notes.empty());
}

} // namespace rock_hero::editor::core
