#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <optional>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The chart-editing fixture wired for the keyframe verbs: the shared glide chart opened through the
// controller's normal route, with the quarter-note grid the lane geometry assumes. Its onset is at
// 2.0s (x = 40 at the fixture geometry's 20 px/s, y = 140 on string 3) and its junction's linked
// head draws at 4.0s (x = 80) — far enough from the onset head that the two boxes cannot overlap.
struct KeyframeFixture
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

    explicit KeyframeFixture(common::core::Chart chart = makeGlideChart())
    {
        controller.attachView(view);
        // Move outside the assertion macro: REQUIRE re-mentions its expression textually, which
        // bugprone-use-after-move reads as a use of the moved-from chart.
        const bool loaded =
            loadChartArrangement(controller, project_services, audio, {}, std::move(chart));
        REQUIRE(loaded);
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

// The same chart under the deferring scheduler and a pinned clock, for the scenarios that need a
// pending value to STAY pending across calls. Under the immediate scheduler the window's wake fires
// inside the arming keystroke, which settles the value before a second digit could reach it.
struct PendingKeyframeFixture
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

    PendingKeyframeFixture()
    {
        controller.attachView(view);
        const bool loaded =
            loadChartArrangement(controller, project_services, audio, {}, makeGlideChart());
        REQUIRE(loaded);
        static_cast<void>(pending.scheduler.runDelayed());
    }
};

// Lane-local pixels of the fixture's two clickable marks, and of the two tail slots the create
// gesture is exercised at: 3.0s (two beats into the ring, on the travel leg toward the junction)
// and 5.0s (six beats in, out where the path holds at the junction's own fret).
constexpr float g_onset_x{40.0f};
constexpr float g_junction_x{80.0f};
constexpr float g_travel_tail_x{60.0f};
constexpr float g_holding_tail_x{100.0f};
constexpr float g_string_3_y{140.0f};

} // namespace

// A keyframe is a selection citizen: the linked head the lane draws at a junction is clickable,
// and clicking it selects the keyframe alone. It sits on a slot of its own — the instant its
// offset reaches along the ring, on the note's string — so the click arms the caret there exactly
// as a click on a head does, and the next digit points at the point.
TEST_CASE("A click selects the keyframe under it and arms the caret on it", "[core][chart]")
{
    KeyframeFixture fixture;

    // Arm the caret on the note first, so the move below is a state CHANGE and not the position a
    // fresh controller would report anyway.
    click(fixture.controller, g_onset_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{0});

    click(fixture.controller, g_junction_x, g_string_3_y);
    const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
    CHECK(
        edit.selected_keyframes ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}}));
    CHECK(edit.selected_notes.empty());
    REQUIRE(edit.caret.has_value());
    if (edit.caret.has_value())
    {
        CHECK_THAT(edit.caret->seconds, Catch::Matchers::WithinAbs(4.0, 1e-9));
        CHECK(edit.caret->string == 3);
    }
    // The selection is not empty, which is what makes the selection-scoped verbs reachable.
    CHECK(publishedState(fixture.view).selection_present);
}

// The caret walks the union stop set — the adjacent grid line or the string's next authored
// object, whichever is nearer — and a keyframe is an authored object standing on its own slot, so
// the arrows stop on it exactly as they stop on a note, and landing arms onto it. A junction OFF
// the grid is what proves the union: the grid line alone would step straight past it.
TEST_CASE("The caret steps onto a keyframe", "[core][chart]")
{
    // The junction moved half a beat off the quarter-note grid: 3.5 beats in, at 3.75s.
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].keyframes[0].offset = common::core::Fraction{7, 2};
    KeyframeFixture fixture{std::move(chart)};

    // Arm on the empty grid slot two beats in (3.0s): nothing sits there, so nothing is selected.
    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.caret.has_value());
    CHECK(publishedState(fixture.view).chart_edit.selected_keyframes.empty());

    // Right lands on the 3.5s grid line first — the junction is still beyond it — and that empty
    // slot keeps the selection empty.
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    CHECK(publishedState(fixture.view).chart_edit.selected_keyframes.empty());

    // Right again: the junction at 3.75s is nearer than the 4.0s line, so the caret stops ON it
    // and arms onto it, selecting the keyframe.
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    {
        const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
        CHECK(
            edit.selected_keyframes == (std::vector<ChartKeyframeRef>{
                                           ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}
                                       }));
        CHECK(edit.selected_notes.empty());
        REQUIRE(edit.caret.has_value());
        if (edit.caret.has_value())
        {
            CHECK_THAT(edit.caret->seconds, Catch::Matchers::WithinAbs(3.75, 1e-9));
            CHECK(edit.caret->string == 3);
        }
    }

    // The next step continues to the 4.0s line (an empty slot clears the selection); stepping
    // back stops on the junction again before the line.
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    CHECK(publishedState(fixture.view).chart_edit.selected_keyframes.empty());
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Left, false);
    CHECK(
        publishedState(fixture.view).chart_edit.selected_keyframes ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}}));
}

// A double click selects the onset GROUP of what it lands on, and a keyframe's group is every
// keyframe at its instant: a chord slide's junctions across strings are one arrival, as a chord's
// heads are one strike. A note struck at that instant is not a member. The marker demotes to a
// cursor in place, as every group selection leaves it.
TEST_CASE("A double click on a keyframe selects the junctions at its instant", "[core][chart]")
{
    // A second glide on string 4 arriving four beats in beside the fixture's, and a note struck on
    // string 5 at that very instant, which the group must not sweep in.
    common::core::Chart chart = makeGlideChart();
    common::core::ChartNote partner =
        makeTestNote({.measure = 2, .beat = 1}, 4, 7, common::core::Fraction{8});
    partner.keyframes = {common::core::Keyframe{.offset = common::core::Fraction{4}, .fret = 11}};
    chart.notes.push_back(std::move(partner));
    chart.notes.push_back(makeTestNote({.measure = 3, .beat = 1}, 5, 3));
    KeyframeFixture fixture{std::move(chart)};

    doubleClick(fixture.controller, g_junction_x, g_string_3_y);
    const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
    CHECK(
        edit.selected_keyframes == (std::vector<ChartKeyframeRef>{
                                       ChartKeyframeRef{.note_index = 0, .keyframe_index = 0},
                                       ChartKeyframeRef{.note_index = 1, .keyframe_index = 0},
                                   }));
    CHECK(edit.selected_notes.empty());
    CHECK_FALSE(edit.caret.has_value());
}

// Ctrl+double-click toggles the GROUP at an instant as one unit onto or off the standing selection:
// the double click's second press retracts the first press's single toggle and applies the
// group's, so the gesture never flips one member twice. A group half in is completed, and a group
// wholly in is taken out.
TEST_CASE("Ctrl+double-click adds and removes the junctions at an instant", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    common::core::ChartNote partner =
        makeTestNote({.measure = 2, .beat = 1}, 4, 7, common::core::Fraction{8});
    partner.keyframes = {common::core::Keyframe{.offset = common::core::Fraction{4}, .fret = 11}};
    chart.notes.push_back(std::move(partner));
    KeyframeFixture fixture{std::move(chart)};
    const auto ctrl_double_click = [&fixture](const float x, const float y) {
        const ChartPointerModifiers ctrl{.ctrl = true};
        click(fixture.controller, x, y, ctrl);
        fixture.controller.onChartPointerDown(pointerEvent(x, y, ctrl, 2));
        fixture.controller.onChartPointerUp(pointerEvent(x, y, ctrl, 2));
    };
    const std::vector<ChartKeyframeRef> both{
        ChartKeyframeRef{.note_index = 0, .keyframe_index = 0},
        ChartKeyframeRef{.note_index = 1, .keyframe_index = 0},
    };

    // The head is selected and the caret armed on it; the Ctrl form ADDS the junctions beside it
    // and demotes the marker, as every multi-select gesture does.
    click(fixture.controller, g_onset_x, g_string_3_y);
    ctrl_double_click(g_junction_x, g_string_3_y);
    {
        const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
        CHECK(edit.selected_notes == std::vector<std::size_t>{0});
        CHECK(edit.selected_keyframes == both);
        CHECK_FALSE(edit.caret.has_value());
    }

    // Every junction is in, so the same gesture takes the group out and leaves the head standing.
    ctrl_double_click(g_junction_x, g_string_3_y);
    {
        const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
        CHECK(edit.selected_notes == std::vector<std::size_t>{0});
        CHECK(edit.selected_keyframes.empty());
    }
}

// A caret armed on a lone keyframe rides its nudge exactly as one on a lone note does: the point's
// slot moves a beat, and the caret moves with it rather than being left on the emptied slot. The
// step back is the case that once dropped it — a run replaying to its origin RETIRES its entry
// rather than replacing it, and the caret must ride that step home like any other.
TEST_CASE("The caret rides a moved keyframe out and back", "[core][chart]")
{
    KeyframeFixture fixture;

    click(fixture.controller, g_junction_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.caret.has_value());

    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    {
        const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
        CHECK(
            edit.selected_keyframes == (std::vector<ChartKeyframeRef>{
                                           ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}
                                       }));
        REQUIRE(edit.caret.has_value());
        if (edit.caret.has_value())
        {
            CHECK_THAT(edit.caret->seconds, Catch::Matchers::WithinAbs(4.5, 1e-9));
            CHECK(edit.caret->string == 3);
        }
    }

    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    {
        const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
        CHECK(
            edit.selected_keyframes == (std::vector<ChartKeyframeRef>{
                                           ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}
                                       }));
        REQUIRE(edit.caret.has_value());
        if (edit.caret.has_value())
        {
            CHECK_THAT(edit.caret->seconds, Catch::Matchers::WithinAbs(4.0, 1e-9));
            CHECK(edit.caret->string == 3);
        }
    }
}

// A keyframe occupies its slot exactly as a note does, so Insert with the caret on one has nothing
// to place: neither a note there nor a second point at an offset one already holds.
TEST_CASE("Insert on a keyframe's slot places nothing", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);

    fixture.controller.onNeutralInsertRequested();
    CHECK(currentChart(fixture.controller) == original);
    CHECK_FALSE(publishedState(fixture.view).chart_edit.insert_ghost.has_value());
}

// The vibrato channel's second authoring scope: with a keyframe selected, `V` states the shake AT
// that keyframe and leaves the note's onset statement alone. The second press inside the verb
// window takes it back — and takes the statement out entirely rather than writing a false one,
// because a statement that restates the state already in force changes neither the path nor the
// state (the generalized dissolve law).
TEST_CASE("The vibrato verb states the shake at a selected keyframe", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);

    const common::core::Chart shaking = currentChart(fixture.controller);
    REQUIRE(shaking.notes.size() == 1);
    REQUIRE(shaking.notes[0].keyframes.size() == 1);
    CHECK(shaking.notes[0].keyframes[0].vibrato == common::core::VibratoState::Narrow);
    // The onset is untouched: the ring opens still and shakes from the junction on.
    CHECK_FALSE(common::core::isShaking(shaking.notes[0].vibrato));
    // And the position channel rides along unchanged — one record, so the coupling needs no copy.
    CHECK(shaking.notes[0].keyframes[0].fret == 9);

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    const common::core::Chart cleared = currentChart(fixture.controller);
    CHECK(cleared == original);
    // A statement saying what was already true is no statement, so the keyframe keeps only its
    // fret rather than gaining a stated `false`.
    REQUIRE(cleared.notes[0].keyframes.size() == 1);
    CHECK_FALSE(cleared.notes[0].keyframes[0].vibrato.has_value());
    // The pair reversed its own entry, so it leaves no history trace at all.
    CHECK_FALSE(publishedState(fixture.view).undo_enabled);
}

// The same clear a press LATER, with the toggle window closed behind a selection change, so the
// dissolve law itself does the work rather than the window's exact reversal: the press writes a
// statement, the statement says what was already true, and a keyframe the drop empties would go
// with it. Here the point states a fret too, so what dissolves is the statement alone — and this
// is a NEW undo entry, not a reversal, which is what the round trip below proves.
TEST_CASE("Clearing the shake at a keyframe dissolves the statement it wrote", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    const common::core::Chart shaking = currentChart(fixture.controller);
    REQUIRE(shaking.notes[0].keyframes[0].vibrato == common::core::VibratoState::Narrow);

    // A selection change commits that entry and closes the verb window, so the press below runs
    // the verb's ordinary law instead of reversing anything.
    click(fixture.controller, g_onset_x, g_string_3_y);
    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);

    const common::core::Chart cleared = currentChart(fixture.controller);
    CHECK(cleared == original);
    REQUIRE(cleared.notes[0].keyframes.size() == 1);
    CHECK_FALSE(cleared.notes[0].keyframes[0].vibrato.has_value());
    CHECK(cleared.notes[0].keyframes[0].fret == 9);

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == shaking);
    fixture.controller.onRedoRequested();
    CHECK(currentChart(fixture.controller) == cleared);
    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == shaking);
}

// Delete reaches keyframes exactly as it reaches notes: it takes every statement
// the selected keyframe makes, and a keyframe stating nothing is no record at all, so the point
// goes with them. The round trip is field-exact in both directions.
TEST_CASE("Delete takes the selected keyframe and undo puts it back", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onSelectionDeleteRequested();

    const common::core::Chart cut = currentChart(fixture.controller);
    REQUIRE(cut.notes.size() == 1);
    CHECK(cut.notes[0].keyframes.empty());
    // The note it rode is untouched — deleting a keyframe is not deleting the gesture.
    CHECK(cut.notes[0].fret == 5);
    CHECK(cut.notes[0].sustain == common::core::Fraction{8});

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
    fixture.controller.onRedoRequested();
    CHECK(currentChart(fixture.controller) == cut);
    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// `Shift+L` on a selected keyframe severs the gesture there (W10's addendum): the note's path ends
// at the junction and a new head takes the remainder. One compound undo entry spanning both
// products, reversed exactly.
TEST_CASE("The disconnect verb severs the gesture at a selected keyframe", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartKeyframeDisconnectRequested();

    const common::core::Chart severed = currentChart(fixture.controller);
    REQUIRE(severed.notes.size() == 2);
    CHECK(severed.notes[0].position == original.notes[0].position);
    CHECK(severed.notes[0].fret == 5);
    CHECK(severed.notes[0].sustain == common::core::Fraction{4});
    // The origin keeps the keyframe it travels to: the leg the user split at is real travel, and
    // the junction is now an equal-fret handover to the new head. The arrival lands the
    // glide-into-a-landing margin before that head (a quarter beat in 4/4) — a fret-stating
    // keyframe may not sit on a later onset of its own string — while the RING still runs to it.
    REQUIRE(severed.notes[0].keyframes.size() == 1);
    CHECK(severed.notes[0].keyframes[0].fret == 9);
    CHECK(severed.notes[0].keyframes[0].offset == common::core::Fraction{15, 4});

    CHECK(
        severed.notes[1].position ==
        common::core::GridPosition{.measure = 3, .beat = 1, .offset = {}});
    CHECK(severed.notes[1].string == 3);
    CHECK(severed.notes[1].fret == 9);
    CHECK(severed.notes[1].sustain == common::core::Fraction{4});
    CHECK(severed.notes[1].keyframes.empty());
    // W10's signed store for a split head. The addendum's proposed UNSTRUCK-tie reading needs
    // LegatoMotion::Continuation, which is unbuilt — the default is a proposal, not a ruling.
    CHECK(severed.notes[1].attack == common::core::NoteAttack::Legato);

    // The SPLIT PRODUCT becomes the selection — it is the one record the plan inserted at a new
    // key, which the follow rule already calls "the edit's own product". So the next verb acts on
    // the new head, and the origin (rewritten in place, and never selected) does not join. The
    // junction key itself resolves to nothing now: its arrival retreated off the offset it named,
    // which is the linger, and nothing wears a ring for it.
    CHECK(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{1});
    CHECK(publishedState(fixture.view).chart_edit.selected_keyframes.empty());

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
    fixture.controller.onRedoRequested();
    CHECK(currentChart(fixture.controller) == severed);
    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// The verb is selection-scoped like every technique verb beside it, so a selection holding no
// keyframe is simply no operand — pressing it is inert, not an error, and leaves no entry.
TEST_CASE("The disconnect verb is inert without a keyframe selected", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    fixture.controller.onChartKeyframeDisconnectRequested();
    CHECK(currentChart(fixture.controller) == original);

    click(fixture.controller, g_onset_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{0});
    fixture.controller.onChartKeyframeDisconnectRequested();
    CHECK(currentChart(fixture.controller) == original);
    CHECK_FALSE(publishedState(fixture.view).undo_enabled);
}

// The arrow move steps whichever kind the selection holds, each where it lives: a note by its
// slot, a keyframe by its offset along the ring it rides (W13's ruling). The step is the placement
// quantum's — one beat on this fixture's quarter-note grid.
//
// Every step RE-KEYS the selection, because a keyframe's identity IS its offset: the plan says so
// itself, since the default follow would leave the key naming an offset nothing sits on. The second
// press in each direction is what proves it — it can only find an operand if the first re-pointed.
TEST_CASE("The arrow move steps a selected keyframe's offset", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_notes.empty());

    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    const common::core::Chart stepped = currentChart(fixture.controller);
    REQUIRE(stepped.notes.size() == 1);
    REQUIRE(stepped.notes[0].keyframes.size() == 1);
    CHECK(stepped.notes[0].keyframes[0].offset == common::core::Fraction{5});
    // Only the offset moved: the point keeps its fret, and the note keeps its slot and its ring.
    CHECK(stepped.notes[0].keyframes[0].fret == 9);
    CHECK(stepped.notes[0].position == original.notes[0].position);
    CHECK(stepped.notes[0].sustain == original.notes[0].sustain);
    // The selection followed the point to its new key rather than lingering on the old offset.
    CHECK(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);

    // The point's offset as the chart now holds it, asked through one guarded read so every
    // assertion below is provably about a record that exists.
    const auto stepped_offset = [&fixture] {
        const common::core::Chart chart = currentChart(fixture.controller);
        REQUIRE(chart.notes.size() == 1);
        REQUIRE(chart.notes[0].keyframes.size() == 1);
        return chart.notes[0].keyframes[0].offset;
    };

    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(stepped_offset() == common::core::Fraction{6});

    // The string step reaches notes alone — a keyframe has no string of its own — so Alt+Up over
    // one is inert rather than refused, and leaves the point where the two right steps put it.
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Up);
    CHECK(stepped_offset() == common::core::Fraction{6});

    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(currentChart(fixture.controller) == original);

    // The four presses above are ONE gesture and one entry, and they replayed back to the offset
    // they started at, so that entry is gone with them (test_chart_move_gesture.cpp pins the burst
    // law itself). This press therefore opens a fresh run, whose single undo restores the point.
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    REQUIRE(stepped_offset() == common::core::Fraction{5});
    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// A mixed selection is one press and one entry: the note moves its slot and the point it carries
// rides along at an unchanged offset, since an offset is relative to the onset it hangs from.
TEST_CASE("The arrow move carries a selected note's own keyframe along", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    click(fixture.controller, g_onset_x, g_string_3_y, ChartPointerModifiers{.ctrl = true});
    REQUIRE(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{0});
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);

    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    const common::core::Chart moved = currentChart(fixture.controller);
    REQUIRE(moved.notes.size() == 1);
    CHECK(
        moved.notes[0].position ==
        common::core::GridPosition{.measure = 2, .beat = 2, .offset = {}});
    REQUIRE(moved.notes[0].keyframes.size() == 1);
    CHECK(moved.notes[0].keyframes[0].offset == original.notes[0].keyframes[0].offset);
    // Both keys followed, so the next press still has the whole selection to act on.
    CHECK(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{0});
    CHECK(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// A typed digit states the selected keyframe's fret exactly as it states a head's (W13's ruling):
// the flow, the pending window and the planner are the note flow's, and the SELECTION KIND is what
// says which stop the digit reached — no third channel, no second entry kind.
TEST_CASE("A typed digit retypes the selected keyframe's fret", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_notes.empty());

    // 7 cannot be widened under the fret cap, so the entry settles in this one keystroke.
    fixture.controller.onChartFretDigitTyped(7);
    const common::core::Chart retyped = currentChart(fixture.controller);
    REQUIRE(retyped.notes.size() == 1);
    REQUIRE(retyped.notes[0].keyframes.size() == 1);
    CHECK(retyped.notes[0].keyframes[0].fret == 7);
    // The head the point rides keeps its own fret: a digit edits exactly what the selection named.
    CHECK(retyped.notes[0].fret == 5);
    CHECK_FALSE(publishedState(fixture.view).chart_edit.pending_fret.has_value());

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);

    // And the routing does not over-reach: the same digit over a selected NOTE still retypes it.
    click(fixture.controller, g_onset_x, g_string_3_y);
    fixture.controller.onChartFretDigitTyped(9);
    const common::core::Chart head = currentChart(fixture.controller);
    REQUIRE(head.notes.size() == 1);
    REQUIRE(head.notes[0].keyframes.size() == 1);
    CHECK(head.notes[0].fret == 9);
    // The point is where the undo above left it, so the head arrives at an equal-fret hold — the
    // legal encoding, and proof the digit reached only what the selection named.
    CHECK(head.notes[0].keyframes[0].fret == 9);
}

// The ±1 fret shift reaches a selected keyframe as the same delta, off the same anchor: the point
// is a stop the selection addressed, so the verb moves it exactly as it moves a head's.
TEST_CASE("The fret shift moves a selected keyframe by one", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartFretShiftRequested(1);
    const common::core::Chart raised = currentChart(fixture.controller);
    REQUIRE(raised.notes.size() == 1);
    REQUIRE(raised.notes[0].keyframes.size() == 1);
    CHECK(raised.notes[0].keyframes[0].fret == 10);
    // The head is not in the selection, so the shift does not reach it.
    CHECK(raised.notes[0].fret == 5);

    fixture.controller.onChartFretShiftRequested(-1);
    CHECK(currentChart(fixture.controller) == original);
}

// The create verb (W13, re-ruled 2026-09-09). Insert on a tail plants a REAL keyframe at the fret
// the path holds there — no ghost, no window — selected, with the caret still on its slot. Where
// that fret makes a HOLD BOUNDARY, as it does on a travel leg, the point already says something and
// simply stays.
TEST_CASE("Insert plants a keyframe on a tail, selected, with the caret on it", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);
    const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

    // Two beats into the ring, on the leg travelling from the head's 5 toward the junction's 9.
    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.caret.has_value());

    fixture.controller.onNeutralInsertRequested();
    const common::core::Chart stated = currentChart(fixture.controller);
    // One note still: the tail took a POINT, not the fret-0 note the neutral create places on an
    // empty slot, and the point states the fret the path last stated — the head's own 5.
    REQUIRE(stated.notes.size() == 1);
    REQUIRE(stated.notes[0].keyframes.size() == 2);
    CHECK(stated.notes[0].keyframes[0].offset == common::core::Fraction{2});
    CHECK(stated.notes[0].keyframes[0].fret == 5);
    CHECK(stated.notes[0].keyframes[1].fret == 9);
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before + 1);

    // Selected, with the caret where Insert was pressed — the point's own slot.
    {
        const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
        CHECK(
            edit.selected_keyframes == (std::vector<ChartKeyframeRef>{
                                           ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}
                                       }));
        CHECK(edit.selected_notes.empty());
        REQUIRE(edit.caret.has_value());
        if (edit.caret.has_value())
        {
            CHECK_THAT(edit.caret->seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));
            CHECK(edit.caret->string == 3);
        }
    }

    // A hold boundary says something, so leaving the point keeps it — and one undo takes it out.
    click(fixture.controller, g_onset_x, g_string_3_y);
    CHECK(currentChart(fixture.controller) == stated);
    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// THE COMMIT LAW, asked at the resting point: out where the path HOLDS, the planted point's fret is
// the one already in force, so it says nothing — it stands while selected, for the digits or the
// technique keys that might give it a meaning, and dissolves the moment the selection leaves it,
// its entry retired rather than left as a dead Ctrl+Z.
TEST_CASE(
    "A planted point that says nothing dissolves when the selection leaves it", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);
    const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onNeutralInsertRequested();
    // Planted for real: the chart holds it, selected, one entry.
    const common::core::Chart planted = currentChart(fixture.controller);
    REQUIRE(planted.notes.size() == 1);
    REQUIRE(planted.notes[0].keyframes.size() == 2);
    CHECK(planted.notes[0].keyframes[1].offset == common::core::Fraction{6});
    CHECK(planted.notes[0].keyframes[1].fret == 9);
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before + 1);
    CHECK(
        publishedState(fixture.view).chart_edit.selected_keyframes ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 1}}));

    // Leaving it is the judgement: nothing said, so it goes, and the run leaves no entry behind.
    click(fixture.controller, g_onset_x, g_string_3_y);
    CHECK(currentChart(fixture.controller) == original);
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before);
}

// A planted point given a meaning stays: retyped off the fret the path held, it says something,
// and leaving it keeps it.
TEST_CASE("A planted point kept once it says something", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onNeutralInsertRequested();
    // 7 cannot be widened under the fret cap, so the retype settles in this one keystroke.
    fixture.controller.onChartFretDigitTyped(7);
    const common::core::Chart stated = currentChart(fixture.controller);
    REQUIRE(stated.notes.size() == 1);
    REQUIRE(stated.notes[0].keyframes.size() == 2);
    CHECK(stated.notes[0].keyframes[1].offset == common::core::Fraction{6});
    CHECK(stated.notes[0].keyframes[1].fret == 7);

    click(fixture.controller, g_onset_x, g_string_3_y);
    CHECK(currentChart(fixture.controller) == stated);

    // The plant and the retype are two entries; two undos restore the tail.
    fixture.controller.onUndoRequested();
    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// Every tail is an authoring surface for points, a plain note's included: Insert there plants a
// point at the note's own fret rather than chopping the ring with a new note, and the point
// dissolves like any other that says nothing.
TEST_CASE("Insert on a plain note's tail plants a point, not a note", "[core][chart]")
{
    common::core::Chart plain = makeGlideChart();
    plain.notes[0].keyframes.clear();
    KeyframeFixture fixture{std::move(plain)};
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onNeutralInsertRequested();
    const common::core::Chart planted = currentChart(fixture.controller);
    REQUIRE(planted.notes.size() == 1);
    REQUIRE(planted.notes[0].keyframes.size() == 1);
    CHECK(planted.notes[0].keyframes[0].offset == common::core::Fraction{2});
    CHECK(planted.notes[0].keyframes[0].fret == 5);
    // The ring is untouched: nothing was chopped.
    CHECK(planted.notes[0].sustain == original.notes[0].sustain);

    click(fixture.controller, g_onset_x, g_string_3_y);
    CHECK(currentChart(fixture.controller) == original);
}

// A digit at a caret a ring covers states a POINT on the tail, not a note that would chop it: the
// typed fret rides the same pending entry a typed note does, and the commit law is asked before it
// settles — a fret the path already passes through authors nothing.
TEST_CASE("A digit at a caret on a tail states a point", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    // Two beats in, the leg from 5 to 9 passes through 7: stating 7 there says nothing.
    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartFretDigitTyped(7);
    CHECK(currentChart(fixture.controller) == original);

    // 6 bends the leg, so it commits — as a point, selected, the ring intact.
    fixture.controller.onChartFretDigitTyped(6);
    const common::core::Chart stated = currentChart(fixture.controller);
    REQUIRE(stated.notes.size() == 1);
    REQUIRE(stated.notes[0].keyframes.size() == 2);
    CHECK(stated.notes[0].keyframes[0].offset == common::core::Fraction{2});
    CHECK(stated.notes[0].keyframes[0].fret == 6);
    CHECK(stated.notes[0].sustain == original.notes[0].sustain);
    CHECK(
        publishedState(fixture.view).chart_edit.selected_keyframes ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}}));

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// The typed point's box draws at the SLOT — the box a typed head wears, never a ghost head — and
// waits for a second digit exactly as a typed note does.
TEST_CASE("A typed point on a tail draws the pending box at its slot", "[core][chart]")
{
    PendingKeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartFretDigitTyped(1);
    CHECK(currentChart(fixture.controller) == original);
    const std::optional<ChartPendingFretViewState>& pending =
        publishedState(fixture.view).chart_edit.pending_fret;
    REQUIRE(pending.has_value());
    if (pending.has_value())
    {
        CHECK(pending->text == "1");
        CHECK(pending->valid);
        const auto* const slot = std::get_if<ChartSlotViewState>(&pending->at);
        REQUIRE(slot != nullptr);
        if (slot != nullptr)
        {
            CHECK_THAT(slot->seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));
            CHECK(slot->string == 3);
        }
    }
    CHECK_FALSE(publishedState(fixture.view).chart_edit.insert_ghost.has_value());

    fixture.controller.onChartFretDigitTyped(2);
    const common::core::Chart stated = currentChart(fixture.controller);
    REQUIRE(stated.notes.size() == 1);
    REQUIRE(stated.notes[0].keyframes.size() == 2);
    CHECK(stated.notes[0].keyframes[0].fret == 12);
    CHECK_FALSE(publishedState(fixture.view).chart_edit.pending_fret.has_value());
}

// A digit at a selected keyframe opens the same pending entry a head's does — the box draws on the
// linked head, the window waits for a second digit, and the two combine — because a keyframe's
// stop is a fret typed through the one flow. Under the deferring scheduler the value stays pending
// exactly until the second digit settles it.
TEST_CASE("A digit at a keyframe draws the pending box and waits for a second", "[core][chart]")
{
    PendingKeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);

    // 1 can widen under the fret cap, so the entry stays pending — and its box names the point,
    // located as the lane draws it, with no note among its targets.
    fixture.controller.onChartFretDigitTyped(1);
    CHECK(currentChart(fixture.controller) == original);
    const std::optional<ChartPendingFretViewState>& pending =
        publishedState(fixture.view).chart_edit.pending_fret;
    REQUIRE(pending.has_value());
    if (pending.has_value())
    {
        CHECK(pending->text == "1");
        CHECK(pending->valid);
        CHECK(
            pending->at ==
            decltype(pending->at){ChartPendingFretTargets{
                .notes = {},
                .keyframes = {ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}},
                .channel = common::core::ChartStopChannel::Sounding,
            }});
    }

    // The second digit combines and settles: fret 12 at the point, the head it rides untouched.
    fixture.controller.onChartFretDigitTyped(2);
    const common::core::Chart retyped = currentChart(fixture.controller);
    REQUIRE(retyped.notes.size() == 1);
    REQUIRE(retyped.notes[0].keyframes.size() == 1);
    CHECK(retyped.notes[0].keyframes[0].fret == 12);
    CHECK(retyped.notes[0].fret == 5);
    CHECK_FALSE(publishedState(fixture.view).chart_edit.pending_fret.has_value());
}

} // namespace rock_hero::editor::core
