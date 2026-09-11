#include <catch2/catch_approx.hpp>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/deferring_message_thread_scheduler.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <variant>

namespace rock_hero::editor::core
{
namespace
{

// The fret the published lane draws at a slot, or empty when no head stands there — what a charter
// SEES, which during a pending entry is the chart with the entry's plan applied.
[[nodiscard]] std::optional<int> drawnFretAt(
    const EditorViewState& state, const double seconds, const int string)
{
    if (state.tab == nullptr)
    {
        return std::nullopt;
    }
    for (const common::core::NoteViewState& note : state.tab->notes)
    {
        if (note.string == string && note.start_seconds == Catch::Approx(seconds))
        {
            return note.fret;
        }
    }
    return std::nullopt;
}

} // namespace

// Typing a digit on the empty caret INSERTS a note there with the typed fret (the caret
// model): a second digit inside the window widens the SAME insert to the combined fret, and
// one undo removes the note entirely.
TEST_CASE("EditorController inserts a note by typing at the caret", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    static_cast<void>(pending.scheduler.runDelayed());

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

// A bare digit at a caret a ring covers STATES A POINT on that ring's path: a fret at an instant
// the string is already sounding is a stop the hand takes, never a second onset, and nothing
// single-press cuts a ring. The Alt digit says the same thing here; the two part company only at
// the ring's exact end, one test below.
TEST_CASE("EditorController digit inside a sustain states a point on the path", "[core][chart]")
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
    const common::core::Chart original = *chartOrNull(controller);

    // The target slot (measure 3 beat 2, 4.5s) sits inside the measure-3 note's two-beat ring on
    // string 1. Reach it via the empty string-2 lane and an arrow down, so the click itself
    // selects nothing.
    click(controller, 90.0f, 180.0f);
    controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    controller.onChartFretDigitTyped(5);

    const auto* chart = chartOrNull(controller);
    // No new note, and the ring is untouched: the digit landed as a keyframe one beat along it.
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[2].fret == 7);
    CHECK(chart->notes[2].sustain == common::core::Fraction{2});
    REQUIRE(chart->notes[2].keyframes.size() == 1);
    CHECK(chart->notes[2].keyframes[0].offset == common::core::Fraction{1});
    CHECK(chart->notes[2].keyframes[0].fret == 5);

    controller.onUndoRequested();
    CHECK(*chartOrNull(controller) == original);
}

// At the ring's EXACT END a bare digit places the ADJACENT head, and nothing is truncated: the
// ring already stops where the new onset starts, so the two stand side by side and sequential
// entry never trips.
TEST_CASE("EditorController digit at a ring's end places an adjacent note", "[core][chart]")
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
    const common::core::Chart original = *chartOrNull(controller);

    // Measure 3 beat 3 (5.0s) is exactly where the measure-3 note's two-beat ring ends.
    click(controller, 100.0f, 180.0f);
    controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    controller.onChartFretDigitTyped(5);

    const auto* chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == 4);
    CHECK(chart->notes[2].sustain == original.notes[2].sustain);
    CHECK(chart->notes[2].keyframes.empty());
    CHECK(chart->notes[3].position == common::core::GridPosition{.measure = 3, .beat = 3});
    CHECK(chart->notes[3].fret == 5);

    controller.onUndoRequested();
    CHECK(*chartOrNull(controller) == original);
}

// Inside a ring the PATH verb says exactly what the bare digit says: a POINT on the ring the slot
// falls inside. The two part company at the ring's end alone, which the case below pins.
TEST_CASE("EditorController Alt digit inside a sustain states a keyframe on it", "[core][chart]")
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
    const common::core::Chart original = *chartOrNull(controller);

    // The target slot (measure 3 beat 2, 4.5s) sits inside the measure-3 note's two-beat ring on
    // string 1. Reach it via the empty string-2 lane and an arrow down, so the click itself
    // selects nothing.
    click(controller, 90.0f, 180.0f);
    controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    controller.onChartPathDigitTyped(5);

    const auto* chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[2].sustain == common::core::Fraction{2, 1});
    REQUIRE(chart->notes[2].keyframes.size() == 1);
    CHECK(chart->notes[2].keyframes[0].offset == common::core::Fraction{1});
    CHECK(chart->notes[2].keyframes[0].fret == 5);

    controller.onUndoRequested();
    CHECK(*chartOrNull(controller) == original);
}

// THE ONE CELL THE VERBS PART COMPANY IN: at the ring's EXACT END `Alt`+digit states its RELEASE —
// the slide-out — where the bare digit above placed the adjacent head.
TEST_CASE("EditorController Alt digit at a ring's end states its release", "[core][chart]")
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
    const common::core::Chart original = *chartOrNull(controller);

    click(controller, 100.0f, 180.0f);
    controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    controller.onChartPathDigitTyped(5);

    const auto* chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[2].sustain == original.notes[2].sustain);
    REQUIRE(chart->notes[2].keyframes.size() == 1);
    CHECK(chart->notes[2].keyframes[0].offset == common::core::Fraction{2});
    CHECK(chart->notes[2].keyframes[0].fret == 5);

    controller.onUndoRequested();
    CHECK(*chartOrNull(controller) == original);
}

// With no ring to join, the PATH verb states what the bare digit does: on an empty slot Alt+digit
// places the same head, so the modifier costs a charter nothing where it means nothing.
TEST_CASE("EditorController Alt digit on an empty slot places a head", "[core][chart]")
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

    // x = 200 is 10.0s on the empty string-4 lane, beyond every fixture note.
    click(controller, 200.0f, 100.0f);
    controller.onChartPathDigitTyped(7);

    const auto* chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == notes_before + 1);
    CHECK(chart->notes.back().string == 4);
    CHECK(chart->notes.back().fret == 7);
    CHECK(chart->notes.back().keyframes.empty());
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    static_cast<void>(pending.scheduler.runDelayed());

    // A plain click selects just the string-1 note (containment hierarchy).
    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();

    // The first digit is PROVISIONAL: the chart holds nothing of it, no entry pushes, and the
    // pending state carries the typed text over the selected head, valid (white, not red).
    controller.onChartFretDigitTyped(1);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 3);
    CHECK(state->undo_history.labels.size() == entries_before);
    REQUIRE(state->chart_edit.pending_fret.has_value());
    if (state->chart_edit.pending_fret.has_value())
    {
        CHECK(state->chart_edit.pending_fret->text == "1");
        CHECK(state->chart_edit.pending_fret->valid);
        CHECK(
            state->chart_edit.pending_fret->at ==
            decltype(state->chart_edit.pending_fret->at){ChartPendingFretTargets{.notes = {0}}});
    }

    // The second digit combines and SETTLES: one action, fret 12, pending gone.
    controller.onChartFretDigitTyped(2);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 12);
    CHECK(state->undo_history.labels.size() == entries_before + 1);
    CHECK(state->undo_label == std::optional<std::string>{"Set Fret 12"});
    CHECK_FALSE(state->chart_edit.pending_fret.has_value());

    // The selection stays on the retyped note under its unchanged key.
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0});

    // One undo restores the original fret 3 in one step.
    controller.onUndoRequested();
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 3);

    // An interleaved verb settles the pending value FIRST (the uniform prologue): "2" commits
    // as its own entry before the sustain grows, and the next digit starts a fresh value —
    // provisional again, committed here by the caret step's own prologue.
    controller.onChartFretDigitTyped(2);
    controller.onChartSustainAdjustRequested(1);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 2);
    controller.onChartFretDigitTyped(3);
    controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 3);
}

// The pending window itself, driven exactly like production: a deferring scheduler holds the wake
// for explicit delivery. Pins the wake laws: a live wake settles its entry unconditionally (the
// stamp is the only guard — a clock re-check could strand a marginally-early wake as a pending
// entry nothing would settle), and a wake left behind by a settled entry is stale and cannot
// double-commit.
TEST_CASE("EditorController pending digit settles on its window wake", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    // Drain anything the load itself scheduled, so the pumps below deliver exactly the fret
    // window's wake.
    static_cast<void>(pending.scheduler.runDelayed());

    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();

    controller.onChartFretDigitTyped(2);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 3);
    CHECK(state->undo_history.labels.size() == entries_before);
    REQUIRE(state->chart_edit.pending_fret.has_value());

    SECTION("the delivered wake settles the entry")
    {
        CHECK(pending.scheduler.runDelayed() == 1);
        chart = chartOrNull(controller);
        CHECK(chart->notes[0].fret == 2);
        CHECK(state->undo_history.labels.size() == entries_before + 1);
        CHECK(state->undo_label == std::optional<std::string>{"Set Fret 2"});
        CHECK_FALSE(state->chart_edit.pending_fret.has_value());

        controller.onUndoRequested();
        CHECK(chartOrNull(controller)->notes[0].fret == 3);
    }
    SECTION("a stale wake after a second digit cannot double-commit")
    {
        controller.onChartFretDigitTyped(3);
        chart = chartOrNull(controller);
        CHECK(chart->notes[0].fret == 23);
        CHECK(state->undo_history.labels.size() == entries_before + 1);

        CHECK(pending.scheduler.runDelayed() == 1);
        chart = chartOrNull(controller);
        CHECK(chart->notes[0].fret == 23);
        CHECK(state->undo_history.labels.size() == entries_before + 1);
    }
}

// The red half of the pending model: an INVALID value is STICKY. It paints red, applies nothing,
// outlives its window (the wake skips it — a refusal display that vanishes on a timer is barely a
// display), always accepts a further digit however long it has sat, and discards only when Esc or
// another intent settles it. The refused first digit is also exactly what keeps the legal two-digit
// target typable under a capo.
TEST_CASE("EditorController keeps an invalid pending digit until it is settled", "[core][chart]")
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
    controller.attachView(view);
    common::core::Chart capo_chart = makeTestChart();
    capo_chart.tuning.capo = 2;
    const bool loaded =
        loadChartArrangement(controller, project_services, audio, {}, std::move(capo_chart));
    REQUIRE(loaded);
    static_cast<void>(pending.scheduler.runDelayed());

    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();

    // "2" targets a fret the capo covers: Invalid, drawn red, nothing applied.
    controller.onChartFretDigitTyped(2);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 3);
    REQUIRE(state->chart_edit.pending_fret.has_value());
    if (state->chart_edit.pending_fret.has_value())
    {
        CHECK(state->chart_edit.pending_fret->text == "2");
        CHECK_FALSE(state->chart_edit.pending_fret->valid);
    }

    SECTION("the wake leaves it red, and a digit long past the window still extends it")
    {
        CHECK(pending.scheduler.runDelayed() == 1);
        CHECK(chartOrNull(controller)->notes[0].fret == 3);
        REQUIRE(state->chart_edit.pending_fret.has_value());

        // Far past the window: the red box is visibly live, so the digit combines — 23 is the
        // legal value the refused first digit exists to keep typable.
        pending.now_ms += 5000;
        controller.onChartFretDigitTyped(3);
        CHECK(chartOrNull(controller)->notes[0].fret == 23);
        CHECK(state->undo_history.labels.size() == entries_before + 1);
        CHECK_FALSE(state->chart_edit.pending_fret.has_value());
    }
    SECTION("another intent's prologue discards it and the verb still applies")
    {
        controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
        CHECK(chartOrNull(controller)->notes[0].fret == 3);
        CHECK(chartOrNull(controller)->notes[0].palm_mute);
        CHECK(state->undo_history.labels.size() == entries_before + 1);
        CHECK_FALSE(state->chart_edit.pending_fret.has_value());
    }
}

// The sticky rule holds for IMMEDIATE digits too: a digit that would settle in its own
// keystroke when valid goes pending red when the gate refuses it, because the refusal must be
// seen — a silent no-op is exactly the invisible refusal W3 exists to end.
TEST_CASE("EditorController keeps an invalid immediate digit pending red", "[core][chart]")
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
    controller.attachView(view);
    common::core::Chart capo_chart = makeTestChart();
    capo_chart.tuning.capo = 5;
    // The fixture's fret-3 notes sit under this capo; re-fret them legal so the chart loads.
    for (common::core::ChartNote& note : capo_chart.notes)
    {
        note.fret += 5;
    }
    const bool loaded =
        loadChartArrangement(controller, project_services, audio, {}, std::move(capo_chart));
    REQUIRE(loaded);
    static_cast<void>(pending.scheduler.runDelayed());

    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();

    // "4" is an immediate digit, but under capo 5 it is Invalid: pending red, nothing applied.
    controller.onChartFretDigitTyped(4);
    CHECK(chartOrNull(controller)->notes[0].fret == 8);
    CHECK(state->undo_history.labels.size() == entries_before);
    REQUIRE(state->chart_edit.pending_fret.has_value());
    if (state->chart_edit.pending_fret.has_value())
    {
        CHECK(state->chart_edit.pending_fret->text == "4");
        CHECK_FALSE(state->chart_edit.pending_fret->valid);
    }

    // Esc cancels the problem: the value discards and the caret survives.
    controller.onChartEscapePressed();
    CHECK(chartOrNull(controller)->notes[0].fret == 8);
    CHECK_FALSE(state->chart_edit.pending_fret.has_value());
    CHECK(state->chart_edit.caret.has_value());
}

// Esc's rung is claimed by an INVALID pending value only: it cancels the problem, so the value
// discards and the caret survives for an immediate retype.
TEST_CASE("EditorController Esc discards an invalid pending value, keeps caret", "[core][chart]")
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
    common::core::Chart capo_chart = makeTestChart();
    capo_chart.tuning.capo = 2;
    const bool loaded =
        loadChartArrangement(controller, project_services, audio, {}, std::move(capo_chart));
    REQUIRE(loaded);

    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();

    controller.onChartFretDigitTyped(2);
    REQUIRE(state->chart_edit.pending_fret.has_value());

    controller.onChartEscapePressed();
    CHECK(chartOrNull(controller)->notes[0].fret == 3);
    CHECK(state->undo_history.labels.size() == entries_before);
    CHECK_FALSE(state->chart_edit.pending_fret.has_value());
    // The caret survives the discard for an immediate retype.
    CHECK(state->chart_edit.caret.has_value());
}

// A VALID pending value is not a cancellable thing: Esc falls through to the caret rung and the
// value commits on the way through the uniform settle — a value you typed is a value you meant.
TEST_CASE("EditorController Esc commits a valid pending value via the caret rung", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    static_cast<void>(pending.scheduler.runDelayed());

    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();

    controller.onChartFretDigitTyped(2);
    REQUIRE(state->chart_edit.pending_fret.has_value());

    controller.onChartEscapePressed();
    CHECK(chartOrNull(controller)->notes[0].fret == 2);
    CHECK(state->undo_history.labels.size() == entries_before + 1);
    CHECK_FALSE(state->chart_edit.pending_fret.has_value());
    // The caret rung consumed the press: the marker dissolved with the value committed.
    CHECK_FALSE(state->chart_edit.caret.has_value());
}

// Undo is NOT special: the uniform prologue settles first, so Ctrl+Z on a valid pending value
// commits it and then undoes it — the value appears and is removed, with a redo entry left behind,
// because the value really was a valid edit.
TEST_CASE("EditorController undo settles a pending value then undoes it", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    static_cast<void>(pending.scheduler.runDelayed());

    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();

    controller.onChartFretDigitTyped(2);
    controller.onUndoRequested();
    CHECK(chartOrNull(controller)->notes[0].fret == 3);
    CHECK(state->undo_history.labels.size() == entries_before + 1);

    // The redo entry left behind IS the committed value.
    controller.onRedoRequested();
    CHECK(chartOrNull(controller)->notes[0].fret == 2);
}

// A pending INSERT plants nothing until it settles: the chart gains no note while the value is
// provisional, and the settle applies one insert carrying the combined value — selected, caret
// armed on it, one undo entry that removes the whole thing.
TEST_CASE("EditorController pending insert plants nothing until it settles", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    static_cast<void>(pending.scheduler.runDelayed());

    // The empty caret at measure 4 beat 1 (x = 120 is 6.0s) on string 1.
    click(controller, 120.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();

    controller.onChartFretDigitTyped(2);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes.size() == 3);
    CHECK(state->undo_history.labels.size() == entries_before);
    // The provisional value draws as the head it will become, projected into the published chart
    // without being stored, and the pending box over it is what says it has not settled.
    CHECK(drawnFretAt(*state, 6.0, 1) == std::optional{2});
    REQUIRE(state->chart_edit.pending_fret.has_value());
    if (state->chart_edit.pending_fret.has_value())
    {
        CHECK(state->chart_edit.pending_fret->text == "2");
        CHECK(state->chart_edit.pending_fret->valid);
        CHECK(std::holds_alternative<ChartSlotViewState>(state->chart_edit.pending_fret->at));
    }

    CHECK(pending.scheduler.runDelayed() == 1);
    chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == 4);
    CHECK(chart->notes[3].position == common::core::GridPosition{.measure = 4, .beat = 1});
    CHECK(chart->notes[3].fret == 2);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{3});
    CHECK(state->undo_history.labels.size() == entries_before + 1);

    controller.onUndoRequested();
    CHECK(chartOrNull(controller)->notes.size() == 3);
}

// THE PENDING INSERT, SEEN. An entry begun on an empty slot draws its value as the head it will
// become — the entry's plan projected into the published chart, nothing stored — and wears the
// pending box over it, the same box a retyped head wears, so the value is visibly provisional
// until the window settles.
TEST_CASE("EditorController boxes a pending insert over its drawn head", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    static_cast<void>(pending.scheduler.runDelayed());

    // The empty caret at measure 4 beat 1 (x = 120 is 6.0s) on string 1.
    click(controller, 120.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t notes_before = chartOrNull(controller)->notes.size();

    SECTION("the first digit draws the head under its box, and the settle keeps the head")
    {
        controller.onChartFretDigitTyped(1);
        CHECK(drawnFretAt(*state, 6.0, 1) == std::optional{1});
        REQUIRE(state->chart_edit.pending_fret.has_value());
        if (state->chart_edit.pending_fret.has_value())
        {
            CHECK(state->chart_edit.pending_fret->text == "1");
            CHECK(state->chart_edit.pending_fret->valid);
            const auto* const slot =
                std::get_if<ChartSlotViewState>(&state->chart_edit.pending_fret->at);
            REQUIRE(slot != nullptr);
            if (slot != nullptr)
            {
                CHECK(slot->seconds == Catch::Approx(6.0));
                CHECK(slot->string == 1);
            }
        }
        // Nothing is in the chart yet: the head is a projection, not the note.
        CHECK(chartOrNull(controller)->notes.size() == notes_before);

        // The settle stores the note the projection was showing: the box goes and the same head
        // stays where it stood.
        CHECK(pending.scheduler.runDelayed() == 1);
        CHECK_FALSE(state->chart_edit.pending_fret.has_value());
        CHECK(drawnFretAt(*state, 6.0, 1) == std::optional{1});
        const auto* const chart = chartOrNull(controller);
        REQUIRE(chart->notes.size() == notes_before + 1);
        CHECK(chart->notes.back().position == common::core::GridPosition{.measure = 4, .beat = 1});
        CHECK(chart->notes.back().fret == 1);
    }

    SECTION("a second digit ends the entry, so the box never outlives the value it showed")
    {
        // The projection and the box are republished from the entry's own value, so they follow
        // every digit — but at the 24-fret cap a second digit always EXHAUSTS the entry, which then
        // settles in the same keystroke. So the observable fact is that 1 was drawn pending and the
        // box went the moment the combined value became a note.
        controller.onChartFretDigitTyped(1);
        CHECK(drawnFretAt(*state, 6.0, 1) == std::optional{1});
        REQUIRE(state->chart_edit.pending_fret.has_value());

        controller.onChartFretDigitTyped(2);
        CHECK_FALSE(state->chart_edit.pending_fret.has_value());
        const auto* const chart = chartOrNull(controller);
        REQUIRE(chart->notes.size() == notes_before + 1);
        CHECK(chart->notes.back().fret == 12);
    }

    SECTION("Esc leaves no residue")
    {
        controller.onChartFretDigitTyped(1);
        REQUIRE(state->chart_edit.pending_fret.has_value());
        // Esc on a value that CAN apply is not a cancellable thing: it falls through to the caret
        // rung and commits on the way past the uniform settle. Either way the box is gone — what
        // it was marking pending is now the note itself, so nothing provisional is left drawn.
        controller.onChartEscapePressed();
        CHECK_FALSE(state->chart_edit.pending_fret.has_value());
        CHECK(chartOrNull(controller)->notes.size() == notes_before + 1);
    }
}

// The discrimination for the case above: an insert the gate would refuse projects NOTHING. A head
// drawn for a note that cannot exist is exactly the lying affordance the preview must avoid — and
// the refusal is still seen, in the red box at the slot.
TEST_CASE("EditorController projects nothing for a refused pending insert", "[core][chart]")
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
    controller.attachView(view);
    // A capo at 2 makes fret 1 unplayable, which is the one refusal a typed insert can reach: every
    // other value the digits can express is a legal fret somewhere.
    common::core::Chart capo_chart = makeTestChart();
    capo_chart.tuning.capo = 2;
    const bool loaded =
        loadChartArrangement(controller, project_services, audio, {}, std::move(capo_chart));
    REQUIRE(loaded);
    static_cast<void>(pending.scheduler.runDelayed());

    click(controller, 120.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t notes_before = chartOrNull(controller)->notes.size();

    controller.onChartFretDigitTyped(1);
    CHECK_FALSE(drawnFretAt(*state, 6.0, 1).has_value());
    REQUIRE(state->chart_edit.pending_fret.has_value());
    if (state->chart_edit.pending_fret.has_value())
    {
        CHECK(state->chart_edit.pending_fret->text == "1");
        CHECK_FALSE(state->chart_edit.pending_fret->valid);
        CHECK(std::holds_alternative<ChartSlotViewState>(state->chart_edit.pending_fret->at));
    }
    CHECK(chartOrNull(controller)->notes.size() == notes_before);

    // Esc claims the invalid value's rung: the box goes and nothing is planted — the refusal never
    // projected a head either, which is the whole point.
    controller.onChartEscapePressed();
    CHECK_FALSE(state->chart_edit.pending_fret.has_value());
    CHECK(chartOrNull(controller)->notes.size() == notes_before);

    // And the refused digit is what keeps the legal two-digit target typable: 1 then 2 states fret
    // 12, which the capo allows. Nothing provisional is left drawn once it lands.
    controller.onChartFretDigitTyped(1);
    controller.onChartFretDigitTyped(2);
    CHECK_FALSE(state->chart_edit.pending_fret.has_value());
    const auto* const chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == notes_before + 1);
    CHECK(chart->notes.back().fret == 12);
}

// An insert that REPLACES the note already at its slot is pending the same way: the box at the
// slot, over the head the projection redraws at the typed value. The slot is reachable without
// contrivance — a caret does not move on undo, so undoing a delete leaves one armed over a restored
// note with an empty selection, and the next digit takes the insert flow and replaces.
TEST_CASE("EditorController boxes a pending insert that would replace", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    static_cast<void>(pending.scheduler.runDelayed());

    // Plant a note at the empty caret (measure 4 beat 1, string 1), then delete it and undo: the
    // note is back under the caret, and the selection the delete emptied does not come back with
    // it — which is exactly the state that routes the next digit into the insert flow.
    click(controller, 120.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    controller.onChartFretDigitTyped(1);
    controller.onChartFretDigitTyped(2);
    const std::size_t occupied = chartOrNull(controller)->notes.size();
    controller.onSelectionDeleteRequested();
    REQUIRE(chartOrNull(controller)->notes.size() == occupied - 1);
    controller.onUndoRequested();
    REQUIRE(chartOrNull(controller)->notes.size() == occupied);
    REQUIRE(state->chart_edit.selected_notes.empty());
    // The earlier entries' window wakes are spent here (each is a no-op past its own settle), so
    // the count below is this entry's own timer and nothing else.
    static_cast<void>(pending.scheduler.runDelayed());

    // The value is valid — it would land, replacing — so this is not the refusal case: the box
    // draws in its ordinary (non-red) form, and the head beneath it already shows the typed value.
    controller.onChartFretDigitTyped(1);
    CHECK(drawnFretAt(*state, 6.0, 1) == std::optional{1});
    REQUIRE(state->chart_edit.pending_fret.has_value());
    if (state->chart_edit.pending_fret.has_value())
    {
        CHECK(state->chart_edit.pending_fret->text == "1");
        CHECK(state->chart_edit.pending_fret->valid);
        CHECK(std::holds_alternative<ChartSlotViewState>(state->chart_edit.pending_fret->at));
    }

    // And it really does replace when it settles: one note at the slot, at the typed fret.
    CHECK(pending.scheduler.runDelayed() == 1);
    CHECK_FALSE(state->chart_edit.pending_fret.has_value());
    const auto* const chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == occupied);
    CHECK(chart->notes.back().position == common::core::GridPosition{.measure = 4, .beat = 1});
    CHECK(chart->notes.back().fret == 1);
}

// The caret funnel is where the pending entry settles, BEFORE the marker moves, so every caret
// mover — pointer, arrow, jump, row step — commits a typed value with the selection landing where
// the caret lands. The hole the funnel closes: settling AFTER the marker moves (the End key's
// shape) selects the committed note at the slot the caret has LEFT.
TEST_CASE("EditorController settles a pending entry through every caret mover", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    static_cast<void>(pending.scheduler.runDelayed());

    // The empty caret at measure 4 beat 1 on string 1, with a provisional 1 typed at it.
    click(controller, 120.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();
    controller.onChartFretDigitTyped(1);
    // An entry begun on an empty slot is visibly pending: its head is drawn and the box sits on it.
    REQUIRE(state->chart_edit.pending_fret.has_value());
    CHECK(chartOrNull(controller)->notes.size() == 3);

    SECTION("a caret jump commits the value and the selection follows the caret, not the note")
    {
        controller.onChartCaretJumpRequested(ChartCaretJump::ChartEnd);
        const auto* chart = chartOrNull(controller);
        REQUIRE(chart->notes.size() == 4);
        CHECK(chart->notes[3].position == common::core::GridPosition{.measure = 4, .beat = 1});
        CHECK(chart->notes[3].fret == 1);
        CHECK(state->undo_history.labels.size() == entries_before + 1);
        CHECK_FALSE(state->chart_edit.pending_fret.has_value());
        // The caret sits at the chart end on an empty slot, so the selection is empty: "armed
        // implies the selection is what sits under the caret" survives the jump.
        const ChartCaretViewState* caret = caretOrNull(state->chart_edit);
        REQUIRE(caret != nullptr);
        CHECK(caret->seconds > 6.0);
        CHECK(state->chart_edit.selected_notes.empty());
    }
}

// A caret move is a commit point for the technique toggle windows (the legato ruling's settle
// set): stepping away and back does not leave the window armed, so the next press is the verb's
// ordinary law rather than a reversal of the entry it remembers.
TEST_CASE("EditorController closes a technique toggle window on a caret move", "[core][chart]")
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
    const std::size_t entries_before = state->undo_history.labels.size();

    controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
    CHECK(chartOrNull(controller)->notes[0].palm_mute);
    CHECK(state->undo_history.labels.size() == entries_before + 1);

    // Away and back: the selection is the same note again, and the entry is still the history
    // top, but the window died with the first move.
    controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    controller.onChartCaretStepRequested(ChartStepDirection::Left, false);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0});

    // The ordinary law: a second entry that removes the mute, not a reversal that erases the first.
    controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
    CHECK_FALSE(chartOrNull(controller)->notes[0].palm_mute);
    CHECK(state->undo_history.labels.size() == entries_before + 2);
    REQUIRE_FALSE(state->undo_history.labels.empty());
    CHECK(state->undo_history.labels.back() == "Remove Palm Mute");
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
    controller.attachView(view);

    // String 1: fret 9 at measure 2 ringing exactly to a legato note at fret 5 in measure 3, four
    // beats on, so the claim resolves as a pull-off.
    common::core::Chart chart_with_claim;
    chart_with_claim.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart_with_claim.notes = {
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 9,
            .sustain = common::core::Fraction{4},
            .bend = 0.0,
            .keyframes = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 5,
            .sustain = g_fixture_sustain,
            .attack = common::core::NoteAttack::Legato,
            .bend = 0.0,
            .keyframes = {},
        },
    };
    REQUIRE(loadChartArrangement(controller, project_services, audio, {}, chart_with_claim));
    static_cast<void>(pending.scheduler.runDelayed());

    const auto resolution = [&](const std::size_t index) {
        const common::core::Chart& current = *controller.session().currentArrangement()->chart;
        return common::core::chartConnections(current.notes, controller.session().song().tempo_map)
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

    SECTION("two digits settle as one entry, and the mark follows the committed value")
    {
        // Mid-entry the chart is UNTOUCHED — the pending model's whole point: the claim never
        // re-projects through a half-typed value — which would flicker — because no half-typed
        // value ever reaches the chart. The provisional "1" lives only in the pending state.
        controller.onChartFretDigitTyped(1);
        const auto* chart = chartOrNull(controller);
        REQUIRE(chart->notes.size() == 2);
        CHECK(chart->notes[0].fret == 9);
        CHECK(chart->notes[1] == claim_bytes);
        CHECK(resolution(1) == common::core::LegatoMotion::Pull);
        REQUIRE(state->chart_edit.pending_fret.has_value());
        if (state->chart_edit.pending_fret.has_value())
        {
            CHECK(state->chart_edit.pending_fret->text == "1");
        }
        // The armed caret's selection is still exactly the note under it.
        CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{0});

        // The second digit combines to 12 and SETTLES as one entry; the claim's bytes never
        // change and its direction re-projects against the committed value only.
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
    // that justifies NOTHING. Storing the direction rather than the intent could not hold that
    // middle state: with no attack to name it, an edit passing through would have to repair the
    // note or refuse. Here it is simply a claim displaying as the pick it plays as, and stepping
    // back out of it needs no repair — the mark returns because the fret did.
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

// A refused first digit still arms the multi-digit entry window, so an in-range two-digit
// value stays typeable when the digit alone refuses — here a scrape start stilled against its
// path terminal, the fret-verb law's surviving scrape refusal (the path itself never retypes).
TEST_CASE("EditorController fret typing recovers from a refused first digit", "[core][chart]")
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
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));
    static_cast<void>(pending.scheduler.runDelayed());

    // Build a high downward scrape: type the note to fret 17, then toggle — the default path
    // travels to fret 3, so typing "3" would still the start against the terminal.
    click(controller, 40.0f, 220.0f);
    controller.onChartFretDigitTyped(1);
    controller.onChartFretDigitTyped(7);
    controller.onChartTechniqueToggleRequested(ChartTechnique::PickSlide);
    const auto* chart = chartOrNull(controller);
    const common::core::ChartNote& scrape = chart->notes[0];
    REQUIRE(scrape.attack == common::core::NoteAttack::PickSlide);
    REQUIRE(scrape.fret == 17);
    const int* const scrape_terminal = common::core::slideOutFretOrNull(scrape);
    REQUIRE(scrape_terminal != nullptr);
    if (scrape_terminal != nullptr)
    {
        REQUIRE(*scrape_terminal == 3);
    }

    // "3" refuses (start stilled against the terminal — a scrape cannot sit still): at the
    // 24-fret cap it is an immediate digit, so it goes pending red rather than silently
    // no-oping, and the chart holds its old value.
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    controller.onChartFretDigitTyped(3);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].fret == 17);
    REQUIRE(state->chart_edit.pending_fret.has_value());
    if (state->chart_edit.pending_fret.has_value())
    {
        CHECK_FALSE(state->chart_edit.pending_fret->valid);
    }

    // Esc cancels the problem and the caret survives, so the recovery is an immediate retype:
    // "1" then "3" combine to 13, which travels to the terminal again, and the path stays where
    // it was authored per the fret-verb law.
    controller.onChartEscapePressed();
    CHECK_FALSE(state->chart_edit.pending_fret.has_value());
    controller.onChartFretDigitTyped(1);
    controller.onChartFretDigitTyped(3);
    chart = chartOrNull(controller);
    const common::core::ChartNote& retyped = chart->notes[0];
    CHECK(retyped.fret == 13);
    const int* const retyped_terminal = common::core::slideOutFretOrNull(retyped);
    REQUIRE(retyped_terminal != nullptr);
    if (retyped_terminal != nullptr)
    {
        CHECK(*retyped_terminal == 3);
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
    controller.onSelectionMoveRequested(ChartStepDirection::Up);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].string == 1);
    CHECK(chart->notes[0].position == (common::core::GridPosition{.measure = 2, .beat = 1}));

    // Alt+Right moves one quarter-note step; the selection follows the moved note.
    controller.onSelectionMoveRequested(ChartStepDirection::Right);
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

// Off-grid authoring with snap off: Alt+Left/Right moves the selection by one tick (1/3840 whole
// note, which is 1/960 beat in x/4); grid steps with snap back on stay relative, so the offset
// rides along, and a tick step back with snap off returns to the exact lattice slot.
TEST_CASE("EditorController moves the selection by one tick with grid snap off", "[core][chart]")
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
    turnGridSnapOff(controller);
    controller.onSelectionMoveRequested(ChartStepDirection::Right);
    const auto* chart = chartOrNull(controller);
    CHECK(
        chart->notes[1].position ==
        (common::core::GridPosition{
            .measure = 2, .beat = 1, .offset = common::core::Fraction{1, 960}
        }));
    CHECK(chart->notes[1].string == 1);

    // A grid step from the off-grid slot stays relative: the 1/960 offset rides along.
    controller.onGridSnapToggleRequested();
    controller.onSelectionMoveRequested(ChartStepDirection::Right);
    chart = chartOrNull(controller);
    CHECK(
        chart->notes[1].position ==
        (common::core::GridPosition{
            .measure = 2, .beat = 2, .offset = common::core::Fraction{1, 960}
        }));

    // The tick step back lands exactly on the lattice again — no residue.
    turnGridSnapOff(controller);
    controller.onSelectionMoveRequested(ChartStepDirection::Left);
    chart = chartOrNull(controller);
    CHECK(chart->notes[1].position == (common::core::GridPosition{.measure = 2, .beat = 2}));
}

} // namespace rock_hero::editor::core
