#include <catch2/catch_test_macros.hpp>
#include <rock_hero/audio/i_transport.h>
#include <vector>

namespace rock_hero::audio
{

namespace
{

// Minimal fake transport that lets contract tests exercise status flow and listener registration.
class FakeTransport final : public ITransport
{
public:
    // Seeds the fake with known status and position without issuing commands.
    explicit FakeTransport(
        TransportStatus initial_status = {}, rock_hero::core::TimePosition initial_position = {})
        : m_status{initial_status}
        , m_position{initial_position}
    {}

    // Simulates the transport starting playback and broadcasts the resulting coarse transition.
    void play() override
    {
        m_status.playing = true;
        notifyListeners();
    }

    // Simulates pause semantics by clearing playback without changing the current position.
    void pause() override
    {
        m_status.playing = false;
        notifyListeners();
    }

    // Simulates stop semantics by clearing playback and resetting the current position.
    void stop() override
    {
        m_status.playing = false;
        m_position = rock_hero::core::TimePosition{};
        notifyListeners();
    }

    // Records seek intent through the live position channel without broadcasting coarse status.
    void seek(rock_hero::core::TimePosition position) override
    {
        m_position = position;
    }

    // Returns the fake's current coarse status so tests can verify status-based consumers.
    [[nodiscard]] TransportStatus status() const noexcept override
    {
        return m_status;
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
    // Sends the current coarse status to every registered test listener.
    void notifyListeners()
    {
        for (Listener* listener : m_listeners)
        {
            listener->onTransportStatusChanged(m_status);
        }
    }

    // Current coarse status returned by status() and sent through listener callbacks.
    TransportStatus m_status{};

    // Current live position returned by position(); intentionally excluded from status().
    rock_hero::core::TimePosition m_position{};

    // Non-owning listeners registered by tests; each listener outlives its registration.
    std::vector<Listener*> m_listeners{};
};

// Captures the latest transport callback so tests can verify listener delivery without Engine.
class CapturingTransportListener final : public ITransport::Listener
{
public:
    // Records every callback so tests can inspect both delivery count and latest status.
    void onTransportStatusChanged(const TransportStatus& status) override
    {
        last_status = status;
        ++call_count;
    }

    // Latest status observed through the listener callback.
    TransportStatus last_status{};

    // Number of callbacks received, used to verify removal stops later notifications.
    int call_count{0};
};

} // namespace

// Verifies transport status carries semantic timeline value types rather than raw seconds.
TEST_CASE("TransportStatus uses a timeline duration value type", "[audio][transport]")
{
    const TransportStatus status{
        .playing = true,
        .duration = rock_hero::core::TimeDuration{180.0},
    };

    CHECK(status.playing);
    CHECK(status.duration == rock_hero::core::TimeDuration{180.0});
}

// Verifies future headless controller tests can treat ITransport as a status source.
TEST_CASE("ITransport fake stores status and position separately", "[audio][transport]")
{
    const TransportStatus expected_status{
        .playing = true,
        .duration = rock_hero::core::TimeDuration{90.0},
    };
    const auto expected_position = rock_hero::core::TimePosition{5.0};

    const FakeTransport transport{expected_status, expected_position};

    CHECK(transport.status() == expected_status);
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
    CHECK(listener.last_status.playing);

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
    CHECK(transport.status() == TransportStatus{});
    CHECK(listener.call_count == 0);
}

} // namespace rock_hero::audio
