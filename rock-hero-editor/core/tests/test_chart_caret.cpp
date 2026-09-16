#include <catch2/catch_approx.hpp>
#include <optional>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>

namespace rock_hero::editor::core
{

// Selecting a section takes the whole selection, caret included: the chip is what the shared verbs
// act on, so an armed caret standing beside it would be a second answer to where the next keystroke
// lands. Demoted in place, like every other selecting gesture, so the cursor line stays put.
TEST_CASE("EditorController demotes the caret when a section is selected", "[core][chart]")
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

    click(controller, 120.0f, 220.0f);
    const EditorViewState* const armed = stateOrNull(view.last_state);
    REQUIRE(armed != nullptr);
    REQUIRE(caretOrNull(armed->chart_edit) != nullptr);

    controller.onSongSectionSelected(common::core::GridPosition{.measure = 4, .beat = 1});
    const EditorViewState* const taken = stateOrNull(view.last_state);
    REQUIRE(taken != nullptr);
    CHECK(caretOrNull(taken->chart_edit) == nullptr);
}

// Selecting a tone region takes the whole selection, caret included, exactly as selecting a section
// chip does: the region is what the shared verbs act on, so an armed caret left standing beside it
// would be a second answer to where the next keystroke lands. Demoted in place, like every other
// selecting gesture, so the cursor line stays put.
TEST_CASE("EditorController demotes the caret when a tone region is selected", "[core][chart]")
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
    REQUIRE(loadChartArrangement(
        controller,
        project_services,
        audio,
        {},
        makeTestChart(),
        std::nullopt,
        {common::core::ToneRegion{
            .id = "solo-region",
            .start = common::core::GridPosition{.measure = 1, .beat = 1},
            .tone_document_ref = "tones/solo.rht",
        }}));

    click(controller, 120.0f, 220.0f);
    const EditorViewState* const armed = stateOrNull(view.last_state);
    REQUIRE(armed != nullptr);
    REQUIRE(caretOrNull(armed->chart_edit) != nullptr);

    controller.onToneRegionSelected("solo-region");
    const EditorViewState* const taken = stateOrNull(view.last_state);
    REQUIRE(taken != nullptr);
    CHECK(caretOrNull(taken->chart_edit) == nullptr);
}

// The other half of that handoff: the selection is one of the three inputs the audible tone is
// derived from, and the chart's own seam is where it changes here. A plain press on a note replaces
// the selected region with the chart alternative through chartSelectionMutable rather than
// setSelection, and it seeks nothing — so only that emplace can hand the rig back to the tone under
// the cursor, which the lanes and the signal-chain panel already follow.
TEST_CASE(
    "EditorController hands the rig back to the cursor's tone when a caret arms", "[core][chart]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, plugin_host, live_rig),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadChartArrangement(
        controller,
        project_services,
        audio,
        {},
        makeTestChart(),
        std::nullopt,
        {
            common::core::ToneRegion{
                .id = "opening-region",
                .start = common::core::GridPosition{.measure = 1, .beat = 1},
                .tone_document_ref = "tones/clean.rht",
            },
            common::core::ToneRegion{
                .id = "solo-region",
                .start = common::core::GridPosition{.measure = 3, .beat = 1},
                .tone_document_ref = "tones/solo.rht",
            },
        }));

    // The transport rests at the song start, inside the opening region, so selecting the later one
    // makes a tone audible that the cursor is nowhere near.
    REQUIRE(transport.position().seconds < 4.0);
    controller.onToneRegionSelected("solo-region");
    REQUIRE(live_rig.last_audible_tone_ref == std::optional<std::string>{"tones/solo.rht"});

    // The press on the measure-2 note arms the caret and takes the selection with it.
    const int seek_baseline = transport.seek_call_count;
    click(controller, 40.0f, 220.0f);
    CHECK(transport.seek_call_count == seek_baseline);
    CHECK(live_rig.last_audible_tone_ref == std::optional<std::string>{"tones/clean.rht"});
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
    // The focus anchor mirrors the armed caret for the keep-in-view reveal.
    const std::optional<double>& anchor = state->focus_anchor_seconds;
    REQUIRE(anchor.has_value());
    if (anchor.has_value())
    {
        CHECK(*anchor == Catch::Approx(6.0));
    }

    // Right by one quarter-note grid step: 6.0s -> 6.5s at 120 BPM; Left steps back.
    controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(6.5));
    controller.onChartCaretStepRequested(ChartStepDirection::Left, false);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(6.0));

    // Up/Down move across strings. String 1 is not the walk's edge — the tone row lies below it
    // (the focus-row cases cover that) — so this stays on the strings.
    controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->string == 2);
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

    // PageUp again -> no section before measure 3, so the chart start (0.0s) is the stop.
    controller.onChartCaretJumpRequested(ChartCaretJump::PreviousSection);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(0.0));

    // PageUp at the chart start -> nothing before it: refused, caret stays.
    controller.onChartCaretJumpRequested(ChartCaretJump::PreviousSection);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(0.0));

    // PageDown twice -> the two sections (measure 3 = 4.0s, measure 7 = 12.0s).
    controller.onChartCaretJumpRequested(ChartCaretJump::NextSection);
    controller.onChartCaretJumpRequested(ChartCaretJump::NextSection);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(12.0));

    // PageDown again -> no section after measure 7, so the chart end (30.0s) is the stop; a
    // further press is refused there.
    controller.onChartCaretJumpRequested(ChartCaretJump::NextSection);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(30.0));
    controller.onChartCaretJumpRequested(ChartCaretJump::NextSection);
    caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->seconds == Catch::Approx(30.0));
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

    // Again -> no section past measure 7, so the chart end (30.0s) is the stop; a further press
    // is refused and the range stays put.
    controller.onTimeSelectionExtendRequested(
        TimeSelectionExtent::Section, ChartStepDirection::Right);
    time_selection = timeSelectionOrNull(*state);
    REQUIRE(time_selection != nullptr);
    CHECK(time_selection->end.seconds == Catch::Approx(30.0));
    controller.onTimeSelectionExtendRequested(
        TimeSelectionExtent::Section, ChartStepDirection::Right);
    time_selection = timeSelectionOrNull(*state);
    REQUIRE(time_selection != nullptr);
    CHECK(time_selection->end.seconds == Catch::Approx(30.0));
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

// Off-grid notes are first-class caret stops (the union stop set): plain arrows step to the nearer
// of the adjacent grid line and the row's next note, so a note placed with snap off stays reachable
// from the keyboard once snap is back on — and landing on it arms onto it.
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

    // Slide the (2,1) string-1 note one tick off the grid with snap off; the caret (armed on the
    // note by the click) rides the move with it. Snap goes back on so the plain arrows below step
    // the grid again.
    click(controller, 40.0f, 220.0f);
    turnGridSnapOff(controller);
    controller.onSelectionMoveRequested(ChartStepDirection::Right);
    controller.onGridSnapToggleRequested();
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

// A step onto the tone row demotes the caret in place and a step back re-arms it at the cursor.
// The re-arm keeps the exact slot of an object standing on the cursor's tick, so a caret that
// leaves an off-grid note for the tone row walks straight back onto it rather than onto the
// nearest grid line.
TEST_CASE("EditorController walks a caret back onto an off-grid note", "[core][chart]")
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
    REQUIRE(loadChartArrangement(
        controller,
        project_services,
        audio,
        {},
        makeTestChart(),
        std::nullopt,
        {common::core::ToneRegion{
            .id = "solo-region",
            .start = common::core::GridPosition{.measure = 1, .beat = 1},
            .tone_document_ref = "tones/solo.rht",
        }}));

    // The same off-grid setup as the stepping case above: the (2,1) string-1 note slid one tick
    // right with snap off, and snap back on.
    click(controller, 40.0f, 220.0f);
    turnGridSnapOff(controller);
    controller.onSelectionMoveRequested(ChartStepDirection::Right);
    controller.onGridSnapToggleRequested();
    const EditorViewState* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->chart_edit.selected_notes == std::vector<std::size_t>{1});

    controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    CHECK(caretOrNull(state->chart_edit) == nullptr);
    CHECK(state->chart_edit.selected_notes.empty());
    REQUIRE(state->tone_track.regions.size() == 1);
    CHECK(state->tone_track.regions.front().selected);

    controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
    const ChartCaretViewState* const caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->string == 1);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{1});
}

// A dissolved caret leaves its exact position with the cursor, and the next arming takes it back
// even on a row with nothing there: a caret moved off an off-grid note onto the empty string above,
// dissolved by Esc and re-armed by an arrow, still stands in the note's column, so stepping down
// lands back on the note instead of on the nearest grid line beside it.
TEST_CASE("EditorController re-arms at the exact slot a dissolved caret left", "[core][chart]")
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
    turnGridSnapOff(controller);
    controller.onSelectionMoveRequested(ChartStepDirection::Right);
    controller.onGridSnapToggleRequested();
    const EditorViewState* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->chart_edit.selected_notes == std::vector<std::size_t>{1});

    controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
    REQUIRE(state->chart_edit.selected_notes.empty());
    controller.onChartEscapePressed();
    REQUIRE(caretOrNull(state->chart_edit) == nullptr);

    controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    const ChartCaretViewState* const caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->string == 2);
    controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{1});
}

} // namespace rock_hero::editor::core
