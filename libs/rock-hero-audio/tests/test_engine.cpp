#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/audio/engine.h>

namespace rock_hero::audio
{

namespace
{

// Verifies at compile time that the concrete adapter is usable through its audio port surfaces.
static_assert(std::derived_from<Engine, ITransport>);
static_assert(std::derived_from<Engine, IEdit>);
static_assert(std::derived_from<Engine, IThumbnailFactory>);

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

// Records callback activity from the project-owned transport listener surface.
class TransportNotificationRecorder final : public ITransport::Listener
{
public:
    void onTransportStatusChanged(const TransportStatus& status) override
    {
        last_transport_status = status;
        ++transport_status_call_count;
    }

    TransportStatus last_transport_status{};
    int transport_status_call_count{0};
};

// Owns the JUCE runtime guard before constructing the real Tracktion-backed engine.
class EngineTestHarness final
{
public:
    // Creates JUCE's message-manager lifetime needed by Tracktion timers and callbacks in tests.
    juce::ScopedJuceInitialiser_GUI scoped_gui;

    // Real adapter under test; destroyed before scoped_gui because members tear down in reverse.
    Engine engine;
};

} // namespace

// Verifies the concrete engine starts with empty status and a zero live position.
TEST_CASE("Engine starts with empty transport status", "[audio][engine][integration]")
{
    const EngineTestHarness harness;
    const Engine& engine = harness.engine;
    ITransport const& transport = engine;

    const auto current_status = transport.status();

    CHECK_FALSE(current_status.playing);
    CHECK(current_status.duration == core::TimeDuration{});
    CHECK(transport.position() == core::TimePosition{});
}

// Verifies edit-driven source replacement updates status synchronously.
TEST_CASE("Engine edit updates status synchronously", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport const& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));

    const auto applied = edit.setTrackAudioSource(core::TrackId{1}, audio_asset);

    CHECK(applied);

    const auto current_status = transport.status();
    CHECK_FALSE(current_status.playing);
    CHECK(current_status.duration.seconds > 0.0);
    CHECK(transport.position() == core::TimePosition{});
}

// Verifies a failed edit request leaves the previously loaded content visible through status().
TEST_CASE(
    "Failed engine edit preserves the existing loaded content", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport const& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    REQUIRE(edit.setTrackAudioSource(core::TrackId{1}, audio_asset));

    const auto loaded_status = transport.status();
    const core::AudioAsset missing_asset{audio_asset.path.parent_path() / "missing.wav"};

    const auto applied = edit.setTrackAudioSource(core::TrackId{1}, missing_asset);

    CHECK_FALSE(applied);
    CHECK(transport.status() == loaded_status);
}

// Verifies direct ITransport seek commands update the live position without mutating status.
TEST_CASE("Engine seek updates live transport position", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    REQUIRE(edit.setTrackAudioSource(core::TrackId{1}, audio_asset));

    const auto loaded_status = transport.status();
    const double duration_seconds = loaded_status.duration.seconds;
    REQUIRE(duration_seconds > 0.0);

    const double target_seconds = std::min(0.25, duration_seconds * 0.5);
    transport.seek(core::TimePosition{target_seconds});

    CHECK(transport.status() == loaded_status);
    CHECK(transport.position() == core::TimePosition{target_seconds});
}

// Verifies the cursor-position read reports the post-seek position from the concrete adapter.
TEST_CASE("Engine position reflects public transport seeks", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;
    const ITransport& read_only_transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    REQUIRE(edit.setTrackAudioSource(core::TrackId{1}, audio_asset));

    const double duration_seconds = transport.status().duration.seconds;
    REQUIRE(duration_seconds > 0.0);

    const double target_seconds = std::min(0.3, duration_seconds * 0.5);
    transport.seek(core::TimePosition{target_seconds});

    CHECK(read_only_transport.position() == core::TimePosition{target_seconds});
}

// Verifies that a successful edit naturally notifies the project-owned transport listener before
// the edit call returns.
TEST_CASE(
    "Engine edit notifies transport listeners when status changes", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;
    TransportNotificationRecorder recorder;

    transport.addListener(recorder);

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));

    const auto applied = edit.setTrackAudioSource(core::TrackId{1}, audio_asset);

    CHECK(applied);
    CHECK(recorder.transport_status_call_count >= 1);
    CHECK(recorder.last_transport_status == transport.status());
    CHECK(recorder.last_transport_status.duration.seconds > 0.0);

    transport.removeListener(recorder);
}

// Verifies position-only seeking remains invisible to coarse transport status listeners.
TEST_CASE("Engine seek does not emit status callbacks", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    REQUIRE(edit.setTrackAudioSource(core::TrackId{1}, audio_asset));

    const double duration_seconds = transport.status().duration.seconds;
    REQUIRE(duration_seconds > 0.0);

    TransportNotificationRecorder recorder;
    transport.addListener(recorder);

    const double target_seconds = std::min(0.2, duration_seconds * 0.5);
    transport.seek(core::TimePosition{target_seconds});

    CHECK(recorder.transport_status_call_count == 0);
    CHECK(transport.position() == core::TimePosition{target_seconds});

    transport.removeListener(recorder);
}

// Verifies that a failed edit leaves listener counts unchanged when transport-visible status does
// not change.
TEST_CASE(
    "Failed engine edit does not emit callbacks when status is unchanged",
    "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;
    TransportNotificationRecorder recorder;

    transport.addListener(recorder);

    const auto missing_asset =
        core::AudioAsset{fixtureAudioPath().parent_path() / "definitely-missing.wav"};

    const auto applied = edit.setTrackAudioSource(core::TrackId{1}, missing_asset);

    CHECK_FALSE(applied);
    CHECK(recorder.transport_status_call_count == 0);
    CHECK(transport.status() == TransportStatus{});

    transport.removeListener(recorder);
}

} // namespace rock_hero::audio
