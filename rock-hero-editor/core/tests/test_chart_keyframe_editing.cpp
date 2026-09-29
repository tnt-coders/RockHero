#include <algorithm>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
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

// Lane-local pixels of the fixture's two clickable marks, and of the two tail slots a typed
// digit is exercised at: 3.0s (two beats into the ring, on the travel leg toward the junction)
// and 5.0s (six beats in, out where the path holds at the junction's own fret).
constexpr float g_onset_x{40.0f};
constexpr float g_junction_x{80.0f};
constexpr float g_travel_tail_x{60.0f};
constexpr float g_holding_tail_x{100.0f};
constexpr float g_string_3_y{140.0f};
// An empty lane beside the note's: a click here takes the caret off the note entirely, which is
// what leaves the note's focus and lets the commit law judge its points.
constexpr float g_string_2_y{180.0f};

// Alt held on a press: the reveal modifier, which creates nothing.
constexpr ChartPointerModifiers g_alt{.ctrl = false, .shift = false, .alt = true};

// The fixture's ring ends at 6.0s, one measure past the junction's linked head.
constexpr float g_ring_end_x{120.0f};

// The glide chart with a SLIDE-OUT at the ring's end: the same eight-beat gesture, plus the fret
// the hand slides out toward exactly where the ring stops. The figure where the end's own statement
// is reached by the object walk rather than by a landing, since no slot holds it.
[[nodiscard]] common::core::Chart makeSlideOutGlideChart()
{
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].keyframes.push_back(
        common::core::Keyframe{.offset = common::core::Fraction{8}, .fret = 12});
    return chart;
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

// The vibrato channel's second authoring scope: with a keyframe selected, `V` states the width of
// the leg that keyframe begins and leaves the note's onset statement alone. The second press inside
// the verb window takes it back — and takes the width out entirely rather than writing a false
// one, because a leg without vibrato states nothing.
TEST_CASE("The vibrato verb states the vibrato at a selected keyframe", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);

    const common::core::Chart vibrating = currentChart(fixture.controller);
    REQUIRE(vibrating.notes.size() == 1);
    REQUIRE(vibrating.notes[0].keyframes.size() == 1);
    CHECK(vibrating.notes[0].keyframes[0].vibrato == common::core::VibratoState::Narrow);
    // The onset is untouched: the ring opens still and vibrates over the leg the junction begins.
    CHECK_FALSE(common::core::hasVibrato(vibrating.notes[0].vibrato));
    // And the position channel rides along unchanged — one record, so the coupling needs no copy.
    CHECK(vibrating.notes[0].keyframes[0].fret == 9);

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    const common::core::Chart cleared = currentChart(fixture.controller);
    CHECK(cleared == original);
    // The clear takes the width off, so the keyframe keeps only its fret.
    REQUIRE(cleared.notes[0].keyframes.size() == 1);
    CHECK_FALSE(hasVibrato(cleared.notes[0].keyframes[0].vibrato));
    // The pair reversed its own entry, so it leaves no history trace at all.
    CHECK_FALSE(publishedState(fixture.view).undo_enabled);
}

// The same clear a press LATER, with the toggle window closed behind a selection change, so the
// clear itself does the work rather than the window's exact reversal: the press takes the width
// off the leg, and a keyframe the clear empties would go with it. Here the point states a fret
// too, so what goes is the width alone — and this is a NEW undo entry, not a reversal, which is
// what the round trip below proves.
TEST_CASE("Clearing the vibrato at a keyframe dissolves the statement it wrote", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    const common::core::Chart vibrating = currentChart(fixture.controller);
    REQUIRE(vibrating.notes[0].keyframes[0].vibrato == common::core::VibratoState::Narrow);

    // A selection change commits that entry and closes the verb window, so the press below runs
    // the verb's ordinary law instead of reversing anything.
    click(fixture.controller, g_onset_x, g_string_3_y);
    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);

    const common::core::Chart cleared = currentChart(fixture.controller);
    CHECK(cleared == original);
    REQUIRE(cleared.notes[0].keyframes.size() == 1);
    CHECK_FALSE(hasVibrato(cleared.notes[0].keyframes[0].vibrato));
    CHECK(cleared.notes[0].keyframes[0].fret == 9);

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == vibrating);
    fixture.controller.onRedoRequested();
    CHECK(currentChart(fixture.controller) == cleared);
    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == vibrating);
}

// A BARE `V` ON A COVERED SLOT STARTS THE VIBRATO THERE. The key can create no note and split no
// ring, so on a slot a ring covers it has one meaning: the width of the leg from that instant on,
// which plants a fret-less point carrying it and selects that point. The second press inside the
// verb window takes the whole write back, leaving no history trace.
TEST_CASE("A bare vibrato on a covered slot vibrates the ring from there", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::WideVibrato);

    const common::core::Chart vibrating = currentChart(fixture.controller);
    REQUIRE(vibrating.notes.size() == 1);
    REQUIRE(vibrating.notes[0].keyframes.size() == 2);
    const common::core::Keyframe& point = vibrating.notes[0].keyframes[1];
    CHECK(point.offset == common::core::Fraction{6});
    CHECK_FALSE(point.fret.has_value());
    CHECK(point.vibrato == common::core::VibratoState::Wide);
    // The ring still opens still, and the junction before the point is untouched.
    CHECK_FALSE(common::core::hasVibrato(vibrating.notes[0].vibrato));
    CHECK_FALSE(common::core::hasVibrato(vibrating.notes[0].keyframes[0].vibrato));
    CHECK(
        publishedState(fixture.view).chart_edit.selected_keyframes ==
        std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 1}});

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::WideVibrato);
    CHECK(currentChart(fixture.controller) == original);
    CHECK_FALSE(publishedState(fixture.view).undo_enabled);
}

// THE WIDTH IS A LEG'S, so a press on a covered slot vibrates only up to the next point: a later
// point that states nothing about the vibrato begins a leg without it.
TEST_CASE("A bare vibrato on a covered slot stops at the next point", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].keyframes.push_back(
        common::core::Keyframe{.offset = common::core::Fraction{7}, .bend = 1.0});
    KeyframeFixture fixture{std::move(chart)};

    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);

    const common::core::Chart vibrating = currentChart(fixture.controller);
    REQUIRE(vibrating.notes.size() == 1);
    REQUIRE(vibrating.notes[0].keyframes.size() == 3);
    CHECK(vibrating.notes[0].keyframes[1].offset == common::core::Fraction{6});
    CHECK(vibrating.notes[0].keyframes[1].vibrato == common::core::VibratoState::Narrow);
    CHECK_FALSE(common::core::hasVibrato(vibrating.notes[0].keyframes[2].vibrato));

    const std::shared_ptr<const common::core::ChartViewState>& tab =
        publishedState(fixture.view).tab;
    REQUIRE(tab != nullptr);
    const std::vector<common::core::NoteViewState>& notes = tab->notes;
    REQUIRE(notes.size() == 1);
    REQUIRE(notes[0].vibrato.size() == 1);
    CHECK_THAT(notes[0].vibrato[0].start_seconds, Catch::Matchers::WithinAbs(5.0, 1e-9));
    CHECK_THAT(notes[0].vibrato[0].end_seconds, Catch::Matchers::WithinAbs(5.5, 1e-9));
}

// The hand cannot shake a string it is sliding along, so a covered slot on a travel leg takes no
// vibrato: the press is refused and the chart keeps what it held.
TEST_CASE("A bare vibrato on a travel leg is refused", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    CHECK(currentChart(fixture.controller) == original);
    CHECK_FALSE(publishedState(fixture.view).undo_enabled);
}

// Inside a leg already vibrating at the key's own tier the press reads that leg's width, so it
// CLEARS from the instant on: the point it plants begins a leg without vibrato. At the other tier
// it replaces the width from the instant on instead.
TEST_CASE("A bare vibrato inside a vibrating leg changes it from there", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].keyframes[0].vibrato = common::core::VibratoState::Narrow;

    // Copied into each section: a move in both would read, to the use-after-move check, as a
    // second move of one object, since it cannot see that Catch2 runs one section per pass.
    SECTION("the key's own tier clears")
    {
        KeyframeFixture fixture{chart};
        click(fixture.controller, g_holding_tail_x, g_string_3_y);
        fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);

        const common::core::Chart cleared = currentChart(fixture.controller);
        REQUIRE(cleared.notes.size() == 1);
        REQUIRE(cleared.notes[0].keyframes.size() == 2);
        CHECK(cleared.notes[0].keyframes[0].vibrato == common::core::VibratoState::Narrow);
        CHECK(cleared.notes[0].keyframes[1].offset == common::core::Fraction{6});
        CHECK_FALSE(common::core::hasVibrato(cleared.notes[0].keyframes[1].vibrato));
    }

    SECTION("the other tier replaces")
    {
        KeyframeFixture fixture{chart};
        click(fixture.controller, g_holding_tail_x, g_string_3_y);
        fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::WideVibrato);

        const common::core::Chart widened = currentChart(fixture.controller);
        REQUIRE(widened.notes.size() == 1);
        REQUIRE(widened.notes[0].keyframes.size() == 2);
        CHECK(widened.notes[0].keyframes[0].vibrato == common::core::VibratoState::Narrow);
        CHECK(widened.notes[0].keyframes[1].offset == common::core::Fraction{6});
        CHECK(widened.notes[0].keyframes[1].vibrato == common::core::VibratoState::Wide);
    }
}

// A ring's exact end begins no leg, so a vibrato there has nothing to vibrate and the press is
// inert — which is why `V` needs no `Alt` twin to reach that slot.
TEST_CASE("A bare vibrato at a ring's end is inert", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_ring_end_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    CHECK(currentChart(fixture.controller) == original);
    CHECK_FALSE(publishedState(fixture.view).undo_enabled);
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

// Each technique is its own authored surface (user ruling, 2026-09-29): Delete on a junction that
// also bends takes only its FRET, and the point, still bending, stays selected so the bend's own
// verb is one key away; a second Delete takes it whole.
TEST_CASE("Delete takes a bending junction's fret and keeps it selected", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].keyframes[0].bend = 1.0;
    KeyframeFixture fixture{std::move(chart)};
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onSelectionDeleteRequested();
    const common::core::Chart peeled = currentChart(fixture.controller);
    REQUIRE(peeled.notes.size() == 1);
    REQUIRE(peeled.notes[0].keyframes.size() == 1);
    CHECK_FALSE(peeled.notes[0].keyframes[0].fret.has_value());
    CHECK(
        publishedState(fixture.view).chart_edit.selected_keyframes ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}}));

    fixture.controller.onSelectionDeleteRequested();
    const common::core::Chart emptied = currentChart(fixture.controller);
    REQUIRE(emptied.notes.size() == 1);
    CHECK(emptied.notes[0].keyframes.empty());
    CHECK(publishedState(fixture.view).chart_edit.selected_keyframes.empty());

    fixture.controller.onUndoRequested();
    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// Deleting a point must leave an empty armed slot, so a ring digit can recreate it without moving.
TEST_CASE("Typing recreates a deleted tail keyframe at the caret", "[core][chart]")
{
    KeyframeFixture fixture;
    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    // 9 is the fret the path holds here, so this point says nothing — authoring state, planted and
    // selected all the same.
    fixture.controller.onChartRingDigitTyped(9);
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

    fixture.controller.onChartRingDigitTyped(7);
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
    fixture.controller.onChartRingDigitTyped(7);
    REQUIRE(currentChart(fixture.controller).notes[0].keyframes.size() == 2);

    fixture.controller.onUndoRequested();
    REQUIRE(currentChart(fixture.controller) == original);
    // Nothing stands under the caret any more, so nothing is selected — the state the delete verb
    // leaves, reached here by the transition's own repair.
    CHECK_FALSE(publishedState(fixture.view).selection_present);

    fixture.controller.onChartRingDigitTyped(9);
    const common::core::Chart recreated = currentChart(fixture.controller);
    REQUIRE(recreated.notes.size() == 1);
    REQUIRE(recreated.notes[0].keyframes.size() == 2);
    CHECK(recreated.notes[0].keyframes[1].offset == common::core::Fraction{6});
    CHECK(recreated.notes[0].keyframes[1].fret == 9);
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
}

// `Shift+L` on a selected keyframe severs the gesture there (W10's addendum): the note's path ends
// at the junction and a new head takes the remainder. One compound undo entry spanning both
// products, reversed exactly.
TEST_CASE("The junction toggle severs the gesture at a selected keyframe", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartJunctionToggleRequested();

    const common::core::Chart severed = currentChart(fixture.controller);
    REQUIRE(severed.notes.size() == 2);
    CHECK(severed.notes[0].position == original.notes[0].position);
    CHECK(severed.notes[0].fret == 5);
    CHECK(severed.notes[0].sustain == common::core::Fraction{4});
    // The origin keeps the keyframe it travels to: the leg the user split at is real travel, and
    // the junction is an equal-fret handover to the new head. The arrival stands AT that head, at
    // the ring's own end, and the chart PROVES it is an arrival and not a slide-out: the fret it
    // names is the stop the new head is struck at, at the same instant. The DRAWN copy is spaced a
    // margin before the head by presentation, as every tail is.
    REQUIRE(severed.notes[0].keyframes.size() == 1);
    CHECK(severed.notes[0].keyframes[0].fret == 9);
    CHECK(severed.notes[0].keyframes[0].offset == common::core::Fraction{4});

    CHECK(
        severed.notes[1].position ==
        common::core::GridPosition{.measure = 3, .beat = 1, .offset = {}});
    CHECK(severed.notes[1].string == 3);
    CHECK(severed.notes[1].fret == 9);
    CHECK(severed.notes[1].sustain == common::core::Fraction{4});
    CHECK(severed.notes[1].keyframes.empty());
    // A split head is struck: the join, not the split, is the unstruck junction.
    CHECK(severed.notes[1].attack == common::core::NoteAttack::Pick);

    // The SPLIT PRODUCT becomes the selection — the planner names it exactly, because it is the
    // one record the plan inserted at a new key. So the next verb acts on the new head, and the
    // origin (rewritten in place, and never selected) does not join. That is also what arms the
    // toggle: pressing again with this head selected joins it straight back.
    CHECK(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{1});
    CHECK(publishedState(fixture.view).chart_edit.selected_keyframes.empty());

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
    fixture.controller.onRedoRequested();
    CHECK(currentChart(fixture.controller) == severed);
    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// THE WHOLE ENTRY PATH TO A SPLIT, end to end: a ring digit states a point on the tail, the point
// is left selected, and `Shift+L` severs the gesture there. Splitting at a typed fret is this pair
// — the bare digit's cut strikes a fresh head instead — and it is lossless, the origin ending at
// the new head with the remainder riding on at the fret the point stated.
TEST_CASE("The junction toggle splits at a point a digit planted", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    // Two beats into the eight-beat ring, on the leg travelling from the head's 5 toward 9.
    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartRingDigitTyped(6);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);
    REQUIRE(currentChart(fixture.controller).notes.size() == 1);

    fixture.controller.onChartJunctionToggleRequested();
    const common::core::Chart severed = currentChart(fixture.controller);
    REQUIRE(severed.notes.size() == 2);
    // The origin keeps everything up to the cut; the remainder opens on the point's own fret and
    // carries the arrival on, rebased onto its new onset.
    CHECK(severed.notes[0].fret == 5);
    CHECK(severed.notes[0].sustain == common::core::Fraction{2});
    CHECK(severed.notes[1].position == common::core::GridPosition{.measure = 2, .beat = 3});
    CHECK(severed.notes[1].fret == 6);
    CHECK(severed.notes[1].sustain == common::core::Fraction{6});
    REQUIRE(severed.notes[1].keyframes.size() == 1);
    CHECK(severed.notes[1].keyframes[0].offset == common::core::Fraction{2});
    CHECK(severed.notes[1].keyframes[0].fret == 9);
}

// The same door walked from NOTHING at the session's own default grid — the flow a charter
// actually types, and the one the fixture's quarter-note grid hid. A note typed onto an empty slot
// rings ONE GRID STEP, and at the default 1/16 that step is shorter than the glide-into-a-landing
// margin: the commonest split there is, end to end through the controller.
//
// Typed at the note's own fret, so the arrival is the SILENT one, which is the case that proves the
// authoring state survives the verb's settle prologue and is still there to be cut.
TEST_CASE("The junction toggle splits a grid-step ring at the default grid", "[core][chart]")
{
    KeyframeFixture fixture;
    fixture.controller.onGridNoteValueChangeRequested(g_default_tempo_grid_note_value);

    // An empty lane beside the fixture's glide: a digit here authors a head with a grid-step ring.
    click(fixture.controller, g_onset_x, g_string_2_y);
    fixture.controller.onChartFretDigitTyped(7);
    REQUIRE(currentChart(fixture.controller).notes.size() == 2);

    // Room for a point strictly inside the tail, then the caret stepped onto it.
    fixture.controller.onChartSustainAdjustRequested(1);
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    fixture.controller.onChartRingDigitTyped(7);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);
    REQUIRE(currentChart(fixture.controller).notes.size() == 2);

    fixture.controller.onChartJunctionToggleRequested();
    const common::core::Chart severed = currentChart(fixture.controller);
    REQUIRE(severed.notes.size() == 3);
    // The typed note's two halves, each one grid step of the 1/16 grid — a quarter beat in 4/4.
    const common::core::ChartNote& origin = severed.notes[0];
    const common::core::ChartNote& split = severed.notes[2];
    CHECK(origin.string == 2);
    CHECK(origin.fret == 7);
    CHECK(origin.sustain == common::core::Fraction{1, 4});
    // The point restated the fret in force, and it stays: an ARRIVAL wears a linked head at the
    // ink end, so a silent one is ordinary visible authoring state, unlike a silent SLIDE-OUT,
    // which the gate dissolves for having no face at all.
    REQUIRE(origin.keyframes.size() == 1);
    CHECK(origin.keyframes[0].offset == common::core::Fraction{1, 4});
    CHECK(origin.keyframes[0].fret == 7);
    CHECK(split.string == 2);
    CHECK(split.fret == 7);
    CHECK(
        split.position == common::core::GridPosition{
                              .measure = 2, .beat = 1, .offset = common::core::Fraction{1, 4}
                          });
    CHECK(split.sustain == common::core::Fraction{1, 4});
}

// The verb is selection-scoped like every technique verb beside it, so an EMPTY selection is
// simply no operand — pressing it is inert, not an error, and leaves no entry. A head that cannot
// be joined leaves no entry either, for a different reason: the fixture's glide is the only note
// on its string, so nothing holds that string for its point to join, and the whole plan refuses.
// Both silences look the same from here, which is W5's deferred feedback channel in one line.
TEST_CASE("The junction toggle is inert with no operand to toggle", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    fixture.controller.onChartJunctionToggleRequested();
    CHECK(currentChart(fixture.controller) == original);

    click(fixture.controller, g_onset_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{0});
    fixture.controller.onChartJunctionToggleRequested();
    CHECK(currentChart(fixture.controller) == original);
    CHECK_FALSE(publishedState(fixture.view).undo_enabled);
}

// THE TOGGLE, felt as the charter feels it: one press splits, the next press on what that press
// left selected joins it straight back — no verb window, no second keystroke, just the selection
// the plan handed over. The join's point takes the selection in its turn, so a third press would
// split again, and the undo entry each press leaves is its own.
TEST_CASE("The junction toggle joins a split product back", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartJunctionToggleRequested();
    REQUIRE(currentChart(fixture.controller).notes.size() == 2);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{1});

    // The product is still selected, so the second press is the first one's inverse.
    fixture.controller.onChartJunctionToggleRequested();
    CHECK(currentChart(fixture.controller) == original);
    // And the junction POINT takes the selection, ready to be split again.
    CHECK(
        publishedState(fixture.view).chart_edit.selected_keyframes ==
        std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}});
    CHECK(publishedState(fixture.view).chart_edit.selected_notes.empty());

    // Two presses, two entries: undoing the join puts the split back.
    fixture.controller.onUndoRequested();
    REQUIRE(currentChart(fixture.controller).notes.size() == 2);
    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
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

// THE BURST RUNS INTO THE RING'S END AND STOPS THERE, held. A point stepped ONTO the end would
// become the slide-out, and the run replays from its pre-gesture chart where the point is still
// interior — so the end would never follow the next press and the drag would stick until the
// charter re-selected. The step is refused one short of the end instead, further presses are
// refused the same way (a refused step is never recorded, so nothing accumulates), and the opposite
// press moves the point back inside the SAME burst.
TEST_CASE("A held move burst stops one step short of the ring's end", "[core][chart]")
{
    common::core::Chart plain;
    plain.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    plain.notes = {makeTestNote({.measure = 2, .beat = 1}, 3, 5, common::core::Fraction{8})};
    KeyframeFixture fixture{std::move(plain)};

    // The user's report: a ring digit typed four beats along the tail plants a point that travels
    // from the onset's 5, and the arrows then drag it outward.
    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onChartRingDigitTyped(6);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);
    const std::size_t entries_after_typing =
        publishedState(fixture.view).undo_history.labels.size();

    // The point's offset and the ring it rides, read together so every assertion is about a record
    // that exists — and so the ring's own length is pinned at every step.
    const auto point = [&fixture] {
        const common::core::Chart chart = currentChart(fixture.controller);
        REQUIRE(chart.notes.size() == 1);
        REQUIRE(chart.notes[0].keyframes.size() == 1);
        CHECK(chart.notes[0].sustain == common::core::Fraction{8});
        CHECK(common::core::endStatedFretOrNull(chart.notes[0]) == nullptr);
        return chart.notes[0].keyframes[0].offset;
    };
    REQUIRE(point() == common::core::Fraction{4});

    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(point() == common::core::Fraction{7});

    // The end is eight beats out, so this press and the one after it are refused: the point holds
    // at seven and the run keeps exactly the steps it had.
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(point() == common::core::Fraction{7});
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(point() == common::core::Fraction{7});

    // Nothing accumulated, so one press back is one step back — inside the same burst, which is
    // what a wedged run could not do.
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(point() == common::core::Fraction{6});
    // The whole run is still one entry beside the typed point's.
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_after_typing + 1);
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

// A RING digit (`Alt`+digit) strictly inside a ring states a POINT on the path, never a cut. The
// point lands planted and selected, with the caret still on its slot. Where its fret makes a HOLD
// BOUNDARY, as 5 does on a travel leg, the point already says something and simply stays.
TEST_CASE(
    "A ring digit on a tail plants a keyframe, selected, with the caret on it", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);
    const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

    // Two beats into the ring, on the leg travelling from the head's 5 toward the junction's 9.
    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.caret.has_value());

    fixture.controller.onChartRingDigitTyped(5);
    const common::core::Chart stated = currentChart(fixture.controller);
    // One note still, and its ring untouched: the tail took a POINT carrying the typed 5, not a
    // second onset and not a cut.
    REQUIRE(stated.notes.size() == 1);
    REQUIRE(stated.notes[0].keyframes.size() == 2);
    CHECK(stated.notes[0].keyframes[0].offset == common::core::Fraction{2});
    CHECK(stated.notes[0].keyframes[0].fret == 5);
    CHECK(stated.notes[0].keyframes[1].fret == 9);
    CHECK(stated.notes[0].sustain == original.notes[0].sustain);
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before + 1);

    // Selected, with the caret where the digit was typed — the point's own slot.
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
    fixture.controller.onChartRingDigitTyped(9);
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
    fixture.controller.onChartRingDigitTyped(9);
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
    fixture.controller.onChartRingDigitTyped(5);
    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onChartRingDigitTyped(9);
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

// THE SLIDE WORKFLOW the law exists for: Alt+Insert where the slide starts, walk the caret along
// the same tail to where it lands, type the landing fret. The start says nothing until the landing
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
    fixture.controller.onChartRingDigitTyped(5);
    // The landing: six beats in on the same tail, on the ring plane again — the bare digit would
    // cut the ring there instead.
    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    fixture.controller.onChartRingDigitTyped(9);
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
    fixture.controller.onChartRingDigitTyped(9);
    // The plant leaves the point SELECTED, so the next digit retypes it rather than opening a
    // second entry at the same slot. 7 cannot be widened under the fret cap, so the retype settles
    // in this one keystroke.
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

// Every tail is an authoring surface, a plain note's included, and the plane says what for: a bare
// digit there is "a note here", a fresh head that CUTS the ring and takes its remainder, while the
// ring digit states a point on the path — typed at the note's own fret, a silent one, authoring
// state like any other that says nothing.
TEST_CASE(
    "A digit on a plain note's tail cuts it, where a ring digit plants a point", "[core][chart]")
{
    common::core::Chart plain = makeGlideChart();
    plain.notes[0].keyframes.clear();
    KeyframeFixture fixture{std::move(plain)};
    const common::core::Chart original = currentChart(fixture.controller);

    SECTION("the bare digit cuts the ring")
    {
        const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();
        click(fixture.controller, g_travel_tail_x, g_string_3_y);
        fixture.controller.onChartFretDigitTyped(5);
        const common::core::Chart cut = currentChart(fixture.controller);
        REQUIRE(cut.notes.size() == 2);
        CHECK(cut.notes[0].position == original.notes[0].position);
        CHECK(cut.notes[0].sustain == common::core::Fraction{2});
        CHECK(cut.notes[0].keyframes.empty());
        const common::core::ChartNote& head = cut.notes[1];
        CHECK(head.position == common::core::GridPosition{.measure = 2, .beat = 3});
        CHECK(head.string == 3);
        CHECK(head.fret == 5);
        CHECK(head.sustain == common::core::Fraction{6});
        CHECK(head.attack == common::core::NoteAttack::Pick);
        // The struck head is what the settle selects, and the cut is one entry.
        CHECK(
            publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{1});
        CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before + 1);

        fixture.controller.onUndoRequested();
        CHECK(currentChart(fixture.controller) == original);
    }

    SECTION("the ring digit plants a point")
    {
        click(fixture.controller, g_travel_tail_x, g_string_3_y);
        fixture.controller.onChartRingDigitTyped(5);
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
}

// A bare digit at a caret a GLIDE covers cuts it mid-glide: the origin keeps the fret it set out
// from to the cut, re-timing nothing, and the fresh head at the typed fret carries the rest of
// the path on — the arrival rebased onto its onset.
TEST_CASE("A digit at a caret on a travel leg cuts the glide", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartFretDigitTyped(7);
    const common::core::Chart cut = currentChart(fixture.controller);
    REQUIRE(cut.notes.size() == 2);
    CHECK(cut.notes[0].fret == 5);
    CHECK(cut.notes[0].sustain == common::core::Fraction{2});
    CHECK(cut.notes[0].keyframes.empty());
    const common::core::ChartNote& head = cut.notes[1];
    CHECK(head.position == common::core::GridPosition{.measure = 2, .beat = 3});
    CHECK(head.fret == 7);
    CHECK(head.sustain == common::core::Fraction{6});
    REQUIRE(head.keyframes.size() == 1);
    CHECK(head.keyframes[0].offset == common::core::Fraction{2});
    CHECK(head.keyframes[0].fret == 9);

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// A ring digit at a caret a GLIDE covers states a POINT on the tail: the typed fret rides the same
// pending entry a typed note does and lands planted and selected — so a typed fret the path
// already passes through is a point that says nothing, authoring state that pushes no entry.
TEST_CASE("A ring digit at a caret on a travel leg states a point", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);
    const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

    // Two beats in, the leg from 5 to 9 passes through 7: stating 7 there says nothing yet.
    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartRingDigitTyped(7);
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
    // this digit retypes it.
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
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before + 1);

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// A point stating a bend alone INHERITS the fret in force and states no stop of its own, so the two
// planes read it apart (user ruling, 2026-09-29). The RING digit states its fret there, the bend
// staying beside it on the same point, which now says where the hand is as well as how far the
// string is pushed. The BARE digit addresses stops and finds none, so it means what it means on the
// bare ring — a note here, cutting the ring, the new head carrying the bend in force. A fret SHIFT
// moves stops, and the point has none, so it takes none.
TEST_CASE("The two planes' digits at a bend point", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    // A whole step pushed at 5.0s, six beats in, out where the path holds at the junction's 9.
    chart.notes[0].keyframes.push_back(
        common::core::Keyframe{.offset = common::core::Fraction{6}, .fret = {}, .bend = 1.0});
    KeyframeFixture fixture{std::move(chart)};
    const common::core::Chart original = currentChart(fixture.controller);

    const auto typed_point = [&fixture]() -> common::core::Keyframe {
        const common::core::Chart typed = currentChart(fixture.controller);
        REQUIRE(typed.notes.size() == 1);
        REQUIRE(typed.notes[0].keyframes.size() == 2);
        return typed.notes[0].keyframes[1];
    };
    const auto check_stated = [](const common::core::Keyframe& point) {
        CHECK(point.offset == common::core::Fraction{6});
        CHECK(point.fret == std::optional{7});
        const std::optional<double>& bend = point.bend;
        REQUIRE(bend.has_value());
        if (bend.has_value())
        {
            CHECK_THAT(*bend, Catch::Matchers::WithinULP(1.0, 0));
        }
    };

    SECTION("the ring digit")
    {
        click(fixture.controller, g_holding_tail_x, g_string_3_y);
        fixture.controller.onChartRingDigitTyped(7);
        check_stated(typed_point());
        fixture.controller.onUndoRequested();
        CHECK(currentChart(fixture.controller) == original);
    }

    SECTION("the bare digit cuts the ring there")
    {
        click(fixture.controller, g_holding_tail_x, g_string_3_y);
        fixture.controller.onChartFretDigitTyped(7);
        const common::core::Chart cut = currentChart(fixture.controller);
        REQUIRE(cut.notes.size() == 2);
        CHECK(cut.notes[0].sustain == common::core::Fraction{6});
        CHECK(cut.notes[1].fret == 7);
        CHECK_THAT(cut.notes[1].bend, Catch::Matchers::WithinULP(1.0, 0));
        fixture.controller.onUndoRequested();
        CHECK(currentChart(fixture.controller) == original);
    }

    SECTION("a fret shift leaves it inheriting")
    {
        click(fixture.controller, g_holding_tail_x, g_string_3_y);
        fixture.controller.onChartFretShiftRequested(1);
        CHECK(currentChart(fixture.controller) == original);
    }
}

// The typed point and its changed path draw immediately beneath the pending box, then wait for a
// second digit exactly as a typed note does: the two combine into one value inside the entry
// window, so a point widens like every other typed value. The FIRST key's plane is the entry's,
// so a bare second digit widens the point rather than cutting. The stored chart remains untouched
// until settlement.
TEST_CASE("A typed point on a tail previews the keyframe immediately", "[core][chart]")
{
    PendingKeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartRingDigitTyped(1);
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

    fixture.controller.onChartFretDigitTyped(2);
    const common::core::Chart stated = currentChart(fixture.controller);
    REQUIRE(stated.notes.size() == 1);
    REQUIRE(stated.notes[0].keyframes.size() == 2);
    CHECK(stated.notes[0].keyframes[0].fret == 12);
    CHECK_FALSE(publishedState(fixture.view).chart_edit.pending_fret.has_value());
}

// A pending CUT is a creating entry like any other: the divided ring and its fresh head draw at
// once from the projected plan, the box sits at the cut's slot, and a second digit widens the
// head's fret — all while the stored chart stays whole until the entry settles.
TEST_CASE("A pending cut previews the divided ring under its box", "[core][chart]")
{
    PendingKeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);

    click(fixture.controller, g_travel_tail_x, g_string_3_y);
    fixture.controller.onChartFretDigitTyped(1);
    CHECK(currentChart(fixture.controller) == original);
    const std::shared_ptr<const common::core::ChartViewState>& preview =
        publishedState(fixture.view).tab;
    REQUIRE(preview != nullptr);
    REQUIRE(preview->notes.size() == 2);
    CHECK_THAT(preview->notes[1].start_seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));
    CHECK(preview->notes[1].fret == 1);
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

    fixture.controller.onChartFretDigitTyped(2);
    const common::core::Chart cut = currentChart(fixture.controller);
    REQUIRE(cut.notes.size() == 2);
    CHECK(cut.notes[0].sustain == common::core::Fraction{2});
    CHECK(cut.notes[1].fret == 12);
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

// A DIGIT AT A RING'S EXACT END IS ALWAYS THE NEXT NOTE — the ring already stops where that head
// starts, so sequential entry never trips — and that holds whatever the end states: no landing
// addresses the end's own statement, so nothing there can swallow the keystroke into a retype.
TEST_CASE("A digit at a ring's end places the head, whatever the end states", "[core][chart]")
{
    SECTION("on a ring that simply ends")
    {
        KeyframeFixture fixture;
        const common::core::Chart original = currentChart(fixture.controller);

        click(fixture.controller, g_ring_end_x, g_string_3_y);
        fixture.controller.onChartFretDigitTyped(3);
        const common::core::Chart placed = currentChart(fixture.controller);
        REQUIRE(placed.notes.size() == 2);
        // The glide is untouched: nothing was split and it grew no slide-out.
        CHECK(placed.notes[0].sustain == original.notes[0].sustain);
        CHECK(placed.notes[0].keyframes.size() == original.notes[0].keyframes.size());
        CHECK(placed.notes[1].position == common::core::GridPosition{.measure = 4, .beat = 1});
        CHECK(placed.notes[1].string == 3);
        CHECK(placed.notes[1].fret == 3);
    }

    SECTION("on a ring whose end states a slide-out")
    {
        KeyframeFixture fixture{makeSlideOutGlideChart()};
        const common::core::Chart original = currentChart(fixture.controller);

        click(fixture.controller, g_ring_end_x, g_string_3_y);
        CHECK(publishedState(fixture.view).chart_edit.selected_keyframes.empty());
        fixture.controller.onChartFretDigitTyped(3);
        const common::core::Chart placed = currentChart(fixture.controller);
        REQUIRE(placed.notes.size() == 2);
        // The slide-out is exactly as authored: the digit went to the new head beside it.
        CHECK(placed.notes[0].keyframes == original.notes[0].keyframes);
        CHECK(placed.notes[1].fret == 3);
    }
}

// A CLICK NEVER CREATES, and `Alt` on this lane is the reveal modifier rather than an authoring
// one: a press inside a ring arms the caret at its slot and leaves the chart exactly as it was,
// whichever way it is held. The digit that follows is what states anything.
TEST_CASE("A click inside a tail arms the caret and creates nothing", "[core][chart]")
{
    KeyframeFixture fixture;
    const common::core::Chart original = currentChart(fixture.controller);
    const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

    click(fixture.controller, g_holding_tail_x, g_string_3_y);
    CHECK(currentChart(fixture.controller) == original);
    {
        const ChartEditViewState& edit = publishedState(fixture.view).chart_edit;
        REQUIRE(edit.caret.has_value());
        if (edit.caret.has_value())
        {
            CHECK_THAT(edit.caret->seconds, Catch::Matchers::WithinAbs(5.0, 1e-9));
            CHECK(edit.caret->string == 3);
        }
        CHECK(edit.selected_keyframes.empty());
    }

    click(fixture.controller, g_holding_tail_x, g_string_3_y, g_alt);
    CHECK(currentChart(fixture.controller) == original);
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before);
}

// A TRANSITION WITH NO FOCUS OF ITS OWN KEEPS A SELECTION THE CHART STILL HOLDS, and the end's own
// statement is no exception: the transition's repair asks whether the chart holds what each key
// NAMES, not whether a landing at its slot would address it — a slide-out chip is a real object no
// landing reaches. A chart transition selects what it changed instead, so the repair is what
// answers for an edit off the timeline, the output gain here.
TEST_CASE("A transition with no focus keeps a selected end statement", "[core][chart]")
{
    KeyframeFixture fixture{makeSlideOutGlideChart()};

    click(fixture.controller, g_ring_end_x, g_string_3_y);
    fixture.controller.onRowObjectStepRequested(false, false);
    const std::vector<ChartKeyframeRef> selected =
        publishedState(fixture.view).chart_edit.selected_keyframes;
    REQUIRE(selected.size() == 1);
    fixture.controller.onOutputGainChanged(-12.0);

    // Both directions of the transition leave the glide and its slide-out exactly as authored, so
    // the key goes on naming the statement it named.
    fixture.controller.onUndoRequested();
    CHECK(common::core::endStatedFretOrNull(currentChart(fixture.controller).notes[0]) != nullptr);
    CHECK(publishedState(fixture.view).chart_edit.selected_keyframes == selected);
    fixture.controller.onRedoRequested();
    CHECK(publishedState(fixture.view).chart_edit.selected_keyframes == selected);
}

// The selection rule wins where it applies: with the slide-out selected — reached from the caret at
// its own slot by ONE Shift+Tab, the walk stepping the statement before the head that slot would
// otherwise answer with — a digit is a RETYPE of that keyframe and places nothing.
TEST_CASE("A digit on a selected slide-out retypes it", "[core][chart]")
{
    KeyframeFixture fixture{makeSlideOutGlideChart()};

    click(fixture.controller, g_ring_end_x, g_string_3_y);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.empty());
    fixture.controller.onRowObjectStepRequested(false, false);
    REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);

    fixture.controller.onChartFretDigitTyped(7);
    const common::core::Chart retyped = currentChart(fixture.controller);
    REQUIRE(retyped.notes.size() == 1);
    REQUIRE(retyped.notes[0].keyframes.size() == 2);
    CHECK(retyped.notes[0].keyframes[1].offset == common::core::Fraction{8});
    CHECK(retyped.notes[0].keyframes[1].fret == 7);
    CHECK(retyped.notes[0].sustain == common::core::Fraction{8});
}

// A SILENCED SLIDE-OUT LINGERS IN FOCUS AND GOES ON LEAVE, like every other silent point. The
// charter's report: a slide-out to 6 on a fret-5 ring, then a 6 typed one step before the end. The
// new point travels, so it stands — and it leaves the slide-out falling toward the fret the path
// now holds. That slide-out draws its chip and can be reached, so nothing takes it while the note
// is in focus; the focus-leave sweep does, with no history entry, because none ever held it.
TEST_CASE("A point typed before a slide-out silences it until focus leaves", "[core][chart]")
{
    common::core::Chart falling;
    falling.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote note =
        makeTestNote({.measure = 2, .beat = 1}, 3, 5, common::core::Fraction{8});
    common::core::setSlideOut(note, 6);
    falling.notes = {std::move(note)};
    KeyframeFixture fixture{std::move(falling)};
    const common::core::Chart original = currentChart(fixture.controller);
    const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

    // One beat inside the six-second end, at the fixture geometry's 20 px/s: seven beats along the
    // ring, where the ring digit states a point on the path.
    constexpr float one_beat_inside_x{110.0f};
    click(fixture.controller, one_beat_inside_x, g_string_3_y);
    fixture.controller.onChartRingDigitTyped(6);

    const common::core::Chart stated = currentChart(fixture.controller);
    REQUIRE(stated.notes.size() == 1);
    REQUIRE(stated.notes[0].keyframes.size() == 2);
    CHECK(stated.notes[0].keyframes[0].offset == common::core::Fraction{7});
    CHECK(stated.notes[0].keyframes[0].fret == 6);
    // The silenced slide-out is still there, with the ring it pins.
    CHECK(stated.notes[0].sustain == common::core::Fraction{8});
    CHECK(common::core::endStatedFretOrNull(stated.notes[0]) != nullptr);
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before + 1);

    // Focus leaves for another string: the sweep takes the slide-out and leaves the point that
    // travels.
    click(fixture.controller, g_onset_x, g_string_2_y);
    const common::core::Chart swept = currentChart(fixture.controller);
    REQUIRE(swept.notes.size() == 1);
    REQUIRE(swept.notes[0].keyframes.size() == 1);
    CHECK(swept.notes[0].keyframes[0].offset == common::core::Fraction{7});
    CHECK(swept.notes[0].sustain == common::core::Fraction{8});
    CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before + 1);

    fixture.controller.onUndoRequested();
    CHECK(currentChart(fixture.controller) == original);
}

// `Alt+Insert` ON A TAIL types the fret in force at the caret: the digit route, digit supplied.
// Inside a ring that is the silent point typing the note's own fret makes — selected, and gone when
// its note leaves focus — and at the ring's END it is the end statement at that fret, which a digit
// then retypes into a slide-out that travels.
TEST_CASE("Alt+Insert on a tail states the fret in force", "[core][chart]")
{
    SECTION("inside a ring it plants the silent point")
    {
        KeyframeFixture fixture;
        const common::core::Chart original = currentChart(fixture.controller);
        const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

        // Six beats in, where the glide holds its junction's 9.
        click(fixture.controller, g_holding_tail_x, g_string_3_y);
        fixture.controller.onRingPointInsertRequested();

        const common::core::Chart planted = currentChart(fixture.controller);
        REQUIRE(planted.notes.size() == 1);
        REQUIRE(planted.notes[0].keyframes.size() == 2);
        CHECK(planted.notes[0].keyframes[1].offset == common::core::Fraction{6});
        CHECK(planted.notes[0].keyframes[1].fret == 9);
        CHECK(
            publishedState(fixture.view).chart_edit.selected_keyframes ==
            (std::vector<ChartKeyframeRef>{
                ChartKeyframeRef{.note_index = 0, .keyframe_index = 1}
            }));
        // Authoring state: no history entry, and gone when the note leaves focus.
        CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before);
        click(fixture.controller, g_onset_x, g_string_2_y);
        CHECK(currentChart(fixture.controller) == original);
    }

    SECTION("at the ring's end it states the end, and a digit retypes it into a slide-out")
    {
        KeyframeFixture fixture;
        const common::core::Chart original = currentChart(fixture.controller);

        click(fixture.controller, g_ring_end_x, g_string_3_y);
        fixture.controller.onRingPointInsertRequested();

        const common::core::Chart stated = currentChart(fixture.controller);
        REQUIRE(stated.notes.size() == 1);
        CHECK(stated.notes[0].sustain == original.notes[0].sustain);
        const int* const in_force = common::core::endStatedFretOrNull(stated.notes[0]);
        REQUIRE(in_force != nullptr);
        if (in_force != nullptr)
        {
            CHECK(*in_force == 9);
        }

        // The statement is selected, so the digit that follows retypes it rather than placing the
        // head the same slot's bare digit would.
        fixture.controller.onChartFretDigitTyped(3);
        const common::core::Chart retyped = currentChart(fixture.controller);
        REQUIRE(retyped.notes.size() == 1);
        const int* const slides_out_toward = common::core::endStatedFretOrNull(retyped.notes[0]);
        REQUIRE(slides_out_toward != nullptr);
        if (slides_out_toward != nullptr)
        {
            CHECK(*slides_out_toward == 3);
        }
    }

    SECTION("a statement already standing at the end is selected, not doubled")
    {
        KeyframeFixture fixture{makeSlideOutGlideChart()};
        const common::core::Chart original = currentChart(fixture.controller);

        click(fixture.controller, g_ring_end_x, g_string_3_y);
        REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.empty());
        fixture.controller.onRingPointInsertRequested();

        CHECK(currentChart(fixture.controller) == original);
        CHECK(
            publishedState(fixture.view).chart_edit.selected_keyframes ==
            (std::vector<ChartKeyframeRef>{
                ChartKeyframeRef{.note_index = 0, .keyframe_index = 1}
            }));
    }

    SECTION("a slot no ring reaches does what Insert does")
    {
        KeyframeFixture fixture;
        const common::core::Chart original = currentChart(fixture.controller);

        // The empty string-2 lane: nothing rings there, so the key places a head at the fret in
        // force, which on a string that never sounded is the open string.
        click(fixture.controller, g_holding_tail_x, g_string_2_y);
        fixture.controller.onRingPointInsertRequested();
        const common::core::Chart placed = currentChart(fixture.controller);
        REQUIRE(placed.notes.size() == 2);
        CHECK(placed.notes[0] == original.notes[0]);
        CHECK(placed.notes[1].position == common::core::GridPosition{.measure = 3, .beat = 3});
        CHECK(placed.notes[1].string == 2);
        CHECK(placed.notes[1].fret == 0);

        // On a head no ring ends at, it selects the head and edits nothing.
        click(fixture.controller, g_onset_x, g_string_3_y);
        fixture.controller.onRingPointInsertRequested();
        CHECK(currentChart(fixture.controller) == placed);
        CHECK(
            publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{0});
    }
}

// THE SHIFT SLIDE IN ONE KEY: at the end of a ring abutting a head at the fret in force, the
// statement `Alt+Insert` writes names the stop that head is struck at, at the same instant, which
// is what the chart reads as an ARRIVAL rather than a slide-out.
TEST_CASE("Alt+Insert at an abutting end states the arrival", "[core][chart]")
{
    // A fret-9 ring ending exactly on a fret-9 head of the same string: the glide's junction holds
    // 9 to the end, so the fret in force there IS the landing's own stop.
    common::core::Chart abutting = makeGlideChart();
    abutting.notes.push_back(
        makeTestNote({.measure = 4, .beat = 1}, 3, 9, common::core::Fraction{4}));
    KeyframeFixture fixture{std::move(abutting)};

    click(fixture.controller, g_ring_end_x, g_string_3_y);
    fixture.controller.onRingPointInsertRequested();

    const common::core::Chart stated = currentChart(fixture.controller);
    REQUIRE(stated.notes.size() == 2);
    const common::core::ChartConnections connections =
        common::core::chartConnections(stated.notes, fixture.controller.session().song().tempo_map);
    REQUIRE(connections.arrives_into.size() == 2);
    CHECK(connections.arrives_into[0]);
    // The relation, not a stored kind: the statement is the ordinary end statement at fret 9.
    const int* const arrival = common::core::endStatedFretOrNull(stated.notes[0]);
    REQUIRE(arrival != nullptr);
    if (arrival != nullptr)
    {
        CHECK(*arrival == 9);
    }
}

// `Insert` on a string row is the bare digit's route with the digit supplied — the fret already in
// force on the string at the caret — settled in its own keystroke: a head on an empty slot and at a
// ring's end, the cut inside a ring, and the head under the caret selected rather than edited.
TEST_CASE("Insert at the caret states a note at the fret in force", "[core][chart]")
{
    // Half a beat past the glide's ring end, on its own string.
    constexpr float past_ring_end_x{130.0f};

    SECTION("an empty string gives the open string")
    {
        KeyframeFixture fixture;
        click(fixture.controller, g_holding_tail_x, g_string_2_y);
        fixture.controller.onInsertAtCaretRequested();
        const common::core::Chart placed = currentChart(fixture.controller);
        REQUIRE(placed.notes.size() == 2);
        CHECK(placed.notes[1].position == common::core::GridPosition{.measure = 3, .beat = 3});
        CHECK(placed.notes[1].string == 2);
        CHECK(placed.notes[1].fret == 0);
        CHECK(
            publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{1});
    }

    SECTION("at a ring's end, the head at the last stop, the ring untouched")
    {
        KeyframeFixture fixture;
        const common::core::Chart original = currentChart(fixture.controller);
        click(fixture.controller, g_ring_end_x, g_string_3_y);
        fixture.controller.onInsertAtCaretRequested();
        const common::core::Chart placed = currentChart(fixture.controller);
        REQUIRE(placed.notes.size() == 2);
        CHECK(placed.notes[0] == original.notes[0]);
        CHECK(placed.notes[1].position == common::core::GridPosition{.measure = 4, .beat = 1});
        CHECK(placed.notes[1].fret == 9);
    }

    SECTION("past a slide-out, the last pitched stop rather than the slide's target")
    {
        KeyframeFixture fixture{makeSlideOutGlideChart()};
        click(fixture.controller, past_ring_end_x, g_string_3_y);
        fixture.controller.onInsertAtCaretRequested();
        const common::core::Chart placed = currentChart(fixture.controller);
        REQUIRE(placed.notes.size() == 2);
        CHECK(placed.notes[1].string == 3);
        CHECK(placed.notes[1].fret == 9);
    }

    SECTION("inside a ring, the cut at the stop the path holds there")
    {
        KeyframeFixture fixture;
        const common::core::Chart original = currentChart(fixture.controller);
        const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();
        click(fixture.controller, g_holding_tail_x, g_string_3_y);
        fixture.controller.onInsertAtCaretRequested();
        const common::core::Chart cut = currentChart(fixture.controller);
        REQUIRE(cut.notes.size() == 2);
        CHECK(cut.notes[0].sustain == common::core::Fraction{6});
        CHECK(cut.notes[0].keyframes == original.notes[0].keyframes);
        CHECK(cut.notes[1].position == common::core::GridPosition{.measure = 3, .beat = 3});
        CHECK(cut.notes[1].fret == 9);
        CHECK(cut.notes[1].sustain == common::core::Fraction{2});
        CHECK(cut.notes[1].attack == common::core::NoteAttack::Pick);
        CHECK(
            publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{1});
        CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before + 1);
        CHECK_FALSE(publishedState(fixture.view).chart_edit.pending_fret.has_value());
    }

    SECTION("on a head, it selects the head and edits nothing")
    {
        KeyframeFixture fixture;
        // Delete then undo leaves the caret armed over the restored head, which the transition
        // selects as the change it brought back.
        click(fixture.controller, g_onset_x, g_string_3_y);
        fixture.controller.onSelectionDeleteRequested();
        fixture.controller.onUndoRequested();
        const common::core::Chart restored = currentChart(fixture.controller);
        REQUIRE(restored.notes.size() == 1);
        REQUIRE(
            publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{0});
        const std::size_t entries_before = publishedState(fixture.view).undo_history.labels.size();

        fixture.controller.onInsertAtCaretRequested();
        CHECK(currentChart(fixture.controller) == restored);
        CHECK(
            publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{0});
        CHECK(publishedState(fixture.view).undo_history.labels.size() == entries_before);
    }
}

// A SCRAPE has no junction to divide, so the cut refuses: a bare digit shows it through the pending
// entry's red box at the cut's slot, while `Insert`, settled in its own keystroke, says nothing.
TEST_CASE("A cut inside a scrape refuses", "[core][chart]")
{
    common::core::Chart scraping;
    scraping.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote scrape =
        makeTestNote({.measure = 2, .beat = 1}, 3, 9, common::core::Fraction{8});
    scrape.attack = common::core::NoteAttack::PickSlide;
    scrape.keyframes = {common::core::Keyframe{.offset = common::core::Fraction{4}, .fret = 3}};
    common::core::setSlideOut(scrape, 12);
    scraping.notes = {std::move(scrape)};
    KeyframeFixture fixture{std::move(scraping)};
    const common::core::Chart original = currentChart(fixture.controller);
    click(fixture.controller, g_travel_tail_x, g_string_3_y);

    SECTION("a digit leaves a red box")
    {
        fixture.controller.onChartFretDigitTyped(5);
        CHECK(currentChart(fixture.controller) == original);
        const std::optional<ChartPendingFretViewState>& pending =
            publishedState(fixture.view).chart_edit.pending_fret;
        REQUIRE(pending.has_value());
        if (pending.has_value())
        {
            CHECK(pending->text == "5");
            CHECK_FALSE(pending->valid);
            const auto* const slot = std::get_if<ChartSlotViewState>(&pending->at);
            REQUIRE(slot != nullptr);
            if (slot != nullptr)
            {
                CHECK_THAT(slot->seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));
                CHECK(slot->string == 3);
            }
        }
    }

    SECTION("Insert is silent")
    {
        fixture.controller.onInsertAtCaretRequested();
        CHECK(currentChart(fixture.controller) == original);
        CHECK_FALSE(publishedState(fixture.view).chart_edit.pending_fret.has_value());
    }
}

// THE RING PLANE AT A RING'S END: `Alt`+digit states the end statement where the bare digit would
// place the next head, addresses a statement already standing there rather than doubling it, and
// where no ring reaches the slot does exactly what the bare digit does.
TEST_CASE("A ring digit at a ring's end states the end statement", "[core][chart]")
{
    SECTION("a ring that simply ends takes a slide-out")
    {
        KeyframeFixture fixture;
        const common::core::Chart original = currentChart(fixture.controller);
        click(fixture.controller, g_ring_end_x, g_string_3_y);
        fixture.controller.onChartRingDigitTyped(3);
        const common::core::Chart stated = currentChart(fixture.controller);
        REQUIRE(stated.notes.size() == 1);
        CHECK(stated.notes[0].sustain == original.notes[0].sustain);
        const int* const slides_out_toward = common::core::endStatedFretOrNull(stated.notes[0]);
        REQUIRE(slides_out_toward != nullptr);
        if (slides_out_toward != nullptr)
        {
            CHECK(*slides_out_toward == 3);
        }
        CHECK(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);
    }

    SECTION("a statement already standing is selected and retyped")
    {
        KeyframeFixture fixture{makeSlideOutGlideChart()};
        click(fixture.controller, g_ring_end_x, g_string_3_y);
        REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.empty());
        fixture.controller.onChartRingDigitTyped(7);
        const common::core::Chart retyped = currentChart(fixture.controller);
        REQUIRE(retyped.notes.size() == 1);
        REQUIRE(retyped.notes[0].keyframes.size() == 2);
        CHECK(retyped.notes[0].keyframes[1].offset == common::core::Fraction{8});
        CHECK(retyped.notes[0].keyframes[1].fret == 7);
        CHECK(
            publishedState(fixture.view).chart_edit.selected_keyframes ==
            (std::vector<ChartKeyframeRef>{
                ChartKeyframeRef{.note_index = 0, .keyframe_index = 1}
            }));
    }

    SECTION("one selected head where a ring ends names that ring's end")
    {
        KeyframeFixture fixture{makeAbuttingStringChart(false)};
        // Arming on the abutting head selects it: the operand is that one head.
        click(fixture.controller, g_ring_end_x, g_string_3_y);
        REQUIRE(
            publishedState(fixture.view).chart_edit.selected_notes == std::vector<std::size_t>{1});
        fixture.controller.onChartRingDigitTyped(3);
        const common::core::Chart stated = currentChart(fixture.controller);
        REQUIRE(stated.notes.size() == 2);
        // The head keeps its fret; the ring's end states the head's own stop, an arrival.
        CHECK(stated.notes[1].fret == 3);
        const int* const arrival = common::core::endStatedFretOrNull(stated.notes[0]);
        REQUIRE(arrival != nullptr);
        if (arrival != nullptr)
        {
            CHECK(*arrival == 3);
        }
        const common::core::ChartConnections connections = common::core::chartConnections(
            stated.notes, fixture.controller.session().song().tempo_map);
        REQUIRE(connections.arrives_into.size() == 2);
        CHECK(connections.arrives_into[0]);
    }

    SECTION("no ring at the slot is the bare digit")
    {
        KeyframeFixture fixture;
        click(fixture.controller, g_holding_tail_x, g_string_2_y);
        fixture.controller.onChartRingDigitTyped(5);
        const common::core::Chart placed = currentChart(fixture.controller);
        REQUIRE(placed.notes.size() == 2);
        CHECK(placed.notes[1].string == 2);
        CHECK(placed.notes[1].fret == 5);
    }
}

// The ring plane's redirect is a property of ONE slot, so over a wider selection it is ignored and
// the key retypes everything selected, exactly as the bare digit would.
TEST_CASE("A ring digit over a wider selection retypes it", "[core][chart]")
{
    SECTION("two heads, one of them where a ring ends")
    {
        common::core::Chart chart = makeAbuttingStringChart(false);
        chart.notes.push_back(makeTestNote({.measure = 4, .beat = 1}, 2, 3));
        std::ranges::sort(chart.notes, common::core::chartNoteOrderLess);
        KeyframeFixture fixture{std::move(chart)};
        const common::core::Chart original = currentChart(fixture.controller);

        // A marquee over both heads at 6.0s, on strings 2 and 3, pressed on empty lane past them.
        fixture.controller.onChartPointerDown(pointerEvent(150.0f, 199.0f));
        fixture.controller.onChartPointerDrag(pointerEvent(105.0f, 121.0f));
        fixture.controller.onChartPointerUp(pointerEvent(105.0f, 121.0f));
        REQUIRE(publishedState(fixture.view).chart_edit.selected_notes.size() == 2);
        REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.empty());

        fixture.controller.onChartRingDigitTyped(7);
        const common::core::Chart retyped = currentChart(fixture.controller);
        REQUIRE(retyped.notes.size() == 3);
        CHECK(retyped.notes[0].keyframes == original.notes[0].keyframes);
        CHECK(retyped.notes[1].fret == 7);
        CHECK(retyped.notes[2].fret == 7);
    }

    SECTION("a keyframe")
    {
        KeyframeFixture fixture;
        click(fixture.controller, g_junction_x, g_string_3_y);
        REQUIRE(publishedState(fixture.view).chart_edit.selected_keyframes.size() == 1);
        fixture.controller.onChartRingDigitTyped(7);
        const common::core::Chart retyped = currentChart(fixture.controller);
        REQUIRE(retyped.notes.size() == 1);
        REQUIRE(retyped.notes[0].keyframes.size() == 1);
        CHECK(retyped.notes[0].keyframes[0].fret == 7);
        CHECK(retyped.notes[0].fret == 5);
    }
}

} // namespace rock_hero::editor::core
