#include <algorithm>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
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

// The chart-editing fixture these held-stop scenarios run on: the shared six-string chart opened
// through the controller's normal route, with the quarter-note grid the lane geometry assumes.
struct HeldStopFixture
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

    // The shared six-string chart by default; a scenario whose narrative needs a stream the
    // fixture does not carry — a tap to hold a stop under, say — passes its own.
    explicit HeldStopFixture(common::core::Chart chart = makeTestChart())
    {
        controller.attachView(view);
        // Move outside the assertion macro: REQUIRE re-mentions its expression textually, which
        // bugprone-use-after-move reads as a use of the moved-from chart.
        const bool loaded =
            loadChartArrangement(controller, project_services, audio, {}, std::move(chart));
        REQUIRE(loaded);
    }
};

// The chart-editing overlays the controller last published. Bound through the harness's own
// state accessor so a scenario that never pushed reads as a failed REQUIRE rather than a crash.
[[nodiscard]] const ChartEditViewState& chartEditState(const FakeEditorView& view)
{
    const EditorViewState* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    return state->chart_edit;
}

// The tab projection the controller last published — where a held stop's resolved value and the
// face it wears are actually observable.
[[nodiscard]] const common::core::ChartViewState& tabProjection(const FakeEditorView& view)
{
    const EditorViewState* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tab != nullptr);
    return *state->tab;
}

// The held-stop figure: a chord ringing on strings 1 and 2 across a tap on string 3 the chord
// never holds. That is what the held stop is for — the picking hand sounds a fret the
// fretting hand is not on — and it is also what gives a stop stated here somewhere to resolve: a
// claim on a NEW string inside a standing shape splits it, so the claim lands in a span starting
// at the tap's own instant, which is where its satellite prints.
[[nodiscard]] common::core::Chart makeTappedShapeChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 3, common::core::Fraction{2}),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5, common::core::Fraction{2}),
        makeTestNote({.measure = 2, .beat = 2}, 3, 12, common::core::Fraction{1, 2}),
    };
    chart.notes[2].attack = common::core::NoteAttack::Tap;
    return chart;
}

// A MID-SPAN tap whose held stop the notation DERIVES: the string-1 note rings from the span's
// front, so the beat-2 arrivals accumulate into a span whose bracket backdates behind them and the
// tap fronts nothing. Its stop is stated by the pull-off onto fret 9 rather than by any stored
// field — you cannot pull off onto a fret unless a finger was waiting on it — which is the figure
// whose satellite waits for the reveal.
[[nodiscard]] common::core::Chart makeRevealedHeldChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 5, common::core::Fraction{4}),
        makeTestNote({.measure = 2, .beat = 2}, 2, 7, common::core::Fraction{3}),
        makeTestNote({.measure = 2, .beat = 2}, 3, 12, common::core::Fraction{1}),
        makeTestNote({.measure = 2, .beat = 3}, 3, 9, common::core::Fraction{1}),
    };
    chart.notes[2].attack = common::core::NoteAttack::Tap;
    chart.notes[3].attack = common::core::NoteAttack::Legato;
    std::ranges::sort(chart.notes, common::core::chartNoteOrderLess);
    return chart;
}

// The lane x of the satellite column beside the bracket at one instant, derived from the same
// geometry the paint core draws with rather than restated as a number — so a resized slot moves
// the probe with it instead of silently missing.
[[nodiscard]] float satelliteX(const double seconds)
{
    const common::ui::TabLaneGeometry geometry = makeGeometry();
    const common::ui::TabBracketGeometry bracket = geometry.bracketGeometry();
    const common::ui::TabSatelliteSlot slot = geometry.satelliteSlot();
    const float bar_right =
        geometry.x(seconds) + bracket.radius + static_cast<float>(bracket.bar) / 2.0f;
    return bar_right + static_cast<float>(slot.extent()) / 2.0f;
}

// The armed caret's STOP as the controller last published it. The optional is bound ONCE and the
// guard rides that name: the CI-only optional-access checker cannot tie a `has_value()` on one call
// of an accessor to a dereference on the next, because they are two calls it cannot prove yield the
// same object. It cannot see through Catch2's REQUIRE either, so the explicit guard below repeats
// the assertion the REQUIRE already made; in a passing run its body is unreachable.
[[nodiscard]] common::core::ChartStopChannel caretChannel(const FakeEditorView& view)
{
    const std::optional<ChartCaretViewState>& caret = chartEditState(view).caret;
    REQUIRE(caret.has_value());
    if (!caret.has_value())
    {
        return {};
    }
    return caret->channel;
}

// The armed caret's instant, read through the same one-binding rule.
[[nodiscard]] double caretSeconds(const FakeEditorView& view)
{
    const std::optional<ChartCaretViewState>& caret = chartEditState(view).caret;
    REQUIRE(caret.has_value());
    if (!caret.has_value())
    {
        return 0.0;
    }
    return caret->seconds;
}

} // namespace

// A held stop is one field of the note that carries it, so it has no life of its own: deleting the
// note — the caret on its sounding stop, which is the ordinary Delete — takes the statement with
// it, and no stop is left over on a slot nothing sounds at.
TEST_CASE("Delete takes a note's held stop with the note", "[core][chart]")
{
    common::core::Chart tapped = makeTappedShapeChart();
    tapped.notes[2].held = 7;
    HeldStopFixture fixture{std::move(tapped)};

    // The tap at measure 2 beat 2 on string 3, selected through its head, so the caret is on the
    // sounding stop rather than the held one.
    click(fixture.controller, 50.0f, 140.0f);
    REQUIRE(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{2});
    REQUIRE(caretChannel(fixture.view) == common::core::ChartStopChannel::Sounding);

    fixture.controller.onSelectionDeleteRequested();
    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 2);
    // The chord that rang underneath is all that is left, and it never carried a stop of its own.
    CHECK_FALSE(chart->notes[0].held.has_value());
    CHECK_FALSE(chart->notes[1].held.has_value());
}

// THE AUTHORING ROUTE, end to end. A bare tap carries no stored stop, and its satellite is the
// DEFAULT one every right-hand onset has: the caret reaches it, a digit typed there writes a real
// held stop, and Delete on that channel withdraws the statement and leaves the sound alone. The
// tap's own attack, fret and ring are the picking hand's and are never the fretting hand's to take.
TEST_CASE("Typing at a bare tap's satellite authors its held stop", "[core][chart]")
{
    HeldStopFixture fixture{makeTappedShapeChart()};

    // The tap at measure 2 beat 2 on string 3 (2.5s to x = 50, string 3 to y = 140). Selecting it
    // reveals its truth, which is what puts the default satellite on show beside it.
    click(fixture.controller, 50.0f, 140.0f);
    REQUIRE(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{2});
    REQUIRE(caretChannel(fixture.view) == common::core::ChartStopChannel::Sounding);

    click(fixture.controller, satelliteX(2.5), 140.0f);
    REQUIRE(caretChannel(fixture.view) == common::core::ChartStopChannel::Held);

    fixture.controller.onChartFretDigitTyped(7);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[2].held == std::optional{7});
    // The DISCRIMINATION: nothing the picking hand wrote moved.
    CHECK(chart->notes[2].attack == common::core::NoteAttack::Tap);
    CHECK(chart->notes[2].fret == 12);
    CHECK(chart->notes[2].sustain == common::core::Fraction{1, 2});

    // Delete on the held channel takes the STATEMENT, leaving the onset that carried it.
    fixture.controller.onSelectionDeleteRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    CHECK_FALSE(chart->notes[2].held.has_value());
    CHECK(chart->notes[2].attack == common::core::NoteAttack::Tap);
    CHECK(chart->notes[2].fret == 12);
}

// The grammar the two channels have to keep apart, asked of ONE note so the two readings cannot be
// told apart by anything but the channel: digits typed at the head state what the note SOUNDS, and
// digits typed at the satellite state what the hand HOLDS under it.
TEST_CASE("Digits state the sounding fret or the held stop by channel", "[core][chart]")
{
    HeldStopFixture fixture{makeTappedShapeChart()};

    click(fixture.controller, 50.0f, 140.0f);
    // The satellite press is the channel prefix: these digits land in the held stop.
    click(fixture.controller, satelliteX(2.5), 140.0f);
    fixture.controller.onChartFretDigitTyped(7);

    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[2].held == std::optional{7});
    CHECK(chart->notes[2].fret == 12);

    // Clicking the HEAD moves the caret back to the sounding stop, so the same keystroke now
    // states the note's own fret and leaves the held one exactly where it was.
    click(fixture.controller, 50.0f, 140.0f);
    fixture.controller.onChartFretDigitTyped(9);
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[2].fret == 9);
    CHECK(chart->notes[2].held == std::optional{7});
}

// The satellite is an independent hit TARGET: clicking it selects the note (no new kind of object)
// and arms the channel the digits then state.
TEST_CASE("Clicking the held stop's satellite pre-arms its entry", "[core][chart]")
{
    common::core::Chart tapped = makeTappedShapeChart();
    tapped.notes[2].held = 7;
    HeldStopFixture fixture{std::move(tapped)};

    // Leave the caret on the head, so the satellite click is what changes the channel.
    click(fixture.controller, 50.0f, 140.0f);
    REQUIRE(caretChannel(fixture.view) == common::core::ChartStopChannel::Sounding);

    click(fixture.controller, satelliteX(2.5), 140.0f);
    // The same note is selected — the satellite is a second mark of one object, never a second
    // object — and the caret now sits on the stop that was clicked.
    CHECK(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{2});
    CHECK(caretChannel(fixture.view) == common::core::ChartStopChannel::Held);

    fixture.controller.onChartFretDigitTyped(4);
    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[2].held == std::optional{4});
    CHECK(chart->notes[2].fret == 12);
}

// THE SATELLITE REVEAL at the layers that read it. A DERIVED stop is already printed by the
// pull-off notation, so its satellite does not stand: it appears exactly while the note's truth is
// revealed — the same pick that draws the note's real ring — and the hit test, the caret channel
// and the entry all follow that one answer. What it must never be is standing: this figure's stop
// is the notation's, and a second standing copy would state it twice.
TEST_CASE("A derived held stop's satellite is revealed, never standing", "[core][chart]")
{
    HeldStopFixture fixture{makeRevealedHeldChart()};

    // The projection publishes the face and the terms, for every held stop rather than only where
    // a bracket printed one: this tap is MID-SPAN, so the face is its own and it waits.
    const common::core::ChartViewState& tab = tabProjection(fixture.view);
    const auto tapped = std::ranges::find(
        tab.notes, common::core::NoteAttack::Tap, &common::core::NoteViewState::attack);
    REQUIRE(tapped != tab.notes.end());
    if (tapped == tab.notes.end())
    {
        return;
    }
    CHECK(tapped->held == std::optional{9});
    // Bound once so the presence test and the read are provably the same object.
    const std::optional<common::core::StopMarkViewState>& mark = tapped->stop_mark;
    REQUIRE(mark.has_value());
    if (mark.has_value())
    {
        CHECK(mark->face == common::core::StopMarkFace::Revealed);
    }

    // UNREVEALED: nothing is selected and no caret stands in the ring, so the digit is not drawn —
    // and nothing undrawn is reachable. The press falls through to the ordinary placement, which
    // arms the caret on the stop every note has. Under a law that stood every satellite this press
    // would land on the held one instead.
    click(fixture.controller, satelliteX(2.5), 140.0f);
    CHECK(caretChannel(fixture.view) == common::core::ChartStopChannel::Sounding);

    // REVEALED: selecting the tap makes it the thing under scrutiny, so its whole truth shows —
    // the real ring and this satellite alike — and the same press now reaches the stop.
    click(fixture.controller, 50.0f, 140.0f);
    REQUIRE(caretChannel(fixture.view) == common::core::ChartStopChannel::Sounding);
    click(fixture.controller, satelliteX(2.5), 140.0f);
    CHECK(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{2});
    CHECK(caretChannel(fixture.view) == common::core::ChartStopChannel::Held);

    // AND IT IS READ-ONLY, which is the whole reason the caret must reach it: the derivation owns
    // this stop, so a digit typed at it is REFUSED in red rather than quietly landing on the
    // sounding fret beside it.
    fixture.controller.onChartFretDigitTyped(4);
    const std::optional<ChartPendingFretViewState>& pending =
        chartEditState(fixture.view).pending_fret;
    REQUIRE(pending.has_value());
    if (pending.has_value())
    {
        CHECK(pending->text == "4");
        CHECK_FALSE(pending->valid);
    }
    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    if (chart != nullptr)
    {
        REQUIRE(chart->notes.size() == 4);
        CHECK_FALSE(chart->notes[2].held.has_value());
        CHECK(chart->notes[2].fret == 12);
    }
}

// THE LANE REVEAL reaches the pointer too, because the modifier that holds it is the one the press
// carries: with it down every visible note's truth is on show, so a derived satellite is drawn and
// a press on it addresses the stop it states. The press resolves the mark BEFORE the selection it
// derives, which is exactly why the caret's own precondition asks whether the note HAS a face
// rather than whether it happened to be revealed a moment earlier — arming is itself a reveal.
TEST_CASE("The lane reveal makes a derived satellite pressable", "[core][chart]")
{
    HeldStopFixture fixture{makeRevealedHeldChart()};

    const ChartPointerModifiers reveal_held{.ctrl = false, .shift = false, .alt = true};
    click(fixture.controller, satelliteX(2.5), 140.0f, reveal_held);

    // The note becomes the selection, as an unselected note's satellite press always does, and the
    // caret lands on the stop that was clicked rather than on the head beside it.
    CHECK(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{2});
    CHECK(caretChannel(fixture.view) == common::core::ChartStopChannel::Held);
}

// THE PLANT'S FACE at the same layers. A FRETTING-hand pull-off source wears the stop its pull-off
// plants beneath it as its own reveal-only satellite — the same face a derived tap stop wears — so
// the caret reaches it on the reveal, a digit typed at it is refused in red, and Delete on it is
// refused outright: the stop is the notation's and no field carries it, so there is nothing to
// withdraw. The clearing planner is what refuses, off the same ownership table the retype reads.
TEST_CASE("A plant's satellite is revealed, read-only, and refuses Delete", "[core][chart]")
{
    // The revealed-tap fixture with the tap replaced by a fretting-hand strike: the string-3 source
    // at measure 2 beat 2 sounds 12 and is pulled off onto 9 a beat later, so the hold-under law
    // plants 9 beneath it.
    common::core::Chart planted;
    planted.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    planted.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 5, common::core::Fraction{4}),
        makeTestNote({.measure = 2, .beat = 2}, 2, 7, common::core::Fraction{3}),
        makeTestNote({.measure = 2, .beat = 2}, 3, 12, common::core::Fraction{1}),
        makeTestNote({.measure = 2, .beat = 3}, 3, 9, common::core::Fraction{1}),
    };
    planted.notes[3].attack = common::core::NoteAttack::Legato;
    std::ranges::sort(planted.notes, common::core::chartNoteOrderLess);
    HeldStopFixture fixture{std::move(planted)};

    const common::core::ChartViewState& tab = tabProjection(fixture.view);
    REQUIRE(tab.notes.size() == 4);
    CHECK(tab.notes[2].held == std::optional{9});
    // Bound once so the presence test and the read are provably the same object.
    const std::optional<common::core::StopMarkViewState>& mark = tab.notes[2].stop_mark;
    REQUIRE(mark.has_value());
    if (mark.has_value())
    {
        CHECK(mark->face == common::core::StopMarkFace::Revealed);
    }

    // Unrevealed the satellite is not drawn, so the press lands on the stop every note has.
    click(fixture.controller, satelliteX(2.5), 140.0f);
    CHECK(caretChannel(fixture.view) == common::core::ChartStopChannel::Sounding);

    // Revealed by selection, the same press reaches the plant.
    click(fixture.controller, 50.0f, 140.0f);
    click(fixture.controller, satelliteX(2.5), 140.0f);
    CHECK(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{2});
    REQUIRE(caretChannel(fixture.view) == common::core::ChartStopChannel::Held);

    // READ-ONLY: a digit at it is refused in red, and nothing lands anywhere.
    fixture.controller.onChartFretDigitTyped(4);
    const std::optional<ChartPendingFretViewState>& pending =
        chartEditState(fixture.view).pending_fret;
    REQUIRE(pending.has_value());
    if (pending.has_value())
    {
        CHECK(pending->text == "4");
        CHECK_FALSE(pending->valid);
    }

    // DELETE is refused: the source still sounds, the pull-off still has its predecessor, and
    // nothing was withdrawn from a note that never carried a statement of its own.
    fixture.controller.onSelectionDeleteRequested();
    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    if (chart != nullptr)
    {
        REQUIRE(chart->notes.size() == 4);
        CHECK(chart->notes[2].attack == common::core::NoteAttack::Pick);
        CHECK(chart->notes[2].fret == 12);
        CHECK_FALSE(chart->notes[2].held.has_value());
        CHECK(chart->notes[3].attack == common::core::NoteAttack::Legato);
    }
}

// A TAPPED HARMONIC'S SATELLITE STATES A STOP NO FIELD CARRIES. The picking hand only touches the
// node, so the stop the satellite shows is the one the FRETTING hand presses — the note's own fret,
// which the head beside it does not print because the head prints the node. Nothing else states
// that number, so the mark is drawn without a reveal and the caret reaches it exactly as it
// reaches an authored one. What it is not is writable: a note carrying a node states no planted
// finger, so a digit typed at it is refused in red and Delete on it withdraws nothing.
TEST_CASE("A tapped harmonic's satellite states its pressed stop, read-only", "[core][chart]")
{
    // The tapped-shape figure with the tap touching a node above the fret it presses: the chord
    // holds strings 1 and 2, and string 3 is stopped at 7 with the octave node at 19 touched over
    // it.
    common::core::Chart touching = makeTappedShapeChart();
    touching.notes[2].fret = 7;
    touching.notes[2].harmonic_node = 19.0;
    HeldStopFixture fixture{std::move(touching)};

    const common::core::ChartViewState& tab = tabProjection(fixture.view);
    REQUIRE(tab.notes.size() == 3);
    CHECK(tab.notes[2].held == std::optional{7});
    // Bound once so the presence test and the read are provably the same object.
    const std::optional<common::core::StopMarkViewState>& mark = tab.notes[2].stop_mark;
    REQUIRE(mark.has_value());
    if (mark.has_value())
    {
        CHECK(common::core::stopMarkShown(*mark, false));
    }

    // Leave the caret on the head, so the satellite click is what changes the channel — and it
    // reaches the mark with nothing revealed, because the mark is drawn.
    click(fixture.controller, 50.0f, 140.0f);
    REQUIRE(caretChannel(fixture.view) == common::core::ChartStopChannel::Sounding);
    click(fixture.controller, satelliteX(2.5), 140.0f);
    CHECK(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{2});
    REQUIRE(caretChannel(fixture.view) == common::core::ChartStopChannel::Held);

    // READ-ONLY: the digit is refused in red rather than landing on the sounding fret beside it.
    fixture.controller.onChartFretDigitTyped(4);
    const std::optional<ChartPendingFretViewState>& pending =
        chartEditState(fixture.view).pending_fret;
    REQUIRE(pending.has_value());
    if (pending.has_value())
    {
        CHECK(pending->text == "4");
        CHECK_FALSE(pending->valid);
    }

    // DELETE withdraws nothing, for the same reason: the number the satellite shows is the note's
    // own fret, and there is no held field to take.
    fixture.controller.onSelectionDeleteRequested();
    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    if (chart != nullptr)
    {
        REQUIRE(chart->notes.size() == 3);
        CHECK(chart->notes[2].attack == common::core::NoteAttack::Tap);
        CHECK(chart->notes[2].fret == 7);
        CHECK_FALSE(chart->notes[2].held.has_value());
        // Bound once so the presence test and the read are provably the same object.
        const std::optional<double>& node = chart->notes[2].harmonic_node;
        REQUIRE(node.has_value());
        if (node.has_value())
        {
            CHECK_THAT(*node, Catch::Matchers::WithinAbs(19.0, 0.001));
        }
    }
}

// The keyboard twin of that click: the caret visits both marks of one note in DISPLAY order — the
// head, then the satellite to its right — and reversed going left. On the held stop the digits go
// where the click's do, and Delete takes the STATEMENT rather than the note under it.
TEST_CASE("The caret steps onto a note's held stop and back", "[core][chart]")
{
    common::core::Chart tapped = makeTappedShapeChart();
    tapped.notes[2].held = 7;
    HeldStopFixture fixture{std::move(tapped)};

    // The head, which is where a traversal from the left arrives.
    click(fixture.controller, 50.0f, 140.0f);

    REQUIRE(caretChannel(fixture.view) == common::core::ChartStopChannel::Sounding);
    const double head_seconds = caretSeconds(fixture.view);

    // Rightward: the second stop is WITHIN the slot, so the caret does not move along the axis.
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    CHECK(caretChannel(fixture.view) == common::core::ChartStopChannel::Held);
    CHECK_THAT(caretSeconds(fixture.view), Catch::Matchers::WithinAbs(head_seconds, 1e-9));

    // Leftward is the display order reversed, so it returns to the head without moving either.
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Left, false);
    CHECK(caretChannel(fixture.view) == common::core::ChartStopChannel::Sounding);
    CHECK_THAT(caretSeconds(fixture.view), Catch::Matchers::WithinAbs(head_seconds, 1e-9));

    // Digits on that stop state the held fret, the same entry the satellite click opens.
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    fixture.controller.onChartFretDigitTyped(3);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[2].held == std::optional{3});

    // Delete on the held stop clears the statement and leaves the onset that carried it.
    fixture.controller.onSelectionDeleteRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    CHECK_FALSE(chart->notes[2].held.has_value());
    CHECK(chart->notes[2].attack == common::core::NoteAttack::Tap);
    CHECK(chart->notes[2].fret == 12);

    // The satellite is STILL there, carrying THE DEFAULT: what Delete withdrew is the CHARTER's
    // statement, not the fact that a tap has a fretting hand under it, and the hand here is holding
    // nothing on this string — the open string. So the caret stays on the stop it was on rather
    // than falling back to the head, and the next digit authors a fresh statement in the same
    // place.
    CHECK(caretChannel(fixture.view) == common::core::ChartStopChannel::Held);

    // Delete on the DEFAULT withdraws nothing, because nobody authored it: the chart is exactly
    // what it was, and no held 0 is written in the charter's name — which is why Delete goes
    // through the clearing planner rather than through anything that states a stop.
    fixture.controller.onSelectionDeleteRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    CHECK_FALSE(chart->notes[2].held.has_value());
    CHECK(chart->notes[2].attack == common::core::NoteAttack::Tap);
    CHECK(caretChannel(fixture.view) == common::core::ChartStopChannel::Held);

    // THE DISCRIMINATION the default makes necessary, moved to the note it is now about: the
    // within-slot stop is a fixture of a RIGHT-HAND onset and of nothing else. The chord's string-1
    // note is picked, so the fretting hand IS its onset and there is no second stop under it — the
    // same press is an ordinary step along the axis.
    click(fixture.controller, 40.0f, 220.0f);
    REQUIRE(caretChannel(fixture.view) == common::core::ChartStopChannel::Sounding);
    const double chord_seconds = caretSeconds(fixture.view);
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    CHECK(caretChannel(fixture.view) == common::core::ChartStopChannel::Sounding);
    CHECK(caretSeconds(fixture.view) > chord_seconds);
}

// A nudged note carries the caret with it, and the caret is ON a stop: the charter typing into the
// held one who nudges the note must find the next digit still stating that stop, not the sounding
// fret beside it.
TEST_CASE("A nudged note carries the caret's stop with it", "[core][chart]")
{
    common::core::Chart tapped = makeTappedShapeChart();
    tapped.notes[2].held = 7;
    HeldStopFixture fixture{std::move(tapped)};

    // The satellite press selects the tap and arms its held stop in the one gesture.
    click(fixture.controller, satelliteX(2.5), 140.0f);
    REQUIRE(caretChannel(fixture.view) == common::core::ChartStopChannel::Held);

    // Up one string: the stop is still on a string the chord never holds, so it still states a
    // shape of its own and its satellite still draws.
    fixture.controller.onSelectionMoveRequested(ChartStepDirection::Up);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    REQUIRE(chart->notes[2].string == 4);
    REQUIRE(chart->notes[2].held == std::optional{7});

    CHECK(caretChannel(fixture.view) == common::core::ChartStopChannel::Held);

    // And the consequence that makes it matter: the next digit states the stop the caret is on.
    fixture.controller.onChartFretDigitTyped(3);
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[2].held == std::optional{3});
    CHECK(chart->notes[2].fret == 12);
}

// SATELLITES ARE NOTE-SCOPED, ALWAYS: a satellite is its note's held face, full stop — never a
// bracket's furniture, whatever is selected when it is pressed. What the selection changes is only
// how much of it survives: a press on an UNSELECTED note's satellite selects that note and arms its
// held stop, and a press on a SELECTED one moves the caret there and leaves a wider selection
// standing, since naming a stop inside a selection must not be what takes the selection away.
TEST_CASE("A satellite is its note's held face whatever is selected", "[core][chart]")
{
    common::core::Chart chart = makeTappedShapeChart();
    chart.notes[2].held = 7;
    HeldStopFixture fixture{std::move(chart)};
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    SECTION("unselected, it selects its note and arms that note's held stop")
    {
        click(fixture.controller, satelliteX(2.5), geometry.laneY(3));
        CHECK(caretChannel(fixture.view) == common::core::ChartStopChannel::Held);
        CHECK(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{2});

        // And the digits that follow state THAT NOTE's held stop and nothing else — the sound the
        // picking hand made is not the fretting hand's and never moves.
        fixture.controller.onChartFretDigitTyped(4);
        const common::core::Chart* const edited = chartOrNull(fixture.controller);
        REQUIRE(edited != nullptr);
        REQUIRE(edited->notes.size() == 3);
        CHECK(edited->notes[2].held == std::optional{4});
        CHECK(edited->notes[2].fret == 12);
        // The chord ringing underneath is untouched: the press addressed one note's stop, never a
        // grip the span states across strings.
        CHECK(edited->notes[0].fret == 3);
        CHECK(edited->notes[1].fret == 5);
    }

    SECTION("selected, a wider selection survives the press")
    {
        // A WIDER selection is what makes the handle's preservation observable at all: collapsing
        // to the note aimed at would take the scope away in the very act of naming a stop in it.
        click(fixture.controller, 40.0f, geometry.laneY(1));
        click(fixture.controller, 50.0f, geometry.laneY(3), ChartPointerModifiers{.ctrl = true});
        REQUIRE(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{0, 2});

        click(fixture.controller, satelliteX(2.5), geometry.laneY(3));
        CHECK(caretChannel(fixture.view) == common::core::ChartStopChannel::Held);
        CHECK(chartEditState(fixture.view).selected_notes == std::vector<std::size_t>{0, 2});
    }
}

} // namespace rock_hero::editor::core
