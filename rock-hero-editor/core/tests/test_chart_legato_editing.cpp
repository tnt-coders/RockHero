#include <catch2/catch_approx.hpp>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/deferring_message_thread_scheduler.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>

namespace rock_hero::editor::core
{

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
            .sustain = g_fixture_sustain,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 3,
            .fret = 6,
            .sustain = g_fixture_sustain,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 7,
            .sustain = g_fixture_sustain,
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

        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        CHECK(note_attack(3) == common::core::NoteAttack::Legato);
        CHECK(note_attack(1) == common::core::NoteAttack::Pick);

        // The second press reverses — this used to be the stuck press that re-applied forever.
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        CHECK(note_attack(3) == common::core::NoteAttack::Pick);
        CHECK(note_attack(1) == common::core::NoteAttack::Pick);

        // And the toggle keeps round-tripping.
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        CHECK(note_attack(3) == common::core::NoteAttack::Legato);
    }

    SECTION("the clear leaves a picking-hand rider's own attack alone")
    {
        click(controller, 80.0f, 220.0f);
        click(controller, 40.0f, 140.0f, ChartPointerModifiers{.ctrl = true});

        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        CHECK(note_attack(3) == common::core::NoteAttack::Legato);
        CHECK(note_attack(2) == common::core::NoteAttack::Tap);

        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        CHECK(note_attack(3) == common::core::NoteAttack::Pick);
        CHECK(note_attack(2) == common::core::NoteAttack::Tap);
    }

    SECTION("a left-hand tap survives the clear, because Ctrl+H is its sole author")
    {
        // The clear flattens stored claims only. A tap is the one attack plain H must never
        // destroy: the toggle cannot re-create it, so flattening it would lose authored intent no
        // press could bring back.
        click(controller, 80.0f, 220.0f);
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        CHECK(note_attack(3) == common::core::NoteAttack::Legato);

        // The rider becomes a deliberate tap of its own. Nothing precedes it on its string, so no
        // press can ever justify a claim there.
        click(controller, 40.0f, 140.0f);
        controller.onChartLeftTapRequested();
        CHECK(note_attack(2) == common::core::NoteAttack::LeftTap);

        // With both selected there is nothing left to claim — the connection already stands and the
        // tap cannot be justified — so the press clears, and clears the claim only.
        click(controller, 80.0f, 220.0f, ChartPointerModifiers{.ctrl = true});
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
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
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        CHECK(note_attack(1) == common::core::NoteAttack::Pick);
        CHECK(view.shown_errors.empty());

        // Nor does a press that applies report anything: the marks it moved are the feedback.
        click(controller, 80.0f, 220.0f);
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
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
            .sustain = g_fixture_sustain,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 3,
            .fret = 0,
            .sustain = g_fixture_sustain,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 4,
            .fret = 5,
            .sustain = g_fixture_sustain,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 5,
            .fret = 0,
            .sustain = g_fixture_sustain,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 5,
            .sustain = g_fixture_sustain,
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
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        CHECK(note(5).attack == common::core::NoteAttack::Legato);

        // Only the author knows the predecessor was damped, so the tap overrides the claim.
        controller.onChartLeftTapRequested();
        CHECK(note(5).attack == common::core::NoteAttack::LeftTap);

        // The signed symmetry between the stating verb and the inferring one: where the chart DOES
        // justify a connection, plain H writes it again.
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
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
    // String 1 carries a predecessor four beats before its note whose ring stops long short of it,
    // so no claim resolves until the assist authors the connection; string 2 an
    // open string for changing the selection. String 3 repeats the string-1 shape with a
    // TRAIL-OFF as the predecessor — the same missing hold, but a tail the assist is forbidden to
    // spend — and sits later in the stream so the indices above stay put.
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 5,
            .sustain = g_fixture_sustain,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 2,
            .fret = 0,
            .sustain = g_fixture_sustain,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 7,
            .sustain = g_fixture_sustain,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 4, .beat = 1, .offset = {}},
            .string = 3,
            .fret = 9,
            .sustain = common::core::Fraction{1},
            .bend = {},
            .slides = {},
            .slide_out = common::core::SlideOut{.offset = common::core::Fraction{1}, .fret = 12},
        },
        common::core::ChartNote{
            .position = {.measure = 5, .beat = 1, .offset = {}},
            .string = 3,
            .fret = 7,
            .sustain = g_fixture_sustain,
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
        // ring to the successor's onset and claims the connection in one entry.
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        CHECK(note(2).attack == common::core::NoteAttack::Legato);
        CHECK(note(0).sustain == common::core::Fraction{4});

        // The window (ruling 4): the second press reverses that entry exactly — the grown tail
        // included, which the clear law could never restore — and leaves no history entry.
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
        CHECK(note(0).sustain == g_fixture_sustain);
        const EditorViewState* state = stateOrNull(view.last_state);
        REQUIRE(state != nullptr);
        CHECK(state->undo_label != std::optional<std::string>{"Legato"});

        // And the pair of presses keeps cycling.
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        CHECK(note(2).attack == common::core::NoteAttack::Legato);
        CHECK(note(0).sustain == common::core::Fraction{4});
    }

    SECTION("a selection change closes the window: the clear stands and the tail stays")
    {
        click(controller, 80.0f, 220.0f);
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        CHECK(note(2).attack == common::core::NoteAttack::Legato);

        // Extending the selection kills the window's proof, so the next press means the
        // ordinary law — the clear — and the grown tail stays (Ctrl+Z is the revert once the window
        // is gone).
        click(controller, 40.0f, 180.0f, ChartPointerModifiers{.ctrl = true});
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
        CHECK(note(0).sustain == common::core::Fraction{4});

        // And Ctrl+Z is exact where the window would have been genuine: the clear is its own entry,
        // so the first undo restores the claim with the grown tail still standing, and the second
        // undoes the assist that grew it. Two steps rather than the toggle's zero, which is the
        // price of the context switch, not a loss.
        controller.onUndoRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Legato);
        CHECK(note(0).sustain == common::core::Fraction{4});
        controller.onUndoRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
        CHECK(note(0).sustain == g_fixture_sustain);
    }

    // The assist writes a tail, and a tail is sometimes a gesture's own authored window. Where it
    // is, the verb declines rather than reshaping it: a trail-off's exit is data the author
    // placed, and spending it to buy a connection would silently rewrite the sound. The connection
    // itself stays authorable — the resolver reads the released fret, so a pull off the last
    // pitched stop is legal — but only by dragging the tail out by hand first.
    SECTION("the assist never spends a gesture carrier's tail")
    {
        click(controller, 160.0f, 140.0f);
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);

        // Nothing changed at all: no claim, no growth, and the trail-off's own geometry intact.
        CHECK(note(4).attack == common::core::NoteAttack::Pick);
        CHECK(note(3).sustain == common::core::Fraction{1});
        REQUIRE(note(3).slide_out.has_value());
        CHECK(note(3).slide_out->fret == 12);

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
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);

        controller.onUndoRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
        CHECK(note(0).sustain == g_fixture_sustain);
    }

    SECTION("a save between the presses reverses by pushing the exact inverse")
    {
        click(controller, 80.0f, 220.0f);
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
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

        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
        CHECK(note(2).attack == common::core::NoteAttack::Pick);
        CHECK(note(0).sustain == g_fixture_sustain);
        CHECK(state->undo_history.labels.size() == entries_after_save + 1);
        CHECK(state->undo_history.clean_position != std::optional{state->undo_history.position});

        // And it is exactly undoable: one Ctrl+Z puts the connection and the tail back.
        controller.onUndoRequested();
        CHECK(note(2).attack == common::core::NoteAttack::Legato);
        CHECK(note(0).sustain == common::core::Fraction{4});
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
            .sustain = common::core::Fraction{4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 5,
            .sustain = g_fixture_sustain,
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
        CHECK(note(0).sustain == common::core::Fraction{3});
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
        CHECK(note(0).sustain == common::core::Fraction{4});
        CHECK(note(1).attack == common::core::NoteAttack::Legato);
    }

    SECTION("a settle at a mid-stack resting point defers instead of rewriting history")
    {
        // Two shrinks, each its own entry (the sustain verb does not coalesce), so undo lands
        // between them.
        controller.onChartSustainAdjustRequested(-1, false);
        controller.onChartSustainAdjustRequested(-1, false);
        CHECK(note(0).sustain == common::core::Fraction{2});
        const std::size_t entries_after_edits = state->undo_history.labels.size();

        // Undo steps the cursor off the top, and the state it lands on still holds the claim.
        controller.onUndoRequested();
        CHECK(note(0).sustain == common::core::Fraction{3});
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
    // String 1: fret 9 ringing right up to a legato note at fret 5, so the claim resolves as
    // a pull-off. String 2: a left-hand tap with nothing before it on its own string at all.
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 9,
            .sustain = common::core::Fraction{4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 5,
            .sustain = g_fixture_sustain,
            .attack = common::core::NoteAttack::Legato,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 2,
            .fret = 7,
            .sustain = g_fixture_sustain,
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
    // String 1 climbs 3 -> 7 -> 5, each a measure apart. The first two ring to their successors, so
    // the third note's claim resolves as a pull-off and a claim on the middle note would resolve as
    // a hammer-on.
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        common::core::ChartNote{
            .position = {.measure = 1, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 3,
            .sustain = common::core::Fraction{4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 7,
            .sustain = common::core::Fraction{4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 5,
            .sustain = g_fixture_sustain,
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
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
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
        controller.onChartTechniqueToggleRequested(ChartTechnique::Legato);
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

// String 1 climbs 3 -> 7 -> 2, a measure apart, the first two ringing to their successors: the
// third claim resolves as a pull-off, and shrinking the middle note's tail breaks it. The claim's
// fret is 2 rather than 5 so a single typed digit can also break the claim by making the frets
// equal while still leaving room under the fret cap for a second digit — which is what the
// multi-digit window needs to be armed at all.
[[nodiscard]] common::core::Chart makeBreakableClaimChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        common::core::ChartNote{
            .position = {.measure = 1, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 3,
            .sustain = common::core::Fraction{4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 7,
            .sustain = common::core::Fraction{4},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 2,
            .sustain = g_fixture_sustain,
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

// A transport seek settles the pending fret entry through the action gate's uniform prologue:
// the typed value commits as its own entry BEFORE the seek's settle sweep judges the chart, so
// the sweep folds the claim it broke into that very entry. This is the old model's paused-seek
// bug made unrepresentable — a seek can no longer leave a half-typed window armed.
TEST_CASE("EditorController closes the fret-entry window on a settling seek", "[core][chart]")
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
    REQUIRE(
        loadChartArrangement(controller, project_services, audio, {}, makeBreakableClaimChart()));
    static_cast<void>(pending.scheduler.runDelayed());

    const auto note = [&](const std::size_t index) -> const common::core::ChartNote& {
        return controller.session().currentArrangement()->chart->notes[index];
    };

    click(controller, 40.0f, 220.0f);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();

    // Typing 2 is PROVISIONAL: the chart is untouched, the claim still connects, and nothing
    // has pushed. The value lives in the pending state only.
    controller.onChartFretDigitTyped(2);
    CHECK(note(1).fret == 7);
    CHECK(note(2).attack == common::core::NoteAttack::Legato);
    CHECK(state->undo_history.labels.size() == entries_before);
    REQUIRE(state->chart_edit.pending_fret.has_value());

    // The seek settles: the prologue commits fret 2 first, which breaks the claim, and the
    // seek's own sweep then folds the flatten into that entry — one entry for both.
    controller.onTimelineSeekRequested(common::core::TimePosition{1.0});
    CHECK(note(1).fret == 2);
    CHECK(note(2).attack == common::core::NoteAttack::Pick);
    CHECK(state->undo_history.labels.size() == entries_before + 1);
    CHECK_FALSE(state->chart_edit.pending_fret.has_value());
    REQUIRE_FALSE(state->chart_edit.selected_notes.empty());

    // The next digit starts a FRESH value — provisional again, committed by a second seek's
    // prologue: fret 1, never the widened 21.
    controller.onChartFretDigitTyped(1);
    CHECK(note(1).fret == 2);
    controller.onTimelineSeekRequested(common::core::TimePosition{0.5});
    CHECK(note(1).fret == 1);
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
