#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <filesystem>
#include <rock_hero/audio/engine.h>

namespace rock_hero::audio
{

namespace
{

// Verifies at compile time that the concrete adapter is usable through both new port surfaces.
static_assert(std::derived_from<Engine, ITransport>);
static_assert(std::derived_from<Engine, IEdit>);

// Returns a repo-local audio fixture path that the real Engine can load in tests.
[[nodiscard]] std::filesystem::path fixtureAudioPath()
{
    return std::filesystem::path{ROCK_HERO_AUDIO_TEST_SOURCE_DIR}
               .parent_path()
               .parent_path()
               .parent_path() /
           "external" / "tracktion_engine" / "examples" / "DemoRunner" / "resources" /
           "drum_loop.wav";
}

// Wraps the real fixture path in the framework-free asset type used by IEdit.
[[nodiscard]] core::AudioAsset fixtureAudioAsset()
{
    return core::AudioAsset{fixtureAudioPath()};
}

// Captures legacy Engine listener callbacks so edit-quiet behavior can be verified.
class CapturingEngineListener final : public Engine::Listener
{
public:
    // Records play/pause transitions received from the concrete engine adapter.
    void enginePlayingStateChanged(bool playing) override
    {
        last_playing = playing;
        ++playing_call_count;
    }

    // Records position-change callbacks received from the concrete engine adapter.
    void engineTransportPositionChanged(double seconds) override
    {
        last_position_seconds = seconds;
        ++position_call_count;
    }

    // Latest playing flag observed through the legacy listener surface.
    bool last_playing{false};

    // Latest position observed through the legacy listener surface.
    double last_position_seconds{0.0};

    // Number of play/pause callbacks received.
    int playing_call_count{0};

    // Number of position callbacks received.
    int position_call_count{0};
};

// Captures project-owned transport callbacks so edit work stays off the transport channel.
class CapturingTransportListener final : public ITransport::Listener
{
public:
    // Records the latest transport snapshot pushed through the project-owned listener surface.
    void onTransportStateChanged(const TransportState& state) override
    {
        last_state = state;
        ++call_count;
    }

    // Latest state snapshot observed through ITransport::Listener.
    TransportState last_state{};

    // Number of transport-state callbacks received.
    int call_count{0};
};

} // namespace

// Verifies the concrete engine starts with an empty transport snapshot through ITransport.
TEST_CASE("Engine starts with an empty transport state")
{
    Engine engine;
    ITransport& transport = engine;

    const auto current_state = transport.state();

    CHECK_FALSE(current_state.playing);
    CHECK(current_state.position == core::TimePosition{});
    CHECK(current_state.duration == core::TimeDuration{});
}

// Verifies edit-driven source replacement updates state synchronously without firing listeners.
TEST_CASE("Engine edit updates state without notifying transport listeners")
{
    Engine engine;
    IEdit& edit = engine;
    ITransport& transport = engine;
    CapturingEngineListener engine_listener;
    CapturingTransportListener transport_listener;

    engine.addListener(&engine_listener);
    transport.addListener(transport_listener);

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));

    const auto result = edit.setTrackAudioSource(core::TrackId{1}, audio_asset);

    CHECK(result.applied);

    const auto current_state = transport.state();
    CHECK(result.transport_state == current_state);
    CHECK_FALSE(current_state.playing);
    CHECK(current_state.position == core::TimePosition{});
    CHECK(current_state.duration.seconds > 0.0);
    CHECK(engine_listener.playing_call_count == 0);
    CHECK(engine_listener.position_call_count == 0);
    CHECK(transport_listener.call_count == 0);

    transport.removeListener(transport_listener);
    engine.removeListener(&engine_listener);
}

// Verifies a failed edit request leaves the previously loaded content visible through state().
TEST_CASE("Failed engine edit preserves the existing loaded content")
{
    Engine engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    REQUIRE(edit.setTrackAudioSource(core::TrackId{1}, audio_asset).applied);

    const auto loaded_state = transport.state();
    const core::AudioAsset missing_asset{audio_asset.path.parent_path() / "missing.wav"};

    const auto result = edit.setTrackAudioSource(core::TrackId{1}, missing_asset);

    CHECK_FALSE(result.applied);
    CHECK(result.transport_state == loaded_state);
    CHECK(transport.state() == loaded_state);
}

// Verifies direct ITransport commands refresh the concrete engine snapshot immediately.
TEST_CASE("Engine seek updates transport state synchronously")
{
    Engine engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    REQUIRE(edit.setTrackAudioSource(core::TrackId{1}, audio_asset).applied);

    const double duration_seconds = transport.state().duration.seconds;
    REQUIRE(duration_seconds > 0.0);

    const double target_seconds = std::min(0.25, duration_seconds * 0.5);
    transport.seek(core::TimePosition{target_seconds});

    CHECK(transport.state().position == core::TimePosition{target_seconds});
}

} // namespace rock_hero::audio
