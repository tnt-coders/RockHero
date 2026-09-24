#include <algorithm>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The shared fixture's measure-3 note, the one most scenarios here move: string 1 at measure 3
// beat 1 (x = 80 at the geometry's 20 px/s, y = 220), sounding fret 7. Nothing later sounds on that
// string, so a run of right steps is bounded by nothing and a run back lands byte-for-byte where it
// started.
constexpr float g_measure_3_x{80.0f};
constexpr float g_string_1_y{220.0f};
constexpr int g_measure_3_fret{7};

// The lone note of the meter-change scenario's own chart, which states its own stream.
constexpr int g_crossing_fret{3};

// The controller every move-gesture scenario starts from, built exactly as the duration verb's
// fixture is: the shared chart through the normal open route at the quarter-note grid.
struct MoveFixture
{
    FakeTransport transport{};
    ConfigurableSongAudio audio{};
    FakeProjectServices project_services{};
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
        }
    };
    FakeEditorView view{};

    MoveFixture()
    {
        controller.attachView(view);
    }

    // Loads the shared chart fixture, or a caller's own stream and meter when a scenario needs
    // them; false when the open route did not produce an arrangement.
    [[nodiscard]] bool load(
        common::core::Chart chart = makeTestChart(),
        std::optional<common::core::TempoMap> tempo_map = std::nullopt)
    {
        return loadChartArrangement(
            controller, project_services, audio, {}, std::move(chart), std::move(tempo_map));
    }

    // One press of the move verb.
    void step(ChartStepDirection direction)
    {
        controller.onSelectionMoveRequested(direction);
    }

    // How many entries the undo stack holds, the count a gesture must not grow past one.
    // Assertion-free on purpose: every caller compares the result inside a CHECK, and a nested
    // Catch2 assertion inside another assertion's expression is exactly the shape to avoid.
    [[nodiscard]] std::size_t undoEntryCount() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr ? state->undo_history.labels.size() : 0;
    }

    // The newest entry's label, or empty when the stack holds none: WHICH entry survived a burst,
    // which a count alone cannot say. Assertion-free for the reason above.
    [[nodiscard]] std::string undoTopLabel() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        if (state == nullptr || state->undo_history.labels.empty())
        {
            return {};
        }
        return state->undo_history.labels.back();
    }

    // The chart as it stands, copied so a scenario can compare a later chart against it field for
    // field — which is what an undo round trip has to prove.
    [[nodiscard]] common::core::Chart currentChart() const
    {
        const common::core::Chart* const chart = chartOrNull(controller);
        return chart != nullptr ? *chart : common::core::Chart{};
    }

    // The onset of the note sounding a fret, or an empty position when there is none. Keyed by
    // FRET because the note under test moves its slot, which is the very thing being asserted, and
    // every fixture chart here gives its notes distinct frets. Assertion-free for the reason above:
    // a missing note reads as the origin, which no scenario expects.
    [[nodiscard]] common::core::GridPosition onsetOfFret(int fret) const
    {
        const common::core::Chart* const chart = chartOrNull(controller);
        if (chart == nullptr)
        {
            return {};
        }
        for (const common::core::ChartNote& note : chart->notes)
        {
            if (note.fret == fret)
            {
                return note.position;
            }
        }
        return {};
    }
};

// The shared glide chart's junction, at the geometry's 20 px/s: its linked head draws at 4.0s on
// string 3.
constexpr float g_junction_x{80.0f};
constexpr float g_string_3_y{140.0f};

// The silent-point scenarios' own marks and frets. Both notes of the chart below start at measure 2
// beat 1 (2.0s, x = 40), and the host's tail two beats in draws at 3.0s (x = 60).
constexpr float g_measure_2_x{40.0f};
constexpr float g_host_tail_x{60.0f};
constexpr int g_victim_fret{3};
constexpr int g_host_fret{5};
// What the victim is retyped to where a scenario needs the first edit to be a retype rather than a
// delete: two digits would widen under the 24-fret cap, and 9 cannot, so the entry settles in the
// one keystroke.
constexpr int g_retyped_fret{9};

// The chart the silent-point scenarios run on. The VICTIM on string 1 takes the first edit, whose
// undo entry everything below must leave alone. The HOST rings eight beats on string 3 sounding a
// plain fret with no keyframes, so its whole path holds that fret — and a ring digit typed at it
// two beats in plants a point that says nothing, the edit whose written diff is empty.
[[nodiscard]] common::core::Chart makeSilentPointChart()
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, g_victim_fret),
        makeTestNote({.measure = 2, .beat = 1}, 3, g_host_fret, common::core::Fraction{8}),
    };
    return chart;
}

// THE PARKING FIGURE. A four-beat ring on string 3 whose statement at its end IS the slide-out
// (fret 9 where the ring stops), with the string struck again four beats past it. So the slide-out
// stands exactly ONE whole-note step from the only thing that bounds a ring's end — the next head
// on its own string — and the scenario's first right press reaches that head. Its onset is measure
// 2 beat 1, so at the geometry's 20 px/s the slide-out draws at 4.0s (x = 80, g_junction_x) like
// the junction above, and the next head a whole measure further at 6.0s: far enough that a click on
// the slide-out is nowhere near the 25px head, which is why the step is a whole note and not a
// quarter.
[[nodiscard]] common::core::Chart makeParkedSlideOutChart()
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    common::core::ChartNote glide =
        makeTestNote({.measure = 2, .beat = 1}, 3, 5, common::core::Fraction{4});
    glide.keyframes = {common::core::Keyframe{.offset = common::core::Fraction{4}, .fret = 9}};
    chart.notes = {std::move(glide), makeTestNote({.measure = 4, .beat = 1}, 3, 3)};
    return chart;
}

// Whether any note in the chart carries a point at an offset stating a fret. Found by offset rather
// than by index, because a keyframe list is offset-ordered and a step can carry a point past its
// neighbours.
[[nodiscard]] bool hasPoint(
    const common::core::Chart& chart, common::core::Fraction offset, int fret)
{
    return std::ranges::any_of(chart.notes, [offset, fret](const common::core::ChartNote& note) {
        return std::ranges::any_of(
            note.keyframes, [offset, fret](const common::core::Keyframe& point) {
                return point.offset == offset && point.fret == fret;
            });
    });
}

} // namespace

// The gesture's headline property, the duration verb's own made one axis over: a run of presses is
// ONE undo entry, so one Ctrl+Z puts the note back where the run found it rather than walking the
// presses back one at a time.
TEST_CASE("A move gesture is one undo entry and one undo restores it", "[core][chart]")
{
    MoveFixture fixture;
    REQUIRE(fixture.load());

    click(fixture.controller, g_measure_3_x, g_string_1_y);
    const common::core::Chart original = fixture.currentChart();
    const std::size_t entries_before = fixture.undoEntryCount();

    for (int index = 0; index < 3; ++index)
    {
        fixture.step(ChartStepDirection::Right);
    }
    // Three quarter-note steps off measure 3 beat 1, and each press found its operand — which it
    // can only do because the step before it re-pointed the selection at the moved note.
    CHECK(
        fixture.onsetOfFret(g_measure_3_fret) ==
        common::core::GridPosition{.measure = 3, .beat = 4, .offset = {}});
    CHECK(fixture.undoEntryCount() == entries_before + 1);

    fixture.controller.onUndoRequested();
    CHECK(fixture.currentChart() == original);
    CHECK(fixture.undoEntryCount() == entries_before + 1);
}

// Why the run keeps a step LIST rather than one summed delta: a time step is the placement quantum
// scaled by the meter where the run has REACHED, so the presses on either side of a signature
// change are worth different amounts. Summing the first press's size would put the whole run on the
// 4/4 lattice it has already left.
TEST_CASE("A move gesture steps at each press's own meter", "[core][chart]")
{
    MoveFixture fixture;
    // One 4/4 measure, then 6/8 from measure 3 on, metronome-linear at 120 BPM: a quarter note is
    // one beat in 4/4 and two in 6/8, so a quarter-note grid step is worth 1 beat before the
    // boundary and 2 beats after it.
    common::core::TempoMap map{
        std::vector{
            common::core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
            common::core::TimeSignatureChange{.measure = 3, .numerator = 6, .denominator = 8},
        },
        std::vector{
            common::core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            common::core::BeatAnchor{.measure = 21, .beat = 1, .seconds = 31.0},
        },
    };
    // One note on the last beat of the 4/4 measure — 3.5s, x = 70 — so the very first press carries
    // it across the boundary.
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = {makeTestNote({.measure = 2, .beat = 4}, 1, g_crossing_fret)};
    const bool loaded = fixture.load(std::move(chart), std::move(map));
    REQUIRE(loaded);

    click(fixture.controller, 70.0f, g_string_1_y);
    const std::size_t entries_before = fixture.undoEntryCount();
    const common::core::Chart original = fixture.currentChart();

    fixture.step(ChartStepDirection::Right);
    CHECK(
        fixture.onsetOfFret(g_crossing_fret) ==
        common::core::GridPosition{.measure = 3, .beat = 1, .offset = {}});

    // Now inside 6/8, where the same quarter-note quantum is two beats. A summed delta would have
    // sized both later presses at the 4/4 measure's one beat and landed on beat 3.
    fixture.step(ChartStepDirection::Right);
    fixture.step(ChartStepDirection::Right);
    CHECK(
        fixture.onsetOfFret(g_crossing_fret) ==
        common::core::GridPosition{.measure = 3, .beat = 5, .offset = {}});
    CHECK(fixture.undoEntryCount() == entries_before + 1);

    fixture.controller.onUndoRequested();
    CHECK(fixture.currentChart() == original);
}

// Every commit point that closes the duration gesture closes this one too, because both rest on the
// same proof: after a caret move a press starts a NEW run from where the last one left off, and
// pushes its own entry.
TEST_CASE("A caret move ends the move gesture", "[core][chart]")
{
    MoveFixture fixture;
    REQUIRE(fixture.load());

    click(fixture.controller, g_measure_3_x, g_string_1_y);
    fixture.step(ChartStepDirection::Right);
    fixture.step(ChartStepDirection::Right);
    CHECK(
        fixture.onsetOfFret(g_measure_3_fret) ==
        common::core::GridPosition{.measure = 3, .beat = 3, .offset = {}});
    const std::size_t entries_during = fixture.undoEntryCount();

    // Right onto the empty next grid slot, then back onto the note.
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Left, false);

    fixture.step(ChartStepDirection::Right);
    CHECK(
        fixture.onsetOfFret(g_measure_3_fret) ==
        common::core::GridPosition{.measure = 3, .beat = 4, .offset = {}});
    // The entry count is what separates a continued run from a fresh one: a continued run would
    // have replaced the entry it already owned.
    CHECK(fixture.undoEntryCount() == entries_during + 1);
}

// A run that replays back to where it began describes no edit at all, so it ends at the duration
// gesture's own ending: the entry its first press pushed is taken back out, and the chart is
// byte-identical to what the run found — the selection with it, since the landing IS the start.
// The caret riding the lone note rides that last step home too: a retirement moves the chart
// exactly as a replaced entry does, and once left the caret one step out.
TEST_CASE("A reversed move gesture ends at the origin and leaves no entry", "[core][chart]")
{
    MoveFixture fixture;
    REQUIRE(fixture.load());

    click(fixture.controller, g_measure_3_x, g_string_1_y);
    const common::core::Chart original = fixture.currentChart();
    const std::size_t entries_before = fixture.undoEntryCount();

    // The published caret's time, asked through one guarded read so each assertion below is
    // provably about a caret that exists. Measure 3 beat 1 is 4.0s and beat 2 is 4.5s.
    const auto caret_seconds = [&fixture] {
        const EditorViewState* const state = stateOrNull(fixture.view.last_state);
        REQUIRE(state != nullptr);
        const std::optional<ChartCaretViewState>& caret = state->chart_edit.caret;
        REQUIRE(caret.has_value());
        return caret.has_value() ? caret->seconds : 0.0;
    };

    fixture.step(ChartStepDirection::Right);
    CHECK(
        fixture.onsetOfFret(g_measure_3_fret) ==
        common::core::GridPosition{.measure = 3, .beat = 2, .offset = {}});
    CHECK(fixture.undoEntryCount() == entries_before + 1);
    CHECK_THAT(caret_seconds(), Catch::Matchers::WithinAbs(4.5, 1e-9));

    fixture.step(ChartStepDirection::Left);
    CHECK(fixture.currentChart() == original);
    CHECK(fixture.undoEntryCount() == entries_before);
    CHECK_THAT(caret_seconds(), Catch::Matchers::WithinAbs(4.0, 1e-9));

    // The selection came back with the chart, which the next press is what proves: it has an
    // operand again, and moves the note the run started on.
    fixture.step(ChartStepDirection::Right);
    CHECK(
        fixture.onsetOfFret(g_measure_3_fret) ==
        common::core::GridPosition{.measure = 3, .beat = 2, .offset = {}});
    CHECK(fixture.undoEntryCount() == entries_before + 1);
}

// The keyframe-specific consequence of the burst, and the reason the run re-points at every step: a
// keyframe's identity IS its offset, so a step that did not re-key the selection would leave the
// next press with a key naming an offset nothing sits on — and the window's proof compares against
// that re-pointed key.
TEST_CASE("A keyframe move gesture re-points the selection at every step", "[core][chart]")
{
    MoveFixture fixture;
    REQUIRE(fixture.load(makeGlideChart()));

    click(fixture.controller, g_junction_x, g_string_3_y);
    const EditorViewState* const selected = stateOrNull(fixture.view.last_state);
    REQUIRE(selected != nullptr);
    if (selected != nullptr)
    {
        REQUIRE(selected->chart_edit.selected_keyframes.size() == 1);
    }
    const common::core::Chart original = fixture.currentChart();
    const std::size_t entries_before = fixture.undoEntryCount();

    for (int index = 0; index < 3; ++index)
    {
        fixture.step(ChartStepDirection::Right);
    }

    const common::core::Chart stepped = fixture.currentChart();
    REQUIRE(stepped.notes.size() == 1);
    if (stepped.notes.size() == 1)
    {
        REQUIRE(stepped.notes[0].keyframes.size() == 1);
        if (stepped.notes[0].keyframes.size() == 1)
        {
            // Three one-beat steps off offset 4, which only the third press can reach: each step
            // re-keyed the point, so the one after it still had an operand.
            CHECK(stepped.notes[0].keyframes[0].offset == common::core::Fraction{7});
            CHECK(stepped.notes[0].keyframes[0].fret == 9);
        }
        // The note the point rides kept its own slot and its ring throughout.
        CHECK(stepped.notes[0].position == original.notes[0].position);
        CHECK(stepped.notes[0].sustain == original.notes[0].sustain);
    }
    CHECK(fixture.undoEntryCount() == entries_before + 1);

    fixture.controller.onUndoRequested();
    CHECK(fixture.currentChart() == original);
}

// A RING'S END REACHING THE NEXT HEAD HAS ONE ANSWER, and this is the move verb giving it: the
// slide-out parks ON that head rather than refusing, exactly as a ring GROWN into it parks there.
// The gesture consequence is what the clamp has to earn — a press that cannot move must cost
// nothing to come back from, or the charter pays back an overshoot they never saw, press by press.
TEST_CASE("A move gesture parks a slide-out on the next head and banks nothing", "[core][chart]")
{
    MoveFixture fixture;
    REQUIRE(fixture.load(makeParkedSlideOutChart()));
    // A whole-note step, so one press spans the four beats to the head — see the figure above.
    fixture.controller.onGridNoteValueChangeRequested(common::core::Fraction{1});

    // The slide-out stands at the ring's END, which no landing addresses: the click arms the caret
    // on that slot and one Shift+Tab steps onto the statement itself.
    click(fixture.controller, g_junction_x, g_string_3_y);
    fixture.controller.onRowObjectStepRequested(false, false);
    {
        const EditorViewState* const selected = stateOrNull(fixture.view.last_state);
        REQUIRE(selected != nullptr);
        if (selected != nullptr)
        {
            REQUIRE(selected->chart_edit.selected_keyframes.size() == 1);
        }
    }
    const common::core::Chart original = fixture.currentChart();
    const std::size_t entries_before = fixture.undoEntryCount();

    // One step lands the slide-out exactly on the head, and the ring's end goes with it: the
    // slide-out completes on the head, which is what the store says the hands did.
    fixture.step(ChartStepDirection::Right);
    const common::core::Chart parked = fixture.currentChart();
    REQUIRE(parked.notes.size() == 2);
    if (parked.notes.size() == 2)
    {
        CHECK(parked.notes[0].sustain == common::core::Fraction{8});
        REQUIRE(parked.notes[0].keyframes.size() == 1);
        if (parked.notes[0].keyframes.size() == 1)
        {
            CHECK(parked.notes[0].keyframes[0].offset == common::core::Fraction{8});
            CHECK(parked.notes[0].keyframes[0].fret == 9);
        }
    }
    CHECK(fixture.undoEntryCount() == entries_before + 1);

    // Further presses that way do NOTHING VISIBLE: the clamp holds the slide-out on the head, so
    // the replay describes the plan the entry already holds and the press is not recorded at all.
    fixture.step(ChartStepDirection::Right);
    fixture.step(ChartStepDirection::Right);
    CHECK(fixture.currentChart() == parked);
    CHECK(fixture.undoEntryCount() == entries_before + 1);
    // And the selection still names the slide-out — at the offset the CLAMP landed it on, not the
    // one the delta arithmetic would have named, which nothing in the chart sits on.
    {
        const EditorViewState* const held = stateOrNull(fixture.view.last_state);
        REQUIRE(held != nullptr);
        if (held != nullptr)
        {
            CHECK(held->chart_edit.selected_keyframes.size() == 1);
        }
    }

    // One press back moves again, and it is the FIRST press back: the two no-op presses banked
    // nothing, so the run replays to its start, retires its entry, and the slide-out stands where
    // the charter found it.
    fixture.step(ChartStepDirection::Left);
    CHECK(fixture.currentChart() == original);
    CHECK(fixture.undoEntryCount() == entries_before);
    {
        const EditorViewState* const home = stateOrNull(fixture.view.last_state);
        REQUIRE(home != nullptr);
        if (home != nullptr)
        {
            CHECK(home->chart_edit.selected_keyframes.size() == 1);
        }
    }
}

// THE burst record's own law, and a data-loss regression: the record must not outlive the edit it
// names. An edit whose WRITTEN diff is empty — a point planted or stepped at the fret already in
// force — pushes no entry at all, and an entry-less edit moves the history position not at all, so
// a record left naming the PREVIOUS edit's entry still passes the ownership proof. The next press
// of a gesture then reconstructs its "pre-gesture" chart by reversing a stranger's plan, finds no
// operand there, and retires that stranger's entry — which took a deleted note's only way back.
TEST_CASE("A silent point edit hands the next move gesture no record", "[core][chart]")
{
    MoveFixture fixture;
    REQUIRE(fixture.load(makeSilentPointChart()));
    const common::core::Chart original = fixture.currentChart();
    const std::size_t entries_before = fixture.undoEntryCount();

    // The edit whose entry has to survive everything below.
    click(fixture.controller, g_measure_2_x, g_string_1_y);
    fixture.controller.onSelectionDeleteRequested();
    REQUIRE(fixture.currentChart().notes.size() == 1);
    REQUIRE(fixture.undoEntryCount() == entries_before + 1);
    CHECK(fixture.undoTopLabel() == "Delete Note");

    // A ring digit at the fret the ring already holds: the point stands in the chart as authoring
    // state, with no entry of its own.
    click(fixture.controller, g_host_tail_x, g_string_3_y);
    fixture.controller.onChartRingDigitTyped(g_host_fret);
    CHECK(hasPoint(fixture.currentChart(), common::core::Fraction{2}, g_host_fret));
    CHECK(fixture.undoEntryCount() == entries_before + 1);

    // The first press steps the point and writes nothing; the second is where a stale record
    // strikes, because it is the first press that could CONTINUE a run.
    fixture.step(ChartStepDirection::Right);
    CHECK(hasPoint(fixture.currentChart(), common::core::Fraction{3}, g_host_fret));
    CHECK(fixture.currentChart().notes.size() == 1);
    CHECK(fixture.undoEntryCount() == entries_before + 1);

    fixture.step(ChartStepDirection::Right);
    const common::core::Chart moved = fixture.currentChart();
    // The deleted note did not come back, and the entry that deleted it is still on top.
    REQUIRE(moved.notes.size() == 1);
    CHECK(hasPoint(moved, common::core::Fraction{4}, g_host_fret));
    CHECK(fixture.undoEntryCount() == entries_before + 1);
    CHECK(fixture.undoTopLabel() == "Delete Note");

    // And it is still the DELETE: one Ctrl+Z brings the note back. The point goes with it, because
    // undo replays written states and no entry ever held it.
    fixture.controller.onUndoRequested();
    CHECK(fixture.currentChart() == original);
}

// Not delete-specific: the victim is whatever the newest chart-notes edit was, so the same sequence
// over a RETYPE must leave the retyped fret standing and its entry intact.
TEST_CASE("A silent point edit leaves an earlier retype intact", "[core][chart]")
{
    MoveFixture fixture;
    REQUIRE(fixture.load(makeSilentPointChart()));
    const common::core::Chart original = fixture.currentChart();
    const std::size_t entries_before = fixture.undoEntryCount();

    click(fixture.controller, g_measure_2_x, g_string_1_y);
    fixture.controller.onChartFretDigitTyped(g_retyped_fret);
    const common::core::Chart retyped = fixture.currentChart();
    REQUIRE(retyped.notes.size() == 2);
    CHECK(retyped.notes[0].fret == g_retyped_fret);
    REQUIRE(fixture.undoEntryCount() == entries_before + 1);

    click(fixture.controller, g_host_tail_x, g_string_3_y);
    fixture.controller.onChartRingDigitTyped(g_host_fret);
    CHECK(hasPoint(fixture.currentChart(), common::core::Fraction{2}, g_host_fret));
    CHECK(fixture.undoEntryCount() == entries_before + 1);

    fixture.step(ChartStepDirection::Right);
    fixture.step(ChartStepDirection::Right);
    const common::core::Chart moved = fixture.currentChart();
    REQUIRE(moved.notes.size() == 2);
    CHECK(moved.notes[0].fret == g_retyped_fret);
    CHECK(moved.notes[1].keyframes.size() == 1);
    CHECK(fixture.undoEntryCount() == entries_before + 1);

    // One undo puts the old fret back, which only an entry still describing the retype can do.
    fixture.controller.onUndoRequested();
    CHECK(fixture.currentChart() == original);
}

// The duration verb reaches the record through the same gesture authority, so a burst started right
// after a silent point edit must push its OWN entry rather than reaching for the earlier one: two
// presses are one entry beside the delete's, and the two undo in their own order.
TEST_CASE("A sustain burst after a silent point edit keeps the earlier entry", "[core][chart]")
{
    MoveFixture fixture;
    REQUIRE(fixture.load(makeSilentPointChart()));
    const common::core::Chart original = fixture.currentChart();
    const std::size_t entries_before = fixture.undoEntryCount();

    click(fixture.controller, g_measure_2_x, g_string_1_y);
    fixture.controller.onSelectionDeleteRequested();
    click(fixture.controller, g_host_tail_x, g_string_3_y);
    fixture.controller.onChartRingDigitTyped(g_host_fret);
    REQUIRE(fixture.undoEntryCount() == entries_before + 1);

    fixture.controller.onChartSustainAdjustRequested(1);
    fixture.controller.onChartSustainAdjustRequested(1);
    const common::core::Chart grown = fixture.currentChart();
    REQUIRE(grown.notes.size() == 1);
    CHECK(grown.notes[0].sustain == common::core::Fraction{10});
    CHECK(fixture.undoEntryCount() == entries_before + 2);

    // The burst's own entry undoes the ring alone, and the delete is still a step of its own
    // underneath it.
    fixture.controller.onUndoRequested();
    const common::core::Chart shrunk = fixture.currentChart();
    REQUIRE(shrunk.notes.size() == 1);
    CHECK(shrunk.notes[0].sustain == common::core::Fraction{8});
    CHECK(fixture.undoEntryCount() == entries_before + 2);

    fixture.controller.onUndoRequested();
    CHECK(fixture.currentChart() == original);
}

// The technique toggle's reversal window reads the same record: a press after a silent point edit
// must reverse the entry ITS OWN press pushed, never the edit before it.
TEST_CASE("A technique toggle after a silent point edit reverses its own entry", "[core][chart]")
{
    MoveFixture fixture;
    REQUIRE(fixture.load(makeSilentPointChart()));
    const common::core::Chart original = fixture.currentChart();
    const std::size_t entries_before = fixture.undoEntryCount();

    click(fixture.controller, g_measure_2_x, g_string_1_y);
    fixture.controller.onSelectionDeleteRequested();
    click(fixture.controller, g_host_tail_x, g_string_3_y);
    fixture.controller.onChartRingDigitTyped(g_host_fret);
    REQUIRE(fixture.undoEntryCount() == entries_before + 1);

    // Onto the host's own head, which keeps its note in focus, so the point stands through both
    // presses below.
    click(fixture.controller, g_measure_2_x, g_string_3_y);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
    const common::core::Chart muted = fixture.currentChart();
    REQUIRE(muted.notes.size() == 1);
    CHECK(muted.notes[0].palm_mute);
    CHECK(fixture.undoEntryCount() == entries_before + 2);

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
    const common::core::Chart reverted = fixture.currentChart();
    REQUIRE(reverted.notes.size() == 1);
    CHECK_FALSE(reverted.notes[0].palm_mute);
    CHECK(fixture.undoEntryCount() == entries_before + 1);
    CHECK(fixture.undoTopLabel() == "Delete Note");

    fixture.controller.onUndoRequested();
    CHECK(fixture.currentChart() == original);
}

} // namespace rock_hero::editor::core
