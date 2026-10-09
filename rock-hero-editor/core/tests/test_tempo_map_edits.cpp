#include "timeline/tempo_map_edits.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/timeline/tempo_span_text.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using rock_hero::common::core::testing::anchor;
using rock_hero::common::core::testing::commonTimeMap;

using Catch::Matchers::WithinAbs;

// A 4/4 map at 120 BPM over eight seconds of audio: anchors at 1:1 (0 s) and 5:1 (8 s).
[[nodiscard]] common::core::TempoMap defaultEightSeconds()
{
    return common::core::TempoMap::defaultMap(common::core::TimeDuration{8.0});
}

// A 4/4 map at 120 BPM with anchors at every other downbeat up to the 5:1 terminal.
[[nodiscard]] common::core::TempoMap anchoredMap()
{
    return commonTimeMap(
        {anchor("1:1", 0), anchor("2:1", 2000), anchor("3:1", 4000), anchor("5:1", 8000)});
}

// A 4/4 map at 120 BPM with a sub-beat anchor at 2:1+1/2 beside the on-beat one at 2:1: the pair an
// address floored to its beat cannot tell apart.
[[nodiscard]] common::core::TempoMap subBeatMap()
{
    return commonTimeMap(
        {anchor("1:1", 0), anchor("2:1", 2000), anchor("2:1+1/2", 2250), anchor("5:1", 8000)});
}

// The stored length of one anchor span in whole milliseconds, the unit a ripple must preserve.
[[nodiscard]] std::int64_t spanMilliseconds(
    const common::core::TempoMap& tempo_map, std::size_t span)
{
    const std::vector<common::core::BeatAnchor>& anchors = tempo_map.anchors();
    return anchors[span + 1].milliseconds - anchors[span].milliseconds;
}

// A grid position as the file spells it; a malformed token fails the test.
[[nodiscard]] common::core::GridPosition at(const std::string& token)
{
    const std::optional<common::core::GridPosition> parsed =
        common::core::parseGridPositionToken(token);
    REQUIRE(parsed.has_value());
    return parsed.value_or(common::core::GridPosition{});
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
    const TempoMapEditResult edited = insertTempoAnchor(source, at("2:3"));
    REQUIRE(edited.has_value());
    if (!edited.has_value())
    {
        return;
    }

    REQUIRE(edited->anchors().size() == 3);
    CHECK(edited->anchors()[1].position == at("2:3"));
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

// Verifies a sub-beat insert pins its position's time rounded once, so no tick of the song moves by
// more than half a millisecond.
TEST_CASE("Inserting a sub-beat tempo anchor is time-neutral", "[editor][tempo-map]")
{
    const common::core::TempoMap source = defaultEightSeconds();
    const common::core::GridPosition position = at("2:3+1/3");
    const TempoMapEditResult edited = insertTempoAnchor(source, position);
    REQUIRE(edited.has_value());
    if (!edited.has_value())
    {
        return;
    }

    REQUIRE(edited->anchors().size() == 3);
    CHECK(edited->anchors()[1].position == position);
    CHECK(
        edited->anchors()[1].milliseconds ==
        common::core::nearestAnchorMilliseconds(source.secondsAt(position)));
    double worst = 0.0;
    for (int measure = 1; measure <= 4; ++measure)
    {
        for (int beat = 1; beat <= 4; ++beat)
        {
            for (int tick = 0; tick < 960; ++tick)
            {
                const common::core::GridPosition slot{
                    .measure = measure, .beat = beat, .offset = common::core::Fraction{tick, 960}
                };
                worst = std::max(worst, std::abs(edited->secondsAt(slot) - source.secondsAt(slot)));
            }
        }
    }
    CHECK(worst <= 0.0005);
}

// Verifies an insert refuses the map's two ends, positions past them, and an occupied position.
TEST_CASE("Inserting a tempo anchor refuses the ends and occupied beats", "[editor][tempo-map]")
{
    const common::core::TempoMap source = defaultEightSeconds();
    CHECK(refusalCode(insertTempoAnchor(source, at("1:1"))) == TempoMapEditErrorCode::BrokenRule);
    CHECK(refusalCode(insertTempoAnchor(source, at("5:1"))) == TempoMapEditErrorCode::BrokenRule);
    CHECK(refusalCode(insertTempoAnchor(source, at("6:2"))) == TempoMapEditErrorCode::BrokenRule);
    CHECK(refusalCode(insertTempoAnchor(source, at("2:5"))) == TempoMapEditErrorCode::BrokenRule);
    CHECK(
        refusalCode(insertTempoAnchor(anchoredMap(), at("3:1"))) ==
        TempoMapEditErrorCode::BrokenRule);
    CHECK(
        refusalCode(insertTempoAnchor(subBeatMap(), at("2:1+1/2"))) ==
        TempoMapEditErrorCode::BrokenRule);
}

// Verifies a move changes only the anchor's own time and refuses a time that would reach a
// neighbour or take the first anchor below zero.
TEST_CASE("Moving a tempo anchor pins its neighbours", "[editor][tempo-map]")
{
    const common::core::TempoMap source = anchoredMap();
    const TempoMapEditResult moved = moveTempoAnchor(source, at("3:1"), 4123);
    REQUIRE(moved.has_value());
    if (!moved.has_value())
    {
        return;
    }
    CHECK(moved->anchors()[2].milliseconds == 4123);
    CHECK(moved->anchors()[1].milliseconds == 2000);
    CHECK(moved->anchors()[3].milliseconds == 8000);

    CHECK(
        refusalCode(moveTempoAnchor(source, at("3:1"), 2000)) == TempoMapEditErrorCode::BrokenRule);
    CHECK(
        refusalCode(moveTempoAnchor(source, at("3:1"), 8500)) == TempoMapEditErrorCode::BrokenRule);
    CHECK(
        refusalCode(moveTempoAnchor(source, at("1:1"), -10)) == TempoMapEditErrorCode::BrokenRule);
    CHECK(
        refusalCode(moveTempoAnchor(source, at("2:2"), 2500)) ==
        TempoMapEditErrorCode::NoAnchorAtPosition);
    CHECK(moveTempoAnchor(source, at("1:1"), 250).has_value());
}

// Verifies every verb addresses a sub-beat anchor by its whole position — the on-beat anchor of the
// same beat untouched — and refuses the floored position where no anchor stands.
TEST_CASE("Tempo edits address a sub-beat anchor by its whole position", "[editor][tempo-map]")
{
    const common::core::TempoMap source = subBeatMap();

    const TempoMapEditResult moved = moveTempoAnchor(source, at("2:1+1/2"), 2300);
    REQUIRE(moved.has_value());
    if (moved.has_value())
    {
        CHECK(moved->anchors()[1].milliseconds == 2000);
        CHECK(moved->anchors()[2].milliseconds == 2300);
    }

    const TempoMapEditResult retyped = setTempoSpanBpm(source, at("2:1+1/2"), 60.0, 8.0);
    REQUIRE(retyped.has_value());
    if (retyped.has_value())
    {
        CHECK(retyped->anchors()[1].milliseconds == 2000);
        CHECK(retyped->anchors()[2].milliseconds == 2250);
        CHECK(tempoSpanBpmText(*retyped, 1) == "120");
        CHECK(tempoSpanBpmText(*retyped, 2) == "60");
    }

    const TempoMapEditResult deleted = deleteTempoAnchor(source, at("2:1+1/2"));
    REQUIRE(deleted.has_value());
    if (deleted.has_value())
    {
        CHECK(
            *deleted ==
            commonTimeMap({anchor("1:1", 0), anchor("2:1", 2000), anchor("5:1", 8000)}));
    }

    const common::core::TempoMap sub_beat_only =
        commonTimeMap({anchor("1:1", 0), anchor("2:1+1/2", 2250), anchor("5:1", 8000)});
    CHECK(
        refusalCode(moveTempoAnchor(sub_beat_only, at("2:1"), 2100)) ==
        TempoMapEditErrorCode::NoAnchorAtPosition);
    CHECK(
        refusalCode(setTempoSpanBpm(sub_beat_only, at("2:1"), 60.0, 8.0)) ==
        TempoMapEditErrorCode::NoAnchorAtPosition);
    CHECK(
        refusalCode(deleteTempoAnchor(sub_beat_only, at("2:1"))) ==
        TempoMapEditErrorCode::NoAnchorAtPosition);
}

// Verifies a typed tempo sets the span and shifts every later anchor by one amount, so every later
// span keeps its stored length to the millisecond.
TEST_CASE("Typing a tempo ripples every later anchor", "[editor][tempo-map]")
{
    const common::core::TempoMap source = anchoredMap();
    const TempoMapEditResult rippled = setTempoSpanBpm(source, at("2:1"), 100.0, 8.0);
    REQUIRE(rippled.has_value());
    if (!rippled.has_value())
    {
        return;
    }

    CHECK(rippled->anchors()[1].milliseconds == 2000);
    CHECK(rippled->anchors()[2].milliseconds == 4400);
    CHECK(rippled->anchors()[3].milliseconds == 8400);
    CHECK(rippled->anchors()[3].position == at("5:1"));
    CHECK(spanMilliseconds(*rippled, 0) == spanMilliseconds(source, 0));
    CHECK(spanMilliseconds(*rippled, 2) == spanMilliseconds(source, 2));
    CHECK(tempoSpanBpmText(*rippled, 1) == "100");
}

// Verifies a typed tempo on a sub-beat anchor shifts every later anchor, sub-beat ones included, by
// one exact integer, so every later span keeps its stored length exactly.
TEST_CASE("A sub-beat ripple shifts every later anchor by one integer", "[editor][tempo-map]")
{
    const common::core::TempoMap source = commonTimeMap({
        anchor("1:1", 0),
        anchor("2:1+1/2", 2250),
        anchor("3:1", 4000),
        anchor("3:3+1/4", 5125),
        anchor("5:1", 8000),
    });
    const TempoMapEditResult rippled = setTempoSpanBpm(source, at("2:1+1/2"), 137.0, 7.0);
    REQUIRE(rippled.has_value());
    if (!rippled.has_value())
    {
        return;
    }
    REQUIRE(rippled->anchors().size() == source.anchors().size());

    // 3.5 quarters at 137 BPM is 1.532846 s, so 3:1 lands on 3.783 s.
    CHECK(rippled->anchors()[0] == source.anchors()[0]);
    CHECK(rippled->anchors()[1] == source.anchors()[1]);
    CHECK(rippled->anchors()[2].milliseconds == 3783);
    const std::int64_t shift =
        rippled->anchors()[2].milliseconds - source.anchors()[2].milliseconds;
    for (std::size_t later = 2; later < source.anchors().size(); ++later)
    {
        CHECK(rippled->anchors()[later].position == source.anchors()[later].position);
        CHECK(
            rippled->anchors()[later].milliseconds - source.anchors()[later].milliseconds == shift);
    }
    CHECK(spanMilliseconds(*rippled, 2) == spanMilliseconds(source, 2));
    CHECK(spanMilliseconds(*rippled, 3) == spanMilliseconds(source, 3));
    CHECK(tempoSpanBpmText(*rippled, 1) == "137");
}

// Verifies a faster tempo that would end the map before the audio grows the terminal downbeat by
// downbeat at the last span's tempo, and that a slower one never shrinks it.
TEST_CASE("Typing a faster tempo grows the terminal to cover the audio", "[editor][tempo-map]")
{
    const TempoMapEditResult faster = setTempoSpanBpm(defaultEightSeconds(), at("1:1"), 240.0, 8.0);
    REQUIRE(faster.has_value());
    if (!faster.has_value())
    {
        return;
    }
    REQUIRE(faster->anchors().size() == 2);
    CHECK(faster->anchors()[1].position == at("9:1"));
    CHECK(faster->anchors()[1].milliseconds == 8000);
    CHECK(tempoSpanBpmText(*faster, 0) == "240");

    const TempoMapEditResult slower = setTempoSpanBpm(defaultEightSeconds(), at("1:1"), 60.0, 8.0);
    REQUIRE(slower.has_value());
    if (!slower.has_value())
    {
        return;
    }
    CHECK(slower->anchors()[1].position == at("5:1"));
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
        const TempoMapEditResult rippled = setTempoSpanBpm(long_song, at("1:1"), typed, 300.0);
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
    const TempoMapEditResult rippled = setTempoSpanBpm(anchoredMap(), at("2:1"), 240.0, 8.0);
    REQUIRE(rippled.has_value());
    if (!rippled.has_value())
    {
        return;
    }
    // Span 1 now lasts one second, pulling 3:1 to 3 s and the terminal to 7 s at 120 BPM; one more
    // measure at that tempo covers the eight seconds.
    CHECK(rippled->anchors()[2].milliseconds == 3000);
    CHECK(rippled->anchors().back().position == at("6:1"));
    CHECK(rippled->anchors().back().milliseconds == 9000);
    CHECK(tempoSpanBpmText(*rippled, rippled->anchors().size() - 2) == "120");
}

// Verifies a tempo too fast for the millisecond grid is refused rather than looping to cover the
// audio: the typed span would round to nothing.
TEST_CASE("A tempo too fast for the grid is refused", "[editor][tempo-map]")
{
    CHECK(
        refusalCode(setTempoSpanBpm(defaultEightSeconds(), at("1:1"), 1.0e12, 8.0)) ==
        TempoMapEditErrorCode::BrokenRule);
}

// Verifies a typed tempo refuses a value that is not a positive number and a position opening no
// span.
TEST_CASE("Typing a tempo refuses bad values and the terminal", "[editor][tempo-map]")
{
    const common::core::TempoMap source = anchoredMap();
    CHECK(
        refusalCode(setTempoSpanBpm(source, at("2:1"), 0.0, 8.0)) ==
        TempoMapEditErrorCode::InvalidBpm);
    CHECK(
        refusalCode(setTempoSpanBpm(source, at("2:1"), -90.0, 8.0)) ==
        TempoMapEditErrorCode::InvalidBpm);
    CHECK(
        refusalCode(
            setTempoSpanBpm(source, at("2:1"), std::numeric_limits<double>::quiet_NaN(), 8.0)) ==
        TempoMapEditErrorCode::InvalidBpm);
    // A tempo slow enough puts the span's end past any time an anchor can hold.
    CHECK(
        refusalCode(setTempoSpanBpm(source, at("1:1"), 1.0e-300, 8.0)) ==
        TempoMapEditErrorCode::BrokenRule);
    CHECK(
        refusalCode(setTempoSpanBpm(source, at("5:1"), 120.0, 8.0)) ==
        TempoMapEditErrorCode::NoAnchorAtPosition);
    CHECK(
        refusalCode(setTempoSpanBpm(source, at("2:2"), 120.0, 8.0)) ==
        TempoMapEditErrorCode::NoAnchorAtPosition);
}

// Verifies a scale keeps every anchor's position and divides its distance from the FIRST anchor,
// which keeps its own time, so every span's tempo is multiplied: x2 on a map starting half a second
// in halves every distance and grows the terminal at the doubled last tempo to cover the audio,
// and /2 doubles every distance and leaves the terminal where it stands.
TEST_CASE("Scaling a tempo map rescales every anchor about the first", "[editor][tempo-map]")
{
    const common::core::TempoMap source = commonTimeMap(
        {anchor("1:1", 500), anchor("2:1", 2500), anchor("3:1", 4500), anchor("5:1", 8500)});

    const TempoMapEditResult doubled = scaleTempoMap(source, common::core::Fraction{2}, 8.5);
    REQUIRE(doubled.has_value());
    if (doubled.has_value())
    {
        CHECK(
            doubled->anchors() ==
            std::vector{
                anchor("1:1", 500), anchor("2:1", 1500), anchor("3:1", 2500), anchor("9:1", 8500)
            });
        for (std::size_t span = 0; span < 3; ++span)
        {
            CHECK(tempoSpanBpmText(*doubled, span) == "240");
        }
    }

    const TempoMapEditResult halved = scaleTempoMap(source, common::core::Fraction{1, 2}, 8.5);
    REQUIRE(halved.has_value());
    if (halved.has_value())
    {
        CHECK(
            halved->anchors() ==
            std::vector{
                anchor("1:1", 500), anchor("2:1", 4500), anchor("3:1", 8500), anchor("5:1", 16500)
            });
    }

    // A factor of one re-times nothing, so a terminal that covers the audio leaves the map as it
    // is.
    CHECK(scaleTempoMap(source, common::core::Fraction{1}, 8.5) == source);
}

// Verifies the rounding is exact integer arithmetic with halves away from zero, so a free factor
// lands each anchor on the millisecond nearest its scaled distance: 3001 halves to 1501, thirds to
// 1000 (1000.33) and 8003 thirds to 2668 (2667.67).
TEST_CASE(
    "Scaling a tempo map rounds each distance to the nearest millisecond", "[editor][tempo-map]")
{
    const common::core::TempoMap source = commonTimeMap(
        {anchor("1:1", 0), anchor("3:1", 3001), anchor("5:1", 8003), anchor("9:1", 16005)});

    const TempoMapEditResult doubled = scaleTempoMap(source, common::core::Fraction{2}, 8.0);
    REQUIRE(doubled.has_value());
    if (doubled.has_value())
    {
        CHECK(
            doubled->anchors() ==
            std::vector{
                anchor("1:1", 0), anchor("3:1", 1501), anchor("5:1", 4002), anchor("9:1", 8003)
            });
    }

    const TempoMapEditResult thirds = scaleTempoMap(source, common::core::Fraction{3}, 5.0);
    REQUIRE(thirds.has_value());
    if (thirds.has_value())
    {
        CHECK(
            thirds->anchors() ==
            std::vector{
                anchor("1:1", 0), anchor("3:1", 1000), anchor("5:1", 2668), anchor("9:1", 5335)
            });
    }
}

// Verifies a factor that lands two anchors on one millisecond is the ordering rule's refusal, and
// one that would carry the terminal past any time an anchor can hold is refused before any
// arithmetic runs on it.
TEST_CASE(
    "Scaling a tempo map refuses a collapse and an unrepresentable time", "[editor][tempo-map]")
{
    CHECK(
        refusalCode(scaleTempoMap(
            commonTimeMap({anchor("1:1", 0), anchor("2:1", 7), anchor("5:1", 8000)}),
            common::core::Fraction{16},
            8.0)) == TempoMapEditErrorCode::BrokenRule);
    CHECK(
        refusalCode(scaleTempoMap(
            commonTimeMap(
                {anchor("1:1", 0), anchor("5:1", std::numeric_limits<std::int64_t>::max() / 8)}),
            common::core::Fraction{1, 16},
            8.0)) == TempoMapEditErrorCode::BrokenRule);
}

// Verifies a delete keeps the map's two ends and exactly undoes an insert.
TEST_CASE("Deleting a tempo anchor is the inverse of inserting one", "[editor][tempo-map]")
{
    const common::core::TempoMap source = defaultEightSeconds();
    CHECK(
        refusalCode(deleteTempoAnchor(source, at("1:1"))) ==
        TempoMapEditErrorCode::ProtectedAnchor);
    CHECK(
        refusalCode(deleteTempoAnchor(source, at("5:1"))) ==
        TempoMapEditErrorCode::ProtectedAnchor);
    CHECK(
        refusalCode(deleteTempoAnchor(source, at("3:1"))) ==
        TempoMapEditErrorCode::NoAnchorAtPosition);

    for (const char* const token : {"2:3", "2:3+1/3"})
    {
        const TempoMapEditResult inserted = insertTempoAnchor(source, at(token));
        REQUIRE(inserted.has_value());
        if (!inserted.has_value())
        {
            return;
        }
        const TempoMapEditResult deleted = deleteTempoAnchor(*inserted, at(token));
        REQUIRE(deleted.has_value());
        if (!deleted.has_value())
        {
            return;
        }
        CHECK(*deleted == source);
    }
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
