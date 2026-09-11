#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <optional>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
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
// An empty lane beside the note's: a click here takes the caret off the note entirely, which is
// what leaves the note's focus and lets the commit law judge its points.
constexpr float g_string_2_y{180.0f};

// The Alt modifiers both mouse entry verbs are held under.
constexpr ChartPointerModifiers g_alt{.ctrl = false, .shift = false, .alt = true};

// The fixture's ring ends at 6.0s, one measure past the junction's linked head.
constexpr float g_ring_end_x{120.0f};

// The glide chart with a RELEASE at the ring's end: the same eight-beat gesture, plus the fret the
// hand falls away toward exactly where the ring stops. The one figure where a strike at the exact
// end lands its head on a slot a keyframe already occupies.
[[nodiscard]] common::core::Chart makeReleasedGlideChart()
{
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].keyframes.push_back(
        common::core::Keyframe{.offset = common::core::Fraction{8}, .fret = 12});
    return chart;
}

// THE SHIFT DEFAULT's modifiers: `Shift` names the fret in force, `Alt` is the gesture it rides.
constexpr ChartPointerModifiers g_shift_alt{.ctrl = false, .shift = true, .alt = true};

// The probe column every Shift scenario places at — 10.0s, measure 6 beat 1, later than every note
// the handover chart below builds — and the lanes those notes stand on.
constexpr float g_probe_x{200.0f};
constexpr float g_string_1_y{220.0f};
constexpr float g_string_4_y{100.0f};
constexpr float g_string_5_y{60.0f};
constexpr float g_string_6_y{20.0f};

// One string per KIND of onset, every note in the same column, so a probe far to their right asks
// each lane the same question and gets a different kind of answer. String 6 is left empty: nothing
// has been played there, so the hand has been left nowhere on it.
[[nodiscard]] common::core::Chart makeHandoverChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    constexpr common::core::GridPosition onset{.measure = 2, .beat = 1};

    common::core::ChartNote glide = makeTestNote(onset, 2, 5, common::core::Fraction{2});
    glide.keyframes = {common::core::Keyframe{.offset = common::core::Fraction{1}, .fret = 9}};

    common::core::ChartNote fall = makeTestNote(onset, 3, 7, common::core::Fraction{2});
    common::core::setSlideOut(fall, 3);

    common::core::ChartNote held_tap = makeTestNote(onset, 4, 12);
    held_tap.attack = common::core::NoteAttack::Tap;
    held_tap.held = 5;

    common::core::ChartNote bare_tap = makeTestNote(onset, 5, 12);
    bare_tap.attack = common::core::NoteAttack::Tap;

    chart.notes = {
        makeTestNote(onset, 1, 7),
        std::move(glide),
        std::move(fall),
        std::move(held_tap),
        std::move(bare_tap),
    };
    return chart;
}

// The STRIKE's mouse form: two gestures at one point, the second pair reporting a consecutive-click
// count of two the way JUCE delivers a double click. The shared doubleClick() helper carries no
// modifiers, and this gesture is nothing without Alt.
void altDoubleClick(EditorController& controller, const float x, const float y)
{
    click(controller, x, y, g_alt);
    controller.onChartPointerDown(pointerEvent(x, y, g_alt, 2));
    controller.onChartPointerUp(pointerEvent(x, y, g_alt, 2));
}

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

// A keyframe already states the path at its slot, so the STATE verb has nothing to join there:
// Alt+Insert with the caret on a point places neither a second point at that offset nor anything
// else.
TEST_CASE("The point verb on a keyframe's slot places nothing", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);

    fixture.controller.onChartPointInsertRequested();
    CHECK(currentChart(fixture.controller) == original);
    CHECK_FALSE(publishedState(fixture.view).chart_edit.insert_ghost.has_value());
}

// The STRIKE reaches the same slot, because a point is no onset: a keyframe standing where the
// press lands is exactly the re-strike case, and the split hands that keyframe over — it becomes
// the new head, which therefore opens on the fret the keyframe stated rather than on fret 0.
TEST_CASE(
    "The strike verb on a keyframe's slot splits there, the keyframe as head", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);

    fixture.controller.onNeutralInsertRequested();
    const common::core::Chart struck = currentChart(fixture.controller);
    REQUIRE(struck.notes.size() == 2);
    // The origin's ring runs to the new head, because a re-strike is what stops a ring; only the
    // arrival it travels to retreats, by the clearance margin (a quarter beat in 4/4), since a
    // fret-stating keyframe may not sit on a later onset of its own string.
    CHECK(struck.notes[0].sustain == common::core::Fraction{4});
    REQUIRE(struck.notes[0].keyframes.size() == 1);
    CHECK(struck.notes[0].keyframes[0].offset == common::core::Fraction{15, 4});
    CHECK(struck.notes[0].keyframes[0].fret == 9);
    CHECK(struck.notes[1].position == common::core::GridPosition{.measure = 3, .beat = 1});
    CHECK(struck.notes[1].string == 3);
    CHECK(struck.notes[1].fret == 9);
    // The remainder of the glide's ring, carried on rather than thrown away.
    CHECK(struck.notes[1].sustain == common::core::Fraction{4});
    CHECK(struck.notes[1].attack == common::core::NoteAttack::Pick);

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
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

// Deleting a point must leave an empty armed slot, so an Alt+digit can recreate it without moving.
TEST_CASE("Typing recreates a deleted tail keyframe at the caret", "[core][chart]")
{
    KeyframeFixture fixture;
    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onChartPointInsertRequested();
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);

    fixture.controller.onSelectionDeleteRequested();
    const common::core::Chart deleted = currentChart(fixture.controller);
    CHECK_FALSE(publishedState(fixture.view).selection_present);
    const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
    REQUIRE(edit.caret.has_value());
    if (edit.caret.has_value())
    {
        CHECK_THAT(edit.caret->seconds, Catch::Matchers::WithinAbs(5.0, 1e-9));
        CHECK(edit.caret->string == 3);
    }

    fixture.controller.onChartPathDigitTyped(7);
    const common::core::Chart recreated = currentChart(fixture.controller);
    REQUIRE(recreated.notes.size() == 1);
    REQUIRE(recreated.notes[0].keyframes.size() == 2);
    CHECK(recreated.notes[0].keyframes[1].offset == common::core::Fraction{6});
    CHECK(recreated.notes[0].keyframes[1].fret == 7);
    CHECK(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == deleted);
    fixture.controller.onRedoRequested();
    CHECK(currentChart(fixture.controller) == recreated);
}

// The UNDO twin of the same law. Undo takes the object back but not the key that named it — the
// dissolve law's linger, which serves a live verb window the transition has already ended — and a
// digit routes by the retype OPERAND, so a key resolving to nothing swallowed the keystroke into a
// retype that found nothing to retype. The caret never moved, so nothing else could clear it.
TEST_CASE("Typing recreates a tail keyframe at the caret after undoing it", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onChartPathDigitTyped(7);
    REQUIRE(currentChart(fixture.controller).notes[0].keyframes.size() == 2);

    fixture.controller.onUndoRequested();
    REQUIRE(currentChart(fixture.controller) == original);
    // Nothing stands under the caret any more, so nothing is selected — the state the delete verb
    // leaves, reached here by the transition's own repair.
    CHECK_FALSE(publishedState(fixture.view).selection_present);

    fixture.controller.onChartPathDigitTyped(9);
    const common::core::Chart recreated = currentChart(fixture.controller);
    REQUIRE(recreated.notes.size() == 1);
    REQUIRE(recreated.notes[0].keyframes.size() == 2);
    CHECK(recreated.notes[0].keyframes[1].offset == common::core::Fraction{6});
    CHECK(recreated.notes[0].keyframes[1].fret == 9);
}

// The fret-less half of the same press, and the point verb's own: Alt+Insert on the emptied slot
// plants the silent point exactly as it would have before the undone entry was ever typed.
TEST_CASE("The point verb plants at the caret after undoing a typed point", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onChartPathDigitTyped(7);
    fixture.controller.onUndoRequested();
    REQUIRE(currentChart(fixture.controller) == original);

    fixture.controller.onChartPointInsertRequested();
    const common::core::Chart planted = currentChart(fixture.controller);
    REQUIRE(planted.notes.size() == 1);
    REQUIRE(planted.notes[0].keyframes.size() == 2);
    CHECK(planted.notes[0].keyframes[1].offset == common::core::Fraction{6});
    // The silent point restates the fret the path already holds there — authoring state, so the
    // history never recorded it.
    CHECK(planted.notes[0].keyframes[1].fret == 9);
    CHECK(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);
}

// The HEAD twin: the same linger, the same swallowed digit, on the note the insert took away.
TEST_CASE("Typing recreates a head at the caret after undoing it", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    // An empty lane beside the ringing note's, where a digit has nothing to join and authors a
    // head.
    click(fixture.controller, g_holding_tail_x, g_string_2_y);
    fixture.controller.onChartFretDigitTyped(5);
    REQUIRE(currentChart(fixture.controller).notes.size() == 2);

    fixture.controller.onUndoRequested();
    REQUIRE(currentChart(fixture.controller) == original);
    CHECK_FALSE(publishedState(fixture.view).selection_present);

    fixture.controller.onChartFretDigitTyped(3);
    const common::core::Chart retyped = currentChart(fixture.controller);
    REQUIRE(retyped.notes.size() == 2);
    const common::core::ChartNote& head = retyped.notes[1];
    CHECK(head.string == 2);
    CHECK(head.fret == 3);

    // And the fret-less press reaches the emptied slot the same way.
    fixture.controller.onUndoRequested();
    REQUIRE(currentChart(fixture.controller) == original);
    fixture.controller.onNeutralInsertRequested();
    const common::core::Chart inserted = currentChart(fixture.controller);
    REQUIRE(inserted.notes.size() == 2);
    CHECK(inserted.notes[1].string == 2);
    CHECK(inserted.notes[1].fret == 0);
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

// The STATE verb, fretless (W13, re-ruled 2026-09-09). Alt+Insert on a tail plants a REAL keyframe
// at the fret the path holds there — no ghost, no window — selected, with the caret still on its
// slot. Where that fret makes a HOLD BOUNDARY, as it does on a travel leg, the point already says
// something and simply stays.
TEST_CASE(
    "The point verb plants a keyframe on a tail, selected, with the caret on it", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);
    const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

    // Two beats into the ring, on the leg travelling from the head's 5 toward the junction's 9.
    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.caret.has_value());

    fixture.controller.onChartPointInsertRequested();
    const common::core::Chart stated = currentChart(fixture.controller);
    // One note still: the tail took a POINT, not the fret-0 note the same verb places on an
    // empty slot, and the point states the fret the path last stated — the head's own 5.
    REQUIRE(stated.notes.size() == 1);
    REQUIRE(stated.notes[0].keyframes.size() == 2);
    CHECK(stated.notes[0].keyframes[0].offset == common::core::Fraction{2});
    CHECK(stated.notes[0].keyframes[0].fret == 5);
    CHECK(stated.notes[0].keyframes[1].fret == 9);
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before + 1);

    // Selected, with the caret where the verb was pressed — the point's own slot.
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

// A planted point that says nothing yet is AUTHORING STATE: it stands, selected, for the digits or
// the technique keys that give it a meaning, with no undo entry — the history records written
// states, and this one writes as nothing — and it dissolves the moment its note leaves focus,
// again with no entry, so there is never anything for Ctrl+Z to bring back.
TEST_CASE(
    "A planted point that says nothing makes no entry and dissolves with focus", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);
    const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onChartPointInsertRequested();
    const common::core::Chart planted = currentChart(fixture.controller);
    REQUIRE(planted.notes.size() == 1);
    REQUIRE(planted.notes[0].keyframes.size() == 2);
    CHECK(planted.notes[0].keyframes[1].offset == common::core::Fraction{6});
    CHECK(planted.notes[0].keyframes[1].fret == 9);
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before);
    CHECK(
        publishedState(fixture.view).chart_edit.selected_keyframes ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 1}}));

    // Stepping onto the head keeps the note in focus, so the point stands.
    click(fixture.controller, g_onset_x, g_string_3_y);
    CHECK(currentChart(fixture.controller) == planted);

    // Leaving the note dissolves it, and nothing was ever pushed.
    click(fixture.controller, g_onset_x, g_string_2_y);
    CHECK(currentChart(fixture.controller) == original);
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before);
}

// The dissolve is not the history's to defer: an edit on the note in between — a mute on its head
// — takes its own entry and leaves the silent point standing, and the point still goes when the
// note leaves focus, exactly as it would have with nothing in between. Undo then walks the mute
// back to a tail that never had the point.
TEST_CASE("A silent point outlives an edit on its note and still dissolves", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);
    const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onChartPointInsertRequested();
    click(fixture.controller, g_onset_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
    const common::core::Chart muted = currentChart(fixture.controller);
    REQUIRE(muted.notes.size() == 1);
    CHECK(muted.notes[0].palm_mute);
    CHECK(muted.notes[0].keyframes.size() == 2);
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before + 1);

    click(fixture.controller, g_onset_x, g_string_2_y);
    const common::core::Chart left = currentChart(fixture.controller);
    REQUIRE(left.notes.size() == 1);
    CHECK(left.notes[0].palm_mute);
    CHECK(left.notes[0].keyframes.size() == 1);
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before + 1);

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// Undo and redo replay written states, so a silent point still standing goes BEFORE either
// replays: here the plant on the holding tail (no entry) collapses as the undo takes out the hold
// boundary planted on the travel leg (one entry), and the tail is exactly what it was.
TEST_CASE("Undo collapses a silent point before it replays", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartPointInsertRequested();
    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onChartPointInsertRequested();
    const common::core::Chart planted = currentChart(fixture.controller);
    REQUIRE(planted.notes.size() == 1);
    CHECK(planted.notes[0].keyframes.size() == 3);

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
    fixture.controller.onRedoRequested();
    const common::core::Chart redone = currentChart(fixture.controller);
    REQUIRE(redone.notes.size() == 1);
    CHECK(redone.notes[0].keyframes.size() == 2);
}

// THE SLIDE WORKFLOW the law exists for: Insert where the slide starts, walk the caret along the
// same tail to where it lands, type the landing fret. The start says nothing until the landing
// exists, and the note stays in focus meanwhile, so it is simply there when the landing makes it a
// hold boundary that says something — and the ONE entry the landing pushes carries both points,
// because it diffs from the written state before it, which never had the start.
TEST_CASE("A slide is authored as its start, then its landing, on one tail", "[core][chart]")
{
    common::core::Chart plain = makeGlideChart();
    plain.notes[0].keyframes.clear();
    KeyframeFixture fixture{std::move(plain)};
    const common::core::Chart original = currentChart(fixture.controller);
    const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

    // The start: two beats in, at the note's own fret, so it says nothing yet.
    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartPointInsertRequested();
    // The landing: six beats in on the same tail, typed in the same STATE verb — a bare digit
    // there would strike a new onset through the ring instead of landing the slide on it.
    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onChartPathDigitTyped(9);
    const common::core::Chart authored = currentChart(fixture.controller);
    REQUIRE(authored.notes.size() == 1);
    REQUIRE(authored.notes[0].keyframes.size() == 2);
    CHECK(authored.notes[0].keyframes[0].offset == common::core::Fraction{2});
    CHECK(authored.notes[0].keyframes[0].fret == 5);
    CHECK(authored.notes[0].keyframes[1].offset == common::core::Fraction{6});
    CHECK(authored.notes[0].keyframes[1].fret == 9);

    // Both say something now — the start is where the hold ends and travel begins — so leaving
    // the note keeps both, and the slide is one entry.
    click(fixture.controller, g_onset_x, g_string_2_y);
    CHECK(currentChart(fixture.controller) == authored);
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before + 1);

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// A planted point given a meaning stays: retyped off the fret the path held, it says something,
// and leaving it keeps it.
TEST_CASE("A planted point kept once it says something", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onChartPointInsertRequested();
    // The plant leaves the point selected, so a BARE digit retypes it exactly as it retypes any
    // selection — the verbs part company only where the marker is armed on nothing.
    // 7 cannot be widened under the fret cap, so the retype settles in this one keystroke.
    fixture.controller.onChartFretDigitTyped(7);
    const common::core::Chart stated = currentChart(fixture.controller);
    REQUIRE(stated.notes.size() == 1);
    REQUIRE(stated.notes[0].keyframes.size() == 2);
    CHECK(stated.notes[0].keyframes[1].offset == common::core::Fraction{6});
    CHECK(stated.notes[0].keyframes[1].fret == 7);

    click(fixture.controller, g_onset_x, g_string_3_y);
    CHECK(currentChart(fixture.controller) == stated);

    // The plant wrote as nothing, so the retype is the one entry: one undo restores the tail.
    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// Every tail is an authoring surface for points, a plain note's included: the STATE verb there
// plants a point at the note's own fret rather than chopping the ring with a new note — a silent
// point, authoring state like any other that says nothing.
TEST_CASE("The point verb on a plain note's tail plants a point, not a note", "[core][chart]")
{
    common::core::Chart plain = makeGlideChart();
    plain.notes[0].keyframes.clear();
    KeyframeFixture fixture{std::move(plain)};
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartPointInsertRequested();
    const common::core::Chart planted = currentChart(fixture.controller);
    REQUIRE(planted.notes.size() == 1);
    REQUIRE(planted.notes[0].keyframes.size() == 1);
    CHECK(planted.notes[0].keyframes[0].offset == common::core::Fraction{2});
    CHECK(planted.notes[0].keyframes[0].fret == 5);
    // The ring is untouched: nothing was chopped.
    CHECK(planted.notes[0].sustain == original.notes[0].sustain);

    click(fixture.controller, g_onset_x, g_string_2_y);
    CHECK(currentChart(fixture.controller) == original);
}

// An Alt+digit at a caret a ring covers states a POINT on the tail, not a note that would chop it:
// the typed fret rides the same pending entry a typed note does and lands planted and selected,
// exactly as the fretless plant's point does — so a typed fret the path already passes through is
// a point that says nothing, authoring state that pushes no entry.
TEST_CASE("An Alt digit at a caret on a tail states a point", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);
    const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

    // Two beats in, the leg from 5 to 9 passes through 7: stating 7 there says nothing yet.
    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartPathDigitTyped(7);
    const common::core::Chart silent = currentChart(fixture.controller);
    REQUIRE(silent.notes.size() == 1);
    REQUIRE(silent.notes[0].keyframes.size() == 2);
    CHECK(silent.notes[0].keyframes[0].offset == common::core::Fraction{2});
    CHECK(silent.notes[0].keyframes[0].fret == 7);
    CHECK(
        publishedState(fixture.view).chart_edit.selected_keyframes ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}}));
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before);

    // 6 bends the leg, so it says something — as a point, selected, the ring intact — and the
    // retype is the one entry, carrying the point's creation. The point is SELECTED by now, so
    // this digit retypes it whichever verb types it.
    fixture.controller.onChartPathDigitTyped(6);
    const common::core::Chart stated = currentChart(fixture.controller);
    REQUIRE(stated.notes.size() == 1);
    REQUIRE(stated.notes[0].keyframes.size() == 2);
    CHECK(stated.notes[0].keyframes[0].offset == common::core::Fraction{2});
    CHECK(stated.notes[0].keyframes[0].fret == 6);
    CHECK(stated.notes[0].sustain == original.notes[0].sustain);
    CHECK(
        publishedState(fixture.view).chart_edit.selected_keyframes ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}}));
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before + 1);

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// The typed point and its changed path draw immediately beneath the pending box, then wait for a
// second Alt digit exactly as a typed note does: the two combine into one value inside the entry
// window, so the STATE verb widens like every other. The stored chart remains untouched until
// settlement.
TEST_CASE("A typed point on a tail previews the keyframe immediately", "[core][chart]")
{
    PendingKeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartPathDigitTyped(1);
    CHECK(currentChart(fixture.controller) == original);
    const std::shared_ptr<const common::core::ChartViewState>& preview =
        publishedState(fixture.view).tab;
    REQUIRE(preview != nullptr);
    REQUIRE(preview->notes.size() == 1);
    REQUIRE(preview->notes[0].slides.size() == 2);
    CHECK(preview->notes[0].slides[0].fret == 1);
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

    fixture.controller.onChartPathDigitTyped(2);
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

// The STRIKE at a caret a ring covers: the fretless press SPLITS the ring there rather than
// joining the path the STATE verb joins. Cut mid-glide, the origin holds the stated fret it set
// out from and the remainder travels on to the arrival — the whole gesture survives, redistributed
// across two notes.
TEST_CASE("The strike verb on a tail splits the ring at the caret", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    // Two beats into the eight-beat ring, on the leg travelling toward the junction.
    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onNeutralInsertRequested();

    const common::core::Chart struck = currentChart(fixture.controller);
    REQUIRE(struck.notes.size() == 2);
    CHECK(struck.notes[0].fret == 5);
    CHECK(struck.notes[0].sustain == common::core::Fraction{2});
    CHECK(struck.notes[0].keyframes.empty());
    CHECK(struck.notes[1].position == common::core::GridPosition{.measure = 2, .beat = 3});
    CHECK(struck.notes[1].string == 3);
    // The fretless press states no fret, so the new head opens on the running one — and the
    // arrival it was travelling to rebases onto it, two beats later than it stood.
    CHECK(struck.notes[1].fret == 5);
    CHECK(struck.notes[1].sustain == common::core::Fraction{6});
    REQUIRE(struck.notes[1].keyframes.size() == 1);
    CHECK(struck.notes[1].keyframes[0].offset == common::core::Fraction{2});
    CHECK(struck.notes[1].keyframes[0].fret == 9);
    // The struck note is the selection, with the caret armed on it for an immediate retype.
    CHECK(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{1});

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// With no path to join, the STATE verb places the STRIKE's head: Alt+Insert on an empty slot is
// the fret-0 note the bare press would have made there.
TEST_CASE("The point verb on an empty slot places the fret-0 head", "[core][chart]")
{
    KeyframeFixture fixture;

    // The empty string-2 lane at the onset's own column.
    click(fixture.controller, g_onset_x, g_string_2_y);
    fixture.controller.onChartPointInsertRequested();

    const common::core::Chart placed = currentChart(fixture.controller);
    REQUIRE(placed.notes.size() == 2);
    // Sorted by (position, string), so the new string-2 note stands before the glide on string 3.
    CHECK(placed.notes[0].string == 2);
    CHECK(placed.notes[0].fret == 0);
    CHECK(placed.notes[0].keyframes.empty());
}

// The fretless STATE press leaves an armed caret on the point it planted, which is the whole of
// how a slide's start is authored: the point says nothing yet, and the next digit — bare, because
// the plant SELECTED the point — gives it its fret as one entry carrying the plant with it.
TEST_CASE("The point verb arms the caret so the next digit frets the point", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);
    const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onChartPointInsertRequested();
    {
        const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
        CHECK(
            edit.selected_keyframes == (std::vector<ChartKeyframeRef>{
                                           ChartKeyframeRef{.note_index = 0, .keyframe_index = 1}
                                       }));
        REQUIRE(edit.caret.has_value());
        if (edit.caret.has_value())
        {
            CHECK_THAT(edit.caret->seconds, Catch::Matchers::WithinAbs(5.0, 1e-9));
            CHECK(edit.caret->string == 3);
        }
    }
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before);

    fixture.controller.onChartFretDigitTyped(7);
    const common::core::Chart fretted = currentChart(fixture.controller);
    REQUIRE(fretted.notes.size() == 1);
    REQUIRE(fretted.notes[0].keyframes.size() == 2);
    CHECK(fretted.notes[0].keyframes[1].offset == common::core::Fraction{6});
    CHECK(fretted.notes[0].keyframes[1].fret == 7);
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before + 1);

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// A digit of the OTHER verb never widens the live value: it addresses a different object, so the
// pending point settles as what it already states and the arriving digit opens a strike entry of
// its own at the same slot.
TEST_CASE("A bare digit after a live Alt entry settles the point and strikes", "[core][chart]")
{
    PendingKeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    // Two beats in on the travel leg, with a widenable 1 typed in the STATE verb.
    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartPathDigitTyped(1);
    REQUIRE(publishedState(fixture.view).chart_edit.pending_fret.has_value());
    CHECK(currentChart(fixture.controller) == original);

    // The bare 2 is a statement about a head, not about that point: the point lands at fret 1 and
    // a fresh entry opens for the strike, which the window's wake then settles.
    fixture.controller.onChartFretDigitTyped(2);
    static_cast<void>(fixture.pending.scheduler.runDelayed());

    const common::core::Chart struck = currentChart(fixture.controller);
    REQUIRE(struck.notes.size() == 2);
    // The settled point kept its typed 1, and the strike behind it SPLIT the ring at that very
    // point — so the point becomes the new head's slot, its own statement retreating a clearance
    // margin behind the head while the origin's ring still runs to it.
    REQUIRE(struck.notes[0].keyframes.size() == 1);
    CHECK(struck.notes[0].keyframes[0].fret == 1);
    CHECK(struck.notes[0].keyframes[0].offset == common::core::Fraction{7, 4});
    CHECK(struck.notes[0].sustain == common::core::Fraction{2});
    CHECK(struck.notes[1].position == common::core::GridPosition{.measure = 2, .beat = 3});
    CHECK(struck.notes[1].fret == 2);
    // The rest of the glide rides the new head: six beats left, with the arrival rebased onto it.
    CHECK(struck.notes[1].sustain == common::core::Fraction{6});
    REQUIRE(struck.notes[1].keyframes.size() == 1);
    CHECK(struck.notes[1].keyframes[0].offset == common::core::Fraction{2});
    CHECK(struck.notes[1].keyframes[0].fret == 9);
}

// Alt+click is the STATE verb's mouse form: one press on a slot a ring covers plants the silent
// point there and arms the caret on it, exactly as Alt+Insert does at the caret.
TEST_CASE("Alt+click on a tail plants the silent point", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);
    const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

    click(fixture.controller, g_holding_tail_x, g_string_3_y, g_alt);

    const common::core::Chart planted = currentChart(fixture.controller);
    REQUIRE(planted.notes.size() == 1);
    REQUIRE(planted.notes[0].keyframes.size() == 2);
    CHECK(planted.notes[0].keyframes[1].offset == common::core::Fraction{6});
    CHECK(planted.notes[0].keyframes[1].fret == 9);
    CHECK(planted.notes[0].sustain == original.notes[0].sustain);
    // Authoring state: selected, on an armed caret, and no entry ever held it.
    const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
    CHECK(
        edit.selected_keyframes ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 1}}));
    REQUIRE(edit.caret.has_value());
    if (edit.caret.has_value())
    {
        CHECK_THAT(edit.caret->seconds, Catch::Matchers::WithinAbs(5.0, 1e-9));
        CHECK(edit.caret->string == 3);
    }
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before);
}

// The ring's exact END is the one slot the two fretless path verbs do NOT state on: a silent point
// there would restate the running fret as the release, saying nothing any later gesture could give
// meaning to. So both place the verb's fret-0 head instead, standing adjacent to the ring that
// already stops there. Only Alt+DIGIT reads the end as a path stop, since a fret typed there
// really is the slide-out.
TEST_CASE("The fretless path verbs place a head at a ring's end", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_ring_end_x, g_string_3_y);
    fixture.controller.onChartPointInsertRequested();
    {
        const common::core::Chart placed = currentChart(fixture.controller);
        REQUIRE(placed.notes.size() == 2);
        // The glide is untouched — nothing was split, and it grew no release.
        CHECK(placed.notes[0].sustain == original.notes[0].sustain);
        CHECK(placed.notes[0].keyframes.size() == original.notes[0].keyframes.size());
        CHECK(placed.notes[1].position == common::core::GridPosition{.measure = 4, .beat = 1});
        CHECK(placed.notes[1].string == 3);
        CHECK(placed.notes[1].fret == 0);
    }

    fixture.controller.onUndoRequested();
    REQUIRE(currentChart(fixture.controller) == original);

    // Alt+click is the same verb through the pointer, and reads the end the same way.
    click(fixture.controller, g_ring_end_x, g_string_3_y, g_alt);
    const common::core::Chart clicked = currentChart(fixture.controller);
    REQUIRE(clicked.notes.size() == 2);
    CHECK(clicked.notes[0].keyframes.size() == original.notes[0].keyframes.size());
    CHECK(clicked.notes[1].position == common::core::GridPosition{.measure = 4, .beat = 1});
    CHECK(clicked.notes[1].fret == 0);
}

// THE SHIFT DEFAULT on the keyboard half: `Shift+Insert` is the bare Insert with one thing
// changed — the head it places takes the fret already in force on the string instead of the open
// one — read per the KIND of onset that left the hand there.
TEST_CASE("Shift+Insert places the head at the fret already in force", "[core][chart]")
{
    KeyframeFixture fixture{makeHandoverChart()};
    const std::size_t notes_before = currentChart(fixture.controller).notes.size();

    // Places at the probe column on one lane, reads the fret the head took, and undoes, so every
    // lane below is asked of the same chart.
    const auto placed_fret = [&fixture, notes_before](const float y) {
        click(fixture.controller, g_probe_x, y);
        fixture.controller.onNeutralInsertRepeatRequested();
        const common::core::Chart placed = currentChart(fixture.controller);
        REQUIRE(placed.notes.size() == notes_before + 1);
        // The probe column is later than every fixture note, so the new head sorts last.
        const int fret = placed.notes.back().fret;
        fixture.controller.onUndoRequested();
        return fret;
    };

    // A picked note hands forward the fret it sounded.
    CHECK(placed_fret(g_string_1_y) == 7);
    // A glide hands forward the fret it travelled TO, not the one it left.
    CHECK(placed_fret(g_string_2_y) == 9);
    // A slide-out hands forward the fret it FELL TOWARD: the fall is travel the hand really
    // takes, so it is where the hand was left standing.
    CHECK(placed_fret(g_string_3_y) == 3);
    // A tap's own fret is the other hand's landing, so what it hands forward is the stop it HOLDS.
    CHECK(placed_fret(g_string_4_y) == 5);
    // A tap holding nothing leaves the string open however high it lands.
    CHECK(placed_fret(g_string_5_y) == 0);
    // Nothing has been played on this lane at all, so the bare default stands.
    CHECK(placed_fret(g_string_6_y) == 0);
}

// STRICTLY INSIDE a ring the modifier says nothing: there is no fretless head to default, the
// split's new onset opening on the fret the path already holds — which IS the fret in force. So
// the two spellings must produce the same chart, character for character.
TEST_CASE("Shift+Insert inside a ring is the bare Insert exactly", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onNeutralInsertRepeatRequested();
    const common::core::Chart shifted = currentChart(fixture.controller);
    REQUIRE(shifted.notes.size() == 2);

    fixture.controller.onUndoRequested();
    REQUIRE(currentChart(fixture.controller) == original);

    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onNeutralInsertRequested();
    CHECK(currentChart(fixture.controller) == shifted);
}

// The ring's exact END is a head slot, not a path stop, so the modifier reaches it: the adjacent
// head takes the fret the ring left the hand on — the glide's arrival, never the open string the
// bare press would place there.
TEST_CASE("Shift+Insert at a ring's end takes the fret the ring left", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_ring_end_x, g_string_3_y);
    fixture.controller.onNeutralInsertRepeatRequested();

    {
        const common::core::Chart placed = currentChart(fixture.controller);
        REQUIRE(placed.notes.size() == 2);
        // The glide is untouched: the ring already stops here, so nothing was split.
        CHECK(placed.notes[0].sustain == original.notes[0].sustain);
        CHECK(placed.notes[0].keyframes.size() == original.notes[0].keyframes.size());
        CHECK(placed.notes[1].position == common::core::GridPosition{.measure = 4, .beat = 1});
        CHECK(placed.notes[1].string == 3);
        CHECK(placed.notes[1].fret == 9);
    }

    fixture.controller.onUndoRequested();
    REQUIRE(currentChart(fixture.controller) == original);

    // Shift+Alt+click is the same rule through the pointer, and reads the end the same way.
    click(fixture.controller, g_ring_end_x, g_string_3_y, g_shift_alt);
    const common::core::Chart clicked = currentChart(fixture.controller);
    REQUIRE(clicked.notes.size() == 2);
    CHECK(clicked.notes[0].keyframes.size() == original.notes[0].keyframes.size());
    CHECK(clicked.notes[1].position == common::core::GridPosition{.measure = 4, .beat = 1});
    CHECK(clicked.notes[1].fret == 9);
}

// The pointer half, and the same one rule: `Shift+Alt`+click is `Alt`+click with the fretless
// default changed and nothing else. So it reaches exactly where a head is placed — an empty slot
// and the ring's exact end — and leaves the point verb alone inside a ring.
TEST_CASE("Shift+Alt+click places its head at the fret in force", "[core][chart]")
{
    KeyframeFixture fixture{makeHandoverChart()};
    const std::size_t notes_before = currentChart(fixture.controller).notes.size();

    // An empty slot on the picked note's lane: the head lands on the fret it sounded.
    click(fixture.controller, g_probe_x, g_string_1_y, g_shift_alt);
    {
        const common::core::Chart placed = currentChart(fixture.controller);
        REQUIRE(placed.notes.size() == notes_before + 1);
        CHECK(placed.notes.back().string == 1);
        CHECK(placed.notes.back().fret == 7);
    }
    fixture.controller.onUndoRequested();

    // And the lane nothing has been played on still takes the open string.
    click(fixture.controller, g_probe_x, g_string_6_y, g_shift_alt);
    const common::core::Chart empty_lane = currentChart(fixture.controller);
    REQUIRE(empty_lane.notes.size() == notes_before + 1);
    CHECK(empty_lane.notes.back().string == 6);
    CHECK(empty_lane.notes.back().fret == 0);
}

// Inside a ring the pointer form is the STATE verb whatever `Shift` says: the silent point on the
// path, armed and holding no undo entry, exactly as the bare `Alt`+click plants it. There is no
// fretless head there to default, so the modifier has nothing to change.
TEST_CASE("Shift+Alt+click still plants the silent point inside a ring", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);
    const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

    click(fixture.controller, g_holding_tail_x, g_string_3_y, g_shift_alt);

    const common::core::Chart planted = currentChart(fixture.controller);
    REQUIRE(planted.notes.size() == 1);
    REQUIRE(planted.notes[0].keyframes.size() == 2);
    CHECK(planted.notes[0].keyframes[1].offset == common::core::Fraction{6});
    CHECK(planted.notes[0].keyframes[1].fret == 9);
    CHECK(planted.notes[0].sustain == original.notes[0].sustain);
    // Authoring state: no history entry ever held it, exactly as the bare gesture leaves it.
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before);
    const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
    CHECK(
        edit.selected_keyframes ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 1}}));
}

// A RELEASE standing on the end slot changes none of that, and needs no case of its own. The end
// is not a split — the ring already stops there — so the head is the plain adjacent insert, and
// the gate's clearance repair rides the release back one margin because no keyframe may sit on a
// later onset of its own string. A release IS the ring's end, so that repair moves it by resizing
// the ring: the fall keeps its fret and stays the ring's last statement, a margin earlier. No
// statement is dropped and no leg is clipped.
//
// Arming the caret on that slot SELECTS the release (armChartCaret replaces the selection with
// whatever the slot holds — a head, a held stop or a keyframe alike), which is what makes a bare
// DIGIT there a retype of the release rather than an insert. Insert and Alt+double-click are not
// selection-scoped, so both still strike.
TEST_CASE("A strike at a ring's end stands beside its release", "[core][chart]")
{
    KeyframeFixture fixture{makeReleasedGlideChart()};
    const common::core::Chart original = currentChart(fixture.controller);
    const common::core::Fraction margin = common::core::minimumSustainDistanceBeats(4);

    click(fixture.controller, g_ring_end_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);

    fixture.controller.onNeutralInsertRequested();
    {
        const common::core::Chart struck = currentChart(fixture.controller);
        REQUIRE(struck.notes.size() == 2);
        // The release moves by resizing the ring, so both land a margin before the new head —
        // the slide-out shape the gate keeps off every later onset.
        CHECK(struck.notes[0].sustain == common::core::Fraction{8} - margin);
        REQUIRE(struck.notes[0].keyframes.size() == 2);
        CHECK(struck.notes[0].keyframes[0].offset == common::core::Fraction{4});
        CHECK(struck.notes[0].keyframes[1].offset == common::core::Fraction{8} - margin);
        CHECK(struck.notes[0].keyframes[1].fret == 12);
        CHECK(struck.notes[1].position == common::core::GridPosition{.measure = 4, .beat = 1});
        CHECK(struck.notes[1].string == 3);
        CHECK(struck.notes[1].fret == 0);
    }

    fixture.controller.onUndoRequested();
    REQUIRE(currentChart(fixture.controller) == original);

    // The mouse form takes the same path, never the split walk: its first press finds the release
    // under the pointer and only arms the caret there, and its second strikes the plain head.
    altDoubleClick(fixture.controller, g_ring_end_x, g_string_3_y);
    const common::core::Chart clicked = currentChart(fixture.controller);
    REQUIRE(clicked.notes.size() == 2);
    CHECK(clicked.notes[0].sustain == common::core::Fraction{8} - margin);
    REQUIRE(clicked.notes[0].keyframes.size() == 2);
    CHECK(clicked.notes[0].keyframes[1].offset == common::core::Fraction{8} - margin);
    CHECK(clicked.notes[0].keyframes[1].fret == 12);
    CHECK(clicked.notes[1].position == common::core::GridPosition{.measure = 4, .beat = 1});
    CHECK(clicked.notes[1].fret == 0);
}

// The selection rule wins where it applies: with the release selected, a bare digit is a RETYPE of
// that keyframe and places nothing. The two verbs never race — a digit states the selected stop,
// and only the caret decides when nothing is selected.
TEST_CASE("A bare digit on a selected release retypes it", "[core][chart]")
{
    KeyframeFixture fixture{makeReleasedGlideChart()};

    click(fixture.controller, g_ring_end_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);

    fixture.controller.onChartFretDigitTyped(7);
    const common::core::Chart retyped = currentChart(fixture.controller);
    REQUIRE(retyped.notes.size() == 1);
    REQUIRE(retyped.notes[0].keyframes.size() == 2);
    CHECK(retyped.notes[0].keyframes[1].offset == common::core::Fraction{8});
    CHECK(retyped.notes[0].keyframes[1].fret == 7);
    CHECK(retyped.notes[0].sustain == common::core::Fraction{8});
}

// A typed value draws as the mark it creates, and for a bare digit inside a ring that mark is the
// whole SPLIT: the shortened origin and the head carrying the remainder both draw under the
// pending box, while the stored chart holds nothing until the entry settles.
TEST_CASE("A typed split previews the cut immediately", "[core][chart]")
{
    PendingKeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    // Two beats into the eight-beat ring, with a widenable 1 typed in the STRIKE verb.
    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartFretDigitTyped(1);
    CHECK(currentChart(fixture.controller) == original);

    const std::shared_ptr<const common::core::ChartViewState>& preview =
        publishedState(fixture.view).tab;
    REQUIRE(preview != nullptr);
    CHECK(preview->notes.size() == 2);
    const std::optional<ChartPendingFretViewState>& pending =
        publishedState(fixture.view).chart_edit.pending_fret;
    REQUIRE(pending.has_value());
    if (pending.has_value())
    {
        CHECK(pending->text == "1");
        CHECK(pending->valid);
        // The box rides the slot the new head lands on, not the ring it cuts.
        const auto* const slot = std::get_if<ChartSlotViewState>(&pending->at);
        REQUIRE(slot != nullptr);
        if (slot != nullptr)
        {
            CHECK_THAT(slot->seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));
            CHECK(slot->string == 3);
        }
    }

    // The second digit combines and settles the same split with the widened head fret.
    fixture.controller.onChartFretDigitTyped(2);
    const common::core::Chart split = currentChart(fixture.controller);
    REQUIRE(split.notes.size() == 2);
    CHECK(split.notes[0].sustain == common::core::Fraction{2});
    CHECK(split.notes[1].fret == 12);
    CHECK(split.notes[1].sustain == common::core::Fraction{6});
    CHECK_FALSE(publishedState(fixture.view).chart_edit.pending_fret.has_value());
}

// Alt+DOUBLE-click is the STRIKE's mouse form, and it must survive its own first press: that press
// planted the point above, so the second one dissolves it before splitting the ring — otherwise
// the cut would land on that mark and leave it retreated a margin behind the new head, stating a
// fret nothing travels to.
TEST_CASE("Alt+double-click on a tail splits the ring at the running fret", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    altDoubleClick(fixture.controller, g_holding_tail_x, g_string_3_y);

    const common::core::Chart struck = currentChart(fixture.controller);
    REQUIRE(struck.notes.size() == 2);
    // The origin's ring runs to the new onset carrying its arrival unmoved — the cut is past it —
    // and the silent point the first press planted left no trace at all.
    CHECK(struck.notes[0].sustain == common::core::Fraction{6});
    REQUIRE(struck.notes[0].keyframes.size() == 1);
    CHECK(struck.notes[0].keyframes[0].offset == common::core::Fraction{4});
    CHECK(struck.notes[1].position == common::core::GridPosition{.measure = 3, .beat = 3});
    CHECK(struck.notes[1].string == 3);
    // Six beats in the path is holding the fret it arrived on, so that is what the head opens on.
    CHECK(struck.notes[1].fret == 9);
    CHECK(struck.notes[1].sustain == common::core::Fraction{2});
    CHECK(struck.notes[1].keyframes.empty());
    CHECK(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{1});

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// Over a HEAD the strike refuses: a new onset there would replace the note rather than add one, so
// the Alt double click keeps its plain selection meaning and authors nothing.
TEST_CASE("Alt+double-click on a head authors nothing", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    altDoubleClick(fixture.controller, g_onset_x, g_string_3_y);

    CHECK(currentChart(fixture.controller) == original);
    CHECK(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{0});
}

// The insert ghost is a POINTER affordance: a keyboard entry authors where the caret is, not where
// the pointer rests, so the hovering ring goes rather than advertising a slot the press ignored.
TEST_CASE("A keyboard entry clears the hover ghost", "[core][chart]")
{
    KeyframeFixture fixture;

    // Arm the caret on the tail first, then hover Alt over an empty slot on ANOTHER lane: the
    // ring is showing there while the caret stands somewhere else entirely.
    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartPointerMove(pointerEvent(g_onset_x, g_string_2_y, g_alt));
    REQUIRE(publishedState(fixture.view).chart_edit.insert_ghost.has_value());

    fixture.controller.onChartPathDigitTyped(7);
    CHECK_FALSE(publishedState(fixture.view).chart_edit.insert_ghost.has_value());

    // And the Insert verb clears it for the same reason.
    fixture.controller.onChartPointerMove(pointerEvent(g_onset_x, g_string_2_y, g_alt));
    REQUIRE(publishedState(fixture.view).chart_edit.insert_ghost.has_value());
    fixture.controller.onNeutralInsertRequested();
    CHECK_FALSE(publishedState(fixture.view).chart_edit.insert_ghost.has_value());
}

} // namespace rock_hero::editor::core
