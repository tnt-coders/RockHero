#include <catch2/catch_approx.hpp>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/deferring_message_thread_scheduler.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <variant>

namespace rock_hero::editor::core
{

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

// The pending window itself, driven exactly like production: a deferring scheduler holds the
// wake for explicit delivery. Pins the wake laws: a live wake settles its entry
// unconditionally (the stamp is the only guard — a clock re-check used to be able to strand a
// marginally-early wake as a pending entry nothing would settle), and a wake left behind by a
// settled entry is stale and cannot double-commit.
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

// The red half of the pending model, as re-ruled 2026-08-20: an INVALID value is STICKY. It
// paints red, applies nothing, outlives its window (the wake skips it — a refusal display that
// vanishes on a timer is barely a display), always accepts a further digit however long it has
// sat, and discards only when Esc or another intent settles it. The refused first digit is also
// exactly what keeps the legal two-digit target typable under a capo.
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
// seen — the old model's silent no-op was exactly the invisible refusal W3 exists to end.
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
// discards and the caret survives for an immediate retype (user ruling).
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

// Undo is NOT special (user ruling): the uniform prologue settles first, so Ctrl+Z on a valid
// pending value commits it and then undoes it — the value appears and is removed, with a redo
// entry left behind, because the value really was a valid edit.
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
    // The provisional value is visible as the GHOST HEAD it will become (user ruling 2026-08-27),
    // not as a floating box: the slot has no head yet, so the entry publishes one.
    const ChartInsertGhostViewState* const ghost = insertGhostOrNull(state->chart_edit);
    REQUIRE(ghost != nullptr);
    CHECK(ghost->slot.string == 1);
    CHECK(ghost->fret == std::optional{2});
    // And the box does NOT also draw there: at an empty slot it is the refusal display alone, so
    // one value is never stated twice in one column.
    CHECK_FALSE(state->chart_edit.pending_fret.has_value());

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

// THE GHOST PENDING HEAD (user ruling 2026-08-27). An entry begun on an empty slot has no head to
// wear its value, so the first digit publishes one: the insert ghost, carrying the typed fret and
// drawn as the head that value becomes. The warrant is the dissolve law's own requirement — a
// record nothing draws is worth nothing, so a value held back for a window has to be visibly
// pending — and the gate is the ghost's own: it previews only an insert that would actually happen.
TEST_CASE("EditorController previews a pending insert as a ghost head", "[core][chart]")
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

    SECTION("the first digit publishes it, and the window's settle turns it into the real head")
    {
        controller.onChartFretDigitTyped(1);
        const ChartInsertGhostViewState* const ghost = insertGhostOrNull(state->chart_edit);
        REQUIRE(ghost != nullptr);
        CHECK(ghost->slot.seconds == Catch::Approx(6.0));
        CHECK(ghost->slot.string == 1);
        CHECK(ghost->fret == std::optional{1});
        // Nothing is in the chart yet: the head is a preview, not the note.
        CHECK(chartOrNull(controller)->notes.size() == notes_before);

        // The settle turns the preview into the note: the ghost goes and a real head stands where
        // it stood, at the value it was showing.
        CHECK(pending.scheduler.runDelayed() == 1);
        CHECK_FALSE(state->chart_edit.insert_ghost.has_value());
        const auto* const chart = chartOrNull(controller);
        REQUIRE(chart->notes.size() == notes_before + 1);
        CHECK(chart->notes.back().position == common::core::GridPosition{.measure = 4, .beat = 1});
        CHECK(chart->notes.back().fret == 1);
    }

    SECTION("a second digit ends the entry, so the ring never outlives the value it showed")
    {
        // The ring is republished from the entry's own value, so it follows every digit — but at
        // the 24-fret cap a second digit always EXHAUSTS the entry, which then settles in the same
        // keystroke. So the observable fact is that the ring showed 1 and went the moment the
        // combined value became a note: no ring is ever left standing over the head it turned into.
        controller.onChartFretDigitTyped(1);
        const ChartInsertGhostViewState* const ghost = insertGhostOrNull(state->chart_edit);
        REQUIRE(ghost != nullptr);
        CHECK(ghost->fret == std::optional{1});

        controller.onChartFretDigitTyped(2);
        CHECK_FALSE(state->chart_edit.insert_ghost.has_value());
        CHECK_FALSE(state->chart_edit.pending_fret.has_value());
        const auto* const chart = chartOrNull(controller);
        REQUIRE(chart->notes.size() == notes_before + 1);
        CHECK(chart->notes.back().fret == 12);
    }

    SECTION("Esc leaves no residue")
    {
        controller.onChartFretDigitTyped(1);
        REQUIRE(state->chart_edit.insert_ghost.has_value());
        // Esc on a value that CAN apply is not a cancellable thing: it falls through to the caret
        // rung and commits on the way past the uniform settle. Either way the ring is gone — what
        // it was previewing is now the note itself, so nothing provisional is left drawn.
        controller.onChartEscapePressed();
        CHECK_FALSE(state->chart_edit.insert_ghost.has_value());
        CHECK_FALSE(state->chart_edit.pending_fret.has_value());
        CHECK(chartOrNull(controller)->notes.size() == notes_before + 1);
    }
}

// The ghost's honesty gate, and the discrimination for the case above: an insert the gate would
// refuse previews NOTHING. A ring showing a head that cannot exist is exactly the lying affordance
// the overlay is written to avoid — and the refusal is still seen, in the box that owns that job at
// a slot with no head (the red-box ruling of 2026-08-20).
TEST_CASE("EditorController previews no ghost for a refused pending insert", "[core][chart]")
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
    CHECK_FALSE(state->chart_edit.insert_ghost.has_value());
    REQUIRE(state->chart_edit.pending_fret.has_value());
    if (state->chart_edit.pending_fret.has_value())
    {
        CHECK(state->chart_edit.pending_fret->text == "1");
        CHECK_FALSE(state->chart_edit.pending_fret->valid);
        CHECK(std::holds_alternative<ChartSlotViewState>(state->chart_edit.pending_fret->at));
    }
    CHECK(chartOrNull(controller)->notes.size() == notes_before);

    // Esc claims the invalid value's rung: the box goes, nothing is planted, and there was no ring
    // to leave behind — the refusal never previewed anything, which is the whole point.
    controller.onChartEscapePressed();
    CHECK_FALSE(state->chart_edit.pending_fret.has_value());
    CHECK_FALSE(state->chart_edit.insert_ghost.has_value());
    CHECK(chartOrNull(controller)->notes.size() == notes_before);

    // And the refused digit is what keeps the legal two-digit target typable: 1 then 2 states fret
    // 12, which the capo allows. Nothing provisional is left drawn once it lands.
    controller.onChartFretDigitTyped(1);
    controller.onChartFretDigitTyped(2);
    CHECK_FALSE(state->chart_edit.pending_fret.has_value());
    CHECK_FALSE(state->chart_edit.insert_ghost.has_value());
    const auto* const chart = chartOrNull(controller);
    REQUIRE(chart->notes.size() == notes_before + 1);
    CHECK(chart->notes.back().fret == 12);
}

// The other half of the ghost's honesty gate: a ring is a note-to-BE, so an insert that REPLACES
// the note already at its slot previews no ring. The slot is reachable without contrivance — a
// caret does not move on undo, so undoing a delete leaves one armed over a restored note with an
// empty selection, and the next digit takes the insert flow and replaces. Drawing a ring with a
// digit in it there would print a second fret over a head already showing its own.
TEST_CASE("EditorController previews no ghost where the insert would replace", "[core][chart]")
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

    // The value is valid — it would land, replacing — so this is not the refusal case: the box is
    // the one that draws, in its ordinary (non-red) form, and no ring joins it.
    controller.onChartFretDigitTyped(1);
    CHECK_FALSE(state->chart_edit.insert_ghost.has_value());
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
// mover — pointer, arrow, jump, row step, and the Insert key through the same planting function —
// commits a typed value with the selection landing where the caret lands. Two holes the design
// review of 2026-08-20 found: the End key settled only after moving the marker, so the committed
// note was selected at the slot the caret had LEFT; and the Insert key reached the apply path with
// no prologue at all, so the last-resort branch discarded the typed value and planted fret 0.
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
    // An entry begun on an empty slot is visibly pending as the ghost HEAD it will become, not as a
    // box: the box at that slot is the refusal display alone (user ruling 2026-08-27).
    REQUIRE(state->chart_edit.insert_ghost.has_value());
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

    SECTION("the Insert key commits the typed value instead of discarding it for a fret 0")
    {
        controller.onNeutralInsertRequested();
        const auto* chart = chartOrNull(controller);
        // One note planted, at the typed fret; the slot is now occupied, so the Insert verb itself
        // had nothing further to plant.
        REQUIRE(chart->notes.size() == 4);
        CHECK(chart->notes[3].fret == 1);
        CHECK(state->undo_history.labels.size() == entries_before + 1);
        CHECK_FALSE(state->chart_edit.pending_fret.has_value());
        CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{3});
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
        // re-projects through a half-typed value (the flicker the old model painted), because no
        // half-typed value ever reaches the chart. The provisional "1" lives only in the pending
        // state.
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
    REQUIRE(scrape.slide_out.has_value());
    if (scrape.slide_out.has_value())
    {
        REQUIRE(*scrape.slide_out == 3);
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
    REQUIRE(retyped.slide_out.has_value());
    if (retyped.slide_out.has_value())
    {
        CHECK(*retyped.slide_out == 3);
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
