#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <rock_hero/audio/engine.h>
#include <vector>

namespace rock_hero::audio
{

namespace
{

// Verifies at compile time that the concrete adapter is usable through both new port surfaces.
static_assert(std::derived_from<Engine, ITransport>);
static_assert(std::derived_from<Engine, IEdit>);

// Returns the build-tree copy of the audio fixture that the real Engine loads in tests.
[[nodiscard]] std::filesystem::path fixtureAudioPath()
{
    return std::filesystem::path{TEST_DATA_DIR} / "drum_loop.wav";
}

// Wraps the real fixture path in the framework-free asset type used by IEdit.
[[nodiscard]] core::AudioAsset fixtureAudioAsset()
{
    return core::AudioAsset{fixtureAudioPath()};
}

// Captures the latest notifications observed through both listener surfaces so transport/edit
// contract tests can assert on externally visible behavior.
enum class TransportNotificationEvent : std::uint8_t
{
    EnginePlaying,
    EnginePosition,
    TransportState,
};

// Records callback activity from both listener surfaces.
class TransportNotificationRecorder final : public Engine::Listener, public ITransport::Listener
{
public:
    void enginePlayingStateChanged(bool playing) override
    {
        events.push_back(TransportNotificationEvent::EnginePlaying);
        last_playing = playing;
        ++engine_playing_call_count;
    }

    void engineTransportPositionChanged(double seconds) override
    {
        events.push_back(TransportNotificationEvent::EnginePosition);
        last_position_seconds = seconds;
        ++engine_position_call_count;
    }

    void onTransportStateChanged(const TransportState& state) override
    {
        events.push_back(TransportNotificationEvent::TransportState);
        last_transport_state = state;
        ++transport_state_call_count;
    }

    std::vector<TransportNotificationEvent> events{};
    TransportState last_transport_state{};
    bool last_playing{false};
    double last_position_seconds{0.0};
    int engine_playing_call_count{0};
    int engine_position_call_count{0};
    int transport_state_call_count{0};
};

} // namespace

// Verifies the concrete engine starts with an empty transport snapshot through ITransport.
TEST_CASE("Engine starts with an empty transport state", "[audio][engine][integration]")
{
    Engine const engine;
    ITransport const& transport = engine;

    const auto current_state = transport.state();

    CHECK_FALSE(current_state.playing);
    CHECK(current_state.position == core::TimePosition{});
    CHECK(current_state.duration == core::TimeDuration{});
}

// Verifies edit-driven source replacement updates state synchronously.
TEST_CASE("Engine edit updates state synchronously", "[audio][engine][integration]")
{
    Engine engine;
    IEdit& edit = engine;
    ITransport const& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));

    const auto applied = edit.setTrackAudioSource(core::TrackId{1}, audio_asset);

    CHECK(applied);

    const auto current_state = transport.state();
    CHECK_FALSE(current_state.playing);
    CHECK(current_state.position == core::TimePosition{});
    CHECK(current_state.duration.seconds > 0.0);
}

// Verifies a failed edit request leaves the previously loaded content visible through state().
TEST_CASE(
    "Failed engine edit preserves the existing loaded content", "[audio][engine][integration]")
{
    Engine engine;
    IEdit& edit = engine;
    ITransport const& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    REQUIRE(edit.setTrackAudioSource(core::TrackId{1}, audio_asset));

    const auto loaded_state = transport.state();
    const core::AudioAsset missing_asset{audio_asset.path.parent_path() / "missing.wav"};

    const auto applied = edit.setTrackAudioSource(core::TrackId{1}, missing_asset);

    CHECK_FALSE(applied);
    CHECK(transport.state() == loaded_state);
}

// Verifies direct ITransport commands refresh the concrete engine snapshot immediately.
TEST_CASE("Engine seek updates transport state synchronously", "[audio][engine][integration]")
{
    Engine engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    REQUIRE(edit.setTrackAudioSource(core::TrackId{1}, audio_asset));

    const double duration_seconds = transport.state().duration.seconds;
    REQUIRE(duration_seconds > 0.0);

    const double target_seconds = std::min(0.25, duration_seconds * 0.5);
    transport.seek(core::TimePosition{target_seconds});

    CHECK(transport.state().position == core::TimePosition{target_seconds});
}

// Verifies that a successful edit naturally notifies the project-owned transport listener before
// the edit call returns. A load from the empty state changes duration but leaves position at zero,
// so the legacy Engine::Listener position callback is not expected to fire here.
TEST_CASE(
    "Engine edit notifies transport listeners when transport-visible state changes",
    "[audio][engine][integration]")
{
    Engine engine;
    IEdit& edit = engine;
    ITransport& transport = engine;
    TransportNotificationRecorder recorder;

    engine.addListener(&recorder);
    transport.addListener(recorder);

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));

    const auto applied = edit.setTrackAudioSource(core::TrackId{1}, audio_asset);

    CHECK(applied);
    CHECK(recorder.transport_state_call_count >= 1);
    CHECK(recorder.last_transport_state == transport.state());
    CHECK(recorder.last_transport_state.duration.seconds > 0.0);
    CHECK(recorder.last_transport_state.position == core::TimePosition{});
    CHECK(recorder.engine_position_call_count == 0);
    CHECK(recorder.engine_playing_call_count == 0);

    transport.removeListener(recorder);
    engine.removeListener(&recorder);
}

// Verifies that a failed edit leaves listener counts unchanged when transport-visible state does
// not change.
TEST_CASE(
    "Failed engine edit does not emit transport callbacks when state is unchanged",
    "[audio][engine][integration]")
{
    Engine engine;
    IEdit& edit = engine;
    ITransport& transport = engine;
    TransportNotificationRecorder recorder;

    engine.addListener(&recorder);
    transport.addListener(recorder);

    const auto missing_asset =
        core::AudioAsset{fixtureAudioPath().parent_path() / "definitely-missing.wav"};

    const auto applied = edit.setTrackAudioSource(core::TrackId{1}, missing_asset);

    CHECK_FALSE(applied);
    CHECK(recorder.engine_playing_call_count == 0);
    CHECK(recorder.engine_position_call_count == 0);
    CHECK(recorder.transport_state_call_count == 0);
    CHECK(transport.state() == TransportState{});

    transport.removeListener(recorder);
    engine.removeListener(&recorder);
}

// Verifies that an explicit seek still drives both listener surfaces immediately when position
// actually changes, confirming the legacy Engine::Listener path remains responsive for real
// position transitions.
TEST_CASE(
    "Engine seek notifies position listeners when position changes", "[audio][engine][integration]")
{
    Engine engine;
    IEdit& edit = engine;
    ITransport& transport = engine;
    TransportNotificationRecorder recorder;

    engine.addListener(&recorder);
    transport.addListener(recorder);

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    REQUIRE(edit.setTrackAudioSource(core::TrackId{1}, audio_asset));

    const double duration_seconds = transport.state().duration.seconds;
    REQUIRE(duration_seconds > 0.25);

    recorder.events.clear();
    recorder.engine_playing_call_count = 0;
    recorder.engine_position_call_count = 0;
    recorder.transport_state_call_count = 0;

    const double target_seconds = std::min(0.25, duration_seconds * 0.5);
    transport.seek(core::TimePosition{target_seconds});

    CHECK(recorder.engine_position_call_count >= 1);
    CHECK(recorder.last_position_seconds == Catch::Approx(target_seconds));
    CHECK(recorder.transport_state_call_count >= 1);
    CHECK(recorder.last_transport_state.position == core::TimePosition{target_seconds});
    CHECK_FALSE(recorder.last_transport_state.playing);

    transport.removeListener(recorder);
    engine.removeListener(&recorder);
}

} // namespace rock_hero::audio
