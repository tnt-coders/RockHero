#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <rock_hero/common/core/highway/highway_light.h>
#include <rock_hero/common/core/shared/visible_events.h>
#include <vector>

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

// The merge cases' own rest tolerance, fixed here rather than read from g_hand_rest_seconds: that
// constant is a sighting knob, and these cases pin the merge's breakpoints, not its tuning.
constexpr double g_rest_seconds = 1.0;

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

// THE fold, driven through the merge: evidence at one onset is one light with the earliest start,
// the latest release, and the widest rise among the items at that earliest start only. An item
// starting later rises inside the light, so its wider rise must not win — and it is placed first
// here so the merge cannot lean on the order it was handed.
TEST_CASE("Merging folds evidence into the widest rise at its start", "[core][highway][light]")
{
    const std::vector<HighwayLitStretch> items{
        HighwayLitStretch{.start_seconds = 1.5, .release_seconds = 2.0, .rise_seconds = 0.875},
        HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 1.5, .rise_seconds = 0.25},
        HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 2.5, .rise_seconds = 0.125},
        // Arithmetic noise past the earliest start, well inside the onset tolerance: still at it.
        HighwayLitStretch{
            .start_seconds = 1.000000000001, .release_seconds = 1.25, .rise_seconds = 0.375
        },
    };
    CHECK(
        mergeLitEvidence(items, g_rest_seconds) ==
        std::vector<HighwayLitStretch>{
            HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 2.5, .rise_seconds = 0.375},
        });
    // One item merges to itself.
    CHECK(
        mergeLitEvidence({items.front()}, g_rest_seconds) ==
        std::vector<HighwayLitStretch>{items.front()});
    // No evidence, no light.
    CHECK(mergeLitEvidence({}, g_rest_seconds).empty());

    // At the onset tolerance itself an item is a later strike: its wider rise does not win.
    const std::vector<HighwayLitStretch> at_tolerance{
        HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 1.5, .rise_seconds = 0.25},
        HighwayLitStretch{
            .start_seconds = 1.0 + g_onset_match_epsilon,
            .release_seconds = 1.5,
            .rise_seconds = 0.5,
        },
    };
    CHECK(
        mergeLitEvidence(at_tolerance, g_rest_seconds) ==
        std::vector<HighwayLitStretch>{
            HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 1.5, .rise_seconds = 0.25},
        });
}

// THE rest tolerance: evidence starting less than the tolerance after the run's LATEST release so
// far joins the run, and a gap of exactly the tolerance or more splits it. The latest release is
// measured, not the last item's: a short note inside a long one does not open a gap.
TEST_CASE("Merging splits evidence only at a gap of the rest tolerance", "[core][highway][light]")
{
    const HighwayLitStretch first{
        .start_seconds = 1.0, .release_seconds = 2.0, .rise_seconds = 0.25
    };

    // A gap of exactly the tolerance: two lights, the second's rise untouched by crowding.
    const HighwayLitStretch at_rest{
        .start_seconds = 3.0, .release_seconds = 3.5, .rise_seconds = 0.5
    };
    CHECK(
        mergeLitEvidence({first, at_rest}, g_rest_seconds) ==
        std::vector<HighwayLitStretch>{first, at_rest});

    // A gap just short of it: one light, released at the later release.
    const HighwayLitStretch inside{
        .start_seconds = 2.875, .release_seconds = 3.5, .rise_seconds = 0.5
    };
    CHECK(
        mergeLitEvidence({first, inside}, g_rest_seconds) ==
        std::vector<HighwayLitStretch>{
            HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 3.5, .rise_seconds = 0.25},
        });

    // A short item inside a long one: the gap to the next is measured from the long one's release.
    const std::vector<HighwayLitStretch> nested{
        HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 4.0, .rise_seconds = 0.25},
        HighwayLitStretch{.start_seconds = 1.5, .release_seconds = 1.75, .rise_seconds = 0.25},
        HighwayLitStretch{.start_seconds = 4.5, .release_seconds = 5.0, .rise_seconds = 0.25},
    };
    CHECK(
        mergeLitEvidence(nested, g_rest_seconds) ==
        std::vector<HighwayLitStretch>{
            HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 5.0, .rise_seconds = 0.25},
        });
}

// THE crowding clamp, shown with no tolerance at all so every non-negative gap splits: a rise that
// would reach back past the previous release shrinks to the gap, and one that fits is untouched.
// A start INSIDE the previous light (a negative gap), or touching its release (a gap narrower than
// the onset tolerance is no gap), is not crowded but merged.
TEST_CASE("Merging crowds each rise against the previous release", "[core][highway][light]")
{
    const HighwayLitStretch previous{
        .start_seconds = 0.5, .release_seconds = 1.0, .rise_seconds = 0.0
    };
    const auto second_after = [&previous](const HighwayLitStretch& next) {
        const std::vector<HighwayLitStretch> merged = mergeLitEvidence({previous, next}, 0.0);
        REQUIRE(merged.size() == 2);
        CHECK(merged.front() == previous);
        return merged.back();
    };

    CHECK(
        second_after(
            HighwayLitStretch{
                .start_seconds = 1.25, .release_seconds = 2.0, .rise_seconds = 0.5
            }) ==
        HighwayLitStretch{.start_seconds = 1.25, .release_seconds = 2.0, .rise_seconds = 0.25});
    CHECK(
        second_after(
            HighwayLitStretch{
                .start_seconds = 1.25, .release_seconds = 2.0, .rise_seconds = 0.125
            }) ==
        HighwayLitStretch{.start_seconds = 1.25, .release_seconds = 2.0, .rise_seconds = 0.125});
    CHECK(
        mergeLitEvidence(
            {previous,
             HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 2.0, .rise_seconds = 0.5}},
            0.0) ==
        std::vector<HighwayLitStretch>{
            HighwayLitStretch{.start_seconds = 0.5, .release_seconds = 2.0, .rise_seconds = 0.0},
        });
    CHECK(
        mergeLitEvidence(
            {previous,
             HighwayLitStretch{.start_seconds = 0.75, .release_seconds = 2.0, .rise_seconds = 0.5}},
            0.0) ==
        std::vector<HighwayLitStretch>{
            HighwayLitStretch{.start_seconds = 0.5, .release_seconds = 2.0, .rise_seconds = 0.0},
        });
}

// The merge sorts its own evidence: the producers gather notes and spans in two passes, so the
// order they hand in is never the timeline's, and the result must not depend on it.
TEST_CASE("Merging does not depend on the evidence order", "[core][highway][light]")
{
    std::vector<HighwayLitStretch> items{
        HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 1.5, .rise_seconds = 0.25},
        HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 1.25, .rise_seconds = 0.375},
        HighwayLitStretch{.start_seconds = 2.0, .release_seconds = 2.25, .rise_seconds = 0.125},
        HighwayLitStretch{.start_seconds = 4.0, .release_seconds = 4.5, .rise_seconds = 0.5},
        HighwayLitStretch{.start_seconds = 4.25, .release_seconds = 6.0, .rise_seconds = 0.0},
        HighwayLitStretch{.start_seconds = 8.0, .release_seconds = 8.0, .rise_seconds = 0.25},
    };
    const std::vector<HighwayLitStretch> expected{
        HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 2.25, .rise_seconds = 0.375},
        HighwayLitStretch{.start_seconds = 4.0, .release_seconds = 6.0, .rise_seconds = 0.5},
        HighwayLitStretch{.start_seconds = 8.0, .release_seconds = 8.0, .rise_seconds = 0.25},
    };
    CHECK(mergeLitEvidence(items, g_rest_seconds) == expected);
    std::ranges::reverse(items);
    CHECK(mergeLitEvidence(items, g_rest_seconds) == expected);
    std::ranges::rotate(items, items.begin() + 2);
    CHECK(mergeLitEvidence(items, g_rest_seconds) == expected);
}

} // namespace rock_hero::common::core
