#include <catch2/catch_test_macros.hpp>
#include <rock_hero/audio/i_transport.h>
#include <vector>

namespace rock_hero::audio
{

namespace
{

// Minimal fake transport that lets contract tests exercise state flow and listener registration.
class FakeTransport final : public ITransport
{
public:
    // Seeds the fake with known state and position without issuing commands.
    explicit FakeTransport(
        TransportState initial_state = {}, rock_hero::core::TimePosition initial_position = {})
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
        m_position = rock_hero::core::TimePosition{};
        notifyListeners();
    }

    // Records seek intent through the live position channel without broadcasting coarse state.
    void seek(rock_hero::core::TimePosition position) override
    {
        m_position = position;
    }

    // Returns the fake's current coarse state so tests can verify state-based consumers.
    [[nodiscard]] TransportState state() const noexcept override
    {
        return m_state;
    }

    // Returns the fake's current position through the live-read transport method.
    [[nodiscard]] rock_hero::core::TimePosition position() const noexcept override
    {
        return m_position;
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

    // Current live position returned by position(); intentionally excluded from TransportState.
    rock_hero::core::TimePosition m_position{};

    // Non-owning listeners registered by tests; each listener outlives its registration.
    std::vector<Listener*> m_listeners{};
};

// Captures the latest transport callback so tests can verify listener delivery without Engine.
class CapturingTransportListener final : public ITransport::Listener
{
public:
    // Records every callback so tests can inspect both delivery count and latest state.
    void onTransportStateChanged(const TransportState& state) override
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

// Verifies transport state carries semantic timeline value types rather than raw seconds.
TEST_CASE("TransportState uses a timeline duration value type", "[audio][transport]")
{
    const TransportState state{
        .playing = true,
        .duration = rock_hero::core::TimeDuration{180.0},
    };

    CHECK(state.playing);
    CHECK(state.duration == rock_hero::core::TimeDuration{180.0});
}

// Verifies future headless controller tests can treat ITransport as a state source.
TEST_CASE("ITransport fake stores state and position separately", "[audio][transport]")
{
    const TransportState expected_state{
        .playing = true,
        .duration = rock_hero::core::TimeDuration{90.0},
    };
    const auto expected_position = rock_hero::core::TimePosition{5.0};

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

    transport.seek(rock_hero::core::TimePosition{42.0});

    CHECK(transport.position() == rock_hero::core::TimePosition{42.0});
    CHECK(transport.state() == TransportState{});
    CHECK(listener.call_count == 0);
}

} // namespace rock_hero::audio
