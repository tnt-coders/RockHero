#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <compare>
#include <expected>
#include <optional>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Minimal fake transport that lets contract tests exercise state flow and listener registration.
class FakeTransport final : public ITransport
{
public:
    // Seeds the fake with known state and position without issuing commands.
    explicit FakeTransport(
        TransportState initial_state = {},
        rock_hero::common::core::TimePosition initial_position = {})
        : m_state{initial_state}
        , m_position{initial_position}
    {}

    // Simulates the transport starting playback and broadcasts the resulting coarse transition.
    void play() override
    {
        m_state.playing = true;
        notifyListeners();
    }

    // Simulates pause semantics by clearing playback without changing the current position.
    void pause() override
    {
        m_state.playing = false;
        notifyListeners();
    }

    // Simulates stop semantics by clearing playback and resetting the current position.
    void stop() override
    {
        m_state.playing = false;
        m_position = rock_hero::common::core::TimePosition{};
        notifyListeners();
    }

    // Records seek intent through the current position channel without broadcasting coarse state.
    void seek(rock_hero::common::core::TimePosition position) override
    {
        m_position = position;
    }

    // Returns the fake's current coarse state so tests can verify state-based consumers.
    [[nodiscard]] TransportState state() const noexcept override
    {
        return m_state;
    }

    // Returns the fake's current position through the current-position transport method.
    [[nodiscard]] rock_hero::common::core::TimePosition position() const noexcept override
    {
        return m_position;
    }

    // Mirrors the v1 speed contract: exactly 1.0 is accepted, everything else fails loudly.
    [[nodiscard]] std::expected<void, TransportError> setPlaybackSpeed(double factor) override
    {
        if (std::is_neq(factor <=> 1.0))
        {
            return std::unexpected{TransportError{TransportErrorCode::SpeedNotSupported}};
        }

        m_playback_speed = factor;
        return {};
    }

    // Returns the fake's stored speed factor; 1.0 under the v1 contract.
    [[nodiscard]] double playbackSpeed() const noexcept override
    {
        return m_playback_speed;
    }

    // Mirrors the loop contract: normalize endpoint order, then reject sub-minimum regions
    // without touching any previously engaged loop.
    [[nodiscard]] std::expected<void, TransportError> setLoopRegion(
        rock_hero::common::core::TimeRange region) override
    {
        const rock_hero::common::core::TimeRange normalized{
            .start = rock_hero::common::core::TimePosition{std::min(
                region.start.seconds, region.end.seconds)},
            .end = rock_hero::common::core::TimePosition{std::max(
                region.start.seconds, region.end.seconds)},
        };
        if (normalized.duration().seconds < g_minimum_loop_region_duration.seconds)
        {
            return std::unexpected{TransportError{TransportErrorCode::LoopRegionTooShort}};
        }

        m_loop_region = normalized;
        return {};
    }

    // Disengages the fake's loop; clearing with no loop engaged stays a no-op per the contract.
    void clearLoopRegion() override
    {
        m_loop_region.reset();
    }

    // Returns the engaged normalized loop region, or nullopt when looping is disengaged.
    [[nodiscard]] std::optional<rock_hero::common::core::TimeRange> loopRegion()
        const noexcept override
    {
        return m_loop_region;
    }

    // Stores a non-owning listener pointer to mirror the production listener lifetime contract.
    void addListener(Listener& listener) override
    {
        m_listeners.push_back(&listener);
    }

    // Removes the exact listener object registered by addListener().
    void removeListener(Listener& listener) override
    {
        std::erase(m_listeners, &listener);
    }

private:
    // Sends the current coarse state to every registered test listener.
    void notifyListeners()
    {
        for (Listener* listener : m_listeners)
        {
            listener->onTransportStateChanged(m_state);
        }
    }

    // Current coarse state returned by state() and sent through listener callbacks.
    TransportState m_state{};

    // Current current position returned by position(); intentionally excluded from TransportState.
    rock_hero::common::core::TimePosition m_position{};

    // Port-level speed factor stored by setPlaybackSpeed(); only 1.0 is storable in v1.
    double m_playback_speed{1.0};

    // Engaged normalized loop region; nullopt while looping is disengaged.
    std::optional<rock_hero::common::core::TimeRange> m_loop_region{};

    // Non-owning listeners registered by tests; each listener outlives its registration.
    std::vector<Listener*> m_listeners{};
};

// Captures the latest transport callback so tests can verify listener delivery without Engine.
class CapturingTransportListener final : public ITransport::Listener
{
public:
    // Records every callback so tests can inspect both delivery count and latest state.
    void onTransportStateChanged(TransportState state) override
    {
        last_state = state;
        ++call_count;
    }

    // Latest state observed through the listener callback.
    TransportState last_state{};

    // Number of callbacks received, used to verify removal stops later notifications.
    int call_count{0};
};

} // namespace

// Verifies future headless controller tests can treat ITransport as a state source.
TEST_CASE("ITransport fake stores state and position separately", "[audio][transport]")
{
    const TransportState expected_state{
        .playing = true,
    };
    const auto expected_position = rock_hero::common::core::TimePosition{5.0};

    const FakeTransport transport{expected_state, expected_position};

    CHECK(transport.state() == expected_state);
    CHECK(transport.position() == expected_position);
}

// Verifies reference-based listener registration delivers coarse transitions and stops after
// removal.
TEST_CASE(
    "ITransport listeners are registered, notified, and removed by reference", "[audio][transport]")
{
    FakeTransport transport;
    CapturingTransportListener listener;

    transport.addListener(listener);
    transport.play();

    CHECK(listener.call_count == 1);
    CHECK(listener.last_state.playing);

    transport.removeListener(listener);
    transport.pause();

    CHECK(listener.call_count == 1);
}

// Verifies seek intent crosses the transport boundary as a core timeline position without forcing
// a coarse listener callback for position-only motion.
TEST_CASE("ITransport seek accepts a timeline position value", "[audio][transport]")
{
    FakeTransport transport;
    CapturingTransportListener listener;

    transport.addListener(listener);

    transport.seek(rock_hero::common::core::TimePosition{42.0});

    CHECK(transport.position() == rock_hero::common::core::TimePosition{42.0});
    CHECK(transport.state() == TransportState{});
    CHECK(listener.call_count == 0);
}

// Verifies the v1 speed contract: 1.0 round-trips, everything else is a typed loud failure.
TEST_CASE("ITransport playback speed accepts only 1.0 in v1", "[audio][transport]")
{
    FakeTransport transport;

    CHECK(transport.setPlaybackSpeed(1.0).has_value());
    CHECK_THAT(transport.playbackSpeed(), Catch::Matchers::WithinULP(1.0, 0));

    const auto rejected = transport.setPlaybackSpeed(0.5);
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().code == TransportErrorCode::SpeedNotSupported);
    CHECK_THAT(transport.playbackSpeed(), Catch::Matchers::WithinULP(1.0, 0));
}

// Verifies the loop contract's happy path: set round-trips through loopRegion() and clear
// disengages back to nullopt.
TEST_CASE("ITransport loop region set and clear round-trip", "[audio][transport]")
{
    FakeTransport transport;

    CHECK_FALSE(transport.loopRegion().has_value());

    const rock_hero::common::core::TimeRange region{
        .start = rock_hero::common::core::TimePosition{2.0},
        .end = rock_hero::common::core::TimePosition{6.5},
    };
    REQUIRE(transport.setLoopRegion(region).has_value());
    CHECK(transport.loopRegion() == std::optional{region});

    transport.clearLoopRegion();
    CHECK_FALSE(transport.loopRegion().has_value());

    // Clearing again with no loop engaged stays a harmless no-op per the contract.
    transport.clearLoopRegion();
    CHECK_FALSE(transport.loopRegion().has_value());
}

// Verifies reversed endpoints normalize into a forward region instead of failing.
TEST_CASE("ITransport loop region normalizes reversed endpoints", "[audio][transport]")
{
    FakeTransport transport;

    REQUIRE(transport
                .setLoopRegion({
                    .start = rock_hero::common::core::TimePosition{6.5},
                    .end = rock_hero::common::core::TimePosition{2.0},
                })
                .has_value());

    const auto region = transport.loopRegion();
    REQUIRE(region.has_value());
    if (region.has_value())
    {
        CHECK(region->start == rock_hero::common::core::TimePosition{2.0});
        CHECK(region->end == rock_hero::common::core::TimePosition{6.5});
    }
}

// Verifies a sub-minimum region is rejected with the typed error and any engaged loop survives.
TEST_CASE("ITransport loop region rejects sub-minimum durations", "[audio][transport]")
{
    FakeTransport transport;

    const rock_hero::common::core::TimeRange engaged{
        .start = rock_hero::common::core::TimePosition{1.0},
        .end = rock_hero::common::core::TimePosition{3.0},
    };
    REQUIRE(transport.setLoopRegion(engaged).has_value());

    const auto rejected = transport.setLoopRegion({
        .start = rock_hero::common::core::TimePosition{5.0},
        .end = rock_hero::common::core::TimePosition{
            5.0 + (g_minimum_loop_region_duration.seconds / 2.0)
        },
    });
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().code == TransportErrorCode::LoopRegionTooShort);

    // The previously engaged loop is untouched by the rejected request.
    CHECK(transport.loopRegion() == std::optional{engaged});
}

} // namespace rock_hero::common::audio
