#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
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

// Loads the fixture for a specific project track through the public edit port.
[[nodiscard]] std::optional<core::AudioClip> loadFixtureAudioClipForTrack(
    IEdit& edit, core::TrackId track_id, core::TimePosition position = core::TimePosition{})
{
    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));

    return edit.loadAudioAsset(track_id, audio_asset, position);
}

// Loads the fixture through the public edit port and returns the accepted clip.
[[nodiscard]] std::optional<core::AudioClip> loadFixtureAudioClip(
    IEdit& edit, core::TimePosition position = core::TimePosition{})
{
    return loadFixtureAudioClipForTrack(edit, core::TrackId{1}, position);
}

// Loads the fixture and fails the current test if the concrete backend rejects it.
[[nodiscard]] core::AudioClip requireLoadedFixtureAudioClip(
    IEdit& edit, core::TimePosition position = core::TimePosition{})
{
    auto audio_clip = loadFixtureAudioClip(edit, position);
    REQUIRE(audio_clip.has_value());
    return audio_clip.value_or(core::AudioClip{});
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

// Verifies asset loading returns a framework-free clip while leaving transport stopped.
TEST_CASE("Engine edit loads audio asset synchronously", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport const& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));

    const auto audio_clip =
        edit.loadAudioAsset(core::TrackId{1}, audio_asset, core::TimePosition{});

    const core::AudioClip accepted_clip = audio_clip.value_or(core::AudioClip{});
    REQUIRE(audio_clip.has_value());
    CHECK(accepted_clip.id == core::AudioClipId{});
    CHECK(accepted_clip.asset == audio_asset);
    CHECK(accepted_clip.asset_duration.seconds > 0.0);
    CHECK(accepted_clip.source_range.duration() == accepted_clip.asset_duration);
    CHECK(accepted_clip.position == core::TimePosition{});
    const auto current_state = transport.state();
    CHECK_FALSE(current_state.playing);
    CHECK(transport.position() == core::TimePosition{});
}

// Verifies unsupported nonzero placement is rejected by the current single-file backend.
TEST_CASE("Engine edit rejects nonzero clip placement", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport const& transport = engine;

    const auto audio_clip = loadFixtureAudioClip(edit, core::TimePosition{1.0});

    CHECK_FALSE(audio_clip.has_value());

    const auto current_state = transport.state();
    CHECK_FALSE(current_state.playing);
    CHECK(transport.position() == core::TimePosition{});
}

// Verifies the single-track adapter rejects invalid project track identities.
TEST_CASE("Engine edit rejects invalid track ids", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;

    const auto audio_clip = loadFixtureAudioClipForTrack(edit, core::TrackId{});

    CHECK_FALSE(audio_clip.has_value());
}

// Verifies the single Tracktion track can replace audio for its bound project track id.
TEST_CASE("Engine edit replaces audio for the bound track id", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;

    const auto first_clip = loadFixtureAudioClipForTrack(edit, core::TrackId{1});
    const auto replacement_clip = loadFixtureAudioClipForTrack(edit, core::TrackId{1});

    CHECK(first_clip.has_value());
    CHECK(replacement_clip.has_value());
}

// Verifies the one-track adapter does not pretend to support independent session tracks.
TEST_CASE("Engine edit rejects a different track id after binding", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;

    const auto first_clip = loadFixtureAudioClipForTrack(edit, core::TrackId{1});
    REQUIRE(first_clip.has_value());

    const auto second_track_clip = loadFixtureAudioClipForTrack(edit, core::TrackId{2});

    CHECK_FALSE(second_track_clip.has_value());
}

// Verifies a failed edit request leaves the previous transport state visible through state().
TEST_CASE("Failed engine edit preserves existing transport state", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport const& transport = engine;

    [[maybe_unused]] const auto audio_clip = requireLoadedFixtureAudioClip(edit);

    const auto loaded_state = transport.state();
    const core::AudioAsset missing_asset{
        fixtureAudioPath().parent_path() / "missing.wav",
    };

    const auto loaded_clip =
        edit.loadAudioAsset(core::TrackId{1}, missing_asset, core::TimePosition{});

    CHECK_FALSE(loaded_clip.has_value());
    CHECK(transport.state() == loaded_state);
}

// Verifies direct ITransport seek commands update the live position without mutating state.
TEST_CASE("Engine seek updates live transport position", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const auto audio_clip = requireLoadedFixtureAudioClip(edit);

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

    const auto audio_clip = requireLoadedFixtureAudioClip(edit);

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

    const auto audio_clip = loadFixtureAudioClip(edit);

    const core::AudioClip accepted_clip = audio_clip.value_or(core::AudioClip{});
    REQUIRE(audio_clip.has_value());
    CHECK(accepted_clip.timelineRange().duration().seconds > 0.0);
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

    const auto audio_clip = requireLoadedFixtureAudioClip(edit);

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

    const auto loaded_clip =
        edit.loadAudioAsset(core::TrackId{1}, missing_asset, core::TimePosition{});

    CHECK_FALSE(loaded_clip.has_value());
    CHECK(recorder.transport_state_call_count == 0);
    CHECK(transport.state() == TransportState{});

    transport.removeListener(recorder);
}

} // namespace rock_hero::audio
