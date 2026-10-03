#include "timeline/tempo_map_edits.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/timeline/tempo_span_text.h>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using rock_hero::common::core::testing::anchor;

using Catch::Matchers::WithinAbs;

// A 4/4 map at 120 BPM over eight seconds of audio: anchors at 1:1 (0 s) and 5:1 (8 s).
[[nodiscard]] common::core::TempoMap defaultEightSeconds()
{
    return common::core::TempoMap::defaultMap(common::core::TimeDuration{8.0});
}

// A 4/4 map at 120 BPM with anchors at every other downbeat up to the 5:1 terminal.
[[nodiscard]] common::core::TempoMap anchoredMap()
{
    return common::core::TempoMap{
        {common::core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4}},
        {anchor("1:1", 0), anchor("2:1", 2000), anchor("3:1", 4000), anchor("5:1", 8000)}
    };
}

// The stored length of one anchor span in whole milliseconds, the unit a ripple must preserve.
[[nodiscard]] std::int64_t spanMilliseconds(
    const common::core::TempoMap& tempo_map, std::size_t span)
{
    const std::vector<common::core::BeatAnchor>& anchors = tempo_map.anchors();
    return anchors[span + 1].milliseconds - anchors[span].milliseconds;
}

// A whole beat as the grid position an anchor stands at.
[[nodiscard]] common::core::GridPosition beatAt(int measure, int beat)
{
    return common::core::GridPosition{.measure = measure, .beat = beat, .offset = {}};
}

// The code an edit refused with; fails the test when the edit landed instead.
[[nodiscard]] TempoMapEditErrorCode refusalCode(const TempoMapEditResult& result)
{
    REQUIRE_FALSE(result.has_value());
    return result.has_value() ? TempoMapEditErrorCode{} : result.error().code;
}

} // namespace

// Verifies an insert pins the beat's current time, so no beat of the song moves audibly.
TEST_CASE("Inserting a tempo anchor moves no beat", "[editor][tempo-map]")
{
    const common::core::TempoMap source = defaultEightSeconds();
    const TempoMapEditResult edited = insertTempoAnchor(source, 2, 3);
    REQUIRE(edited.has_value());
    if (!edited.has_value())
    {
        return;
    }

    REQUIRE(edited->anchors().size() == 3);
    CHECK(edited->anchors()[1].position == beatAt(2, 3));
    CHECK(edited->anchors()[1].milliseconds == 3000);
    for (int measure = 1; measure <= 4; ++measure)
    {
        for (int beat = 1; beat <= 4; ++beat)
        {
            CHECK_THAT(
                edited->secondsAtBeat(measure, beat),
                WithinAbs(source.secondsAtBeat(measure, beat), 0.0005));
        }
    }
}

// Verifies an insert refuses the map's two ends, beats past them, and an occupied beat.
TEST_CASE("Inserting a tempo anchor refuses the ends and occupied beats", "[editor][tempo-map]")
{
    const common::core::TempoMap source = defaultEightSeconds();
    CHECK(refusalCode(insertTempoAnchor(source, 1, 1)) == TempoMapEditErrorCode::BrokenRule);
    CHECK(refusalCode(insertTempoAnchor(source, 5, 1)) == TempoMapEditErrorCode::BrokenRule);
    CHECK(refusalCode(insertTempoAnchor(source, 6, 2)) == TempoMapEditErrorCode::BrokenRule);
    CHECK(refusalCode(insertTempoAnchor(source, 2, 5)) == TempoMapEditErrorCode::BrokenRule);
    CHECK(refusalCode(insertTempoAnchor(anchoredMap(), 3, 1)) == TempoMapEditErrorCode::BrokenRule);
}

// Verifies a move changes only the anchor's own time, on the millisecond grid, and refuses a time
// that would reach a neighbour or take the first anchor below zero.
TEST_CASE("Moving a tempo anchor pins its neighbours", "[editor][tempo-map]")
{
    const common::core::TempoMap source = anchoredMap();
    const TempoMapEditResult moved = moveTempoAnchor(source, 3, 1, 4.1234);
    REQUIRE(moved.has_value());
    if (!moved.has_value())
    {
        return;
    }
    CHECK(moved->anchors()[2].milliseconds == 4123);
    CHECK(moved->anchors()[1].milliseconds == 2000);
    CHECK(moved->anchors()[3].milliseconds == 8000);

    CHECK(refusalCode(moveTempoAnchor(source, 3, 1, 2.0)) == TempoMapEditErrorCode::BrokenRule);
    CHECK(refusalCode(moveTempoAnchor(source, 3, 1, 8.5)) == TempoMapEditErrorCode::BrokenRule);
    CHECK(refusalCode(moveTempoAnchor(source, 1, 1, -0.01)) == TempoMapEditErrorCode::BrokenRule);
    CHECK(refusalCode(moveTempoAnchor(source, 2, 2, 2.5)) == TempoMapEditErrorCode::NoAnchorAtBeat);
    CHECK(moveTempoAnchor(source, 1, 1, 0.25).has_value());
    CHECK(
        refusalCode(moveTempoAnchor(source, 5, 1, std::numeric_limits<double>::infinity())) ==
        TempoMapEditErrorCode::BrokenRule);
    CHECK(
        refusalCode(moveTempoAnchor(source, 3, 1, std::numeric_limits<double>::quiet_NaN())) ==
        TempoMapEditErrorCode::BrokenRule);
    CHECK(refusalCode(moveTempoAnchor(source, 5, 1, 1.0e300)) == TempoMapEditErrorCode::BrokenRule);
}

// Verifies a typed tempo sets the span and shifts every later anchor by one amount, so every later
// span keeps its stored length to the millisecond.
TEST_CASE("Typing a tempo ripples every later anchor", "[editor][tempo-map]")
{
    const common::core::TempoMap source = anchoredMap();
    const TempoMapEditResult rippled = setTempoSpanBpm(source, 2, 1, 100.0, 8.0);
    REQUIRE(rippled.has_value());
    if (!rippled.has_value())
    {
        return;
    }

    CHECK(rippled->anchors()[1].milliseconds == 2000);
    CHECK(rippled->anchors()[2].milliseconds == 4400);
    CHECK(rippled->anchors()[3].milliseconds == 8400);
    CHECK(rippled->anchors()[3].position == beatAt(5, 1));
    CHECK(spanMilliseconds(*rippled, 0) == spanMilliseconds(source, 0));
    CHECK(spanMilliseconds(*rippled, 2) == spanMilliseconds(source, 2));
    CHECK(tempoSpanBpmText(*rippled, 1) == "100");
}

// Verifies a faster tempo that would end the map before the audio grows the terminal downbeat by
// downbeat at the last span's tempo, and that a slower one never shrinks it.
TEST_CASE("Typing a faster tempo grows the terminal to cover the audio", "[editor][tempo-map]")
{
    const TempoMapEditResult faster = setTempoSpanBpm(defaultEightSeconds(), 1, 1, 240.0, 8.0);
    REQUIRE(faster.has_value());
    if (!faster.has_value())
    {
        return;
    }
    REQUIRE(faster->anchors().size() == 2);
    CHECK(faster->anchors()[1].position == beatAt(9, 1));
    CHECK(faster->anchors()[1].milliseconds == 8000);
    CHECK(tempoSpanBpmText(*faster, 0) == "240");

    const TempoMapEditResult slower = setTempoSpanBpm(defaultEightSeconds(), 1, 1, 60.0, 8.0);
    REQUIRE(slower.has_value());
    if (!slower.has_value())
    {
        return;
    }
    CHECK(slower->anchors()[1].position == beatAt(5, 1));
    CHECK(slower->anchors()[1].milliseconds == 16000);
}

// Verifies a typed tempo that grows the terminal over a long song still reads back as typed: the
// grown terminal is timed from the anchor before it at the typed rate, rounded once.
TEST_CASE("A typed tempo reads back as typed after the terminal grows", "[editor][tempo-map]")
{
    const common::core::TempoMap long_song =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{300.0});
    for (const double typed : {241.0, 301.0, 137.0})
    {
        const TempoMapEditResult rippled = setTempoSpanBpm(long_song, 1, 1, typed, 300.0);
        REQUIRE(rippled.has_value());
        if (!rippled.has_value())
        {
            return;
        }
        CHECK(rippled->anchors().back().seconds() >= 300.0);
        CHECK(tempoSpanBpmText(*rippled, 0) == std::format("{}", typed));
    }
}

// Verifies a ripple on an earlier span that leaves the audio uncovered grows the terminal at the
// last span's stored tempo, which the ripple itself left alone.
TEST_CASE("A ripple on an earlier span grows the terminal at the last tempo", "[editor][tempo-map]")
{
    const TempoMapEditResult rippled = setTempoSpanBpm(anchoredMap(), 2, 1, 240.0, 8.0);
    REQUIRE(rippled.has_value());
    if (!rippled.has_value())
    {
        return;
    }
    // Span 1 now lasts one second, pulling 3:1 to 3 s and the terminal to 7 s at 120 BPM; one more
    // measure at that tempo covers the eight seconds.
    CHECK(rippled->anchors()[2].milliseconds == 3000);
    CHECK(rippled->anchors().back().position == beatAt(6, 1));
    CHECK(rippled->anchors().back().milliseconds == 9000);
    CHECK(tempoSpanBpmText(*rippled, rippled->anchors().size() - 2) == "120");
}

// Verifies a tempo too fast for the millisecond grid is refused rather than looping to cover the
// audio: the typed span would round to nothing.
TEST_CASE("A tempo too fast for the grid is refused", "[editor][tempo-map]")
{
    CHECK(
        refusalCode(setTempoSpanBpm(defaultEightSeconds(), 1, 1, 1.0e12, 8.0)) ==
        TempoMapEditErrorCode::BrokenRule);
}

// Verifies a typed tempo refuses a value that is not a positive number and a beat opening no span.
TEST_CASE("Typing a tempo refuses bad values and the terminal", "[editor][tempo-map]")
{
    const common::core::TempoMap source = anchoredMap();
    CHECK(
        refusalCode(setTempoSpanBpm(source, 2, 1, 0.0, 8.0)) == TempoMapEditErrorCode::InvalidBpm);
    CHECK(
        refusalCode(setTempoSpanBpm(source, 2, 1, -90.0, 8.0)) ==
        TempoMapEditErrorCode::InvalidBpm);
    CHECK(
        refusalCode(setTempoSpanBpm(source, 2, 1, std::numeric_limits<double>::quiet_NaN(), 8.0)) ==
        TempoMapEditErrorCode::InvalidBpm);
    // A tempo slow enough puts the span's end past any time an anchor can hold.
    CHECK(
        refusalCode(setTempoSpanBpm(source, 1, 1, 1.0e-300, 8.0)) ==
        TempoMapEditErrorCode::BrokenRule);
    CHECK(
        refusalCode(setTempoSpanBpm(source, 5, 1, 120.0, 8.0)) ==
        TempoMapEditErrorCode::NoAnchorAtBeat);
    CHECK(
        refusalCode(setTempoSpanBpm(source, 2, 2, 120.0, 8.0)) ==
        TempoMapEditErrorCode::NoAnchorAtBeat);
}

// Verifies a delete keeps the map's two ends and exactly undoes an insert.
TEST_CASE("Deleting a tempo anchor is the inverse of inserting one", "[editor][tempo-map]")
{
    const common::core::TempoMap source = defaultEightSeconds();
    CHECK(refusalCode(deleteTempoAnchor(source, 1, 1)) == TempoMapEditErrorCode::ProtectedAnchor);
    CHECK(refusalCode(deleteTempoAnchor(source, 5, 1)) == TempoMapEditErrorCode::ProtectedAnchor);
    CHECK(refusalCode(deleteTempoAnchor(source, 3, 1)) == TempoMapEditErrorCode::NoAnchorAtBeat);

    const TempoMapEditResult inserted = insertTempoAnchor(source, 2, 3);
    REQUIRE(inserted.has_value());
    if (!inserted.has_value())
    {
        return;
    }
    const TempoMapEditResult deleted = deleteTempoAnchor(*inserted, 2, 3);
    REQUIRE(deleted.has_value());
    if (!deleted.has_value())
    {
        return;
    }
    CHECK(*deleted == source);
}

// Verifies a nudge is one pixel of the zoom, never finer than a millisecond.
TEST_CASE("A tempo anchor nudge is one pixel of zoom", "[editor][tempo-map]")
{
    CHECK(tempoAnchorNudgeMilliseconds(0.0) == 1);
    CHECK(tempoAnchorNudgeMilliseconds(100.0) == 10);
    CHECK(tempoAnchorNudgeMilliseconds(333.0) == 3);
    CHECK(tempoAnchorNudgeMilliseconds(5000.0) == 1);
    CHECK(tempoAnchorNudgeMilliseconds(1.0e-300) == 1);
}

} // namespace rock_hero::editor::core
