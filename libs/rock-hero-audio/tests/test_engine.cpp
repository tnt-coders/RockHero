#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/audio/engine.h>
#include <rock_hero/audio/i_thumbnail.h>

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

// Provisions the backend track mapping required before clip edits can target a project track id.
[[nodiscard]] bool provisionFixtureTrack(IEdit& edit, core::TrackId track_id = core::TrackId{1})
{
    return edit.provisionTrack(track_id, "Full Mix").has_value();
}

// Provisions the fixture clip for a project track that has already been mapped in the backend.
[[nodiscard]] std::optional<core::AudioClipSpec> provisionFixtureAudioClipForMappedTrack(
    IEdit& edit, core::TrackId track_id, core::AudioClipId audio_clip_id = core::AudioClipId{1},
    core::TimePosition position = core::TimePosition{})
{
    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));

    return edit.provisionAudioClip(track_id, audio_clip_id, audio_asset, position);
}

// Provisions the backend track mapping and fixture clip for a specific project track.
[[nodiscard]] std::optional<core::AudioClipSpec> provisionFixtureAudioClipForTrack(
    IEdit& edit, core::TrackId track_id, core::AudioClipId audio_clip_id = core::AudioClipId{1},
    core::TimePosition position = core::TimePosition{})
{
    REQUIRE(provisionFixtureTrack(edit, track_id));
    return provisionFixtureAudioClipForMappedTrack(edit, track_id, audio_clip_id, position);
}

// Provisions the fixture through the public edit port and returns the accepted clip.
[[nodiscard]] std::optional<core::AudioClipSpec> provisionFixtureAudioClip(
    IEdit& edit, core::TimePosition position = core::TimePosition{})
{
    return provisionFixtureAudioClipForTrack(
        edit, core::TrackId{1}, core::AudioClipId{1}, position);
}

// Provisions the fixture and fails the current test if the concrete backend rejects it.
[[nodiscard]] core::AudioClipSpec requireProvisionedFixtureAudioClip(
    IEdit& edit, core::TimePosition position = core::TimePosition{})
{
    const auto audio_clip = provisionFixtureAudioClip(edit, position);
    REQUIRE(audio_clip.has_value());
    return audio_clip.value_or(core::AudioClipSpec{});
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

// Verifies the concrete engine factory returns a usable Tracktion-backed thumbnail adapter.
TEST_CASE("Engine thumbnail factory creates an adapter", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    juce::Component owner;

    const auto thumbnail = engine.createThumbnail(owner);

    REQUIRE(thumbnail != nullptr);
    CHECK(thumbnail->getLength() == 0.0);
}

// Verifies a real thumbnail adapter can load fixture metadata and render into JUCE graphics.
TEST_CASE("Engine thumbnail loads and draws fixture audio", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    juce::Component owner;
    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    auto thumbnail = engine.createThumbnail(owner);
    REQUIRE(thumbnail != nullptr);

    thumbnail->setSource(audio_asset);

    CHECK(thumbnail->getLength() > 0.0);
    CHECK(thumbnail->getProxyProgress() >= 0.0f);
    CHECK(thumbnail->getProxyProgress() <= 1.0f);

    const juce::Image image(juce::Image::RGB, 128, 48, true);
    juce::Graphics graphics{image};
    CHECK_NOTHROW(thumbnail->drawChannels(graphics, image.getBounds(), 1.0f));
}

// Verifies missing assets do not leave stale duration metadata in the thumbnail adapter.
TEST_CASE("Engine thumbnail clears length for missing assets", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    juce::Component owner;
    auto thumbnail = engine.createThumbnail(owner);
    REQUIRE(thumbnail != nullptr);
    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    thumbnail->setSource(audio_asset);
    REQUIRE(thumbnail->getLength() > 0.0);

    const core::AudioAsset missing_asset{
        fixtureAudioPath().parent_path() / "missing-thumbnail.wav",
    };
    thumbnail->setSource(missing_asset);

    CHECK(thumbnail->getLength() == 0.0);
    CHECK(thumbnail->getProxyProgress() >= 0.0f);
    CHECK(thumbnail->getProxyProgress() <= 1.0f);
}

// Verifies clip provisioning returns a framework-free spec while leaving transport stopped.
TEST_CASE("Engine edit provisions audio clip synchronously", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    const auto track = edit.provisionTrack(core::TrackId{1}, "Full Mix");
    REQUIRE(track.has_value());
    CHECK(track == std::optional{core::TrackSpec{.name = "Full Mix"}});

    TransportNotificationRecorder recorder;
    transport.addListener(recorder);

    const auto audio_clip = edit.provisionAudioClip(
        core::TrackId{1}, core::AudioClipId{7}, audio_asset, core::TimePosition{});

    transport.removeListener(recorder);

    const core::AudioClipSpec accepted_clip = audio_clip.value_or(core::AudioClipSpec{});
    REQUIRE(audio_clip.has_value());
    CHECK(accepted_clip.asset == audio_asset);
    CHECK(accepted_clip.asset_duration.seconds > 0.0);
    CHECK(accepted_clip.source_range.duration() == accepted_clip.asset_duration);
    CHECK(accepted_clip.position == core::TimePosition{});
    const auto current_state = transport.state();
    CHECK_FALSE(current_state.playing);
    CHECK(transport.position() == core::TimePosition{});
    CHECK(recorder.transport_state_call_count == 0);
    CHECK(recorder.last_transport_state == TransportState{});
}

// Verifies clip provisioning cannot target a track id the backend has not mapped.
TEST_CASE("Engine edit rejects clips for unmapped track ids", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;

    const auto audio_clip =
        provisionFixtureAudioClipForMappedTrack(edit, core::TrackId{1}, core::AudioClipId{1});

    CHECK_FALSE(audio_clip.has_value());
}

// Verifies unsupported nonzero placement is rejected by the current single-file backend.
TEST_CASE("Engine edit rejects nonzero clip placement", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport const& transport = engine;

    const auto audio_clip = provisionFixtureAudioClipForTrack(
        edit, core::TrackId{1}, core::AudioClipId{1}, core::TimePosition{1.0});

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

    const auto track_provisioned = edit.provisionTrack(core::TrackId{}, "Invalid");
    const auto audio_clip = provisionFixtureAudioClipForMappedTrack(edit, core::TrackId{});

    CHECK_FALSE(track_provisioned.has_value());
    CHECK_FALSE(audio_clip.has_value());
}

// Verifies the concrete adapter rejects invalid session-allocated clip identities.
TEST_CASE("Engine edit rejects invalid audio clip ids", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;

    const auto audio_clip =
        provisionFixtureAudioClipForTrack(edit, core::TrackId{1}, core::AudioClipId{});

    CHECK_FALSE(audio_clip.has_value());
}

// Verifies the single Tracktion track binds to one caller-chosen project track id.
// TODO: Remove this test when Engine supports loading multiple project tracks independently.
TEST_CASE("Engine edit binds one project track id at a time", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;

    const auto first_track_provisioned = edit.provisionTrack(core::TrackId{2}, "Full Mix");
    REQUIRE(first_track_provisioned.has_value());
    CHECK(first_track_provisioned == std::optional{core::TrackSpec{.name = "Full Mix"}});

    const auto other_track_provisioned = edit.provisionTrack(core::TrackId{1}, "Stem");
    const auto first_track_clip =
        provisionFixtureAudioClipForMappedTrack(edit, core::TrackId{2}, core::AudioClipId{1});
    const auto other_track_clip =
        provisionFixtureAudioClipForMappedTrack(edit, core::TrackId{1}, core::AudioClipId{2});

    CHECK_FALSE(other_track_provisioned.has_value());
    CHECK(first_track_clip.has_value());
    CHECK_FALSE(other_track_clip.has_value());
}

// Verifies the single Tracktion track can replace audio for its bound project track id.
TEST_CASE("Engine edit replaces audio for the bound track id", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;

    REQUIRE(provisionFixtureTrack(edit, core::TrackId{1}));
    const auto first_clip =
        provisionFixtureAudioClipForMappedTrack(edit, core::TrackId{1}, core::AudioClipId{1});
    const auto replacement_clip =
        provisionFixtureAudioClipForMappedTrack(edit, core::TrackId{1}, core::AudioClipId{2});

    CHECK(first_clip.has_value());
    CHECK(replacement_clip.has_value());
}

// Verifies a failed edit request leaves the previous transport state visible through state().
TEST_CASE("Failed engine edit preserves existing transport state", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    [[maybe_unused]] const auto audio_clip = requireProvisionedFixtureAudioClip(edit);

    const auto loaded_state = transport.state();
    const core::AudioAsset missing_asset{
        fixtureAudioPath().parent_path() / "missing.wav",
    };
    TransportNotificationRecorder recorder;
    transport.addListener(recorder);

    const auto provisioned_clip = edit.provisionAudioClip(
        core::TrackId{1}, core::AudioClipId{2}, missing_asset, core::TimePosition{});

    transport.removeListener(recorder);

    CHECK_FALSE(provisioned_clip.has_value());
    CHECK(transport.state() == loaded_state);
    CHECK(recorder.transport_state_call_count == 0);
    CHECK(recorder.last_transport_state == TransportState{});
}

// Verifies direct ITransport seek commands update the live position without mutating state.
TEST_CASE("Engine seek updates live transport position", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const auto audio_clip = requireProvisionedFixtureAudioClip(edit);

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

    const auto audio_clip = requireProvisionedFixtureAudioClip(edit);

    const double duration_seconds = audio_clip.timelineRange().duration().seconds;
    REQUIRE(duration_seconds > 0.0);

    const double target_seconds = std::min(0.3, duration_seconds * 0.5);
    transport.seek(core::TimePosition{target_seconds});

    CHECK(read_only_transport.position() == core::TimePosition{target_seconds});
}

// Verifies position-only seeking remains invisible to coarse transport state listeners.
TEST_CASE("Engine seek does not emit state callbacks", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const auto audio_clip = requireProvisionedFixtureAudioClip(edit);

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

} // namespace rock_hero::audio
