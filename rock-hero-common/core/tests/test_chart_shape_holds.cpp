#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Sixteen seconds of default 4/4 — eight bars — so a whole note is one bar from `0` at 1:1, and
// the song ends at 8.
[[nodiscard]] TempoMap makeTempoMap()
{
    return TempoMap::defaultMap(TimeDuration{16.0});
}

// A picked note at a measure, a beat and a fraction of that beat.
[[nodiscard]] ChartNote noteAt(
    const GridPosition position, const int string, const int fret, const Fraction ring)
{
    ChartNote note;
    note.position = position;
    note.string = string;
    note.fret = fret;
    note.sustain = ring;
    return note;
}

// A grid position, `offset` beats past the beat.
[[nodiscard]] GridPosition at(const int measure, const int beat, const Fraction offset = Fraction{})
{
    return GridPosition{.measure = measure, .beat = beat, .offset = offset};
}

// The notes in the chart's own slot order.
[[nodiscard]] std::vector<ChartNote> streamOf(std::vector<ChartNote> notes)
{
    std::ranges::sort(notes, [](const ChartNote& lhs, const ChartNote& rhs) {
        return lhs.position == rhs.position ? lhs.string < rhs.string : lhs.position < rhs.position;
    });
    return notes;
}

// An unnamed hold between two grid positions.
[[nodiscard]] ShapeHold holdOver(const GridPosition start, const GridPosition end)
{
    return ShapeHold{.start = start, .end = end, .grip = UnnamedGrip{}};
}

// The notes' resolutions under the holds, and whether each hold is valid.
struct Held
{
    ChartResolutions resolutions;
    std::vector<bool> valid;
};

[[nodiscard]] Held deriveUnder(
    const std::vector<ChartNote>& notes, const std::vector<ShapeHold>& holds)
{
    const TempoMap tempo_map = makeTempoMap();
    Held held{.resolutions = chartResolutions(notes, holds, 0, tempo_map), .valid = {}};
    for (const ShapeHold& hold : holds)
    {
        held.valid.push_back(shapeHoldIsValid(held.resolutions.shapes, tempo_map, hold));
    }
    return held;
}

// The stop a span's posture holds on a one-based string.
[[nodiscard]] std::optional<ChartStop> stopOf(
    const ChartResolutions& resolutions, const std::size_t span, const int string)
{
    return resolutions.postures[resolutions.shapes[span].posture]
        .stops[static_cast<std::size_t>(string - 1)];
}

} // namespace

// Inside a hold only a hand conflict breaks the span: a higher fret restated on a stated string is
// another finger, so the notation split the derivation makes there is suppressed, and the hold's
// span keeps the stop first stated.
TEST_CASE("A shape hold is one span across a notation split", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(at(1, 1), 1, 5, Fraction{1, 4}),
        noteAt(at(1, 1), 2, 7, Fraction{1, 4}),
        noteAt(at(1, 2), 1, 5, Fraction{1, 4}),
        noteAt(at(1, 2), 2, 9, Fraction{1, 4}),
    });
    REQUIRE(deriveUnder(notes, {}).resolutions.shapes.size() == 2);

    const Held held = deriveUnder(notes, {holdOver(at(1, 1), at(1, 3))});
    REQUIRE(held.resolutions.shapes.size() == 1);
    CHECK(held.resolutions.shapes[0].authored);
    CHECK(held.resolutions.shapes[0].place == Fraction{});
    CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 2});
    CHECK(stopOf(held.resolutions, 0, 2) == std::optional{fretStop(7)});
    CHECK(held.valid[0]);
}

// A hand conflict inside a hold crops its span there: a lower fret releases the stated stop
// (hand-position rule 6), so the published extent falls short of the stated one.
TEST_CASE("A hand conflict crops a shape hold's span", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(at(1, 1), 1, 5, Fraction{1, 4}),
        noteAt(at(1, 1), 2, 7, Fraction{1, 4}),
        noteAt(at(1, 2), 1, 3, Fraction{1, 4}),
        noteAt(at(1, 2), 2, 7, Fraction{1, 4}),
    });
    const Held held = deriveUnder(notes, {holdOver(at(1, 1), at(1, 3))});
    REQUIRE_FALSE(held.resolutions.shapes.empty());
    CHECK(held.resolutions.shapes[0].authored);
    CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 4});
    CHECK_FALSE(held.valid[0]);
}

// At or after its stated end the span closes wherever the walk would close a span standing there:
// an end where nothing closes one is invalid, so the span runs on to the first close; an end where
// a stated stop has stopped sounding, or at the next hold's start, or at the song's end, is valid.
TEST_CASE("A shape hold's end must be a break", "[core][chart]")
{
    const std::vector<ChartNote> chord = streamOf({
        noteAt(at(1, 1), 1, 5, Fraction{1, 2}),
        noteAt(at(1, 1), 2, 7, Fraction{1, 2}),
    });

    SECTION("an end in the middle of a sustain is invalid, the span running to the break")
    {
        const Held held = deriveUnder(chord, {holdOver(at(1, 1), at(1, 2))});
        REQUIRE(held.resolutions.shapes.size() == 1);
        CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 2});
        CHECK_FALSE(held.valid[0]);
    }

    SECTION("an end where one stated stop has stopped sounding is valid, the rest ringing on")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(at(1, 1), 1, 5, Fraction{1, 4}),
            noteAt(at(1, 1), 2, 7, Fraction{1, 2}),
        });
        CHECK(deriveUnder(notes, {holdOver(at(1, 1), at(1, 2))}).valid[0]);
    }

    SECTION("the next hold's start is a break")
    {
        const Held held =
            deriveUnder(chord, {holdOver(at(1, 1), at(1, 2)), holdOver(at(1, 2), at(1, 3))});
        CHECK(held.valid[0]);
        CHECK(held.valid[1]);
    }

    SECTION("the song's end is a break")
    {
        const std::vector<ChartNote> last = streamOf({
            noteAt(at(8, 1), 1, 5, Fraction{2}),
            noteAt(at(8, 1), 2, 7, Fraction{2}),
        });
        CHECK(deriveUnder(last, {holdOver(at(8, 1), at(9, 1))}).valid[0]);
    }

    SECTION("a finger landing on a string ringing open is a break")
    {
        const std::vector<ChartNote> notes = streamOf({
            noteAt(at(1, 1), 1, 5, Fraction{1, 2}),
            noteAt(at(1, 1), 2, 0, Fraction{1, 4}),
            noteAt(at(1, 2), 2, 4, Fraction{1, 4}),
        });
        CHECK(deriveUnder(notes, {holdOver(at(1, 1), at(1, 2))}).valid[0]);
    }

    SECTION(
        "a tap renewing the rings at the end leaves the end where the fretting hand's rings end")
    {
        ChartNote low_tap = noteAt(at(1, 2), 1, 12, Fraction{1, 4});
        low_tap.attack = NoteAttack::Tap;
        ChartNote high_tap = noteAt(at(1, 2), 2, 14, Fraction{1, 4});
        high_tap.attack = NoteAttack::Tap;
        const std::vector<ChartNote> notes = streamOf({
            noteAt(at(1, 1), 1, 5, Fraction{1, 4}),
            noteAt(at(1, 1), 2, 7, Fraction{1, 4}),
            low_tap,
            high_tap,
        });
        const ChartResolutions plain = deriveUnder(notes, {}).resolutions;
        REQUIRE(plain.shapes.size() == 1);
        REQUIRE(plain.shapes[0].sustain == Fraction{1, 4});
        CHECK(deriveUnder(notes, {holdOver(at(1, 1), at(1, 2))}).valid[0]);
    }
}

// A superset restatement — the grip restruck whole with a finger added on a silent string — splits
// the derived span, while a hold across it keeps one span; the grip was stated only in part at the
// hold's start, so the span sounds in parts. The notes after the hold derive as with no hold.
TEST_CASE("A shape hold keeps one arpeggio across a superset restatement", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(at(1, 1), 1, 3, Fraction{1, 4}),
        noteAt(at(1, 1), 2, 3, Fraction{1, 4}),
        noteAt(at(1, 2), 1, 3, Fraction{1, 8}),
        noteAt(at(1, 2), 2, 3, Fraction{1, 8}),
        noteAt(at(1, 2), 3, 5, Fraction{1, 8}),
        noteAt(at(1, 2, Fraction{1, 2}), 1, 3, Fraction{1, 8}),
        noteAt(at(1, 2, Fraction{1, 2}), 2, 3, Fraction{1, 8}),
    });
    const ChartResolutions plain = deriveUnder(notes, {}).resolutions;
    REQUIRE(plain.shapes.size() == 3);
    REQUIRE(plain.shapes[1].place == Fraction{1, 4});

    const Held held = deriveUnder(notes, {holdOver(at(1, 1), at(1, 2, Fraction{1, 2}))});
    REQUIRE(held.resolutions.shapes.size() == 2);
    CHECK(held.resolutions.shapes[0].authored);
    CHECK(held.resolutions.shapes[0].place == Fraction{});
    CHECK(held.resolutions.shapes[0].sustain == Fraction{3, 8});
    CHECK(stopOf(held.resolutions, 0, 3) == std::optional{fretStop(5)});
    CHECK(held.resolutions.arrivals[0]);
    // The posture table orders its rows by first sighting, so the after-span's row is compared by
    // what it holds rather than by its index.
    ChartShape after = held.resolutions.shapes[1];
    CHECK(held.resolutions.postures[after.posture] == plain.postures[plain.shapes[2].posture]);
    after.posture = plain.shapes[2].posture;
    CHECK(after == plain.shapes[2]);
    CHECK(held.valid[0]);
}

// A finger landing on a string ringing open closes the derived span, and is notation inside a
// hold: the span stays whole.
TEST_CASE("A shape hold spans a finger landing on an open ring", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(at(1, 1), 1, 5, Fraction{1, 2}),
        noteAt(at(1, 1), 2, 0, Fraction{1, 4}),
        noteAt(at(1, 2), 2, 4, Fraction{1, 4}),
    });
    const ChartResolutions plain = deriveUnder(notes, {}).resolutions;
    REQUIRE(plain.shapes.size() == 1);
    REQUIRE(plain.shapes[0].sustain == Fraction{1, 4});
    const Held held = deriveUnder(notes, {holdOver(at(1, 1), at(1, 3))});
    REQUIRE(held.resolutions.shapes.size() == 1);
    CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 2});
    CHECK(held.valid[0]);
}

// Rests are legal inside a hold: a member's silence is notation, so the span runs on over the rest
// and the restrike after it, and a hold ending in a rest ends where its statement is no longer in
// force.
TEST_CASE("A shape hold spans a rest", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(at(1, 1), 1, 5, Fraction{1, 4}),
        noteAt(at(1, 1), 2, 7, Fraction{1, 4}),
        noteAt(at(1, 3), 1, 5, Fraction{1, 4}),
        noteAt(at(1, 3), 2, 7, Fraction{1, 4}),
    });
    REQUIRE(deriveUnder(notes, {}).resolutions.shapes.size() == 2);

    SECTION("a hold over the rest is one span and valid")
    {
        const Held held = deriveUnder(notes, {holdOver(at(1, 1), at(2, 1))});
        REQUIRE(held.resolutions.shapes.size() == 1);
        CHECK(held.resolutions.shapes[0].sustain == Fraction{1});
        CHECK(held.valid[0]);
    }

    SECTION("a hold ending in the rest is valid")
    {
        const Held held = deriveUnder(notes, {holdOver(at(1, 1), at(1, 3))});
        REQUIRE_FALSE(held.resolutions.shapes.empty());
        CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 2});
        CHECK(held.valid[0]);
    }
}

// A hold whose notes never found a span still publishes its span, with the empty posture, and is
// valid: the notes say nothing against it.
TEST_CASE("A shape hold whose notes found nothing publishes an empty span", "[core][chart]")
{
    const std::vector<ChartNote> notes{noteAt(at(1, 1), 1, 5, Fraction{1, 4})};
    const Held held = deriveUnder(notes, {holdOver(at(1, 1), at(2, 1))});
    REQUIRE(held.resolutions.shapes.size() == 1);
    CHECK(held.resolutions.shapes[0].authored);
    CHECK(held.resolutions.shapes[0].sustain == Fraction{1});
    CHECK(
        std::ranges::none_of(
            held.resolutions.postures[held.resolutions.shapes[0].posture].stops,
            [](const std::optional<ChartStop>& stop) { return stop.has_value(); }));
    CHECK(held.valid[0]);
}

// A none hold publishes no arpeggio span inside it, while a chord struck inside keeps its box; the
// notes after the hold derive as they do with no hold.
TEST_CASE("A none hold drops an arpeggio span and keeps a chord box", "[core][chart]")
{
    ShapeHold none = holdOver(at(1, 1), at(2, 1));
    none.grip = NoneGrip{};
    const std::vector<ChartNote> after{
        noteAt(at(3, 1), 1, 3, Fraction{1, 4}),
        noteAt(at(3, 1), 2, 5, Fraction{1, 4}),
    };

    SECTION("an arpeggio inside is dropped")
    {
        std::vector<ChartNote> notes{
            noteAt(at(1, 1), 1, 5, Fraction{3, 4}),
            noteAt(at(1, 2), 2, 7, Fraction{1, 2}),
            noteAt(at(1, 3), 3, 7, Fraction{1, 4}),
        };
        notes.insert(notes.end(), after.begin(), after.end());
        notes = streamOf(notes);
        REQUIRE(deriveUnder(notes, {}).resolutions.shapes.size() == 2);
        const Held held = deriveUnder(notes, {none});
        REQUIRE(held.resolutions.shapes.size() == 1);
        CHECK(held.resolutions.shapes[0].place == Fraction{2});
        CHECK(held.valid[0]);
    }

    SECTION("a chord box inside stays")
    {
        std::vector<ChartNote> notes{
            noteAt(at(1, 1), 1, 5, Fraction{1, 4}),
            noteAt(at(1, 1), 2, 7, Fraction{1, 4}),
        };
        notes.insert(notes.end(), after.begin(), after.end());
        notes = streamOf(notes);
        const Held held = deriveUnder(notes, {none});
        CHECK(held.resolutions.shapes == deriveUnder(notes, {}).resolutions.shapes);
    }
}

// A pull-off source riding the stop the hold's span states, and its release back onto it, are one
// hand: neither is a conflict, so the hold stands.
TEST_CASE("A pull-off inside a shape hold leaves it whole", "[core][chart]")
{
    ChartNote pull = noteAt(at(1, 2, Fraction{1, 2}), 2, 7, Fraction{3, 8});
    pull.attack = NoteAttack::Legato;
    const std::vector<ChartNote> notes = streamOf({
        noteAt(at(1, 1), 1, 5, Fraction{3, 4}),
        noteAt(at(1, 1), 2, 7, Fraction{1, 4}),
        noteAt(at(1, 2), 2, 9, Fraction{1, 8}),
        pull,
    });
    const Held held = deriveUnder(notes, {holdOver(at(1, 1), at(1, 4))});
    REQUIRE(held.resolutions.shapes.size() == 1);
    CHECK(held.resolutions.shapes[0].sustain == Fraction{3, 4});
    CHECK(stopOf(held.resolutions, 0, 2) == std::optional{fretStop(7)});
    CHECK(held.valid[0]);
}

// A member's glide leaving the stop the span states is a hand conflict at its landing (shape rule
// 4), a multi-leg glide landing where it comes to rest; the landed grip opens its successor there.
TEST_CASE("A glide inside a shape hold crops it where it lands", "[core][chart]")
{
    ChartNote gliding = noteAt(at(1, 1), 2, 7, Fraction{1});
    gliding.keyframes = {
        Keyframe{.offset = Fraction{1, 4}, .fret = 9},
        Keyframe{.offset = Fraction{1, 2}, .fret = 11},
    };
    const std::vector<ChartNote> notes = streamOf({
        noteAt(at(1, 1), 1, 5, Fraction{1}),
        gliding,
    });
    const Held held = deriveUnder(notes, {holdOver(at(1, 1), at(2, 1))});
    REQUIRE(held.resolutions.shapes.size() == 2);
    CHECK(held.resolutions.shapes[0].authored);
    CHECK(held.resolutions.shapes[0].sustain == Fraction{1, 2});
    CHECK(held.resolutions.shapes[1].landing_opened);
    CHECK(stopOf(held.resolutions, 1, 2) == std::optional{fretStop(11)});
    CHECK_FALSE(held.valid[0]);
}

// A hold exactly over a derived span changes nothing but marking that span authored, and is valid.
TEST_CASE("A shape hold exactly over a derived span derives it unchanged", "[core][chart]")
{
    const std::vector<ChartNote> notes = streamOf({
        noteAt(at(1, 1), 1, 3, Fraction{3, 4}),
        noteAt(at(1, 1), 2, 5, Fraction{3, 4}),
        noteAt(at(1, 4), 1, 3, Fraction{1, 4}),
        noteAt(at(1, 4), 2, 5, Fraction{1, 4}),
        noteAt(at(2, 1), 1, 5, Fraction{1, 4}),
        noteAt(at(2, 1), 2, 7, Fraction{1, 4}),
    });
    const ChartResolutions plain = deriveUnder(notes, {}).resolutions;
    REQUIRE(plain.shapes.size() == 2);
    const Held held = deriveUnder(notes, {holdOver(at(1, 1), at(2, 1))});
    std::vector<ChartShape> expected = plain.shapes;
    expected[0].authored = true;
    CHECK(held.resolutions.shapes == expected);
    CHECK(held.resolutions.postures == plain.postures);
    CHECK(held.resolutions.arrivals == plain.arrivals);
    CHECK(held.valid[0]);
}

} // namespace rock_hero::common::core
