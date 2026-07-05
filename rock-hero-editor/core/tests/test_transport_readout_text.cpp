#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/domain/tempo_map.h>
#include <rock_hero/editor/core/timeline/transport_readout_text.h>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// One 4/4 measure over four seconds: one beat per second, so beat positions read directly off the
// clock.
[[nodiscard]] common::core::TempoMap makeOneMeasure44Map()
{
    return common::core::TempoMap{
        std::vector{
            common::core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
        },
        std::vector{
            common::core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            common::core::BeatAnchor{.measure = 2, .beat = 1, .seconds = 4.0},
        },
    };
}

} // namespace

// Verifies the sub-hour format and the millisecond field's zero padding.
TEST_CASE("Timeline time text formats minutes, seconds, and milliseconds", "[core][readout]")
{
    CHECK(timelineTimeText(0.0) == "0:00:000");
    CHECK(timelineTimeText(1.5) == "0:01:500");
    CHECK(timelineTimeText(61.002) == "1:01:002");
}

// Verifies the hour field appears only past one hour, with minutes zero-padded under it.
TEST_CASE("Timeline time text adds the hour field past one hour", "[core][readout]")
{
    CHECK(timelineTimeText(3599.999) == "59:59:999");
    CHECK(timelineTimeText(3600.0) == "1:00:00:000");
    CHECK(timelineTimeText(3661.25) == "1:01:01:250");
}

// Verifies rounding happens on one millisecond total, so a position a hair under a minute rolls
// the whole readout forward instead of showing a four-digit millisecond field.
TEST_CASE("Timeline time text carries rounded milliseconds across fields", "[core][readout]")
{
    CHECK(timelineTimeText(59.9996) == "1:00:000");
    CHECK(timelineTimeText(-2.0) == "0:00:000");
}

// Verifies the measure.beat.hundredths readout against a one-beat-per-second map.
TEST_CASE("Beat position text tracks the transport position", "[core][readout]")
{
    const common::core::TempoMap map = makeOneMeasure44Map();

    CHECK(beatPositionText(map, 0.0) == "1.1.00");
    CHECK(beatPositionText(map, 1.5) == "1.2.50");
    CHECK(beatPositionText(map, 3.0) == "1.4.00");
}

// Verifies a mid-span downbeat displays as its own measure start. The seconds-to-beat inverse of
// forward-mapped downbeat seconds can round to just under the whole beat (e.g. 3.9999...), and
// the readout once floored that raw, showing 1.4.99 when the cursor sat exactly on measure 2.
TEST_CASE("Beat position text lands on measure starts", "[core][readout]")
{
    // Two 4/4 measures over 7.3s: measure 2's downbeat is not an anchor, so its seconds resolve
    // through anchor-span interpolation and the inverse picks up rounding error.
    const common::core::TempoMap map{
        std::vector{
            common::core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
        },
        std::vector{
            common::core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            common::core::BeatAnchor{.measure = 3, .beat = 1, .seconds = 7.3},
        },
    };

    CHECK(beatPositionText(map, map.secondsAtBeat(2, 1)) == "2.1.00");
}

} // namespace rock_hero::editor::core
