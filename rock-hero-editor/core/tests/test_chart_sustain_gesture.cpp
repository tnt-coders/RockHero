#include <cstddef>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// The controller every duration-verb scenario starts from, with the shared six-string chart loaded
// through the normal open route at the quarter-note grid. One struct because the gesture narratives
// below are long and their construction is identical in every one of them; the members are declared
// in the order the controller's own initializer reads them.
struct GestureFixture
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

    GestureFixture()
    {
        controller.attachView(view);
    }

    // Loads the shared chart fixture, or a caller's own stream when a scenario needs different
    // rings; false when the open route did not produce an arrangement.
    [[nodiscard]] bool load(common::core::Chart chart = makeTestChart())
    {
        return loadChartArrangement(controller, project_services, audio, {}, std::move(chart));
    }

    // The ring of the note on a (measure, string) slot, or a zero Fraction when there is none.
    // Deliberately assertion-free: every caller compares the result inside a CHECK, and a nested
    // Catch2 assertion inside another assertion's expression is exactly the shape to avoid — a
    // missing note reads as a zero ring, which no scenario here expects, so the caller's own
    // comparison fails.
    [[nodiscard]] common::core::Fraction ringAt(int measure, int string) const
    {
        const common::core::Chart* const chart = chartOrNull(controller);
        if (chart == nullptr)
        {
            return {};
        }
        for (const common::core::ChartNote& note : chart->notes)
        {
            if (note.position.measure == measure && note.string == string)
            {
                return note.sustain;
            }
        }
        return {};
    }

    // How many entries the undo stack holds, the count a gesture must not grow past one. Also
    // assertion-free, for the same reason: it is read inside CHECK expressions.
    [[nodiscard]] std::size_t undoEntryCount() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr ? state->undo_history.labels.size() : 0;
    }

    // Whether the history cursor sits on the state the file holds — the session's "no unsaved
    // edits" answer, which is what a gesture netting to zero has to leave behind. Assertion-free
    // like the others: false is the failing answer every caller below already checks for.
    [[nodiscard]] bool atCleanState() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr && state->undo_history.clean_position.has_value() &&
               *state->undo_history.clean_position == state->undo_history.position;
    }

    // One step of the duration verb at the session's grid, the shape every scenario repeats.
    void step(int direction)
    {
        controller.onChartSustainAdjustRequested(direction);
    }

    // One step at the TICK lattice: snap off, step, snap back on. The gesture survives the toggle
    // — its window proof reads the selection and the history top, neither of which a toggle
    // moves — which is exactly what lets one run mix lattices.
    void tickStep(int direction)
    {
        turnGridSnapOff(controller);
        step(direction);
        controller.onGridSnapToggleRequested();
    }
};

} // namespace

// Sustain growth stops at exact adjacency with the next onset on the note's OWN string (40-Q2-B,
// the one bound on a ring), and a note on another string blocks nothing; shrinking stops one step
// short of empty. A step at the tick lattice runs through the same verb.
TEST_CASE("EditorController grows and clamps sustains on the grid", "[core][chart]")
{
    GestureFixture fixture;
    REQUIRE(fixture.load());

    // A plain click selects just the string-1 note (containment hierarchy). Every fixture note
    // starts at the eighth-of-a-beat fixture ring, which ends BETWEEN grid lines, so the first grid
    // step snaps its end onto the next line rather than adding a beat to it.
    click(fixture.controller, 40.0f, 220.0f);
    fixture.step(1);
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{1});

    // Five more quarter-note steps would reach six beats, but the measure-3 note is the next
    // onset on this string, four beats later: growth stops exactly there.
    for (int index = 0; index < 5; ++index)
    {
        fixture.step(1);
    }
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{4});

    // Nothing bounds the string-2 chord member, because the bound is its OWN string's next onset
    // and it has none: the measure-3 note on string 1 no longer stops its ring.
    click(fixture.controller, 40.0f, 180.0f);
    for (int index = 0; index < 6; ++index)
    {
        fixture.step(1);
    }
    CHECK(fixture.ringAt(2, 2) == common::core::Fraction{6});

    // Shrinking stops one step short of empty: every note rings, so the step that would reach zero
    // holds the ring where it is.
    click(fixture.controller, 40.0f, 220.0f);
    for (int index = 0; index < 6; ++index)
    {
        fixture.step(-1);
    }
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{1});

    // The click ENDS that gesture, which matters here: those six shrinks accumulated three steps
    // of overshoot past the hold, and inside one gesture a grow has to pay them back before the
    // ring moves (the bound's own behaviour, in the other direction). The tick lattice is what
    // this section pins, so it gets a gesture of its own.
    click(fixture.controller, 40.0f, 220.0f);
    fixture.tickStep(1);
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{961, 960});
    fixture.tickStep(-1);
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{1});
}

// The gesture's headline property (user ruling 2026-08-22): a run of steps is ONE undo entry, and a
// run that comes back to where it started leaves the notes byte-identical — because every step
// re-plans from the rings the gesture began with rather than from the ring the last step left.
//
// The measure-3 note is the one this can be said of: its two-beat ring ends ON a grid line, and a
// grid step from the lattice lands on the lattice, so the steps back retrace the steps out. From a
// ring left between lines the first grid step snaps, and the run cannot return — by design, below.
TEST_CASE("A sustain gesture is one undo entry and round-trips exactly", "[core][chart]")
{
    GestureFixture fixture;
    REQUIRE(fixture.load());

    click(fixture.controller, 80.0f, 220.0f);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    const common::core::ChartNote start = chart->notes[2];
    const std::size_t entries_before = fixture.undoEntryCount();

    for (int index = 0; index < 3; ++index)
    {
        fixture.step(1);
    }
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{5});
    // Three presses, one entry: the first pushed it and every later step replaced it, so the entry
    // always describes start → now.
    CHECK(fixture.undoEntryCount() == entries_before + 1);

    for (int index = 0; index < 3; ++index)
    {
        fixture.step(-1);
    }

    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    if (chart != nullptr)
    {
        REQUIRE(chart->notes.size() == 3);
        CHECK(chart->notes[2] == start);
    }
    // And a run that nets to zero leaves NO entry: it describes nothing, so it is taken back out
    // rather than replaced with an empty one the user would have to Ctrl+Z past.
    CHECK(fixture.undoEntryCount() == entries_before);
}

// The case the gesture exists for: a chord whose members have different room. The bounded member
// pins at its own restrike while the free one keeps growing, and on the way back it leaves the
// bound on exactly the step that put it there — so the chord's shape survives the round trip
// instead of shrinking asymmetrically.
TEST_CASE("A blocked chord member diverges and rejoins in one gesture", "[core][chart]")
{
    GestureFixture fixture;
    // The fixture chart with on-grid rings on the measure-2 onset, because this run has to return
    // exactly and only a ring ending on a grid line can (a grid step from between lines snaps).
    common::core::Chart chart = makeTestChart();
    chart.notes[0].sustain = common::core::Fraction{1};
    chart.notes[1].sustain = common::core::Fraction{1};
    const bool loaded = fixture.load(std::move(chart));
    REQUIRE(loaded);

    // The double click selects the whole measure-2 onset: string 1 (bounded four beats later by its
    // own restrike) and string 2 (bounded by nothing).
    doubleClick(fixture.controller, 40.0f, 220.0f);
    const std::size_t entries_before = fixture.undoEntryCount();

    for (int index = 0; index < 4; ++index)
    {
        fixture.step(1);
    }
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{4});
    CHECK(fixture.ringAt(2, 2) == common::core::Fraction{5});
    CHECK(fixture.undoEntryCount() == entries_before + 1);

    // The step back is the one that put the bounded member there, so it rejoins its neighbour on
    // the same ring they parted from: the clamp never entered the replay.
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{4});
    CHECK(fixture.ringAt(2, 2) == common::core::Fraction{4});

    fixture.step(-1);
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{3});
    CHECK(fixture.ringAt(2, 2) == common::core::Fraction{3});

    for (int index = 0; index < 2; ++index)
    {
        fixture.step(-1);
    }
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{1});
    CHECK(fixture.ringAt(2, 2) == common::core::Fraction{1});
    // Both members are back where they started, so the whole run describes nothing and its entry
    // goes with it.
    CHECK(fixture.undoEntryCount() == entries_before);
}

// The floor is the bound's mirror image: a ring the replay would take to zero holds where it is
// instead of vanishing, and rejoins the gesture the moment the replay is positive again — which
// means the overshoot has to be paid back first, exactly as it is at the bound.
TEST_CASE("An emptied ring holds and rejoins inside one gesture", "[core][chart]")
{
    GestureFixture fixture;
    REQUIRE(fixture.load());

    // The measure-3 note is the fixture's two-beat ring, and nothing later sounds on its string.
    click(fixture.controller, 80.0f, 220.0f);
    const std::size_t entries_before = fixture.undoEntryCount();

    fixture.step(-1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{1});
    // 2 - 2 is not a ring, so the note keeps the one it has.
    fixture.step(-1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{1});
    fixture.step(-1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{1});
    CHECK(fixture.undoEntryCount() == entries_before + 1);

    // Paying the overshoot back: still held at -2, ringing again at -1.
    fixture.step(1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{1});
    fixture.step(1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{1});
    fixture.step(1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{2});
    // Back at the start, so the run leaves no entry behind.
    CHECK(fixture.undoEntryCount() == entries_before);
}

// The bug this verb's step list exists for (user 2026-08-23), end to end: a tick step nudges the
// ring off the grid, and the GRID step after it snaps the end onto the next line instead of
// carrying that remainder — which a single accumulated delta could not do, because it had no idea
// where the end sat. Steps on different lattices still mix freely inside one gesture.
TEST_CASE("Tick and grid steps mix inside one sustain gesture", "[core][chart]")
{
    GestureFixture fixture;
    REQUIRE(fixture.load());

    click(fixture.controller, 80.0f, 220.0f);
    const std::size_t entries_before = fixture.undoEntryCount();

    fixture.tickStep(1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{1921, 960});
    // Three beats exactly, not 2881/960: the grid step lands on the grid line, wherever the tick
    // step had left the end.
    fixture.step(1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{3});
    CHECK(fixture.undoEntryCount() == entries_before + 1);
    fixture.tickStep(-1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{2879, 960});
    // And back: the end sits a hair short of the line at three beats, so the line strictly before
    // it is the two-beat one the note started on — the run describes nothing and its entry goes.
    fixture.step(-1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{2});
    CHECK(fixture.undoEntryCount() == entries_before);
}

// Undo is a commit point like any other: ONE pop restores the ring the gesture started from, and
// the gesture is over — the next step opens a new entry from the restored value rather than
// continuing a delta whose entry the history no longer holds.
TEST_CASE("Undo mid-gesture restores the start and ends the gesture", "[core][chart]")
{
    GestureFixture fixture;
    REQUIRE(fixture.load());

    click(fixture.controller, 40.0f, 220.0f);
    const std::size_t entries_before = fixture.undoEntryCount();
    for (int index = 0; index < 3; ++index)
    {
        fixture.step(1);
    }
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{3});

    fixture.controller.onUndoRequested();
    CHECK(fixture.ringAt(2, 1) == g_fixture_sustain);

    // A fresh gesture: one grid step off the RESTORED ring — which snaps back onto the grid from
    // the fixture's off-grid eighth — and a new entry replacing the redo branch rather than a
    // replacement of an entry the cursor has walked away from.
    fixture.step(1);
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{1});
    CHECK(fixture.undoEntryCount() == entries_before + 1);
}

// Every commit point that closes the technique toggle window closes the gesture too, because both
// rest on the same proof: after any of them a step starts a new gesture from the CURRENT rings and
// pushes its own entry.
TEST_CASE("Selection, caret and other verbs end the sustain gesture", "[core][chart]")
{
    GestureFixture fixture;
    REQUIRE(fixture.load());

    click(fixture.controller, 40.0f, 220.0f);
    fixture.step(1);
    fixture.step(1);
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{2});

    SECTION("a selection change")
    {
        click(fixture.controller, 40.0f, 180.0f);
        click(fixture.controller, 40.0f, 220.0f);
    }
    SECTION("a caret move")
    {
        // Right onto the empty next grid slot, then back onto the note.
        fixture.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
        fixture.controller.onChartCaretStepRequested(ChartStepDirection::Left, false);
    }
    SECTION("another verb")
    {
        fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
    }

    const std::size_t entries_before = fixture.undoEntryCount();
    fixture.step(-1);
    // One grid step off two beats — a continued gesture would have replayed to one beat too, so
    // the entry count is what separates a continued gesture from a fresh one.
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{1});
    CHECK(fixture.undoEntryCount() == entries_before + 1);
}

// A save is a commit point of its own, and the reason is the entry rather than the chart: the file
// now holds what the gesture's entry produced, so replaceTop refuses to rewrite it. The step after
// a save must therefore open a NEW gesture — the alternative is a dead key, which is what asking
// replaceTop and taking its refusal would produce.
TEST_CASE("A save ends the gesture and the next step still lands", "[core][chart]")
{
    GestureFixture fixture;
    REQUIRE(fixture.load());

    click(fixture.controller, 40.0f, 220.0f);
    const std::size_t entries_before = fixture.undoEntryCount();
    fixture.step(1);
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{1});

    fixture.controller.onSaveRequested();
    REQUIRE(fixture.project_services.save_call_count == 1);

    fixture.step(1);
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{2});
    // Two entries: the saved one, which stays exactly as the file has it, and this step's own.
    CHECK(fixture.undoEntryCount() == entries_before + 2);
}

// What retiring the entry is FOR, stated against the file: a run that nets to zero has put the
// chart back byte-for-byte, so the session must report no unsaved edits. Leaving the empty entry
// behind instead would claim the document differs from a file it is identical to, and hand the
// user a Ctrl+Z that changes nothing before their real edits start undoing.
TEST_CASE("A net-zero sustain gesture leaves the document clean", "[core][chart]")
{
    GestureFixture fixture;
    REQUIRE(fixture.load());

    // The measure-3 note again: only a ring already on a grid line can be stepped away from and
    // back onto byte-for-byte, which is what "no unsaved edits" has to mean here.
    click(fixture.controller, 80.0f, 220.0f);
    fixture.controller.onSaveRequested();
    REQUIRE(fixture.project_services.save_call_count == 1);
    const std::size_t entries_before = fixture.undoEntryCount();
    REQUIRE(fixture.atCleanState());

    fixture.step(1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{3});
    CHECK(fixture.undoEntryCount() == entries_before + 1);
    CHECK_FALSE(fixture.atCleanState());

    fixture.step(-1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{2});
    CHECK(fixture.undoEntryCount() == entries_before);
    CHECK(fixture.atCleanState());
}

// A scrape needs somewhere to travel, so its ring floors at the minimum gesture window instead of
// holding — and because every step re-plans from the pre-gesture note, growing back out restores
// the path the floor compressed away, terminal and turnarounds alike.
TEST_CASE("A scrape floors and recovers its path inside one gesture", "[core][chart]")
{
    GestureFixture fixture;
    REQUIRE(fixture.load());

    click(fixture.controller, 80.0f, 220.0f);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::PickSlide);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    const common::core::ChartNote scrape = chart->notes[2];
    REQUIRE(scrape.attack == common::core::NoteAttack::PickSlide);
    REQUIRE(scrape.slide_out.has_value());
    const std::size_t entries_before = fixture.undoEntryCount();

    for (int index = 0; index < 5; ++index)
    {
        fixture.step(-1);
    }
    CHECK(fixture.ringAt(3, 1) == common::core::g_minimum_slide_window);
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    if (chart != nullptr)
    {
        const common::core::ChartNote& slid = chart->notes[2];
        REQUIRE(slid.slide_out.has_value());
        if (slid.slide_out.has_value())
        {
            CHECK(slid.slide_out->offset == common::core::g_minimum_slide_window);
        }
    }

    for (int index = 0; index < 5; ++index)
    {
        fixture.step(1);
    }
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    if (chart != nullptr)
    {
        CHECK(chart->notes[2] == scrape);
    }
    // The run netted to zero, so its own entry is gone — and only its own: the pick-slide entry it
    // started from is still the history top.
    CHECK(fixture.undoEntryCount() == entries_before);
}

} // namespace rock_hero::editor::core
