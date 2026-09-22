#include <cstddef>
#include <optional>
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

    // The click ENDS that gesture. The tick lattice is what this section pins, so it gets a
    // gesture of its own.
    click(fixture.controller, 40.0f, 220.0f);
    fixture.tickStep(1);
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{961, 960});
    fixture.tickStep(-1);
    CHECK(fixture.ringAt(2, 1) == common::core::Fraction{1});
}

// The gesture's headline property: a run of steps is ONE undo entry, and a run that comes back to
// where it started leaves the notes byte-identical — because every step re-plans from the rings the
// gesture began with rather than from the ring the last step left.
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
// instead of vanishing. A lone note's step into the floor moves nothing — the replay answers the
// plan the entry already holds — so the gesture authority never records it, and the next grow is
// the first visible step back with no unseen overshoot to pay. THIS is where that is asserted: the
// planner states nothing about it (`planAdjustSustain` is a pure function of the step list).
TEST_CASE("An emptied ring holds and the steps into the floor are not recorded", "[core][chart]")
{
    GestureFixture fixture;
    REQUIRE(fixture.load());

    // The measure-3 note is the fixture's two-beat ring, and nothing later sounds on its string.
    click(fixture.controller, 80.0f, 220.0f);
    const std::size_t entries_before = fixture.undoEntryCount();

    fixture.step(-1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{1});
    // 2 - 2 is not a ring, so the note keeps the one it has, and the press is not part of the run.
    fixture.step(-1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{1});
    fixture.step(-1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{1});
    CHECK(fixture.undoEntryCount() == entries_before + 1);

    // One grow is one step back: the run replays the single shrink it holds and this grow, lands
    // on the start, and leaves no entry behind.
    fixture.step(1);
    CHECK(fixture.ringAt(3, 1) == common::core::Fraction{2});
    CHECK(fixture.undoEntryCount() == entries_before);
}

// The floor a keyframe raises: a pitched glide's last junction bounds its ring, so a shrink that
// would pull the end back past it holds where it is instead — the same hold the onset gives a ring
// with no keyframe — and the junction is never clipped away. Pulling the end exactly ONTO a
// junction that states a fret and NOTHING ELSE is the one legal landing: the stop it stated is
// never sounded there, so the junction becomes the SLIDE-OUT and the glide an unpitched slide-out —
// the tail meeting the waypoint is how a charter authors one. From there the ring holds: the ribbon
// cannot pass its own end point, and the slide-out's length is the point's to change (the move
// verb). The held steps are not recorded, so the first grow after them moves the tail at once — and
// a point never moves because the ring did: the ribbon runs on past the junction, which is a
// pitched stop again.
TEST_CASE("A ring lands on its last keyframe inside one gesture", "[core][chart]")
{
    GestureFixture fixture;
    REQUIRE(fixture.load(makeGlideChart()));

    // The glide chart's one note: an eight-beat ring on string 3 with its junction four beats in,
    // stating fret 9 and nothing else.
    click(fixture.controller, 40.0f, 140.0f);
    const auto one_keyframe_at = [&fixture](const common::core::Fraction offset) {
        const common::core::Chart* const chart = chartOrNull(fixture.controller);
        return chart != nullptr && chart->notes.size() == 1 &&
               chart->notes[0].keyframes.size() == 1 &&
               chart->notes[0].keyframes[0].offset == offset;
    };
    const auto slides_out = [&fixture] {
        const common::core::Chart* const chart = chartOrNull(fixture.controller);
        return chart != nullptr && chart->notes.size() == 1 &&
               common::core::endStatedFretOrNull(chart->notes[0]) != nullptr;
    };

    fixture.step(-1);
    fixture.step(-1);
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{5});
    CHECK(one_keyframe_at(common::core::Fraction{4}));
    CHECK_FALSE(slides_out());
    // 8 - 4 lands the end ON the junction: the junction is now the slide-out, fret 9 the slide-out,
    // and the landing cost the junction nothing it had stated.
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{4});
    CHECK(one_keyframe_at(common::core::Fraction{4}));
    CHECK(slides_out());
    // A slide-out needs its own leg, so the ring holds here.
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{4});
    CHECK(one_keyframe_at(common::core::Fraction{4}));
    CHECK(slides_out());

    // The held press left no trace: one grow is one visible step — and the junction stays where
    // it was, a pitched stop again with the ribbon running on past it.
    fixture.step(1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{5});
    CHECK(one_keyframe_at(common::core::Fraction{4}));
    CHECK_FALSE(slides_out());
}

// A BEND at the junction costs the landing nothing, so the landing is taken. A bend stated exactly
// at a ring's end is the curve's LAST value — it shapes the final leg running into the end — so it
// is as meaningful on a slide-out as anywhere else and rides to the end with the statement. The
// ring therefore lands on its junction exactly as a bare-fret one does, and the bend survives it.
TEST_CASE("A ring lands on a last keyframe that also states a bend", "[core][chart]")
{
    GestureFixture fixture;
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].keyframes[0].bend = 1.0;
    const bool loaded = fixture.load(std::move(chart));
    REQUIRE(loaded);

    click(fixture.controller, 40.0f, 140.0f);
    const auto keeps_its_bend = [&fixture](const common::core::Fraction offset) {
        const common::core::Chart* const chart_now = chartOrNull(fixture.controller);
        if (chart_now == nullptr || chart_now->notes.size() != 1 ||
            chart_now->notes[0].keyframes.size() != 1)
        {
            return false;
        }
        const common::core::Keyframe& only = chart_now->notes[0].keyframes[0];
        // Bound once, with the explicit guard the CI-only optional checker needs.
        const std::optional<double>& bend = only.bend;
        if (!bend.has_value())
        {
            return false;
        }
        return only.offset == offset && std::is_eq(*bend <=> 1.0);
    };
    const auto slides_out = [&fixture] {
        const common::core::Chart* const chart_now = chartOrNull(fixture.controller);
        return chart_now != nullptr && chart_now->notes.size() == 1 &&
               common::core::endStatedFretOrNull(chart_now->notes[0]) != nullptr;
    };

    fixture.step(-1);
    fixture.step(-1);
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{5});
    CHECK(keeps_its_bend(common::core::Fraction{4}));
    CHECK_FALSE(slides_out());

    // 8 - 4 lands the end ON the junction: the junction is the slide-out and its bend the curve's
    // last value, both completing as the ring ends.
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{4});
    CHECK(keeps_its_bend(common::core::Fraction{4}));
    CHECK(slides_out());

    // From there the ring holds: the ribbon cannot pass its own end point.
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{4});
    CHECK(keeps_its_bend(common::core::Fraction{4}));
    CHECK(slides_out());
}

// The landing is taken only where it ERASES NOTHING. A junction that also states a SHAKE would lose
// it — a state stated where the string is let go has no ring to shake in — and this verb shortens
// rings rather than deleting statements, so such a junction holds the ring STRICTLY above it,
// exactly as a fretless one does, and the step into it is refused rather than recorded.
TEST_CASE("A ring holds above a last keyframe that states a shake", "[core][chart]")
{
    GestureFixture fixture;
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].keyframes[0].vibrato = common::core::VibratoState::Narrow;
    const bool loaded = fixture.load(std::move(chart));
    REQUIRE(loaded);

    click(fixture.controller, 40.0f, 140.0f);
    const auto one_keyframe_at = [&fixture](const common::core::Fraction offset) {
        const common::core::Chart* const chart_now = chartOrNull(fixture.controller);
        return chart_now != nullptr && chart_now->notes.size() == 1 &&
               chart_now->notes[0].keyframes.size() == 1 &&
               chart_now->notes[0].keyframes[0].offset == offset;
    };
    const auto slides_out = [&fixture] {
        const common::core::Chart* const chart_now = chartOrNull(fixture.controller);
        return chart_now != nullptr && chart_now->notes.size() == 1 &&
               common::core::endStatedFretOrNull(chart_now->notes[0]) != nullptr;
    };
    // Asked with the production predicate, so the test cannot drift from the law it pins: the
    // junction still carries the one statement an end could not have kept.
    const auto states_a_shake = [&fixture] {
        const common::core::Chart* const chart_now = chartOrNull(fixture.controller);
        return chart_now != nullptr && chart_now->notes.size() == 1 &&
               chart_now->notes[0].keyframes.size() == 1 &&
               common::core::endStatementWouldShedShake(chart_now->notes[0].keyframes[0]);
    };

    fixture.step(-1);
    fixture.step(-1);
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{5});
    CHECK(one_keyframe_at(common::core::Fraction{4}));
    CHECK_FALSE(slides_out());
    CHECK(states_a_shake());

    // 8 - 4 would land the end ON the junction and bare it: the ring stays one step above instead,
    // the junction keeps what it stated, and the press moved no ring so it is not recorded.
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{5});
    CHECK(one_keyframe_at(common::core::Fraction{4}));
    CHECK_FALSE(slides_out());
    CHECK(states_a_shake());
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{5});
    CHECK(states_a_shake());

    // The two held presses left no trace, so one grow is one visible step out from five beats.
    fixture.step(1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{6});
    CHECK(one_keyframe_at(common::core::Fraction{4}));
    CHECK(states_a_shake());
}

// The defect the landing rule exists to prevent, end to end. A delayed shake mid-hold is a keyframe
// stating the fret already in force plus its shake: bared by a landing it would say nothing the
// path does not already say, and the settle's silent-point sweep would then dissolve it — the
// charter's shake shrunk out of existence. The ring holds above it, so the statement survives the
// shrink and the leave.
TEST_CASE("A shrink to the floor keeps a mid-hold shake through the settle", "[core][chart]")
{
    GestureFixture fixture;
    common::core::Chart shaking;
    shaking.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote held =
        makeTestNote({.measure = 2, .beat = 1}, 3, 5, common::core::Fraction{8});
    held.keyframes = {common::core::Keyframe{
        .offset = common::core::Fraction{4},
        .fret = 5,
        .bend = {},
        .vibrato = common::core::VibratoState::Narrow,
    }};
    shaking.notes = {std::move(held)};
    const bool loaded = fixture.load(std::move(shaking));
    REQUIRE(loaded);

    click(fixture.controller, 40.0f, 140.0f);
    // Far more presses than the ring has room for: three move it, and the rest are held at the
    // floor the shake raises.
    for (int index = 0; index < 6; ++index)
    {
        fixture.step(-1);
    }
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{5});

    // Clicking an empty lane leaves the note, which is the settle that sweeps silent points.
    click(fixture.controller, 40.0f, 220.0f);
    const common::core::Chart* const swept = chartOrNull(fixture.controller);
    REQUIRE(swept != nullptr);
    if (swept != nullptr)
    {
        REQUIRE(swept->notes.size() == 1);
        REQUIRE(swept->notes[0].keyframes.size() == 1);
        if (swept->notes[0].keyframes.size() == 1)
        {
            const common::core::Keyframe& shake = swept->notes[0].keyframes[0];
            CHECK(shake.offset == common::core::Fraction{4});
            CHECK(shake.vibrato == common::core::VibratoState::Narrow);
        }
    }
}

// The landing must also SAY something. A last keyframe repeating the fret already in force states
// no travel, so baring it would author a slide-out toward the fret the string already holds:
// nothing draws that mark, the settle's silent-point sweep dissolves the point, and until then the
// released ring refuses to shorten any further — a floor the charter cannot see. Such a keyframe
// holds the ring strictly above it, and "the fret already in force" is the PATH's answer, so a fret
// an earlier junction travelled to counts exactly as the onset's own does.
//
// The point is TYPED rather than loaded, because a chart reaching the editor through the package
// reader never carries one: the load repair sheds it (stripSilentKeyframes), so the digit is the
// only route by which a shrinking ring can meet one.
TEST_CASE("A ring holds above a last keyframe whose fret says nothing", "[core][chart]")
{
    GestureFixture fixture;
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote holding =
        makeTestNote({.measure = 2, .beat = 1}, 3, 5, common::core::Fraction{8});
    // The fret typed four beats along the tail, and how many points the tail then carries. Either
    // way the typed value repeats the fret in force at that instant: the onset's own where nothing
    // else states one, an earlier junction's where the ring has already travelled.
    int typed{};
    std::size_t points{};
    SECTION("the onset's own fret")
    {
        typed = 5;
        points = 1;
    }
    SECTION("the fret an earlier junction travelled to")
    {
        typed = 7;
        points = 2;
        holding.keyframes = {common::core::Keyframe{
            .offset = common::core::Fraction{2},
            .fret = 7,
            .bend = {},
            .vibrato = {},
        }};
    }
    chart.notes = {std::move(holding)};
    const bool loaded = fixture.load(std::move(chart));
    REQUIRE(loaded);

    // Four beats in — 4.0s at the fixture geometry's 20 px/s — where a typed digit plants a point
    // on the path, selected, and the duration verb then reaches the ring that point rides.
    click(fixture.controller, 80.0f, 140.0f);
    fixture.controller.onChartFretDigitTyped(typed);
    const auto path_intact = [&fixture, points] {
        const common::core::Chart* const chart_now = chartOrNull(fixture.controller);
        return chart_now != nullptr && chart_now->notes.size() == 1 &&
               chart_now->notes[0].keyframes.size() == points &&
               chart_now->notes[0].keyframes.back().offset == common::core::Fraction{4} &&
               common::core::endStatedFretOrNull(chart_now->notes[0]) == nullptr;
    };
    // Asked with the production predicate, so the test cannot drift from the law it pins: the
    // landing this floor would take is one that says nothing, and stays refused.
    const auto landing_refused = [&fixture] {
        const common::core::Chart* const chart_now = chartOrNull(fixture.controller);
        return chart_now != nullptr && chart_now->notes.size() == 1 &&
               !common::core::ringEndMayLandOnLastKeyframe(chart_now->notes[0]);
    };

    fixture.step(-1);
    fixture.step(-1);
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{5});
    CHECK(path_intact());
    CHECK(landing_refused());

    // 8 - 4 would land the end ON the point and bare it into a slide-out that goes nowhere: the
    // ring stays one step above instead, and the press moved no ring so it is not recorded.
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{5});
    CHECK(path_intact());
    CHECK(landing_refused());
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{5});
    CHECK(path_intact());

    // The two held presses left no trace, so one grow is one visible step out from five beats.
    fixture.step(1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{6});
    CHECK(path_intact());
}

// The other half of the same rule: a last keyframe stating a fret the path does NOT already hold
// travels, so baring it states a real slide-out and the landing is taken — here a point that turns
// the glide back toward the onset's own fret, reached across an earlier junction.
TEST_CASE("A ring lands on a last keyframe that travels back", "[core][chart]")
{
    GestureFixture fixture;
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote turning =
        makeTestNote({.measure = 2, .beat = 1}, 3, 5, common::core::Fraction{8});
    turning.keyframes = {
        common::core::Keyframe{
            .offset = common::core::Fraction{2},
            .fret = 7,
            .bend = {},
            .vibrato = {},
        },
        common::core::Keyframe{
            .offset = common::core::Fraction{4},
            .fret = 5,
            .bend = {},
            .vibrato = {},
        },
    };
    chart.notes = {std::move(turning)};
    const bool loaded = fixture.load(std::move(chart));
    REQUIRE(loaded);

    click(fixture.controller, 40.0f, 140.0f);
    const auto slides_out_toward = [&fixture]() -> std::optional<int> {
        const common::core::Chart* const chart_now = chartOrNull(fixture.controller);
        if (chart_now == nullptr || chart_now->notes.size() != 1)
        {
            return std::nullopt;
        }
        const int* const slide_out = common::core::endStatedFretOrNull(chart_now->notes[0]);
        return slide_out != nullptr ? std::optional<int>{*slide_out} : std::nullopt;
    };

    fixture.step(-1);
    fixture.step(-1);
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{5});
    CHECK_FALSE(slides_out_toward().has_value());

    // 8 - 4 lands the end ON the turn: the point is the slide-out now, and the slide-out from the 7
    // in force down to its 5 is a slide-out the surfaces draw.
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{4});
    const std::optional<int> landed = slides_out_toward();
    REQUIRE(landed.has_value());
    if (landed.has_value())
    {
        CHECK(*landed == 5);
    }
    // A slide-out needs its own leg, so the ring holds here.
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{4});
    CHECK(slides_out_toward().has_value());
}

// The defect end to end, by the route that authored it: a digit typed on a tail plants a point, and
// a digit repeating the note's own fret plants one that says nothing. Shrinking onto it would have
// made it an invisible slide-out, after which the ring refused to shorten from the head either —
// the charter's tail stuck on a mark nothing draws. The ring now floors one step above the point
// the charter CAN see, from the point's selection and from the head alike; and once the point
// dissolves at the settle, the floor falls back to the onset and the tail moves again.
TEST_CASE("A typed point that says nothing never pins the ring", "[core][chart]")
{
    GestureFixture fixture;
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {makeTestNote({.measure = 2, .beat = 1}, 3, 5, common::core::Fraction{8})};
    const bool loaded = fixture.load(std::move(chart));
    REQUIRE(loaded);

    // Four beats into the eight-beat ring — 4.0s at the fixture geometry's 20 px/s — and the note's
    // own fret typed there, which states nothing the path does not already say.
    click(fixture.controller, 80.0f, 140.0f);
    fixture.controller.onChartFretDigitTyped(5);
    const auto point_at = [&fixture](const common::core::Fraction offset) {
        const common::core::Chart* const chart_now = chartOrNull(fixture.controller);
        return chart_now != nullptr && chart_now->notes.size() == 1 &&
               chart_now->notes[0].keyframes.size() == 1 &&
               chart_now->notes[0].keyframes[0].offset == offset;
    };
    REQUIRE(point_at(common::core::Fraction{4}));

    // The duration verb reaches the ring the selected point rides, and it floors one step above it.
    for (int index = 0; index < 5; ++index)
    {
        fixture.step(-1);
    }
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{5});
    CHECK(point_at(common::core::Fraction{4}));

    // Selecting the head keeps the note in focus, so the point stands — and the floor it raises is
    // the same one, not a lower one hidden behind a slide-out the landing would have written.
    click(fixture.controller, 40.0f, 140.0f);
    fixture.step(-1);
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{5});
    CHECK(point_at(common::core::Fraction{4}));

    // Leaving the note dissolves the point, exactly as it would have with no gesture at all. With
    // nothing on the tail the floor is the onset again, so the ring shortens past where the point
    // stood.
    click(fixture.controller, 40.0f, 180.0f);
    const common::core::Chart* const swept = chartOrNull(fixture.controller);
    REQUIRE(swept != nullptr);
    if (swept != nullptr)
    {
        REQUIRE(swept->notes.size() == 1);
        CHECK(swept->notes[0].keyframes.empty());
    }
    click(fixture.controller, 40.0f, 140.0f);
    fixture.step(-1);
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{3});
}

// A keyframe sits on the tail, and this is the verb that acts on the tail: with a junction selected
// the ring it rides grows and shrinks exactly as it does from the head, so a charter standing at
// the end of a slide can pull the tail out without going back for the head. The head verbs do not
// reach through a keyframe; only the tail's own verb does.
TEST_CASE("The duration verb reaches the ring a selected keyframe rides", "[core][chart]")
{
    GestureFixture fixture;
    REQUIRE(fixture.load(makeGlideChart()));

    // The junction's linked head, four beats into the eight-beat ring on string 3.
    click(fixture.controller, 80.0f, 140.0f);
    const EditorViewState* const state = stateOrNull(fixture.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->chart_edit.selected_keyframes.size() == 1);
    REQUIRE(state->chart_edit.selected_notes.empty());
    const std::size_t entries_before = fixture.undoEntryCount();

    fixture.step(1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{9});
    CHECK(fixture.undoEntryCount() == entries_before + 1);
    // The step back retraces the run and retires its entry, the gesture's ordinary ending.
    fixture.step(-1);
    CHECK(fixture.ringAt(2, 3) == common::core::Fraction{8});
    CHECK(fixture.undoEntryCount() == entries_before);
}

// Why the verb keeps a step LIST rather than one accumulated delta, end to end: a tick step nudges
// the ring off the grid, and the GRID step after it snaps the end onto the next line instead of
// carrying that remainder — which a single delta cannot do, having no idea where the end sat. Steps
// on different lattices still mix freely inside one gesture.
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
// the path the floor compressed away, terminal and turnarounds alike. Of the five shrinks only the
// two that moved the ring are recorded (two beats to one, one to the floor), so two grows are the
// whole way back.
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
    REQUIRE(common::core::endStatedFretOrNull(scrape) != nullptr);
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
        // The terminal rode the ring down: it is the keyframe at the floored end.
        const common::core::Keyframe* const slide_out = common::core::endFretStatement(slid);
        REQUIRE(slide_out != nullptr);
        if (slide_out != nullptr)
        {
            CHECK(slid.sustain == common::core::g_minimum_slide_window);
            CHECK(slide_out->offset == common::core::g_minimum_slide_window);
        }
    }

    for (int index = 0; index < 2; ++index)
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
