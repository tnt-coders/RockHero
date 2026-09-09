#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
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

// The glide chart the keyframe scenario runs on: one pitched slide on string 3 whose junction sits
// four beats into an eight-beat ring, so three right steps still land the point strictly inside the
// ring and well short of the next path stop. Measure 2 beat 1 is 2.0s (x = 40, y = 140 on string 3)
// and the junction's linked head draws at 4.0s (x = 80).
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

constexpr float g_junction_x{80.0f};
constexpr float g_string_3_y{140.0f};

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

} // namespace rock_hero::editor::core
