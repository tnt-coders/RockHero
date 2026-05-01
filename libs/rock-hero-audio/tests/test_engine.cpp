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

// Builds the audio clip that current single-file playback accepts for a fixture asset.
[[nodiscard]] core::AudioClip fixtureAudioClip(IEdit& edit)
{
    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));

    const auto duration = edit.readAudioAssetDuration(audio_asset);
    REQUIRE(duration.has_value());
    REQUIRE(duration->seconds > 0.0);

    return core::AudioClip{
        .id = core::AudioClipId{},
        .asset = audio_asset,
        .asset_duration = *duration,
        .source_range =
            core::TimeRange{
                .start = core::TimePosition{},
                .end = core::TimePosition{duration->seconds},
            },
        .position = core::TimePosition{},
    };
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

// Verifies duration probing reads metadata without mutating transport state.
TEST_CASE("Engine edit probes audio duration synchronously", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport const& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));

    const auto duration = edit.readAudioAssetDuration(audio_asset);

    REQUIRE(duration.has_value());
    CHECK(duration->seconds > 0.0);
    const auto current_state = transport.state();
    CHECK_FALSE(current_state.playing);
    CHECK(transport.position() == core::TimePosition{});
}

// Verifies edit-driven clip replacement accepts the requested source range and placement.
TEST_CASE("Engine edit accepts requested clip synchronously", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport const& transport = engine;

    const auto audio_clip = fixtureAudioClip(edit);

    const bool applied = edit.setTrackAudioClip(core::TrackId{1}, audio_clip);

    CHECK(applied);

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

    const auto audio_clip = fixtureAudioClip(edit);
    REQUIRE(edit.setTrackAudioClip(core::TrackId{1}, audio_clip));

    const auto loaded_state = transport.state();
    const core::AudioClip missing_clip{
        .id = core::AudioClipId{},
        .asset = core::AudioAsset{audio_clip.asset.path.parent_path() / "missing.wav"},
        .asset_duration = audio_clip.asset_duration,
        .source_range = audio_clip.source_range,
        .position = audio_clip.position,
    };

    const bool applied = edit.setTrackAudioClip(core::TrackId{1}, missing_clip);

    CHECK_FALSE(applied);
    CHECK(transport.state() == loaded_state);
}

// Verifies direct ITransport seek commands update the live position without mutating state.
TEST_CASE("Engine seek updates live transport position", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const auto audio_clip = fixtureAudioClip(edit);
    REQUIRE(edit.setTrackAudioClip(core::TrackId{1}, audio_clip));

    const auto loaded_state = transport.state();
    const double duration_seconds = audio_clip.timelineRange().duration().seconds;
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

    const auto audio_clip = fixtureAudioClip(edit);
    REQUIRE(edit.setTrackAudioClip(core::TrackId{1}, audio_clip));

    const double duration_seconds = audio_clip.timelineRange().duration().seconds;
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

    const auto audio_clip = fixtureAudioClip(edit);

    const bool applied = edit.setTrackAudioClip(core::TrackId{1}, audio_clip);

    REQUIRE(applied);
    CHECK(audio_clip.timelineRange().duration().seconds > 0.0);
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

    const auto audio_clip = fixtureAudioClip(edit);
    REQUIRE(edit.setTrackAudioClip(core::TrackId{1}, audio_clip));

    const double duration_seconds = audio_clip.timelineRange().duration().seconds;
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
    const core::AudioClip missing_clip{
        .id = core::AudioClipId{},
        .asset = missing_asset,
        .asset_duration = core::TimeDuration{4.0},
        .source_range =
            core::TimeRange{
                .start = core::TimePosition{},
                .end = core::TimePosition{4.0},
            },
        .position = core::TimePosition{},
    };

    const bool applied = edit.setTrackAudioClip(core::TrackId{1}, missing_clip);

    CHECK_FALSE(applied);
    CHECK(recorder.transport_state_call_count == 0);
    CHECK(transport.state() == TransportState{});

    transport.removeListener(recorder);
}

} // namespace rock_hero::audio
