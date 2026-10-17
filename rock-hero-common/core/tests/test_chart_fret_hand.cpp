#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/chart/head_margins.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <span>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Sixteen seconds of default 4/4 — eight bars — which is all the timeline these cases need.
[[nodiscard]] TempoMap makeTempoMap()
{
    return TempoMap::defaultMap(TimeDuration{16.0});
}

// A picked note at a measure and beat, ringing a quarter unless a case says otherwise.
[[nodiscard]] ChartNote noteAt(
    const int measure, const int beat, const int string, const int fret,
    const Fraction ring = Fraction{1, 4})
{
    ChartNote note;
    note.position = GridPosition{.measure = measure, .beat = beat};
    note.string = string;
    note.fret = fret;
    note.sustain = ring;
    return note;
}

// A two-hand tap landing at `fret`.
[[nodiscard]] ChartNote tapAt(const int measure, const int beat, const int string, const int fret)
{
    ChartNote note = noteAt(measure, beat, string, fret);
    note.attack = NoteAttack::Tap;
    return note;
}

// A note claiming a hammer-on or pull-off from the note before it on its string.
[[nodiscard]] ChartNote legatoAt(
    const int measure, const int beat, const int string, const int fret)
{
    ChartNote note = noteAt(measure, beat, string, fret);
    note.attack = NoteAttack::Legato;
    return note;
}

// A note whose fret channel states `fret` at `offset` along its ring.
[[nodiscard]] ChartNote withFretAt(ChartNote note, const Fraction offset, const int fret)
{
    note.keyframes.push_back(Keyframe{.offset = offset, .fret = fret});
    return note;
}

// What one stream demands of the hand, through the spans it implies, as the resolutions derive it,
// under a capo where a case names one.
[[nodiscard]] FretHandDemands demandsOf(
    const ChartConnections& connections, const TempoMap& tempo_map, const int capo = 0)
{
    return fretHandDemands(
        connections,
        deriveChartShapes(connections, HeadMargins{connections.places, tempo_map}),
        capo,
        tempo_map);
}

// The holds one stream derives, through the spans it implies, as the resolutions derive them.
[[nodiscard]] std::vector<FretHandHold> holdsOf(
    const ChartConnections& connections, const TempoMap& tempo_map)
{
    return fretHandHolds(
        connections, deriveChartShapes(connections, HeadMargins{connections.places, tempo_map}), 0);
}

// One stream's spans and the holds derived through them, ring holds first.
struct SpanHolds
{
    ChartShapes shapes;
    std::vector<FretHandHold> holds;
    // How many of `holds` lead as ring holds: the ones a stream derives through no span at all.
    std::size_t ring_holds{0};
};

[[nodiscard]] SpanHolds spanHoldsOf(const std::vector<ChartNote>& notes)
{
    const TempoMap tempo_map = makeTempoMap();
    const ChartConnections connections = chartConnections(notes, tempo_map);
    ChartShapes shapes = deriveChartShapes(connections, HeadMargins{connections.places, tempo_map});
    std::vector<FretHandHold> holds = fretHandHolds(connections, shapes, 0);
    const std::size_t ring_holds = fretHandHolds(connections, ChartShapes{}, 0).size();
    return SpanHolds{
        .shapes = std::move(shapes),
        .holds = std::move(holds),
        .ring_holds = ring_holds,
    };
}

// Where `note`, alone on its string, releases the `held_fret` there, judged from `from`.
[[nodiscard]] std::optional<Fraction> releaseOf(
    const ChartNote& note, const Fraction from, const int held_fret)
{
    return noteReleasesStopAt(chartConnections({note}, makeTempoMap()), 0, from, held_fret);
}

// Whether `holds` carries a hold of exactly `expected`'s fret over exactly its stretch.
[[nodiscard]] bool containsHold(
    const std::span<const FretHandHold> holds, const FretHandHold& expected)
{
    return std::ranges::any_of(holds, [&expected](const FretHandHold& hold) {
        return hold.from == expected.from && hold.to == expected.to && hold.fret == expected.fret;
    });
}

// The hand events the 13 hand-position rules derive from one stream, under a capo where a case
// names one and around the authored positions where a case states any.
[[nodiscard]] std::vector<FretHandEvent> eventsOf(
    const std::vector<ChartNote>& notes, const int capo = 0,
    const std::vector<FretHandPosition>& authored = {})
{
    const TempoMap tempo_map = makeTempoMap();
    const ChartConnections connections = chartConnections(notes, tempo_map);
    return deriveFretHandEvents(
        connections, demandsOf(connections, tempo_map, capo), tempo_map, authored);
}

// An authored position at a measure and beat, with an end fret where a case states one.
[[nodiscard]] FretHandPosition positionAt(
    const int measure, const int beat, const int fret, const std::optional<int> end_fret = {})
{
    return FretHandPosition{
        .position = GridPosition{.measure = measure, .beat = beat},
        .fret = fret,
        .end_fret = end_fret,
    };
}

// Whether one authored position covers every fretted note of a stream sounding at its instant.
[[nodiscard]] bool coveredBy(const std::vector<ChartNote>& notes, const FretHandPosition& position)
{
    const TempoMap tempo_map = makeTempoMap();
    return validateFretHandCoverage(chartResolutions(notes, 0, tempo_map), {position}, tempo_map)
        .has_value();
}

// An expected event: its front at a measure and beat of 4/4, and its window.
[[nodiscard]] FretHandEvent eventAt(
    const int measure, const int beat, const int fret, const int width = g_min_fret_hand_width)
{
    return FretHandEvent{
        .front = Fraction{measure - 1} + Fraction{beat - 1, 4},
        .window = FretWindow{.fret = fret, .width = width},
    };
}

// A picked note sliding out to `exit_fret` at the end of its ring, an eighth unless a case says
// otherwise.
[[nodiscard]] ChartNote slideOutAt(
    const int measure, const int beat, const int string, const int fret, const int exit_fret,
    const Fraction ring = Fraction{1, 8})
{
    return withFretAt(noteAt(measure, beat, string, fret, ring), ring, exit_fret);
}

// The slide-out rides over the hand events one stream derives, around the authored positions where
// a case states any.
[[nodiscard]] std::vector<FretHandRide> ridesOf(
    const std::vector<ChartNote>& notes, const std::vector<FretHandPosition>& authored = {})
{
    const TempoMap tempo_map = makeTempoMap();
    const ChartConnections connections = chartConnections(notes, tempo_map);
    const FretHandDemands demands = demandsOf(connections, tempo_map);
    return deriveFretHandRides(
        connections, demands, deriveFretHandEvents(connections, demands, tempo_map, authored));
}

// An expected ride in whole notes: where it departs and ends, the floors of the window it leaves
// and of the one it travels toward, the note it follows, and where it is back, if anywhere.
struct ExpectedRide
{
    Fraction departure{};
    Fraction end{};
    int standing{0};
    int ridden{0};
    std::size_t note{0};
    std::optional<Fraction> back{};
    int width{g_min_fret_hand_width};
};

[[nodiscard]] FretHandRide rideOf(const ExpectedRide& expected)
{
    return FretHandRide{
        .departure = expected.departure,
        .end = expected.end,
        .standing = FretWindow{.fret = expected.standing, .width = expected.width},
        .ridden = FretWindow{.fret = expected.ridden, .width = expected.width},
        .note = expected.note,
        .back = expected.back,
    };
}

} // namespace

// The projection publishes one chip per hand event, derived and authored alike, so both surfaces
// draw the windows the notes and the charter state, each chip carrying its front's grid position.
// Here a derived window stands before the authored position, the authored one reaches the top of
// the chord it heads, and a fret past its window at the next bar line ends it.
TEST_CASE("Chart projection publishes every hand event as a chip", "[core][chart]")
{
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        noteAt(1, 1, 1, 3),
        noteAt(1, 3, 2, 5),
        noteAt(1, 3, 3, 9),
        noteAt(2, 1, 4, 11),
    };
    chart.fret_hand_positions = {positionAt(1, 3, 5)};
    Arrangement arrangement;
    arrangement.chart = chart;

    const ChartViewState state = makeChartViewState(arrangement, makeTempoMap());
    REQUIRE(state.fret_hand_positions.size() == 3);
    const FhpViewState& before = state.fret_hand_positions[0];
    CHECK(before.position == GridPosition{.measure = 1, .beat = 1});
    CHECK(before.fret == 3);
    CHECK(before.width == 4);
    CHECK(before.authored == FhpAuthored::Nothing);
    const FhpViewState& stated = state.fret_hand_positions[1];
    CHECK(stated.position == GridPosition{.measure = 1, .beat = 3});
    CHECK(stated.fret == 5);
    CHECK(stated.width == 5);
    CHECK(stated.authored == FhpAuthored::Fret);
    const FhpViewState& after = state.fret_hand_positions[2];
    CHECK(after.position == GridPosition{.measure = 2, .beat = 1});
    CHECK(after.fret == 11);
    CHECK(after.authored == FhpAuthored::Nothing);

    // An authored end is the window's end, and published as authored, which is what the chip
    // prints as a range. The chord's fret 9 above it is struck at the position's own instant, which
    // the position owns, so it does not end the position.
    arrangement.chart->fret_hand_positions.front().end_fret = 7;
    const ChartViewState ended = makeChartViewState(arrangement, makeTempoMap());
    REQUIRE(ended.fret_hand_positions.size() == 3);
    CHECK(ended.fret_hand_positions[1].width == 3);
    CHECK(ended.fret_hand_positions[1].authored == FhpAuthored::FretAndEnd);
}

// A placement is a marker, so it may start only where every marker may: on the grid and strictly
// before the closing barline, which governs a passage of no length.
TEST_CASE("Fret-hand validation refuses a placement on the closing barline", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    ChartTuning tuning;
    tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    const GridPosition terminal = terminalGridPosition(tempo_map);

    CHECK_FALSE(validateFretHandPositions(
                    {FretHandPosition{.position = terminal, .fret = 5}}, tuning, tempo_map)
                    .has_value());
    CHECK(validateFretHandPositions({positionAt(1, 1, 5)}, tuning, tempo_map).has_value());
}

// An authored end below the index finger is a window reaching nowhere, and no fit can say which of
// the two frets was meant, so it is refused; an end at the finger is a one-fret window and stands.
TEST_CASE("Fret-hand validation refuses an end fret below the index finger", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    ChartTuning tuning;
    tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};

    CHECK_FALSE(validateFretHandPositions({positionAt(1, 1, 5, 4)}, tuning, tempo_map).has_value());
    CHECK(validateFretHandPositions({positionAt(1, 1, 5, 5)}, tuning, tempo_map).has_value());
}

// An authored position covers every fretted note sounding at its instant: one struck there, and one
// ringing through from before, each read against the fret the position states and its end where
// one is stated.
TEST_CASE("Fret-hand coverage refuses a sounding note outside the stated window", "[core][chart]")
{
    // Struck at the instant.
    CHECK_FALSE(coveredBy({noteAt(1, 1, 1, 3)}, positionAt(1, 1, 5)));
    CHECK(coveredBy({noteAt(1, 1, 1, 5)}, positionAt(1, 1, 5)));
    CHECK_FALSE(coveredBy({noteAt(1, 1, 1, 9)}, positionAt(1, 1, 5, 7)));
    CHECK(coveredBy({noteAt(1, 1, 1, 7)}, positionAt(1, 1, 5, 7)));

    // A half note struck on beat 1 still sounds on beat 2.
    CHECK_FALSE(coveredBy({noteAt(1, 1, 1, 3, Fraction{1, 2})}, positionAt(1, 2, 5)));
    CHECK_FALSE(coveredBy({noteAt(1, 1, 1, 9, Fraction{1, 2})}, positionAt(1, 2, 5, 7)));
    CHECK(coveredBy({noteAt(1, 1, 1, 6, Fraction{1, 2})}, positionAt(1, 2, 5, 7)));
}

// A ring reaching exactly to the instant is not there; an open string and a tap demand nothing of
// the fretting hand; and with no end stated, nothing above the stated fret is outside the window.
TEST_CASE("Fret-hand coverage leaves what the hand does not hold alone", "[core][chart]")
{
    CHECK(coveredBy({noteAt(1, 1, 1, 3)}, positionAt(1, 2, 5)));
    CHECK(coveredBy({noteAt(1, 1, 1, 0, Fraction{1}), tapAt(1, 1, 2, 15)}, positionAt(1, 1, 5, 7)));
    CHECK(coveredBy({noteAt(1, 1, 1, 20)}, positionAt(1, 1, 5)));
}

// A dead note's stored ring proves its finger held the stop exactly as a live note's does; only
// its tail's display differs.
TEST_CASE("Fret-hand coverage holds a dead note's ring like any other", "[core][chart]")
{
    ChartNote dead = noteAt(1, 1, 1, 3, Fraction{1, 2});
    dead.dead = true;

    const TempoMap tempo_map = makeTempoMap();
    const std::vector<FretHandHold> holds = holdsOf(chartConnections({dead}, tempo_map), tempo_map);
    REQUIRE(holds.size() == 1);
    CHECK(holds[0].from == Fraction{});
    CHECK(holds[0].to == Fraction{1, 2});
    CHECK(holds[0].fret == 3);
    CHECK_FALSE(coveredBy({dead}, positionAt(1, 2, 5)));
}

// A slide-out's finger stays pressed while it slides, so the stop it slides from is held, and
// demanded, until the ring ends.
TEST_CASE("Fret-hand coverage holds a slide-out's stop to its ring end", "[core][chart]")
{
    const ChartNote sliding_out = slideOutAt(1, 1, 1, 5, 1, Fraction{1, 2});

    const TempoMap tempo_map = makeTempoMap();
    const std::vector<FretHandHold> holds =
        holdsOf(chartConnections({sliding_out}, tempo_map), tempo_map);
    REQUIRE(holds.size() == 1);
    CHECK(holds[0].from == Fraction{});
    CHECK(holds[0].to == Fraction{1, 2});
    CHECK(holds[0].fret == 5);
    CHECK_FALSE(coveredBy({sliding_out}, positionAt(1, 2, 7)));
    CHECK(coveredBy({sliding_out}, positionAt(1, 3, 7)));
}

// A span's grip holds every pressed stop from the span's front until its string releases it, else
// to the close: a free string joining a ringing chord is held from the front, and a pull-off source
// riding above the grip's stop is another finger, which leaves that stop down beneath it (shape
// rule 7). The finger on the stop sliding away releases it, and so do the open string and a tap on
// the stop, while a tap above it or a fretting-hand restrike of it leaves it held. The grip holds
// follow every ring hold.
TEST_CASE("A span's grip is held from its front to its close", "[core][chart]")
{
    SECTION("a chord ringing while a free string joins")
    {
        const SpanHolds derived = spanHoldsOf({
            noteAt(1, 1, 1, 5, Fraction{1}),
            noteAt(1, 1, 2, 7, Fraction{1}),
            noteAt(1, 3, 3, 9),
        });
        REQUIRE(derived.shapes.shapes.size() == 1);
        const ChartShape& shape = derived.shapes.shapes.front();
        const Fraction close = shape.place + shape.sustain;
        const std::span<const FretHandHold> grip =
            std::span{derived.holds}.subspan(derived.ring_holds);
        CHECK(grip.size() == 3);
        CHECK(containsHold(grip, FretHandHold{.from = shape.place, .to = close, .fret = 5}));
        CHECK(containsHold(grip, FretHandHold{.from = shape.place, .to = close, .fret = 7}));
        CHECK(containsHold(grip, FretHandHold{.from = shape.place, .to = close, .fret = 9}));
    }

    SECTION("a pull-off source riding above the grip's stop")
    {
        // The 5's ring ends where its string is struck again, so only the grip can hold it there.
        const SpanHolds derived = spanHoldsOf({
            noteAt(1, 1, 1, 5),
            noteAt(1, 1, 2, 7, Fraction{1}),
            noteAt(1, 2, 1, 7),
            legatoAt(1, 3, 1, 5),
        });
        REQUIRE(derived.shapes.shapes.size() == 1);
        const ChartShape& shape = derived.shapes.shapes.front();
        const Fraction close = shape.place + shape.sustain;
        const std::span<const FretHandHold> grip =
            std::span{derived.holds}.subspan(derived.ring_holds);
        CHECK(grip.size() == 2);
        CHECK(containsHold(grip, FretHandHold{.from = shape.place, .to = close, .fret = 5}));
        const std::span<const FretHandHold> rings =
            std::span{derived.holds}.first(derived.ring_holds);
        CHECK(containsHold(
            rings, FretHandHold{.from = Fraction{1, 4}, .to = Fraction{1, 2}, .fret = 7}));
    }

    SECTION("a chord whose fingers slide off the grip")
    {
        // One travel (shape rule 4): the fingers glide through 6/8 and land on 7/9 at 1/4, where
        // the span closes. The grip is released at the fingers' first statement off it.
        const SpanHolds derived = spanHoldsOf({
            withFretAt(
                withFretAt(noteAt(1, 1, 1, 5, Fraction{1, 2}), Fraction{1, 8}, 6),
                Fraction{1, 4},
                7),
            withFretAt(
                withFretAt(noteAt(1, 1, 2, 7, Fraction{1, 2}), Fraction{1, 8}, 8),
                Fraction{1, 4},
                9),
        });
        REQUIRE_FALSE(derived.shapes.shapes.empty());
        const ChartShape& shape = derived.shapes.shapes.front();
        REQUIRE(shape.place == Fraction{});
        REQUIRE(shape.sustain == Fraction{1, 4});
        const Fraction slide = shape.place + Fraction{1, 8};
        const std::span<const FretHandHold> grip =
            std::span{derived.holds}.subspan(derived.ring_holds);
        CHECK(containsHold(grip, FretHandHold{.from = shape.place, .to = slide, .fret = 5}));
        CHECK(containsHold(grip, FretHandHold{.from = shape.place, .to = slide, .fret = 7}));
        CHECK_FALSE(
            containsHold(grip, FretHandHold{.from = shape.place, .to = Fraction{1, 4}, .fret = 5}));
        const std::span<const FretHandHold> rings =
            std::span{derived.holds}.first(derived.ring_holds);
        CHECK(containsHold(rings, FretHandHold{.from = slide, .to = Fraction{1, 4}, .fret = 6}));
        CHECK(containsHold(rings, FretHandHold{.from = slide, .to = Fraction{1, 4}, .fret = 8}));
    }

    SECTION("a tap on a joining string's stop before the fretting hand strikes it")
    {
        // The tap sounds the 9 before the string joins the grip, so shape rule 3's tap floor has no
        // gripped fret to reach and the span stands; hand-position rule 6 still holds the 9 from
        // the front, and the tap on it says the finger there has lifted.
        const SpanHolds derived = spanHoldsOf({
            noteAt(1, 1, 1, 5, Fraction{1}),
            noteAt(1, 1, 2, 7, Fraction{1}),
            tapAt(1, 2, 3, 9),
            noteAt(1, 3, 3, 9),
        });
        REQUIRE(derived.shapes.shapes.size() == 1);
        const ChartShape& shape = derived.shapes.shapes.front();
        const Fraction close = shape.place + shape.sustain;
        const Fraction tap_onset{1, 4};
        const std::span<const FretHandHold> grip =
            std::span{derived.holds}.subspan(derived.ring_holds);
        CHECK(containsHold(grip, FretHandHold{.from = shape.place, .to = tap_onset, .fret = 9}));
        CHECK_FALSE(containsHold(grip, FretHandHold{.from = shape.place, .to = close, .fret = 9}));
        CHECK(containsHold(grip, FretHandHold{.from = shape.place, .to = close, .fret = 5}));
        CHECK(containsHold(grip, FretHandHold{.from = shape.place, .to = close, .fret = 7}));
    }

    SECTION("a tap above a joining string's stop")
    {
        const SpanHolds derived = spanHoldsOf({
            noteAt(1, 1, 1, 5, Fraction{1}),
            noteAt(1, 1, 2, 7, Fraction{1}),
            tapAt(1, 2, 3, 11),
            noteAt(1, 3, 3, 9),
        });
        REQUIRE(derived.shapes.shapes.size() == 1);
        const ChartShape& shape = derived.shapes.shapes.front();
        const Fraction close = shape.place + shape.sustain;
        const std::span<const FretHandHold> grip =
            std::span{derived.holds}.subspan(derived.ring_holds);
        CHECK(containsHold(grip, FretHandHold{.from = shape.place, .to = close, .fret = 9}));
    }

    SECTION("a fretting-hand restrike of a joining string's stop")
    {
        const SpanHolds derived = spanHoldsOf({
            noteAt(1, 1, 1, 5, Fraction{1}),
            noteAt(1, 1, 2, 7, Fraction{1}),
            noteAt(1, 2, 3, 9),
            noteAt(1, 3, 3, 9),
        });
        REQUIRE(derived.shapes.shapes.size() == 1);
        const ChartShape& shape = derived.shapes.shapes.front();
        const Fraction close = shape.place + shape.sustain;
        const std::span<const FretHandHold> grip =
            std::span{derived.holds}.subspan(derived.ring_holds);
        CHECK(containsHold(grip, FretHandHold{.from = shape.place, .to = close, .fret = 9}));
    }

    SECTION("an open strike on a gripped string")
    {
        // The open strike sounds the string with no finger down, so it releases the grip's 5. The
        // walk closes the span at that strike too, so the release and the close coincide.
        const SpanHolds derived = spanHoldsOf({
            noteAt(1, 1, 1, 5, Fraction{1}),
            noteAt(1, 1, 2, 7, Fraction{1}),
            noteAt(1, 3, 1, 0),
        });
        REQUIRE(derived.shapes.shapes.size() == 1);
        const ChartShape& shape = derived.shapes.shapes.front();
        const Fraction close = shape.place + shape.sustain;
        const Fraction open_onset{1, 2};
        CHECK(close == open_onset);
        const std::span<const FretHandHold> grip =
            std::span{derived.holds}.subspan(derived.ring_holds);
        CHECK(containsHold(grip, FretHandHold{.from = shape.place, .to = close, .fret = 5}));
        CHECK(containsHold(grip, FretHandHold{.from = shape.place, .to = close, .fret = 7}));
    }
}

// Hand-position rule 6's release: the note gripping a stop lets go of it where its finger slides
// away, and any other note where it sounds the string open, at a node, or at or below the stop — a
// tap on the stop included; a note above is another finger and leaves the stop held.
TEST_CASE("A note releases a held stop where its channel leaves it held", "[core][chart]")
{
    SECTION("a tap struck on the held fret releases it at its onset")
    {
        CHECK(releaseOf(tapAt(1, 2, 1, 5), Fraction{}, 5) == Fraction{1, 4});
    }

    SECTION("a tap above the held fret never releases it")
    {
        CHECK_FALSE(releaseOf(tapAt(1, 2, 1, 7), Fraction{}, 5).has_value());
    }

    SECTION("the gripping note sliding away releases at its new fret")
    {
        const ChartNote sliding = withFretAt(noteAt(1, 1, 1, 5, Fraction{1, 2}), Fraction{1, 4}, 7);
        CHECK(releaseOf(sliding, Fraction{}, 5) == Fraction{1, 4});
    }

    SECTION("a note above the stop never releases it")
    {
        CHECK_FALSE(releaseOf(noteAt(1, 2, 1, 7, Fraction{1, 2}), Fraction{}, 5).has_value());
    }

    SECTION("a note below the stop releases it at its onset")
    {
        CHECK(releaseOf(noteAt(1, 3, 1, 3), Fraction{}, 5) == Fraction{1, 2});
    }

    SECTION("a note sliding down from above releases where it reaches the stop")
    {
        const ChartNote falling = withFretAt(noteAt(1, 1, 1, 7, Fraction{1}), Fraction{1, 2}, 4);
        CHECK(releaseOf(falling, Fraction{1, 4}, 5) == Fraction{1, 2});
    }

    SECTION("a note sliding exactly onto the held fret releases where it lands")
    {
        // Two fingers cannot share one fret on one string, so arriving AT it proves the lift.
        const ChartNote landing = withFretAt(noteAt(1, 1, 1, 7, Fraction{1}), Fraction{1, 2}, 5);
        CHECK(releaseOf(landing, Fraction{1, 4}, 5) == Fraction{1, 2});
    }

    SECTION("the open string releases it")
    {
        CHECK(releaseOf(noteAt(1, 2, 1, 0), Fraction{}, 5) == Fraction{1, 4});
    }

    SECTION("a natural harmonic's node releases it")
    {
        ChartNote chime = noteAt(1, 2, 1, 0);
        chime.harmonic_node = 12.0;
        CHECK(releaseOf(chime, Fraction{}, 5) == Fraction{1, 4});
    }

    SECTION("a ring ended before the judgement begins releases nothing")
    {
        CHECK_FALSE(releaseOf(noteAt(1, 1, 1, 0), Fraction{1, 4}, 5).has_value());
    }
}

// A natural harmonic's node is demanded at its strike, and its chime holds nothing after it,
// exactly as the hand derivation's ring pins read it.
TEST_CASE("Fret-hand coverage demands a natural harmonic's node only", "[core][chart]")
{
    ChartNote chime = noteAt(1, 1, 1, 0, Fraction{1, 2});
    chime.harmonic_node = 12.0;

    CHECK_FALSE(coveredBy({chime}, positionAt(1, 1, 13)));
    CHECK(coveredBy({chime}, positionAt(1, 1, 12)));
    CHECK(coveredBy({chime}, positionAt(1, 2, 1, 4)));
}

// A natural harmonic is touched over a wire, which equal temperament puts its node a hair to either
// side of: the hand is demanded at the nearest fret, so a window ending on that fret covers it.
TEST_CASE("A natural harmonic demands the fret nearest its node", "[core][chart]")
{
    const auto chime = [](const double node) {
        ChartNote note = noteAt(1, 1, 1, 0);
        note.harmonic_node = node;
        return note;
    };
    CHECK(fretHandDemandAt(chime(7.01955), Fraction{}, 0) == std::optional{7});
    CHECK(fretHandDemandAt(chime(4.98045), Fraction{}, 0) == std::optional{5});
    CHECK(fretHandDemandAt(chime(19.01955), Fraction{}, 0) == std::optional{19});
    CHECK(coveredBy({chime(19.01955)}, positionAt(1, 1, 7, 19)));
}

// The coverage authority reports every breaking position once, ascending, with the first demand it
// misses; a position's verdict reads the notes alone, so its neighbours never change it.
TEST_CASE("Fret-hand coverage finds every position that breaks it", "[core][chart]")
{
    const std::vector<ChartNote> notes = {
        noteAt(1, 1, 1, 3, Fraction{1, 2}),
        noteAt(2, 1, 1, 9),
    };
    const std::vector<FretHandPosition> positions = {
        positionAt(1, 2, 5), positionAt(1, 3, 5), positionAt(2, 1, 5, 8)
    };
    const TempoMap tempo_map = makeTempoMap();
    const ChartResolutions resolved = chartResolutions(notes, 0, tempo_map);
    const std::vector<UncoveringFretHandPosition> uncovering =
        uncoveringFretHandPositions(resolved, positions, tempo_map);
    REQUIRE(uncovering.size() == 2);
    CHECK(uncovering[0].position == 0);
    CHECK(uncovering[0].fret == 3);
    CHECK(uncovering[0].gripped_from == GridPosition{.measure = 1, .beat = 1});
    CHECK(uncovering[1].position == 2);
    CHECK(uncovering[1].fret == 9);

    // Without the breaking positions, the one left still passes.
    CHECK(uncoveringFretHandPositions(resolved, {positions[1]}, tempo_map).empty());
}

// Hand-position rule 1: an open string and a tap constrain nothing; the window is the fretted
// notes' alone.
TEST_CASE("Hand derivation ignores open strings and taps", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), tapAt(1, 2, 2, 15), noteAt(1, 3, 3, 0)}) ==
        std::vector{eventAt(1, 1, 5)});
    CHECK(eventsOf({noteAt(1, 1, 1, 0), tapAt(1, 2, 2, 15)}).empty());
}

// Hand-position rule 1's fretted note is where the fretting finger is: a natural harmonic's is on
// its node.
TEST_CASE("Hand derivation places a natural harmonic at its node's fret", "[core][chart]")
{
    ChartNote harmonic = noteAt(1, 1, 1, 0);
    harmonic.harmonic_node = 12.0;
    CHECK(eventsOf({harmonic}) == std::vector{eventAt(1, 1, 12)});
}

// Hand-position rule 2: a pitched keyframe fret statement demands its fret at its own place; a
// slide-out, where pressure is already off, demands nothing.
TEST_CASE("Hand derivation follows a pitched keyframe but not a slide-out", "[core][chart]")
{
    const ChartNote slid = withFretAt(noteAt(1, 1, 1, 5, Fraction{1, 2}), Fraction{1, 4}, 12);
    CHECK(eventsOf({slid}) == std::vector{eventAt(1, 1, 5), eventAt(1, 2, 12)});

    const ChartNote sliding_out =
        withFretAt(noteAt(1, 1, 1, 5, Fraction{1, 2}), Fraction{1, 2}, 12);
    CHECK(eventsOf({sliding_out}) == std::vector{eventAt(1, 1, 5)});
}

// A keyframe's demand lands on a struck instant without making it a chord: only strikes count, so
// the run keeps the single note and grows to cover both.
TEST_CASE("Hand derivation never counts a keyframe demand toward a chord", "[core][chart]")
{
    const ChartNote held = withFretAt(noteAt(1, 1, 1, 5, Fraction{1, 2}), Fraction{1, 4}, 5);
    CHECK(eventsOf({held, noteAt(1, 2, 2, 3)}) == std::vector{eventAt(1, 1, 3)});
}

// The hand-position slide rule: a finger sliding from one pressed stop to another carries the hand
// with it, so the window at the arrival is the standing one shifted by the travel — over an
// authored position, over a derived window, and over one that already covers the arrival.
TEST_CASE("Hand derivation shifts the window by a slide's travel", "[core][chart]")
{
    const ChartNote slid = withFretAt(noteAt(1, 1, 2, 5, Fraction{1, 2}), Fraction{1, 4}, 7);
    CHECK(
        eventsOf({slid}, 0, {positionAt(1, 1, 4)}) ==
        std::vector{eventAt(1, 1, 4), eventAt(1, 2, 6)});
    CHECK(eventsOf({slid}) == std::vector{eventAt(1, 1, 5), eventAt(1, 2, 7)});

    const ChartNote nudged = withFretAt(noteAt(1, 1, 2, 5, Fraction{1, 2}), Fraction{1, 4}, 6);
    CHECK(eventsOf({nudged}) == std::vector{eventAt(1, 1, 5), eventAt(1, 2, 6)});
}

// A chord whose two fretted strings glide 5 to 7 and back over the open strings: the grip's span
// holds no 5 while the fingers are on 7, so the hand goes up by the travel and comes back down.
TEST_CASE("Hand derivation follows a chord gliding up and back", "[core][chart]")
{
    const auto gliding = [](const int string) {
        return withFretAt(
            withFretAt(noteAt(1, 1, string, 5, Fraction{3, 8}), Fraction{1, 8}, 7),
            Fraction{1, 4},
            5);
    };
    const std::vector<ChartNote> notes{
        noteAt(1, 1, 1, 0, Fraction{3, 8}),
        gliding(2),
        gliding(4),
        noteAt(1, 1, 5, 0, Fraction{3, 8})
    };
    CHECK(
        eventsOf(notes, 0, {positionAt(1, 1, 4)}) ==
        std::vector{
            eventAt(1, 1, 4),
            FretHandEvent{
                .front = Fraction{1, 8},
                .window = FretWindow{.fret = 6, .width = g_min_fret_hand_width},
            },
            FretHandEvent{
                .front = Fraction{1, 4},
                .window = FretWindow{.fret = 4, .width = g_min_fret_hand_width},
            },
        });
}

// Hand-position rule 3: a run closes at a chord, at a bar line, and where the grip outgrows the
// hand.
TEST_CASE("Hand derivation closes a run at a chord, a bar line or an overflow", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 6), noteAt(1, 2, 2, 7)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 2, 6)});
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), noteAt(2, 1, 1, 6), noteAt(2, 2, 1, 7)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(2, 1, 6)});
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 9)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 2, 9)});
}

// Hand-position rule 5: a one-fret run the standing window covers restates nothing; one it does not
// still moves.
TEST_CASE("Hand derivation keeps a covered one-fret run in place", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), noteAt(1, 2, 2, 7), noteAt(2, 1, 1, 6)}) ==
        std::vector{eventAt(1, 1, 5)});
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), noteAt(1, 2, 2, 7), noteAt(2, 1, 1, 12)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(2, 1, 12)});
}

// Hand-position rule 6: nothing moves unforced while a plain ring struck earlier sounds; a forced
// run still moves — keeping the ring it is forced over — and the next run after the ring ends
// re-derives. A chord struck over the ring accumulates a span with it, whose grip is held from the
// ring's onset.
TEST_CASE("Hand derivation holds still while a written ring sounds", "[core][chart]")
{
    const ChartNote ring = noteAt(1, 1, 1, 5, Fraction{3, 4});
    CHECK(
        eventsOf({ring, noteAt(1, 2, 2, 6), noteAt(1, 2, 3, 7)}) == std::vector{eventAt(1, 1, 5)});
    CHECK(
        eventsOf({ring, noteAt(1, 2, 2, 10)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 2, 5, 6)});
    CHECK(
        eventsOf({ring, noteAt(1, 2, 2, 9), noteAt(1, 2, 3, 10)}) ==
        std::vector{eventAt(1, 1, 5, 6)});
    CHECK(
        eventsOf({ring, noteAt(1, 4, 2, 6), noteAt(1, 4, 3, 7)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 4, 6)});
}

// A held ring is a demand for as long as it holds: a window arriving while it sounds reaches it,
// and a ring ending exactly where the next note is struck is no longer there.
TEST_CASE("Hand derivation covers every held ring sounding at an instant", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 3, Fraction{1, 2}), noteAt(1, 2, 2, 8)}) ==
        std::vector{eventAt(1, 1, 3), eventAt(1, 2, 3, 6)});
    CHECK(
        eventsOf({noteAt(1, 1, 1, 3), noteAt(1, 2, 2, 8)}) ==
        std::vector{eventAt(1, 1, 3), eventAt(1, 2, 8)});
}

// A span's grip is held from the span's front to its close, so an arpeggiated five-fret grip gets
// one window from its first strike, reaching the stops not yet struck there.
TEST_CASE("Hand derivation covers an arpeggiated grip from the span's front", "[core][chart]")
{
    const std::vector<ChartNote> arpeggio{
        noteAt(1, 1, 3, 11, Fraction{3, 4}),
        noteAt(1, 2, 2, 7, Fraction{1, 2}),
        noteAt(1, 3, 4, 9, Fraction{1, 4}),
    };
    CHECK(eventsOf(arpeggio) == std::vector{eventAt(1, 1, 7, 5)});
    CHECK_FALSE(coveredBy(arpeggio, positionAt(1, 1, 11)));
    CHECK(coveredBy(arpeggio, positionAt(1, 1, 7, 11)));
}

// A chord span's grip likewise: a stop the grip grows by later is held from the chord.
TEST_CASE("Hand derivation covers a chord span's grown grip from the chord", "[core][chart]")
{
    const std::vector<ChartNote> chord{
        noteAt(1, 1, 1, 5, Fraction{1, 1}),
        noteAt(1, 1, 2, 7, Fraction{1, 1}),
        noteAt(1, 3, 3, 9, Fraction{1, 2}),
    };
    CHECK(eventsOf(chord) == std::vector{eventAt(1, 1, 5, 5)});
    CHECK_FALSE(coveredBy(chord, positionAt(1, 1, 5, 8)));
}

// THE INVARIANT: every derived window covers every hold (\ref fretHandHolds) at every onset and
// every front inside it, with nothing authored and around an authored position the hand-position
// coverage rule (\ref validateFretHandCoverage) passes, which reads the same holds. With nothing
// authored, every window, read as a position stating its fret and its top, passes that rule too.
// The second figure plants a pull-off under a tap while lower rings still sound. Each authored
// position stands mid-ring over silence, where the window's top must reach a ring that ends before
// the next instant.
TEST_CASE("Every derived window covers every hold standing under it", "[core][chart]")
{
    ChartNote tap = noteAt(2, 3, 3, 16);
    tap.attack = NoteAttack::Tap;
    struct Figure
    {
        std::vector<ChartNote> notes;
        FretHandPosition authored;
    };
    const std::vector<Figure> figures{
        {.notes = {noteAt(1, 1, 1, 5, Fraction{3, 4}), noteAt(1, 2, 2, 9), noteAt(1, 2, 3, 10)},
         .authored = positionAt(1, 3, 1)},
        {.notes =
             {noteAt(1, 2, 2, 4, Fraction{3, 2}),
              noteAt(1, 3, 4, 4, Fraction{5, 4}),
              tap,
              legatoAt(2, 4, 3, 11),
              noteAt(3, 1, 2, 7, Fraction{3, 4})},
         .authored = positionAt(3, 3, 3)},
        {.notes =
             {noteAt(1, 1, 3, 11, Fraction{3, 4}),
              noteAt(1, 2, 2, 7, Fraction{1, 2}),
              noteAt(1, 3, 4, 9, Fraction{1, 4}),
              noteAt(2, 1, 1, 2),
              noteAt(2, 2, 1, 14)},
         .authored =
             FretHandPosition{
                 .position = GridPosition{.measure = 1, .beat = 3, .offset = Fraction{1, 2}},
                 .fret = 7,
                 .end_fret = std::nullopt,
             }},
        {.notes =
             {noteAt(1, 1, 1, 5, Fraction{1, 1}),
              noteAt(1, 1, 2, 7, Fraction{1, 1}),
              noteAt(1, 3, 3, 9, Fraction{1, 2}),
              noteAt(1, 4, 4, 12)},
         .authored = positionAt(1, 2, 5)},
        {.notes = {noteAt(1, 1, 1, 10, Fraction{1, 1}), noteAt(2, 1, 2, 3)},
         .authored = positionAt(1, 3, 3)},
    };
    const TempoMap tempo_map = makeTempoMap();
    for (const Figure& figure : figures)
    {
        const ChartConnections connections = chartConnections(figure.notes, tempo_map);
        const FretHandDemands demands = demandsOf(connections, tempo_map);
        const ChartResolutions resolved = chartResolutions(figure.notes, 0, tempo_map);
        REQUIRE(validateFretHandCoverage(resolved, {figure.authored}, tempo_map).has_value());
        const auto check_holds_covered = [&](const std::vector<FretHandEvent>& derived) {
            std::vector<Fraction> instants = connections.places;
            for (const FretHandEvent& front_event : derived)
            {
                instants.push_back(front_event.front);
            }
            for (const FretHandHold& hold : demands.holds)
            {
                for (const Fraction instant : instants)
                {
                    if (instant < hold.from || !(instant < hold.to))
                    {
                        continue;
                    }
                    const auto past = firstFretHandEventAfter(derived, instant);
                    REQUIRE(past != derived.begin());
                    CHECK(std::prev(past)->window.covers(hold.fret));
                }
            }
        };
        const std::vector<FretHandEvent> events =
            deriveFretHandEvents(connections, demands, tempo_map, {});
        check_holds_covered(events);
        check_holds_covered(
            deriveFretHandEvents(connections, demands, tempo_map, {figure.authored}));
        for (const FretHandEvent& event : events)
        {
            const FretHandPosition stated{
                .position = tempo_map.gridPositionAtWholeNote(event.front),
                .fret = event.window.fret,
                .end_fret = event.window.top(),
            };
            CHECK(validateFretHandCoverage(resolved, {stated}, tempo_map).has_value());
        }
    }
}

// Hand-position rule 6's plain ring is any pressed stop: a pinch harmonic pins, a natural harmonic
// (fret 0) does not.
TEST_CASE("Hand derivation pins a pinch harmonic's ring but not a natural's", "[core][chart]")
{
    ChartNote pinch = noteAt(1, 1, 1, 5, Fraction{3, 4});
    pinch.attack = NoteAttack::Pinch;
    pinch.harmonic_node = 17.0;
    CHECK(
        eventsOf({pinch, noteAt(1, 2, 2, 6), noteAt(1, 2, 3, 7)}) == std::vector{eventAt(1, 1, 5)});

    ChartNote natural = noteAt(1, 1, 1, 0, Fraction{3, 4});
    natural.harmonic_node = 5.0;
    CHECK(
        eventsOf({natural, noteAt(1, 2, 2, 6), noteAt(1, 2, 3, 7)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 2, 6)});
}

// Hand-position rule 6's pin runs from the last pitched stop: a slide-then-hold pins at its
// destination, and so does one that then slides out, since the finger stays pressed until the ring
// ends. The slide carries the window to 7, and the figure the bar line cuts under the held 7 widens
// it down to the 6 at once, its front unable to reach back past the hold. That figure never has
// three notes sounding together, so no span holds anything.
TEST_CASE("Hand derivation pins a slide-then-hold at its destination", "[core][chart]")
{
    const std::vector expected{eventAt(1, 1, 5), eventAt(1, 2, 7), eventAt(2, 1, 6)};
    const ChartNote held = withFretAt(noteAt(1, 1, 1, 5, Fraction{3, 2}), Fraction{1, 4}, 7);
    CHECK(eventsOf({held, noteAt(2, 1, 2, 6), noteAt(2, 2, 3, 8)}) == expected);

    const ChartNote slid_out = withFretAt(held, Fraction{3, 2}, 3);
    CHECK(eventsOf({slid_out, noteAt(2, 1, 2, 6), noteAt(2, 2, 3, 8)}) == expected);
}

// Hand-position rule 7: the window sits on the run's lowest fret, wherever in the run it falls.
TEST_CASE("Hand derivation places the window on the run's floor", "[core][chart]")
{
    CHECK(eventsOf({noteAt(1, 1, 1, 7), noteAt(1, 2, 2, 5)}) == std::vector{eventAt(1, 1, 5)});
}

// Hand-position rule 8: the window spans the run's hull, never narrower than four frets.
TEST_CASE("Hand derivation widens the window to a wide grip", "[core][chart]")
{
    CHECK(eventsOf({noteAt(1, 1, 1, 5), noteAt(1, 1, 2, 10)}) == std::vector{eventAt(1, 1, 5, 6)});
}

// Hand-position rule 9: the window fits on the board — never below the capo, never past the last
// fret — the floor winning.
TEST_CASE("Hand window fits above the capo and below the last fret", "[core][chart]")
{
    CHECK(fittedWindowFret(10, g_min_fret_hand_width, 0) == 10);
    CHECK(fittedWindowFret(3, g_min_fret_hand_width, 5) == 6);
    CHECK(fittedWindowFret(g_max_fret, g_min_fret_hand_width, 0) == 21);
    CHECK(fittedWindowFret(20, 6, 0) == 19);

    CHECK(eventsOf({noteAt(1, 1, 1, g_max_fret)}) == std::vector{eventAt(1, 1, 21)});
    CHECK(
        eventsOf({noteAt(1, 1, 1, 20), noteAt(1, 1, 2, g_max_fret)}) ==
        std::vector{eventAt(1, 1, 20, 5)});
    ChartNote bent_open = noteAt(1, 1, 1, 0);
    bent_open.bend = 1.0;
    CHECK(eventsOf({bent_open}, 5) == std::vector{eventAt(1, 1, 6)});
}

// Hand-position rule 10: a front reaches back over onsets that demand nothing, to the first after
// the previous run's last instant and the end of the last ring — the first run to the chart's first
// note.
TEST_CASE("Hand derivation fronts a run at its leading open strings", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 0), noteAt(1, 2, 2, 0), noteAt(1, 3, 1, 5)}) ==
        std::vector{eventAt(1, 1, 5)});
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 5, Fraction{1, 2}),
             noteAt(1, 2, 2, 0),
             noteAt(1, 3, 2, 0),
             noteAt(1, 4, 1, 12)}) == std::vector{eventAt(1, 1, 5), eventAt(1, 3, 12)});
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 5), noteAt(1, 2, 3, 0), noteAt(1, 3, 1, 9), noteAt(1, 3, 2, 10)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 2, 9)});
}

// Hand-position rule 10's reach-back stops at the bar line that broke the run: an open-string
// pickup before it stays with the old window, an open string after it fronts the new one.
TEST_CASE("Hand derivation never reaches back across the breaking bar line", "[core][chart]")
{
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 5), noteAt(1, 4, 2, 0), noteAt(2, 1, 2, 0), noteAt(2, 2, 1, 12)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(2, 1, 12)});
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), noteAt(1, 4, 2, 0), noteAt(2, 2, 1, 12)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(2, 2, 12)});
}

// Hand-position rule 11: a run whose window is the standing one emits nothing.
TEST_CASE("Hand derivation emits only where the window changes", "[core][chart]")
{
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 6), noteAt(2, 1, 1, 5), noteAt(2, 2, 1, 6)}) ==
        std::vector{eventAt(1, 1, 5)});
}

// Hand-position rule 12: a pull-off's plant is a demand at its source's onset — a tap's included,
// which demands nothing itself — so the hand arrives there rather than at the pull-off, still
// covering the 7 ringing through it.
TEST_CASE("Hand derivation covers a plant from its tapped source's onset", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 2, 7, Fraction{1, 2}), tapAt(1, 2, 1, 15), legatoAt(1, 3, 1, 3)}) ==
        std::vector{eventAt(1, 1, 7), eventAt(1, 2, 3, 5)});
}

// Hand-position rule 12: the plant also widens every instant up to its pull-off, so the window over
// the span the chord accumulates with the source, held from the source's onset, still covers the
// waiting finger.
TEST_CASE("Hand derivation widens the instants a plant waits under", "[core][chart]")
{
    CHECK(
        eventsOf(
            {noteAt(1, 3, 1, 7, Fraction{1, 2}),
             noteAt(1, 4, 2, 8),
             noteAt(1, 4, 3, 9),
             legatoAt(2, 1, 1, 5)}) == std::vector{eventAt(1, 3, 5, 5)});
}

// Hand-position rule 13: an open string bent anywhere along its ring is bent behind the nut, so the
// hand is at the nut; an unbent open string asks nothing.
TEST_CASE("Hand derivation puts a behind-the-nut bend at fret one", "[core][chart]")
{
    ChartNote pre_bent = noteAt(1, 1, 1, 0);
    pre_bent.bend = 1.0;
    CHECK(eventsOf({pre_bent}) == std::vector{eventAt(1, 1, 1)});

    ChartNote bent_later = noteAt(1, 1, 1, 0, Fraction{1, 2});
    bent_later.keyframes.push_back(Keyframe{.offset = Fraction{1, 4}, .bend = 1.0});
    CHECK(eventsOf({bent_later}) == std::vector{eventAt(1, 1, 1)});

    CHECK(eventsOf({noteAt(1, 1, 1, 0)}).empty());
}

// The demand is a playable fret: behind the nut under a capo is behind the capo, and a node nearest
// the capo's own fret is reached from the first fret above it — the floor every window and every
// authored position is fitted to, so a position there covers the demand.
TEST_CASE("The hand's demand never lies at or below the capo", "[core][chart]")
{
    ChartNote bent = noteAt(1, 1, 1, 0);
    bent.bend = 1.0;
    CHECK(fretHandDemandAt(bent, Fraction{}, 0) == std::optional{1});
    CHECK(fretHandDemandAt(bent, Fraction{}, 2) == std::optional{3});

    ChartNote chime = noteAt(1, 1, 1, 0);
    chime.harmonic_node = 3.2;
    CHECK(fretHandDemandAt(chime, Fraction{}, 0) == std::optional{3});
    CHECK(fretHandDemandAt(chime, Fraction{}, 3) == std::optional{4});

    const TempoMap tempo_map = makeTempoMap();
    CHECK(validateFretHandCoverage(
              chartResolutions({bent}, 2, tempo_map), {positionAt(1, 1, 3)}, tempo_map)
              .has_value());
    CHECK(validateFretHandCoverage(
              chartResolutions({chime}, 3, tempo_map), {positionAt(1, 1, 4)}, tempo_map)
              .has_value());
}

// An authored position is a row object of its own, so it is emitted even where the window it states
// is the one already standing — the one event hand-position rule 11 never drops.
TEST_CASE("An authored position is an event even where it restates the window", "[core][chart]")
{
    const std::vector notes{noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 6)};
    CHECK(eventsOf(notes) == std::vector{eventAt(1, 1, 5)});
    CHECK(
        eventsOf(notes, 0, {positionAt(1, 2, 5)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 2, 5)});
}

// A stated fret pins the floor and hand-position rule 8 gives the top from there: the top of the
// run the position heads, never under four frets.
TEST_CASE("An authored fret reaches the top of the run it heads", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 7), noteAt(1, 2, 1, 10)}, 0, {positionAt(1, 1, 5)}) ==
        std::vector{eventAt(1, 1, 5, 6)});
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5)}, 0, {positionAt(1, 1, 5)}) == std::vector{eventAt(1, 1, 5)});
}

// A position holds nothing below its fret, so a run dipping below it widens nothing: the chord
// spanning frets 4 to 9 is the derivation's, and the position keeps the four-fret floor.
TEST_CASE("An authored fret never reaches for a run it does not hold", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 2, 1, 4), noteAt(1, 2, 2, 9)}, 0, {positionAt(1, 1, 5)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 2, 4, 6)});
}

// A stated end fret IS the top, however narrow, and the derivation resumes at the first instant
// that falls outside the window.
TEST_CASE("An authored end fret is the window's top", "[core][chart]")
{
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 6), noteAt(1, 3, 1, 7)},
            0,
            {positionAt(1, 1, 5, 6)}) == std::vector{eventAt(1, 1, 5, 2), eventAt(1, 3, 7)});
}

// The notes alone move the hand to a two-fret figure's floor in the next bar; an authored window
// that covers the figure stands through it, bar line and all.
TEST_CASE("An authored position stands through every figure that fits", "[core][chart]")
{
    const std::vector notes{noteAt(1, 1, 1, 5), noteAt(2, 1, 1, 7), noteAt(2, 2, 1, 8)};
    CHECK(eventsOf(notes) == std::vector{eventAt(1, 1, 5), eventAt(2, 1, 7)});
    CHECK(eventsOf(notes, 0, {positionAt(1, 1, 5)}) == std::vector{eventAt(1, 1, 5)});
}

TEST_CASE("The derivation resumes where an authored window is contradicted", "[core][chart]")
{
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 7), noteAt(1, 3, 1, 12), noteAt(1, 4, 1, 13)},
            0,
            {positionAt(1, 1, 5)}) == std::vector{eventAt(1, 1, 5), eventAt(1, 3, 12)});
}

// What sounds at the position's own instant cannot end it: the window stands from there, and a
// stop below its floor stays outside it.
TEST_CASE("An authored position owns its own instant", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 8)}, 0, {positionAt(1, 1, 7)}) ==
        std::vector{eventAt(1, 1, 7)});
}

// The next authored position ends the one before it, and no run crosses it: the first window's
// top is read from the notes before the second position, not from the ones it takes.
TEST_CASE("No run crosses an authored position", "[core][chart]")
{
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 6), noteAt(1, 3, 1, 9), noteAt(1, 4, 1, 12)},
            0,
            {positionAt(1, 1, 5), positionAt(1, 3, 9)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 3, 9)});
}

// Hand-position rule 10 reaches a derived front back over leading open strings, but never past an
// authored position: one standing over silence keeps every onset up to its own instant.
TEST_CASE("A derived front never reaches back past an authored position", "[core][chart]")
{
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 3, Fraction{1, 8}), noteAt(1, 2, 2, 0), noteAt(1, 4, 1, 12)},
            0,
            {positionAt(1, 3, 5, 8)}) ==
        std::vector{eventAt(1, 1, 3), eventAt(1, 3, 5), eventAt(1, 4, 12)});
}

TEST_CASE("An authored position after the last note is still an event", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5)}, 0, {positionAt(2, 1, 9)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(2, 1, 9)});
}

// The hand travels with a slide-out, so the window rides toward the window covering the exit fret
// from where the slide departs to where it ends, and is back at the standing window by the next
// instant the hand covers. That is motion beside the hand events, which stay exactly what the
// notes derive.
TEST_CASE("The window rides a slide-out to its exit and back", "[core][chart]")
{
    const std::vector notes{slideOutAt(1, 1, 1, 5, 1), noteAt(1, 3, 1, 5)};
    CHECK(
        ridesOf(notes) == std::vector{rideOf({
                              .departure = Fraction{},
                              .end = Fraction{1, 8},
                              .standing = 5,
                              .ridden = 1,
                              .note = 0,
                              .back = Fraction{1, 2},
                          })});
    CHECK(eventsOf(notes) == std::vector{eventAt(1, 1, 5)});
}

// A window already covering the exit fret does not move, and neither does the fret hand for a
// tap's slide-out, which is the picking hand's travel.
TEST_CASE("The window does not ride an exit it covers or a tap's slide-out", "[core][chart]")
{
    CHECK(ridesOf({slideOutAt(1, 1, 1, 8, 6), noteAt(1, 2, 1, 5)}).empty());
    CHECK(ridesOf({withFretAt(tapAt(1, 1, 1, 5), Fraction{1, 4}, 1), noteAt(1, 1, 2, 5)}).empty());
}

// A slide-out stays planted only where the hand must act strictly inside it: one ringing past an
// instant the hand covers. One reaching exactly to that instant is ridden, and since the window
// there is the standing one, the ride's way back lands at its very end.
TEST_CASE("A slide-out stays planted only where the hand acts inside it", "[core][chart]")
{
    CHECK(ridesOf({slideOutAt(1, 1, 1, 5, 1, Fraction{1, 2}), noteAt(1, 2, 2, 5)}).empty());
    CHECK(
        ridesOf({slideOutAt(1, 1, 1, 5, 1, Fraction{1, 4}), noteAt(1, 2, 2, 5)}) ==
        std::vector{rideOf({
            .departure = Fraction{},
            .end = Fraction{1, 4},
            .standing = 5,
            .ridden = 1,
            .note = 0,
            .back = Fraction{1, 4},
        })});
    CHECK(
        ridesOf({slideOutAt(1, 1, 1, 5, 1, Fraction{3, 16}), noteAt(1, 2, 2, 5)}) ==
        std::vector{rideOf({
            .departure = Fraction{},
            .end = Fraction{3, 16},
            .standing = 5,
            .ridden = 1,
            .note = 0,
            .back = Fraction{1, 4},
        })});
}

// The way back is a hand event wherever one fronts first, a hand event fronting exactly where the
// slide ends is the window the ride leads into, and one fronting while the slide travels is the
// hand moving on, so that slide-out is not ridden at all.
TEST_CASE("A hand event is the window's way back from a ride, or its departure", "[core][chart]")
{
    CHECK(
        ridesOf({slideOutAt(1, 1, 1, 5, 1), noteAt(2, 1, 1, 12)}) == std::vector{rideOf({
                                                                         .departure = Fraction{},
                                                                         .end = Fraction{1, 8},
                                                                         .standing = 5,
                                                                         .ridden = 1,
                                                                         .note = 0,
                                                                         .back = std::nullopt,
                                                                     })});
    CHECK(
        ridesOf({slideOutAt(1, 1, 1, 12, 8, Fraction{1, 4}), noteAt(1, 2, 2, 2)}) ==
        std::vector{rideOf({
            .departure = Fraction{},
            .end = Fraction{1, 4},
            .standing = 12,
            .ridden = 8,
            .note = 0,
            .back = std::nullopt,
        })});
    CHECK(
        ridesOf(
            {slideOutAt(1, 1, 1, 5, 12, Fraction{1, 2}), noteAt(2, 1, 1, 5)}, {positionAt(1, 2, 9)})
            .empty());
}

// A chain that glides before it slides out departs from its last stop, under the window that stop
// moved the hand to, not from its onset.
TEST_CASE("A chain's ride departs where its slide-out does", "[core][chart]")
{
    const ChartNote chain = withFretAt(
        withFretAt(noteAt(1, 1, 1, 5, Fraction{1, 4}), Fraction{1, 8}, 9), Fraction{1, 4}, 4);
    CHECK(
        ridesOf({chain, noteAt(1, 3, 1, 9)}) == std::vector{rideOf({
                                                    .departure = Fraction{1, 8},
                                                    .end = Fraction{1, 4},
                                                    .standing = 9,
                                                    .ridden = 4,
                                                    .note = 0,
                                                    .back = Fraction{1, 2},
                                                })});
}

// A later ride arriving by an earlier one's way back takes the window on from there, so only the
// later one returns.
TEST_CASE("A later ride takes over an earlier one's way back", "[core][chart]")
{
    CHECK(
        ridesOf(
            {slideOutAt(1, 1, 1, 5, 1),
             slideOutAt(1, 1, 2, 5, 3, Fraction{3, 16}),
             noteAt(1, 3, 1, 5)}) == std::vector{
                                         rideOf({
                                             .departure = Fraction{},
                                             .end = Fraction{1, 8},
                                             .standing = 5,
                                             .ridden = 1,
                                             .note = 0,
                                             .back = std::nullopt,
                                         }),
                                         rideOf({
                                             .departure = Fraction{},
                                             .end = Fraction{3, 16},
                                             .standing = 5,
                                             .ridden = 3,
                                             .note = 1,
                                             .back = Fraction{1, 2},
                                         }),
                                     });
}

// Strings sliding out together ride one window, shifted the least distance that covers every exit.
TEST_CASE("Strings sliding out together ride one window", "[core][chart]")
{
    CHECK(
        ridesOf({slideOutAt(1, 1, 1, 5, 2), slideOutAt(1, 1, 2, 7, 4), noteAt(1, 3, 1, 5)}) ==
        std::vector{rideOf({
            .departure = Fraction{},
            .end = Fraction{1, 8},
            .standing = 5,
            .ridden = 2,
            .note = 0,
            .back = Fraction{1, 2},
        })});
}

// An authored window is ridden like a derived one, keeping its width.
TEST_CASE("An authored position standing over a slide-out gets the same ride", "[core][chart]")
{
    CHECK(
        ridesOf({slideOutAt(1, 1, 1, 7, 12), noteAt(1, 3, 1, 6)}, {positionAt(1, 1, 5, 9)}) ==
        std::vector{rideOf({
            .departure = Fraction{},
            .end = Fraction{1, 8},
            .standing = 5,
            .ridden = 8,
            .note = 0,
            .back = Fraction{1, 2},
            .width = 5,
        })});
}

// THE INVARIANT the position row's identity rests on: the hand events front strictly ascending, so
// no two share a front and none is out of order, with nothing authored and around authored
// positions alike. The figures are the ones that move a front off its own run's first onset — a
// reach back over leading open strings, a slide's arrival, a plant waiting under a tap, a ring a
// window must reach, a bar line cutting a held figure — each beside an authored position standing
// where it can collide with them: on a derived front, between two, off the grid and after the last
// note.
TEST_CASE("The hand events front strictly ascending", "[core][chart]")
{
    ChartNote bent_open = noteAt(2, 1, 1, 0);
    bent_open.bend = 1.0;
    ChartNote harmonic = noteAt(1, 3, 2, 0);
    harmonic.harmonic_node = 12.0;
    struct Figure
    {
        std::vector<ChartNote> notes;
        std::vector<FretHandPosition> authored;
    };
    const std::vector<Figure> figures{
        {.notes =
             {noteAt(1, 1, 1, 5, Fraction{1, 2}),
              noteAt(1, 2, 2, 0),
              noteAt(1, 3, 2, 0),
              noteAt(1, 4, 1, 12),
              noteAt(2, 2, 3, 0),
              noteAt(2, 3, 1, 3)},
         .authored = {positionAt(1, 3, 9), positionAt(2, 3, 3)}},
        {.notes =
             {withFretAt(noteAt(1, 1, 2, 5, Fraction{1, 2}), Fraction{1, 4}, 7),
              noteAt(1, 4, 1, 9),
              withFretAt(noteAt(2, 1, 1, 5, Fraction{3, 2}), Fraction{1, 4}, 7),
              noteAt(3, 1, 2, 6),
              noteAt(3, 2, 3, 8)},
         .authored = {positionAt(1, 2, 6), positionAt(3, 1, 6)}},
        {.notes =
             {noteAt(1, 1, 2, 7, Fraction{1, 2}),
              tapAt(1, 2, 1, 15),
              legatoAt(1, 3, 1, 3),
              harmonic,
              bent_open,
              noteAt(2, 2, 2, 10)},
         .authored = {positionAt(1, 2, 3), positionAt(4, 1, 9)}},
        {.notes =
             {noteAt(1, 1, 1, 3, Fraction{1, 2}),
              noteAt(1, 2, 2, 8),
              noteAt(1, 3, 3, 11, Fraction{3, 4}),
              noteAt(1, 4, 2, 7, Fraction{1, 2}),
              noteAt(2, 1, 4, 9),
              noteAt(2, 2, 1, 14)},
         .authored =
             {FretHandPosition{
                  .position = GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 2}},
                  .fret = 3,
                  .end_fret = 8,
              },
              positionAt(1, 4, 7, 11)}},
        {.notes =
             {slideOutAt(1, 1, 1, 5, 1),
              noteAt(1, 3, 1, 5),
              noteAt(2, 1, 1, 5),
              noteAt(2, 2, 1, 6),
              noteAt(2, 3, 1, 5)},
         .authored = {positionAt(1, 1, 5), positionAt(2, 2, 5)}},
    };
    const auto check_ascending = [](const std::vector<FretHandEvent>& events) {
        REQUIRE_FALSE(events.empty());
        for (std::size_t index = 1; index < events.size(); ++index)
        {
            CHECK(events[index - 1].front < events[index].front);
        }
    };
    for (const Figure& figure : figures)
    {
        check_ascending(eventsOf(figure.notes));
        check_ascending(eventsOf(figure.notes, 0, figure.authored));
    }
}

} // namespace rock_hero::common::core
