#include "timeline/tempo_map_edits.h"

#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/timeline/tempo_span_text.h>

namespace rock_hero::editor::core
{

namespace
{

using rock_hero::common::core::testing::anchor;
using rock_hero::common::core::testing::commonTimeMap;

// A 4/4 map whose one span runs `measures` measures at `quarter_bpm`, its end rounded to a whole
// millisecond exactly as a stored map holds it.
[[nodiscard]] common::core::TempoMap oneSpanAt(double quarter_bpm, int measures)
{
    return commonTimeMap({
        anchor("1:1", 0),
        common::core::BeatAnchor{
            .position = {.measure = measures + 1, .beat = 1, .offset = {}},
            .milliseconds = common::core::nearestAnchorMilliseconds(
                static_cast<double>(measures) * 4.0 * 60.0 / quarter_bpm),
        },
    });
}

} // namespace

// Verifies a typed tempo reads back as typed, though its span end was rounded to the millisecond.
TEST_CASE("A tempo span prints the tempo that was typed", "[editor][tempo-map]")
{
    CHECK(tempoSpanBpmText(oneSpanAt(137.0, 1), 0) == "137");
    CHECK(tempoSpanBpmText(oneSpanAt(120.0, 1), 0) == "120");
    CHECK(tempoSpanBpmText(oneSpanAt(136.5, 1), 0) == "136.5");
    CHECK(tempoSpanBpmText(oneSpanAt(97.25, 16), 0) == "97.25");
}

// Verifies an insert leaves both halves printing their parent's tempo, though each half's ends
// were rounded separately.
TEST_CASE("An inserted anchor's halves print the parent tempo", "[editor][tempo-map]")
{
    const TempoMapEditResult split = insertTempoAnchor(oneSpanAt(133.0, 2), 2, 1);
    REQUIRE(split.has_value());
    if (!split.has_value())
    {
        return;
    }
    CHECK(tempoSpanBpmText(*split, 0) == "133");
    CHECK(tempoSpanBpmText(*split, 1) == "133");
}

// Verifies the limit of that promise: a span whose stored length is already most of a millisecond
// off its printed tempo can split into a half that carries the error over a shorter length.
TEST_CASE("An inserted anchor's half can print the parent's stored error", "[editor][tempo-map]")
{
    const common::core::TempoMap parent{
        {common::core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4}},
        {anchor("1:1", 0), anchor("5:1", 7219)}
    };
    REQUIRE(tempoSpanBpmText(parent, 0) == "133");

    const TempoMapEditResult split = insertTempoAnchor(parent, 4, 4);
    REQUIRE(split.has_value());
    if (!split.has_value())
    {
        return;
    }
    CHECK(tempoSpanBpmText(*split, 0) == "132.98");
}

// Verifies a span that does not exist prints nothing.
TEST_CASE("A missing tempo span prints nothing", "[editor][tempo-map]")
{
    CHECK(tempoSpanBpmText(oneSpanAt(120.0, 1), 1).empty());
}

} // namespace rock_hero::editor::core
