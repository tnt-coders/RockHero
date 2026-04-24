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
    // Seeds the fake with a known state so tests can verify state() without issuing commands.
    explicit FakeTransport(TransportState initial_state = {})
        : m_state{initial_state}
    {}

    // Simulates the transport starting playback and broadcasts the resulting state.
    void play() override
    {
        m_state.playing = true;
        for (Listener* listener : m_listeners)
        {
            listener->onTransportStateChanged(m_state);
        }
    }

    // Simulates pause semantics by clearing playback without changing the current position.
    void pause() override
    {
        m_state.playing = false;
        for (Listener* listener : m_listeners)
        {
            listener->onTransportStateChanged(m_state);
        }
    }

    // Simulates stop semantics by clearing playback and resetting the current position.
    void stop() override
    {
        m_state.playing = false;
        m_state.position = rock_hero::core::TimePosition{};
        for (Listener* listener : m_listeners)
        {
            listener->onTransportStateChanged(m_state);
        }
    }

    // Records seek intent through the same semantic time type exposed by ITransport.
    void seek(rock_hero::core::TimePosition position) override
    {
        m_state.position = position;
        for (Listener* listener : m_listeners)
        {
            listener->onTransportStateChanged(m_state);
        }
    }

    // Returns the fake's current snapshot so tests can verify state-based consumers.
    [[nodiscard]] TransportState state() const override
    {
        return m_state;
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
    // Current fake snapshot returned by state() and sent through listener callbacks.
    TransportState m_state{};

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

// Verifies transport snapshots carry semantic timeline value types rather than raw seconds.
TEST_CASE("TransportState uses timeline position and duration value types", "[audio][transport]")
{
    const TransportState state{
        .playing = true,
        .position = rock_hero::core::TimePosition{12.5},
        .duration = rock_hero::core::TimeDuration{180.0},
    };

    CHECK(state.playing);
    CHECK(state.position == rock_hero::core::TimePosition{12.5});
    CHECK(state.duration == rock_hero::core::TimeDuration{180.0});
}

// Verifies future headless controller tests can treat ITransport as a state source.
TEST_CASE("ITransport fake stores and returns transport state", "[audio][transport]")
{
    const TransportState expected{
        .playing = true,
        .position = rock_hero::core::TimePosition{5.0},
        .duration = rock_hero::core::TimeDuration{90.0},
    };

    const FakeTransport transport{expected};

    CHECK(transport.state() == expected);
}

// Verifies reference-based listener registration delivers state and stops after removal.
TEST_CASE(
    "ITransport listeners are registered, notified, and removed by reference", "[audio][transport]")
{
    FakeTransport transport;
    CapturingTransportListener listener;

    transport.addListener(listener);
    transport.seek(rock_hero::core::TimePosition{8.0});

    CHECK(listener.call_count == 1);
    CHECK(listener.last_state.position == rock_hero::core::TimePosition{8.0});

    transport.removeListener(listener);
    transport.pause();

    CHECK(listener.call_count == 1);
}

// Verifies seek intent crosses the transport boundary as a core timeline position.
TEST_CASE("ITransport seek accepts a timeline position value", "[audio][transport]")
{
    FakeTransport transport;

    transport.seek(rock_hero::core::TimePosition{42.0});

    CHECK(transport.state().position == rock_hero::core::TimePosition{42.0});
}

} // namespace rock_hero::audio
