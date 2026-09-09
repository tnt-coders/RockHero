#include <optional>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The chart every scenario here opens: one pitched glide on string 3, a lane the shared fixture
// leaves empty so a probe can only land on the gesture under test. Measure 2 beat 1 is 2.0s
// (x = 40 at the fixture geometry's 20 px/s, y = 140 on string 3), the ring runs eight beats to
// 6.0s, and the junction it arrives at four beats in draws its linked head at 4.0s (x = 80) —
// far enough from the onset head that the two boxes cannot overlap.
[[nodiscard]] common::core::Chart makeGlideChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote glide =
        makeTestNote({.measure = 2, .beat = 1}, 3, 5, common::core::Fraction{8});
    glide.keyframes = {common::core::Keyframe{.offset = common::core::Fraction{4}, .fret = 9}};
    chart.notes = {std::move(glide)};
    return chart;
}

// The chart-editing fixture wired for the keyframe verbs: the glide chart opened through the
// controller's normal route, with the quarter-note grid the lane geometry assumes.
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

// Lane-local pixels of the fixture's two clickable marks.
constexpr float g_onset_x{40.0f};
constexpr float g_junction_x{80.0f};
constexpr float g_string_3_y{140.0f};

} // namespace

// A keyframe is a selection citizen: the linked head the lane draws at a junction is clickable,
// and clicking it selects the keyframe alone. It occupies no slot, so nothing arms a caret for it
// — the marker demotes to a cursor in place, exactly as every multi-select gesture leaves it.
TEST_CASE("A click selects the keyframe under it", "[core][chart]")
{
    KeyframeFixture fixture;

    // Arm the caret on the note first, so the demotion below is a state CHANGE and not the
    // absence a fresh controller would report anyway.
    click(fixture.controller, g_onset_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.caret.has_value());
    REQUIRE(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{0});

    click(fixture.controller, g_junction_x, g_string_3_y);
    const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
    CHECK(
        edit.selected_keyframes ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}}));
    CHECK(edit.selected_notes.empty());
    CHECK_FALSE(edit.caret.has_value());
    // The selection is not empty, which is what makes the selection-scoped verbs reachable.
    CHECK(publishedState(fixture.view).selection_present);
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

    // Per-press entries in this slice (the one-entry burst law is the sustain gesture's own task),
    // so undo walks back one step at a time — and the first one restores the offset the press
    // moved, which is what makes the original key name the point again.
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

} // namespace rock_hero::editor::core
