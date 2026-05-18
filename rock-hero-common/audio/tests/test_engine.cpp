#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <expected>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/audio/engine.h>
#include <rock_hero/common/audio/i_audio.h>
#include <rock_hero/common/audio/i_audio_device_configuration.h>
#include <rock_hero/common/audio/i_live_rig.h>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <rock_hero/common/audio/i_thumbnail.h>

namespace rock_hero::common::audio
{

namespace
{

// Verifies at compile time that the concrete adapter is usable through its audio port surfaces.
static_assert(std::derived_from<Engine, ITransport>);
static_assert(std::derived_from<Engine, IAudio>);
static_assert(std::derived_from<Engine, IAudioDeviceConfiguration>);
static_assert(std::derived_from<Engine, IPluginHost>);
static_assert(std::derived_from<Engine, ILiveRig>);
static_assert(std::derived_from<Engine, IThumbnailFactory>);

// Returns the build-tree copy of the audio fixture that the real Engine loads in tests.
[[nodiscard]] std::filesystem::path fixtureAudioPath()
{
    return std::filesystem::path{TEST_DATA_DIR} / "drum_loop.wav";
}

// Wraps the real fixture path in the framework-free asset type used by IAudio.
[[nodiscard]] common::core::AudioAsset fixtureAudioAsset()
{
    return common::core::AudioAsset{fixtureAudioPath()};
}

// Converts an accepted duration into the timeline range used by rendering and seeks.
[[nodiscard]] common::core::TimeRange timelineRangeForDuration(common::core::TimeDuration duration)
{
    return common::core::TimeRange{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{duration.seconds},
    };
}

// Builds a minimal song that references the real fixture audio.
[[nodiscard]] common::core::Song makeFixtureSong()
{
    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));

    common::core::Song song;
    song.arrangements.push_back(
        common::core::Arrangement{
            .id = "lead",
            .part = common::core::Part::Lead,
            .difficulty = common::core::DifficultyRating{},
            .audio_asset = audio_asset,
            .audio_duration = common::core::TimeDuration{},
            .tone_document_ref = {},
            .note_events = {},
        });
    return song;
}

// Prepares and activates the fixture arrangement, failing the test if either step is rejected.
[[nodiscard]] common::core::TimeDuration requireLoadedFixtureAudio(IAudio& audio)
{
    auto song = makeFixtureSong();
    REQUIRE(audio.prepareSong(song));
    REQUIRE(song.arrangements.size() == 1);
    const common::core::Arrangement& arrangement = song.arrangements.front();
    REQUIRE(audio.setActiveArrangement(arrangement));
    return arrangement.audio_duration;
}

// Records callback activity from the project-owned transport listener surface.
class TransportNotificationRecorder final : public ITransport::Listener
{
public:
    // Captures the latest coarse transport state delivered by Engine.
    void onTransportStateChanged(TransportState state) override
    {
        last_transport_state = state;
        ++transport_state_call_count;
    }

    // Latest coarse transport state delivered to this listener.
    TransportState last_transport_state{};

    // Number of coarse transport callbacks received.
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

// Creates a syntactically valid plugin-load probe command without starting a helper process.
[[nodiscard]] std::string makePluginLoadProbeCommandLine()
{
    const juce::String payload{"C:\\Plugins\\Amp.vst3\nplugin-id\nC:\\Temp\\probe.txt"};
    return ("--RockHeroPluginLoadProbe:" + juce::Base64::toBase64(payload)).toStdString();
}

} // namespace

// Verifies normal app launches are not consumed as scan or probe child processes.
TEST_CASE("Engine child-process helpers ignore normal startup", "[audio][engine]")
{
    CHECK_FALSE(Engine::isPluginScanChildProcessCommandLine("--normal"));
    CHECK_FALSE(Engine::startPluginScanChildProcess("--normal"));
    CHECK_FALSE(Engine::isPluginLoadProbeChildProcessCommandLine("--normal"));
    CHECK_FALSE(Engine::startPluginLoadProbeChildProcess("--normal"));
}

// Verifies plugin helper command lines are recognized before normal editor startup.
TEST_CASE("Engine recognizes plugin helper command lines", "[audio][engine]")
{
    CHECK(Engine::isPluginScanChildProcessCommandLine("--PluginScan:abc"));
    CHECK(Engine::isPluginLoadProbeChildProcessCommandLine(makePluginLoadProbeCommandLine()));
}

// Verifies the concrete engine starts with empty state and a zero current position.
TEST_CASE("Engine starts with empty transport state", "[audio][engine][integration]")
{
    const EngineTestHarness harness;
    const Engine& engine = harness.engine;
    ITransport const& transport = engine;

    const auto current_state = transport.state();

    CHECK_FALSE(current_state.playing);
    CHECK(transport.position() == common::core::TimePosition{});
}

// Verifies the concrete engine factory returns a usable Tracktion-backed thumbnail adapter.
TEST_CASE("Engine thumbnail factory creates an adapter", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    juce::Component owner;

    const auto thumbnail = engine.createThumbnail(owner);

    REQUIRE(thumbnail != nullptr);
    CHECK_FALSE(thumbnail->hasSource());
}

// Verifies a real thumbnail adapter can load fixture metadata and render into JUCE graphics.
TEST_CASE("Engine thumbnail loads and draws fixture audio", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    juce::Component owner;
    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    const common::core::TimeDuration audio_duration = requireLoadedFixtureAudio(engine);
    auto thumbnail = engine.createThumbnail(owner);
    REQUIRE(thumbnail != nullptr);

    thumbnail->setSource(audio_asset);

    CHECK(thumbnail->hasSource());
    CHECK(thumbnail->getProxyProgress() >= 0.0f);
    CHECK(thumbnail->getProxyProgress() <= 1.0f);

    const juce::Image image(juce::Image::RGB, 128, 48, true);
    juce::Graphics graphics{image};
    CHECK(thumbnail->drawChannels(
        graphics, image.getBounds(), timelineRangeForDuration(audio_duration), 1.0f));
}

// Verifies invalid visible ranges fail before the adapter asks Tracktion to draw them.
TEST_CASE("Engine thumbnail rejects invalid visible ranges", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    juce::Component owner;
    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    auto thumbnail = engine.createThumbnail(owner);
    REQUIRE(thumbnail != nullptr);
    thumbnail->setSource(audio_asset);
    REQUIRE(thumbnail->hasSource());

    const juce::Image image(juce::Image::RGB, 128, 48, true);
    juce::Graphics graphics{image};
    CHECK_FALSE(thumbnail->drawChannels(
        graphics,
        image.getBounds(),
        common::core::TimeRange{
            .start = common::core::TimePosition{4.0},
            .end = common::core::TimePosition{2.0},
        },
        1.0f));
}

// Verifies missing assets do not leave stale source-readiness state in the thumbnail adapter.
TEST_CASE("Engine thumbnail clears source for missing assets", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    juce::Component owner;
    auto thumbnail = engine.createThumbnail(owner);
    REQUIRE(thumbnail != nullptr);
    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    thumbnail->setSource(audio_asset);
    REQUIRE(thumbnail->hasSource());

    const common::core::AudioAsset missing_asset{
        fixtureAudioPath().parent_path() / "missing-thumbnail.wav",
    };
    thumbnail->setSource(missing_asset);

    CHECK_FALSE(thumbnail->hasSource());
    CHECK(thumbnail->getProxyProgress() >= 0.0f);
    CHECK(thumbnail->getProxyProgress() <= 1.0f);
}

// Verifies activating prepared audio leaves transport stopped.
TEST_CASE("Engine audio port sets active arrangement", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IAudio& audio = engine;
    ITransport& transport = engine;

    auto song = makeFixtureSong();
    REQUIRE(audio.prepareSong(song));
    REQUIRE(song.arrangements.size() == 1);

    TransportNotificationRecorder recorder;
    transport.addListener(recorder);

    const bool active_set = audio.setActiveArrangement(song.arrangements.front());

    transport.removeListener(recorder);

    CHECK(active_set);
    CHECK(song.arrangements.front().audio_duration.seconds > 0.0);
    const auto current_state = transport.state();
    CHECK_FALSE(current_state.playing);
    CHECK(transport.position() == common::core::TimePosition{});
    CHECK(recorder.transport_state_call_count == 0);
    CHECK(recorder.last_transport_state == TransportState{});
}

// Verifies song preparation fills fixture audio duration without mutating transport state.
TEST_CASE("Engine audio port prepares song", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IAudio& audio = harness.engine;
    const ITransport& transport = harness.engine;
    auto song = makeFixtureSong();

    const bool prepared = audio.prepareSong(song);

    CHECK(prepared);
    REQUIRE(song.arrangements.size() == 1);
    CHECK(song.arrangements.front().audio_duration.seconds > 0.0);
    CHECK(transport.state() == TransportState{});
    CHECK(transport.position() == common::core::TimePosition{});
}

// Verifies song preparation rejects missing assets without loading fallback media.
TEST_CASE("Engine audio port rejects missing files", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IAudio& audio = harness.engine;
    auto song = makeFixtureSong();
    song.arrangements.front().audio_asset =
        common::core::AudioAsset{fixtureAudioPath().parent_path() / "missing-probe.wav"};

    const bool prepared = audio.prepareSong(song);

    CHECK_FALSE(prepared);
}

// Verifies plugin scanning rejects missing plugin paths without touching Tracktion graph state.
TEST_CASE("Engine plugin host rejects missing plugin files", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IPluginHost& plugin_host = harness.engine;

    const auto candidates =
        plugin_host.scanPluginFile(fixtureAudioPath().parent_path() / "missing.vst3");

    REQUIRE_FALSE(candidates.has_value());
    CHECK(candidates.error().code == PluginHostErrorCode::MissingPluginFile);
}

// Verifies plugin insertion reports unknown candidate IDs as a typed boundary failure.
TEST_CASE("Engine plugin host rejects unknown plugin IDs", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IPluginHost& plugin_host = harness.engine;
    const ITransport& transport = harness.engine;

    const auto handle = plugin_host.addPlugin("missing-plugin-id");

    REQUIRE_FALSE(handle.has_value());
    CHECK(handle.error().code == PluginHostErrorCode::PluginNotFound);
    CHECK(transport.state() == TransportState{});
    CHECK(transport.position() == common::core::TimePosition{});
}

// Verifies plugin removal reports unknown instance IDs as a typed boundary failure.
TEST_CASE("Engine plugin host rejects unknown plugin instances", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IPluginHost& plugin_host = harness.engine;
    const ITransport& transport = harness.engine;

    const auto result = plugin_host.removePlugin("missing-instance-id");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == PluginHostErrorCode::PluginInstanceNotFound);
    CHECK(transport.state() == TransportState{});
    CHECK(transport.position() == common::core::TimePosition{});
}

// Verifies clearing an empty live rig uses the same message-thread adapter path.
TEST_CASE("Engine live rig clears empty chain", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ILiveRig& live_rig = harness.engine;

    const auto result = live_rig.clearRig();

    CHECK(result.has_value());
}

// Verifies an empty tone document reference is treated as a request to clear the chain.
TEST_CASE("Engine live rig loads empty tone", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ILiveRig& live_rig = harness.engine;

    std::optional<std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>>
        result;
    live_rig.loadRig(
        common::audio::LiveRigLoadRequest{}, [&result](auto value) { result = std::move(value); });

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    CHECK((*result)->plugins.empty());
}

// Verifies the single Tracktion arrangement track can replace its loaded audio.
TEST_CASE("Engine audio port replaces arrangement audio", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IAudio& audio = engine;
    auto song = makeFixtureSong();
    REQUIRE(audio.prepareSong(song));
    REQUIRE(song.arrangements.size() == 1);
    const common::core::Arrangement& arrangement = song.arrangements.front();

    const bool first_audio_set = audio.setActiveArrangement(arrangement);
    const bool replacement_audio_set = audio.setActiveArrangement(arrangement);

    CHECK(first_audio_set);
    CHECK(replacement_audio_set);
}

// Verifies a failed activation leaves the previous transport state visible through state().
TEST_CASE(
    "Failed engine audio activation preserves transport state", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IAudio& audio = engine;
    ITransport& transport = engine;

    [[maybe_unused]] const auto audio_duration = requireLoadedFixtureAudio(audio);

    const auto loaded_state = transport.state();
    const common::core::AudioAsset missing_asset{
        fixtureAudioPath().parent_path() / "missing.wav",
    };
    TransportNotificationRecorder recorder;
    transport.addListener(recorder);

    const bool active_set = audio.setActiveArrangement(
        common::core::Arrangement{
            .id = "missing",
            .part = common::core::Part::Lead,
            .difficulty = common::core::DifficultyRating{},
            .audio_asset = missing_asset,
            .audio_duration = common::core::TimeDuration{1.0},
            .tone_document_ref = {},
            .note_events = {},
        });

    transport.removeListener(recorder);

    CHECK_FALSE(active_set);
    CHECK(transport.state() == loaded_state);
    CHECK(recorder.transport_state_call_count == 0);
    CHECK(recorder.last_transport_state == TransportState{});
}

// Verifies direct ITransport seek commands update the current position without mutating state.
TEST_CASE("Engine seek updates current transport position", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IAudio& audio = engine;
    ITransport& transport = engine;

    const common::core::TimeDuration audio_duration = requireLoadedFixtureAudio(audio);

    const auto loaded_state = transport.state();
    const double duration_seconds = audio_duration.seconds;
    REQUIRE(duration_seconds > 0.0);

    const double target_seconds = std::min(0.25, duration_seconds * 0.5);
    transport.seek(common::core::TimePosition{target_seconds});

    CHECK(transport.state() == loaded_state);
    CHECK(transport.position() == common::core::TimePosition{target_seconds});
}

// Verifies the cursor-position read reports the post-seek position from the concrete adapter.
TEST_CASE("Engine position reflects public transport seeks", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IAudio& audio = engine;
    ITransport& transport = engine;
    const ITransport& read_only_transport = engine;

    const common::core::TimeDuration audio_duration = requireLoadedFixtureAudio(audio);

    const double duration_seconds = audio_duration.seconds;
    REQUIRE(duration_seconds > 0.0);

    const double target_seconds = std::min(0.3, duration_seconds * 0.5);
    transport.seek(common::core::TimePosition{target_seconds});

    CHECK(read_only_transport.position() == common::core::TimePosition{target_seconds});
}

// Verifies position-only seeking remains invisible to coarse transport state listeners.
TEST_CASE("Engine seek does not emit state callbacks", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IAudio& audio = engine;
    ITransport& transport = engine;

    const common::core::TimeDuration audio_duration = requireLoadedFixtureAudio(audio);

    const double duration_seconds = audio_duration.seconds;
    REQUIRE(duration_seconds > 0.0);

    TransportNotificationRecorder recorder;
    transport.addListener(recorder);

    const double target_seconds = std::min(0.2, duration_seconds * 0.5);
    transport.seek(common::core::TimePosition{target_seconds});

    CHECK(recorder.transport_state_call_count == 0);
    CHECK(transport.position() == common::core::TimePosition{target_seconds});

    transport.removeListener(recorder);
}

} // namespace rock_hero::common::audio
