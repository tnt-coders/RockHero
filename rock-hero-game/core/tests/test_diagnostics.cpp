#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <rock_hero/game/core/diagnostics/diagnostics.h>
#include <variant>
#include <vector>

namespace
{

using namespace std::chrono_literals;
using rock_hero::game::core::ChartSourceWatcher;
using rock_hero::game::core::DiagnosticsController;
using rock_hero::game::core::DiagnosticsIntent;
using rock_hero::game::core::ReloadChartIntent;
using rock_hero::game::core::SeekToSectionIntent;

TEST_CASE("Diagnostics controller starts visible in dev mode", "[diagnostics]")
{
    const DiagnosticsController dev_controller{true};
    CHECK(dev_controller.state().dev_mode);
    CHECK(dev_controller.state().overlay_visible);
    CHECK_FALSE(dev_controller.state().autoplay_enabled);

    const DiagnosticsController player_controller{false};
    CHECK_FALSE(player_controller.state().dev_mode);
    CHECK_FALSE(player_controller.state().overlay_visible);
}

TEST_CASE("Diagnostics controller toggles flip state in dev mode", "[diagnostics]")
{
    DiagnosticsController controller{true};

    controller.toggleOverlay();
    CHECK_FALSE(controller.state().overlay_visible);
    controller.toggleOverlay();
    CHECK(controller.state().overlay_visible);

    controller.toggleAutoplay();
    CHECK(controller.state().autoplay_enabled);
    controller.toggleAutoplay();
    CHECK_FALSE(controller.state().autoplay_enabled);
}

TEST_CASE("Diagnostics controller ignores every mutation outside dev mode", "[diagnostics]")
{
    DiagnosticsController controller{false};

    controller.toggleOverlay();
    controller.toggleAutoplay();
    controller.requestChartReload();
    controller.requestSeekToSection(3);

    CHECK_FALSE(controller.state().overlay_visible);
    CHECK_FALSE(controller.state().autoplay_enabled);
    CHECK(controller.takePendingIntents().empty());
}

TEST_CASE("Diagnostics controller queues and drains intents in order", "[diagnostics]")
{
    DiagnosticsController controller{true};

    controller.requestChartReload();
    controller.requestSeekToSection(7);

    const std::vector<DiagnosticsIntent> intents = controller.takePendingIntents();
    REQUIRE(intents.size() == 2);
    CHECK(std::holds_alternative<ReloadChartIntent>(intents[0]));
    REQUIRE(std::holds_alternative<SeekToSectionIntent>(intents[1]));
    CHECK(std::get<SeekToSectionIntent>(intents[1]).section_index == 7);

    // Draining empties the queue; a second drain reports nothing.
    CHECK(controller.takePendingIntents().empty());
}

TEST_CASE("Chart source watcher primes on the first observation", "[diagnostics]")
{
    ChartSourceWatcher watcher{250ms};

    CHECK_FALSE(watcher.update(std::chrono::nanoseconds{100}, 0ns));
    // The primed stamp settling for any duration is not a change.
    CHECK_FALSE(watcher.update(std::chrono::nanoseconds{100}, 10s));
}

TEST_CASE("Chart source watcher reports a change only after it settles", "[diagnostics]")
{
    ChartSourceWatcher watcher{250ms};
    REQUIRE_FALSE(watcher.update(std::chrono::nanoseconds{100}, 0ns));

    // Change observed: pending, not yet reported.
    CHECK_FALSE(watcher.update(std::chrono::nanoseconds{200}, 1s));
    // Still inside the settle interval.
    CHECK_FALSE(watcher.update(std::chrono::nanoseconds{200}, 1s + 100ms));
    // Settled: reported exactly once.
    CHECK(watcher.update(std::chrono::nanoseconds{200}, 1s + 300ms));
    CHECK_FALSE(watcher.update(std::chrono::nanoseconds{200}, 2s));

    // A later change goes through the same cycle.
    CHECK_FALSE(watcher.update(std::chrono::nanoseconds{300}, 3s));
    CHECK(watcher.update(std::chrono::nanoseconds{300}, 3s + 250ms));
}

TEST_CASE("Chart source watcher restarts settling while the stamp keeps moving", "[diagnostics]")
{
    ChartSourceWatcher watcher{250ms};
    REQUIRE_FALSE(watcher.update(std::chrono::nanoseconds{100}, 0ns));

    // A write burst: every new stamp restarts the settle timer.
    CHECK_FALSE(watcher.update(std::chrono::nanoseconds{200}, 1s));
    CHECK_FALSE(watcher.update(std::chrono::nanoseconds{300}, 1s + 200ms));
    CHECK_FALSE(watcher.update(std::chrono::nanoseconds{400}, 1s + 400ms));
    // 240ms after the last movement: still not settled.
    CHECK_FALSE(watcher.update(std::chrono::nanoseconds{400}, 1s + 640ms));
    // Settled relative to the LAST movement, not the first.
    CHECK(watcher.update(std::chrono::nanoseconds{400}, 1s + 660ms));
}

TEST_CASE("Chart source watcher ignores missing samples and reverts", "[diagnostics]")
{
    ChartSourceWatcher watcher{250ms};
    REQUIRE_FALSE(watcher.update(std::chrono::nanoseconds{100}, 0ns));

    // Missing samples (atomic replace in flight) disturb nothing.
    CHECK_FALSE(watcher.update(std::nullopt, 1s));
    CHECK_FALSE(watcher.update(std::nullopt, 2s));

    // A pending change that reverts to the baseline stamp is cancelled.
    CHECK_FALSE(watcher.update(std::chrono::nanoseconds{200}, 3s));
    CHECK_FALSE(watcher.update(std::chrono::nanoseconds{100}, 3s + 100ms));
    CHECK_FALSE(watcher.update(std::chrono::nanoseconds{100}, 10s));
}

} // namespace
