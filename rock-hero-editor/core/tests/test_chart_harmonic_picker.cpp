#include <catch2/catch_approx.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/deferring_message_thread_scheduler.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The picker only stays visible under the DEFERRING scheduler: with the immediate one the window's
// wake fires inside the arming keystroke, which is production-correct and useless for pinning the
// armed state. Every scenario here builds its controller over that harness, so a press can be
// examined between the arm and the settle.
struct HarmonicPickerFixture
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

    explicit HarmonicPickerFixture(common::core::Chart chart = makeTestChart())
    {
        controller.attachView(view);
        // Hoisted out of the assertion: a Catch2 macro mentions its expression a second time, and
        // the never-run mention reads a moved-from operand that CI's use-after-move check sees.
        const bool loaded =
            loadChartArrangement(controller, project_services, audio, {}, std::move(chart));
        REQUIRE(loaded);
        static_cast<void>(pending.scheduler.runDelayed());
    }

    // The picker's published state, or nothing while none is armed.
    [[nodiscard]] const ChartPendingHarmonicViewState* picker() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        if (state == nullptr || !state->chart_edit.pending_harmonic.has_value())
        {
            return nullptr;
        }
        return &*state->chart_edit.pending_harmonic;
    }
};

} // namespace

// THE ONE AMBIGUOUS LABEL. The fixture's string-1 note carries fret 3, which names both the 7th
// partial's 2.669 and the 6th's 3.156, so `H` states nothing yet: it arms the picker with the
// nearest candidate, a second press cycles to the other, and the window's elapse commits whichever
// is showing as ONE undo entry.
TEST_CASE("The harmonic picker arms on the ambiguous label and cycles", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);

    // Nothing has committed: the picker states a provisional value exactly as a typed digit does.
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].fret == 3);
    CHECK_FALSE(chart->notes[0].harmonic_node.has_value());

    const ChartPendingHarmonicViewState* armed = fixture.picker();
    REQUIRE(armed != nullptr);
    if (armed != nullptr)
    {
        REQUIRE(armed->notes.size() == 1);
        const ChartPendingHarmonicNode& head = armed->notes.front();
        REQUIRE(head.nodes.size() == 2);
        // Ascending, and the NEAREST to the typed 3 is armed: 3.156 over 2.669.
        CHECK(head.nodes[0] == Catch::Approx(2.6687).margin(0.001));
        CHECK(head.nodes[1] == Catch::Approx(3.1564).margin(0.001));
        CHECK(head.chosen == 1);
    }

    // A second press CYCLES rather than reversing: no entry exists yet to reverse.
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);
    armed = fixture.picker();
    REQUIRE(armed != nullptr);
    if (armed != nullptr)
    {
        REQUIRE(armed->notes.size() == 1);
        CHECK(armed->notes.front().chosen == 0);
    }
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK_FALSE(chart->notes[0].harmonic_node.has_value());

    // The window elapsing settles and commits the candidate that was showing.
    const EditorViewState* const state = stateOrNull(fixture.view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();
    static_cast<void>(fixture.pending.scheduler.runDelayed());
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].fret == 0);
    const std::optional<double>& node = chart->notes[0].harmonic_node;
    REQUIRE(node.has_value());
    if (node.has_value())
    {
        CHECK(*node == Catch::Approx(2.6687).margin(0.001));
    }
    CHECK(fixture.picker() == nullptr);

    // ONE entry for the whole press, and undo restores the fret the touch consumed.
    CHECK(state->undo_history.labels.size() == entries_before + 1);
    fixture.controller.onUndoRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].fret == 3);
    CHECK_FALSE(chart->notes[0].harmonic_node.has_value());
}

// Every other label settles in the SAME keystroke through the pending entry's own disposition rule
// — the picker arms on ambiguity, not on the verb — so fret 5 states its node with one press and
// no window to wait out.
TEST_CASE("An unambiguous harmonic press settles in one keystroke", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    // The string-2 note at measure 2 beat 1 carries fret 5, the 4th partial's label.
    click(fixture.controller, 40.0f, 180.0f);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);

    CHECK(fixture.picker() == nullptr);
    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[1].fret == 0);
    const std::optional<double>& node = chart->notes[1].harmonic_node;
    REQUIRE(node.has_value());
    if (node.has_value())
    {
        CHECK(*node == Catch::Approx(4.98).margin(0.001));
    }
}

// Esc COMMITS the shown candidate rather than discarding the press: the discard rung is
// invalid-only, and a candidate is always valid — it was filtered by the rule authority before it
// was ever offered.
TEST_CASE("Esc commits the harmonic candidate on show", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);
    REQUIRE(fixture.picker() != nullptr);

    fixture.controller.onChartEscapePressed();
    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].fret == 0);
    const std::optional<double>& node = chart->notes[0].harmonic_node;
    REQUIRE(node.has_value());
    if (node.has_value())
    {
        CHECK(*node == Catch::Approx(3.1564).margin(0.001));
    }
}

// ONE picker per press over a chord, and one plan: ambiguity happens at a single offset, so every
// ambiguous member offers the same pair and shares one choice, while an unambiguous member resolves
// in the same plan. The fixture's measure-2 chord is exactly that mix — fret 3 and fret 5.
TEST_CASE("The harmonic picker covers a chord in one entry", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    fixture.controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    fixture.controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    fixture.controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);
    const ChartPendingHarmonicViewState* const armed = fixture.picker();
    REQUIRE(armed != nullptr);
    if (armed != nullptr)
    {
        // Both heads draw: the ambiguous one offering two nodes, the settled one its single node.
        REQUIRE(armed->notes.size() == 2);
        CHECK(armed->notes[0].nodes.size() == 2);
        CHECK(armed->notes[1].nodes.size() == 1);
        CHECK(armed->notes[1].chosen == 0);
    }

    const EditorViewState* const state = stateOrNull(fixture.view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();
    static_cast<void>(fixture.pending.scheduler.runDelayed());

    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].harmonic_node.has_value());
    CHECK(chart->notes[1].harmonic_node.has_value());
    CHECK(state->undo_history.labels.size() == entries_before + 1);
}

// The skips are silent: an open string states no position and fret 11 names no node, so the press
// leaves both alone and authors no undo entry. (The counted-skip reason waits on the non-modal
// refusal channel, exactly as the legato verb's does.)
TEST_CASE("A harmonic press over frets that name nothing is inert", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 0),
        makeTestNote({.measure = 2, .beat = 1}, 2, 11),
    };
    HarmonicPickerFixture fixture{std::move(chart)};

    fixture.controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    fixture.controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    fixture.controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    const EditorViewState* const state = stateOrNull(fixture.view.last_state);
    REQUIRE(state != nullptr);
    const std::size_t entries_before = state->undo_history.labels.size();
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Harmonic);

    CHECK(fixture.picker() == nullptr);
    const common::core::Chart* const live = chartOrNull(fixture.controller);
    REQUIRE(live != nullptr);
    CHECK(live->notes[0].fret == 0);
    CHECK_FALSE(live->notes[0].harmonic_node.has_value());
    CHECK(live->notes[1].fret == 11);
    CHECK_FALSE(live->notes[1].harmonic_node.has_value());
    CHECK(state->undo_history.labels.size() == entries_before);
}

// The picker's MOUSE form: the same choices the keyboard cycles, published for the menu with the
// partial that names each — and choosing one applies at once, because a menu row is already
// deliberate.
TEST_CASE("The harmonic choices publish for the menu and apply directly", "[core][chart]")
{
    HarmonicPickerFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    const EditorViewState* const state = stateOrNull(fixture.view.last_state);
    REQUIRE(state != nullptr);
    const std::vector<ChartHarmonicNodeChoice>& choices = state->chart_edit.harmonic_node_choices;
    REQUIRE(choices.size() == 2);
    CHECK(choices[0].node == Catch::Approx(2.6687).margin(0.001));
    CHECK(choices[0].partial == 7);
    CHECK(choices[1].node == Catch::Approx(3.1564).margin(0.001));
    CHECK(choices[1].partial == 6);

    fixture.controller.onChartHarmonicNodeRequested(7);
    CHECK(fixture.picker() == nullptr);
    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].fret == 0);
    const std::optional<double>& node = chart->notes[0].harmonic_node;
    REQUIRE(node.has_value());
    if (node.has_value())
    {
        CHECK(*node == Catch::Approx(2.6687).margin(0.001));
    }

    // A selection with nothing ambiguous offers no rows, so the menu shows the plain verb alone.
    click(fixture.controller, 40.0f, 180.0f);
    CHECK(state->chart_edit.harmonic_node_choices.empty());
}

} // namespace rock_hero::editor::core
