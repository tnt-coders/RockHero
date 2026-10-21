#include "shared/flash_pulse.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace rock_hero::editor::ui
{

// THE ONE RULE: a new refusal REPLACES the running flash's record and restarts its clock, so a
// refusal never re-lights what an earlier, different one named. Replaced at the train's first
// trough, a third of the way through, where the old clock is dark: only a restarted clock reads
// lit there.
TEST_CASE("A new refusal replaces the running flash and restarts its clock", "[ui][flash-pulse]")
{
    std::optional<PulsedFlash<std::vector<int>>> flash;
    CHECK(startRefusalFlash(flash, std::vector{1}));
    REQUIRE(flash.has_value());

    juce::Thread::sleep(static_cast<int>(g_refusal_flash_train.seconds * 1000.0 / 3.0));
    CHECK(startRefusalFlash(flash, std::vector{2}));

    REQUIRE(flash.has_value());
    if (flash.has_value())
    {
        CHECK(flash->subject == std::vector{2});
        CHECK(flash->pulse.level() > 0.9f);
        CHECK_FALSE(flash->pulse.finished());
    }
}

// A refusal naming nothing on a surface clears its flash, and asks for a repaint only where one
// ran.
TEST_CASE("A refusal naming nothing clears the running flash", "[ui][flash-pulse]")
{
    std::optional<PulsedFlash<std::vector<int>>> flash;
    CHECK_FALSE(startRefusalFlash(flash, std::vector<int>{}));
    CHECK_FALSE(flash.has_value());
    CHECK(startRefusalFlash(flash, std::vector{1}));
    CHECK(startRefusalFlash(flash, std::vector<int>{}));
    CHECK_FALSE(flash.has_value());
}

// A flash that holds a mark is not finished while the mark still stands, though its pulse train
// has run: finished() is the one answer to whether the flash shows anything, which the surface
// retiring the flash reads. A flash holding nothing finishes with its train.
TEST_CASE("A flash holding a mark finishes only once the mark has faded", "[ui][flash-pulse]")
{
    constexpr FlashTrain short_train{.pulses = 1.0, .seconds = 0.01};
    const FlashPulse holding{short_train, FlashHold{.hold_seconds = 30.0, .fade_seconds = 1.0}};
    const FlashPulse bare{short_train};

    juce::Thread::sleep(40);

    CHECK_FALSE(holding.finished());
    CHECK_FALSE(holding.heldFinished());
    CHECK(holding.heldLevel() > 0.99f);
    CHECK(bare.finished());
    CHECK(bare.heldFinished());
    CHECK(bare.heldLevel() < 0.01f);
}

} // namespace rock_hero::editor::ui
