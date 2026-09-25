#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <rock_hero/common/core/highway/highway_light.h>
#include <rock_hero/common/core/shared/visible_events.h>
#include <span>

namespace rock_hero::common::core
{

namespace
{

// A light full from 2 s through 3 s that rises over half a second, with every instant the tests
// sample exactly representable, so each expected level is an exact value rather than a tolerance.
constexpr HighwayLitStretch g_held_light{
    .start_seconds = 2.0,
    .release_seconds = 3.0,
    .rise_seconds = 0.5,
};

// A layer decay whose halfway point, like the rise's, lands on an exact binary fraction.
constexpr double g_decay_seconds = 0.5;

} // namespace

// THE envelope: dark before the rise, a linear rise to full at the start, full through the hold
// up to and including the release, and a linear decay that is dark again from its end on.
TEST_CASE("Light level rises, holds through the release, then decays", "[core][highway][light]")
{
    using Catch::Matchers::WithinULP;

    CHECK_THAT(highwayLightLevel(g_held_light, 1.0, g_decay_seconds), WithinULP(0.0, 0));
    CHECK_THAT(highwayLightLevel(g_held_light, 1.5, g_decay_seconds), WithinULP(0.0, 0));
    CHECK_THAT(highwayLightLevel(g_held_light, 1.75, g_decay_seconds), WithinULP(0.5, 0));
    CHECK_THAT(highwayLightLevel(g_held_light, 2.0, g_decay_seconds), WithinULP(1.0, 0));
    CHECK_THAT(highwayLightLevel(g_held_light, 2.5, g_decay_seconds), WithinULP(1.0, 0));
    CHECK_THAT(highwayLightLevel(g_held_light, 3.0, g_decay_seconds), WithinULP(1.0, 0));
    CHECK_THAT(highwayLightLevel(g_held_light, 3.25, g_decay_seconds), WithinULP(0.5, 0));
    CHECK_THAT(highwayLightLevel(g_held_light, 3.5, g_decay_seconds), WithinULP(0.0, 0));
    CHECK_THAT(highwayLightLevel(g_held_light, 4.0, g_decay_seconds), WithinULP(0.0, 0));
}

// A zero rise lights nothing before the start — never a division by the zero duration — and is
// full the instant the start arrives.
TEST_CASE("Light level with no rise switches on at the start", "[core][highway][light]")
{
    using Catch::Matchers::WithinULP;

    const HighwayLitStretch sudden{
        .start_seconds = 2.0, .release_seconds = 3.0, .rise_seconds = 0.0
    };
    const double just_before = std::nextafter(2.0, 0.0);

    CHECK_THAT(highwayLightLevel(sudden, just_before, g_decay_seconds), WithinULP(0.0, 0));
    CHECK_THAT(highwayLightLevel(sudden, 2.0, g_decay_seconds), WithinULP(1.0, 0));
}

// THE lit interval runs from the rise's start to the decay's end.
TEST_CASE("Lit interval spans the rise start to the decay end", "[core][highway][light]")
{
    using Catch::Matchers::WithinULP;

    const HighwayLitInterval lit = highwayLitInterval(g_held_light, g_decay_seconds);
    CHECK_THAT(lit.from_seconds, WithinULP(1.5, 0));
    CHECK_THAT(lit.to_seconds, WithinULP(3.5, 0));
}

// THE fold: the earliest start, the latest release, and the widest rise among the items at that
// earliest start only. An item starting later rises inside the light, so its wider rise must not
// win — and it is placed first here so the fold cannot lean on the order it was handed.
TEST_CASE("Folding evidence keeps the widest rise at the earliest start", "[core][highway][light]")
{
    const std::array<HighwayLitStretch, 4> items{
        HighwayLitStretch{.start_seconds = 1.5, .release_seconds = 2.0, .rise_seconds = 0.875},
        HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 1.5, .rise_seconds = 0.25},
        HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 2.5, .rise_seconds = 0.125},
        // Arithmetic noise past the earliest start, well inside the onset tolerance: still at it.
        HighwayLitStretch{
            .start_seconds = 1.000000000001, .release_seconds = 1.25, .rise_seconds = 0.375
        },
    };

    CHECK(
        foldLitEvidence(items) ==
        HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 2.5, .rise_seconds = 0.375});
    // One item folds to itself.
    CHECK(foldLitEvidence(std::span(items).first(1)) == items.front());

    // At the tolerance itself an item is a later strike: its wider rise does not win.
    const std::array<HighwayLitStretch, 2> at_tolerance{
        HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 1.5, .rise_seconds = 0.25},
        HighwayLitStretch{
            .start_seconds = 1.0 + g_onset_match_epsilon,
            .release_seconds = 1.5,
            .rise_seconds = 0.5,
        },
    };
    CHECK(
        foldLitEvidence(at_tolerance) ==
        HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 1.5, .rise_seconds = 0.25});
}

// THE crowding clamp: a rise that would reach back past the previous release shrinks to the gap,
// one that fits is untouched, and a start at or before the previous release rises not at all.
TEST_CASE("Crowding clamps a rise to the gap after the previous release", "[core][highway][light]")
{
    const HighwayLitStretch previous{
        .start_seconds = 0.5, .release_seconds = 1.0, .rise_seconds = 0.0
    };

    CHECK(
        crowdedAfter(
            HighwayLitStretch{.start_seconds = 1.25, .release_seconds = 2.0, .rise_seconds = 0.5},
            previous) ==
        HighwayLitStretch{.start_seconds = 1.25, .release_seconds = 2.0, .rise_seconds = 0.25});
    CHECK(
        crowdedAfter(
            HighwayLitStretch{.start_seconds = 1.25, .release_seconds = 2.0, .rise_seconds = 0.125},
            previous) ==
        HighwayLitStretch{.start_seconds = 1.25, .release_seconds = 2.0, .rise_seconds = 0.125});
    CHECK(
        crowdedAfter(
            HighwayLitStretch{.start_seconds = 0.75, .release_seconds = 2.0, .rise_seconds = 0.5},
            previous) ==
        HighwayLitStretch{.start_seconds = 0.75, .release_seconds = 2.0, .rise_seconds = 0.0});
    CHECK(
        crowdedAfter(
            HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 2.0, .rise_seconds = 0.5},
            previous) ==
        HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 2.0, .rise_seconds = 0.0});
}

} // namespace rock_hero::common::core
