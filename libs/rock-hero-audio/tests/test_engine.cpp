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
    void onTransportStateChanged(TransportState state) override
    {
        last_transport_state = state;
        ++transport_state_call_count;
    }

    TransportState last_transport_state{};
    int transport_state_call_count{0};
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

// Verifies the concrete engine starts with empty state and a zero live position.
TEST_CASE("Engine starts with empty transport state", "[audio][engine][integration]")
{
    const EngineTestHarness harness;
    const Engine& engine = harness.engine;
    ITransport const& transport = engine;

    const auto current_state = transport.state();

    CHECK_FALSE(current_state.playing);
    CHECK(transport.position() == core::TimePosition{});
}

// Verifies edit-driven source replacement returns the accepted timeline synchronously.
TEST_CASE("Engine edit returns accepted timeline synchronously", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport const& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));

    const auto applied = edit.setTrackAudioSource(core::TrackId{1}, audio_asset);

    REQUIRE(applied.has_value());
    CHECK(applied->start == core::TimePosition{});
    CHECK(applied->duration().seconds > 0.0);

    const auto current_state = transport.state();
    CHECK_FALSE(current_state.playing);
    CHECK(transport.position() == core::TimePosition{});
}

// Verifies a failed edit request leaves the previous transport state visible through state().
TEST_CASE("Failed engine edit preserves existing transport state", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport const& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    REQUIRE(edit.setTrackAudioSource(core::TrackId{1}, audio_asset).has_value());

    const auto loaded_state = transport.state();
    const core::AudioAsset missing_asset{audio_asset.path.parent_path() / "missing.wav"};

    const auto applied = edit.setTrackAudioSource(core::TrackId{1}, missing_asset);

    CHECK_FALSE(applied.has_value());
    CHECK(transport.state() == loaded_state);
}

// Verifies direct ITransport seek commands update the live position without mutating state.
TEST_CASE("Engine seek updates live transport position", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    const auto timeline_range = edit.setTrackAudioSource(core::TrackId{1}, audio_asset);
    REQUIRE(timeline_range.has_value());

    const auto loaded_state = transport.state();
    const double duration_seconds = timeline_range->duration().seconds;
    REQUIRE(duration_seconds > 0.0);

    const double target_seconds = std::min(0.25, duration_seconds * 0.5);
    transport.seek(core::TimePosition{target_seconds});

    CHECK(transport.state() == loaded_state);
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
    const auto timeline_range = edit.setTrackAudioSource(core::TrackId{1}, audio_asset);
    REQUIRE(timeline_range.has_value());

    const double duration_seconds = timeline_range->duration().seconds;
    REQUIRE(duration_seconds > 0.0);

    const double target_seconds = std::min(0.3, duration_seconds * 0.5);
    transport.seek(core::TimePosition{target_seconds});

    CHECK(read_only_transport.position() == core::TimePosition{target_seconds});
}

// Verifies successful timeline-only edits do not emit coarse transport state callbacks.
TEST_CASE(
    "Engine edit does not notify listeners when playback state is unchanged",
    "[audio][engine][integration]")
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

    REQUIRE(applied.has_value());
    CHECK(applied->duration().seconds > 0.0);
    CHECK(recorder.transport_state_call_count == 0);
    CHECK(recorder.last_transport_state == TransportState{});

    transport.removeListener(recorder);
}

// Verifies position-only seeking remains invisible to coarse transport state listeners.
TEST_CASE("Engine seek does not emit state callbacks", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    const auto timeline_range = edit.setTrackAudioSource(core::TrackId{1}, audio_asset);
    REQUIRE(timeline_range.has_value());

    const double duration_seconds = timeline_range->duration().seconds;
    REQUIRE(duration_seconds > 0.0);

    TransportNotificationRecorder recorder;
    transport.addListener(recorder);

    const double target_seconds = std::min(0.2, duration_seconds * 0.5);
    transport.seek(core::TimePosition{target_seconds});

    CHECK(recorder.transport_state_call_count == 0);
    CHECK(transport.position() == core::TimePosition{target_seconds});

    transport.removeListener(recorder);
}

// Verifies that a failed edit leaves listener counts unchanged when transport-visible state does
// not change.
TEST_CASE(
    "Failed engine edit does not emit callbacks when state is unchanged",
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

    CHECK_FALSE(applied.has_value());
    CHECK(recorder.transport_state_call_count == 0);
    CHECK(transport.state() == TransportState{});

    transport.removeListener(recorder);
}

} // namespace rock_hero::audio
