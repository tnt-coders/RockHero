#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/engine.h>
#include <rock_hero/common/audio/i_audio_device_configuration.h>
#include <rock_hero/common/audio/i_live_input.h>
#include <rock_hero/common/audio/i_live_rig.h>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <rock_hero/common/audio/i_song_audio.h>
#include <rock_hero/common/audio/i_thumbnail.h>
#include <rock_hero/common/core/package_id.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

constexpr const char* g_arrangement_id = "4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4";
constexpr const char* g_rejected_audio_type_name = "Rejected Audio";
constexpr const char* g_rejected_input_name = "Rejected Input";
constexpr const char* g_rejected_output_name = "Rejected Output";

// Verifies at compile time that the concrete adapter is usable through its audio port surfaces.
static_assert(std::derived_from<Engine, ITransport>);
static_assert(std::derived_from<Engine, ISongAudio>);
static_assert(std::derived_from<Engine, IAudioDeviceConfiguration>);
static_assert(std::derived_from<Engine, IAudioMeterSource>);
static_assert(std::derived_from<Engine, IPluginHost>);
static_assert(std::derived_from<Engine, ILiveInput>);
static_assert(std::derived_from<Engine, ILiveRig>);
static_assert(std::derived_from<Engine, IThumbnailFactory>);

// Returns the build-tree copy of the audio fixture that the real Engine loads in tests.
[[nodiscard]] std::filesystem::path fixtureAudioPath()
{
    return std::filesystem::path{TEST_DATA_DIR} / "drum_loop.wav";
}

// Wraps the real fixture path in the framework-free asset type used by ISongAudio.
[[nodiscard]] common::core::AudioAsset fixtureAudioAsset()
{
    return common::core::AudioAsset{.path = fixtureAudioPath(), .normalization = std::nullopt};
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
        });
    return song;
}

// Prepares and activates the fixture arrangement, failing the test if either step is rejected.
[[nodiscard]] common::core::TimeDuration requireLoadedFixtureAudio(ISongAudio& audio)
{
    auto song = makeFixtureSong();
    REQUIRE(audio.prepareSong(song).has_value());
    REQUIRE(song.arrangements.size() == 1);
    const common::core::Arrangement& arrangement = song.arrangements.front();
    REQUIRE(audio.setActiveArrangement(arrangement).has_value());
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

// Device type whose named route exists but cannot create a device, forcing JUCE restore failure.
class RejectingAudioDeviceType final : public juce::AudioIODeviceType
{
public:
    RejectingAudioDeviceType()
        : juce::AudioIODeviceType(g_rejected_audio_type_name)
    {}

    // No scan work is needed because the fake returns a fixed route list.
    void scanForDevices() override
    {}

    // Exposes one input and one output name so restore reaches device creation.
    [[nodiscard]] juce::StringArray getDeviceNames(bool want_input_names) const override
    {
        return want_input_names ? juce::StringArray{g_rejected_input_name}
                                : juce::StringArray{g_rejected_output_name};
    }

    // The fixed route list has only one default entry for both directions.
    [[nodiscard]] int getDefaultDeviceIndex(bool /*for_input*/) const override
    {
        return 0;
    }

    // The restore test does not need reverse lookup from an opened device.
    [[nodiscard]] int getIndexOfDevice(
        juce::AudioIODevice* /*device*/, bool /*want_input_names*/) const override
    {
        return -1;
    }

    // Separate input/output names make the serialized setup explicit.
    [[nodiscard]] bool hasSeparateInputsAndOutputs() const override
    {
        return true;
    }

    // Returning null forces AudioDeviceManager to surface a backend restore failure.
    [[nodiscard]] juce::AudioIODevice* createDevice(
        const juce::String& /*output_device_name*/,
        const juce::String& /*input_device_name*/) override
    {
        return nullptr;
    }
};

// Replaces platform audio types so serialized restore cannot fall back to a real device.
void installOnlyRejectingAudioDeviceType(juce::AudioDeviceManager& manager)
{
    const int original_type_count = manager.getAvailableDeviceTypes().size();
    for (int index = 0; index < original_type_count; ++index)
    {
        juce::AudioIODeviceType* const type = manager.getAvailableDeviceTypes().getFirst();
        if (type != nullptr)
        {
            manager.removeAudioDeviceType(type);
        }
    }

    manager.addAudioDeviceType(std::make_unique<RejectingAudioDeviceType>());
}

// Owns a temporary song workspace for live-rig file persistence tests.
class TemporarySongDirectory final
{
public:
    TemporarySongDirectory()
        : m_path(
              std::filesystem::temp_directory_path() /
              ("rock-hero-engine-test-" + common::core::generatePackageId()))
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~TemporarySongDirectory() noexcept
    {
        try
        {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }
        catch (...)
        {
            m_path.clear();
        }
    }

    TemporarySongDirectory(const TemporarySongDirectory&) = delete;
    TemporarySongDirectory& operator=(const TemporarySongDirectory&) = delete;
    TemporarySongDirectory(TemporarySongDirectory&&) = delete;
    TemporarySongDirectory& operator=(TemporarySongDirectory&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

// Extracts the tone ID segment from tones/<tone-id>/tone.json.
[[nodiscard]] std::string toneIdFromRef(const std::string& tone_document_ref)
{
    return std::filesystem::path{tone_document_ref}.parent_path().filename().generic_string();
}

// Encodes a small Tracktion plugin-state XML fixture into the port's opaque memento shape.
[[nodiscard]] PluginInstanceState pluginStateFromXml(std::string_view xml)
{
    std::vector<std::byte> bytes;
    bytes.reserve(xml.size());
    for (const char character : xml)
    {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }

    return PluginInstanceState{.opaque_data = std::move(bytes)};
}

// Builds syntactically valid external-plugin state that points at a plugin no test machine has.
[[nodiscard]] PluginInstanceState missingExternalPluginState()
{
    return pluginStateFromXml(
        R"(<PLUGIN type="vst" uniqueId="0" uid="0" )"
        R"(filename="Z:/missing/RockHeroMissing.vst3" name="Missing Plugin" )"
        R"(manufacturer="Rock Hero Tests"/>)");
}

} // namespace

// Verifies normal app launches are not consumed as scanner child processes.
TEST_CASE("Engine child-process helpers ignore normal startup", "[audio][engine]")
{
    CHECK_FALSE(Engine::isPluginScanChildProcessCommandLine("--normal"));
    CHECK_FALSE(Engine::startPluginScanChildProcess("--normal"));
}

// Verifies plugin scanner command lines are recognized before normal editor startup.
TEST_CASE("Engine recognizes plugin helper command lines", "[audio][engine]")
{
    CHECK(Engine::isPluginScanChildProcessCommandLine("--PluginScan:abc"));
}

// Verifies the concrete engine starts with empty state and a zero current position.
TEST_CASE("Engine starts with empty transport state", "[audio][engine][integration]")
{
    const EngineTestHarness harness;
    const Engine& engine = harness.engine;
    const ITransport& transport = engine;

    const auto current_state = transport.state();

    CHECK_FALSE(current_state.playing);
    CHECK(transport.position() == common::core::TimePosition{});
}

// Serialized device restore rejects malformed XML with its own stable error code.
TEST_CASE("Engine rejects invalid serialized audio-device state", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IAudioDeviceConfiguration& audio_devices = harness.engine;

    const auto restored = audio_devices.restoreSerializedDeviceState("[not-xml");

    REQUIRE_FALSE(restored.has_value());
    CHECK(restored.error().code == AudioDeviceConfigurationErrorCode::InvalidSerializedState);
}

// Serialized device restore reports backend route rejection separately from parse failure.
TEST_CASE("Engine reports rejected serialized audio-device state", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IAudioDeviceConfiguration& audio_devices = harness.engine;
    installOnlyRejectingAudioDeviceType(audio_devices.deviceManager());

    const auto restored = audio_devices.restoreSerializedDeviceState(
        R"(<DEVICESETUP deviceType="Rejected Audio" audioInputDeviceName="Rejected Input" )"
        R"(audioOutputDeviceName="Rejected Output"/>)");

    REQUIRE_FALSE(restored.has_value());
    CHECK(restored.error().code == AudioDeviceConfigurationErrorCode::RestoreFailed);
    CHECK_FALSE(restored.error().message.empty());
}

// Verifies meter reads are safe before the playback graph has produced any audio.
TEST_CASE("Engine audio meters start silent", "[audio][engine][integration]")
{
    const EngineTestHarness harness;
    const IAudioMeterSource& meters = harness.engine;

    const AudioMeterSnapshot snapshot = meters.audioMeterSnapshot();

    CHECK(snapshot.live_rig_input == AudioMeterLevel{});
    CHECK(snapshot.live_rig_output == AudioMeterLevel{});
    CHECK(snapshot.master_output == AudioMeterLevel{});
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
        .path = fixtureAudioPath().parent_path() / "missing-thumbnail.wav",
        .normalization = std::nullopt,
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
    ISongAudio& audio = engine;
    ITransport& transport = engine;

    auto song = makeFixtureSong();
    REQUIRE(audio.prepareSong(song).has_value());
    REQUIRE(song.arrangements.size() == 1);

    TransportNotificationRecorder recorder;
    transport.addListener(recorder);

    const auto active_set = audio.setActiveArrangement(song.arrangements.front());

    transport.removeListener(recorder);

    CHECK(active_set.has_value());
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
    ISongAudio& audio = harness.engine;
    const ITransport& transport = harness.engine;
    auto song = makeFixtureSong();

    const auto prepared = audio.prepareSong(song);

    CHECK(prepared.has_value());
    REQUIRE(song.arrangements.size() == 1);
    CHECK(song.arrangements.front().audio_duration.seconds > 0.0);
    CHECK(transport.state() == TransportState{});
    CHECK(transport.position() == common::core::TimePosition{});
}

// Verifies song preparation rejects missing assets without loading fallback media.
TEST_CASE("Engine audio port rejects missing files", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ISongAudio& audio = harness.engine;
    auto song = makeFixtureSong();
    song.arrangements.front().audio_asset = common::core::AudioAsset{
        .path = fixtureAudioPath().parent_path() / "missing-probe.wav",
        .normalization = std::nullopt,
    };

    const auto prepared = audio.prepareSong(song);

    REQUIRE_FALSE(prepared.has_value());
    CHECK(prepared.error().code == SongAudioErrorCode::UnreadableAudioFile);
    CHECK(prepared.error().message.find("missing-probe.wav") != std::string::npos);
}

// Verifies plugin insertion reports unknown candidate IDs as a typed boundary failure.
TEST_CASE("Engine plugin host rejects unknown plugin IDs", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IPluginHost& plugin_host = harness.engine;
    const ITransport& transport = harness.engine;

    const auto snapshot = plugin_host.insertPlugin(
        PluginCandidate{
            .id = "missing-plugin-id",
            .name = "Missing Plugin",
            .manufacturer = {},
            .format_name = "VST3",
            .category = {},
            .file_path = {},
        },
        0);

    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().code == PluginHostErrorCode::PluginNotFound);
    CHECK(transport.state() == TransportState{});
    CHECK(transport.position() == common::core::TimePosition{});
}

// Verifies plugin moves report unknown instance IDs as a typed boundary failure.
TEST_CASE("Engine plugin host rejects unknown plugin moves", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IPluginHost& plugin_host = harness.engine;
    const ITransport& transport = harness.engine;

    const auto result = plugin_host.movePlugin("missing-instance-id", 0);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == PluginHostErrorCode::PluginInstanceNotFound);
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

// Verifies malformed plugin mementos are rejected before the concrete adapter mutates state.
TEST_CASE("Engine plugin host rejects invalid plugin state", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IPluginHost& plugin_host = harness.engine;
    const PluginInstanceState invalid_state{
        .opaque_data = {std::byte{0x01}, std::byte{0x02}},
    };

    const auto insert_result = plugin_host.recreatePluginStatePreservingId(invalid_state, 0);
    const auto set_result = plugin_host.setPluginState("missing-instance-id", invalid_state);

    REQUIRE_FALSE(insert_result.has_value());
    CHECK(insert_result.error().code == PluginHostErrorCode::PluginStateRestoreFailed);
    REQUIRE_FALSE(set_result.has_value());
    CHECK(set_result.error().code == PluginHostErrorCode::PluginStateRestoreFailed);
}

// Verifies plugin-state operations report missing existing targets through the stable port error.
TEST_CASE("Engine plugin host rejects unknown state targets", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IPluginHost& plugin_host = harness.engine;
    const PluginInstanceState valid_state = missingExternalPluginState();

    const auto capture_result = plugin_host.capturePluginState("missing-instance-id");
    const auto set_result = plugin_host.setPluginState("missing-instance-id", valid_state);

    REQUIRE_FALSE(capture_result.has_value());
    CHECK(capture_result.error().code == PluginHostErrorCode::PluginInstanceNotFound);
    REQUIRE_FALSE(set_result.has_value());
    CHECK(set_result.error().code == PluginHostErrorCode::PluginInstanceNotFound);
}

// Verifies state-recreate load failure removes any partial Tracktion plugin before returning.
TEST_CASE("Engine plugin host rejects missing recreated plugin", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    const TemporarySongDirectory song_directory;
    IPluginHost& plugin_host = harness.engine;
    ILiveRig& live_rig = harness.engine;

    const auto result =
        plugin_host.recreatePluginStatePreservingId(missingExternalPluginState(), 0);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == PluginHostErrorCode::PluginStateRestoreFailed);

    const auto snapshot = live_rig.captureActiveRig(
        LiveRigCaptureRequest{
            .song_directory = song_directory.path(),
            .arrangement_id = g_arrangement_id,
            .existing_tone_document_ref = {},
            .block_indices = {},
            .display_type_overrides = {},
        });
    REQUIRE(snapshot.has_value());
    CHECK(snapshot->plugins.empty());
}

// Verifies plugin window requests reject unknown instance IDs before asking Tracktion for a UI.
TEST_CASE("Engine plugin host rejects unknown plugin windows", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IPluginHost& plugin_host = harness.engine;

    const auto result = plugin_host.openPluginWindow("missing-instance-id");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == PluginHostErrorCode::PluginInstanceNotFound);
}

// Verifies project clear resets authored gain without clearing input calibration.
TEST_CASE("Engine live rig clear preserves input gain", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ILiveRig& live_rig = harness.engine;
    ILiveInput& live_input = harness.engine;

    REQUIRE(live_input.setInputGain(Gain{12.0}).has_value());
    REQUIRE(live_rig.setOutputGain(Gain{-12.0}).has_value());

    const auto result = live_rig.clearLiveRig();

    CHECK(result.has_value());
    CHECK(live_input.inputGain().db == Catch::Approx(12.0));
    CHECK(live_rig.outputGain().db == Catch::Approx(defaultGainDb()));
}

// Verifies empty tone loads clear project tone state without clearing input calibration.
TEST_CASE("Engine live rig loads empty tone", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ILiveRig& live_rig = harness.engine;
    ILiveInput& live_input = harness.engine;

    std::optional<std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>>
        result;
    REQUIRE(live_input.setInputGain(Gain{18.0}).has_value());
    REQUIRE(live_rig.setOutputGain(Gain{-18.0}).has_value());

    live_rig.loadLiveRig(
        common::audio::LiveRigLoadRequest{}, [&result](auto value) { result = std::move(value); });

    REQUIRE(result.has_value());
    if (result.has_value())
    {
        const auto& load_result = result.value();
        REQUIRE(load_result.has_value());
        CHECK(load_result->plugins.empty());
        CHECK(load_result->output_gain.db == 0.0);
        CHECK(live_rig.outputGain().db == 0.0);
        CHECK(live_input.inputGain().db == Catch::Approx(18.0));
    }
}

// Verifies tone document load restores authored output while preserving input calibration.
TEST_CASE("Engine live rig loads tone without clearing input gain", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    const TemporarySongDirectory song_directory;
    ILiveRig& live_rig = harness.engine;
    ILiveInput& live_input = harness.engine;

    REQUIRE(live_rig.setOutputGain(Gain{-9.0}).has_value());
    const auto snapshot = live_rig.captureActiveRig(
        LiveRigCaptureRequest{
            .song_directory = song_directory.path(),
            .arrangement_id = g_arrangement_id,
            .existing_tone_document_ref = {},
            .block_indices = {},
            .display_type_overrides = {},
        });

    REQUIRE(snapshot.has_value());
    REQUIRE(live_input.setInputGain(Gain{9.0}).has_value());
    REQUIRE(live_rig.setOutputGain(Gain{3.0}).has_value());

    std::optional<std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>>
        result;
    live_rig.loadLiveRig(
        LiveRigLoadRequest{
            .song_directory = song_directory.path(),
            .tone_document_ref = snapshot->tone_document_ref,
            .progress_callback = {},
            .yield_callback = [](const auto& next) { next(); },
        },
        [&result](auto value) { result = std::move(value); });

    REQUIRE(result.has_value());
    if (result.has_value())
    {
        const auto& load_result = result.value();
        REQUIRE(load_result.has_value());
        CHECK(load_result->plugins.empty());
        CHECK(load_result->output_gain.db == Catch::Approx(-9.0));
        CHECK(live_rig.outputGain().db == Catch::Approx(-9.0));
        CHECK(live_input.inputGain().db == Catch::Approx(9.0));
    }
}

// Verifies output gain persists through captured tone-chain metadata while input gain remains
// app-local live-input state.
TEST_CASE("Engine live rig output gain persists through capture", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    const TemporarySongDirectory song_directory;
    ILiveRig& live_rig = harness.engine;
    ILiveInput& live_input = harness.engine;

    const auto input_result = live_input.setInputGain(Gain{24.0});
    const auto output_result = live_rig.setOutputGain(Gain{-24.0});

    REQUIRE(input_result.has_value());
    REQUIRE(output_result.has_value());
    CHECK(live_input.inputGain().db == Catch::Approx(24.0));
    CHECK(live_rig.outputGain().db == Catch::Approx(-24.0));

    const auto snapshot = live_rig.captureActiveRig(
        LiveRigCaptureRequest{
            .song_directory = song_directory.path(),
            .arrangement_id = g_arrangement_id,
            .existing_tone_document_ref = {},
            .block_indices = {},
            .display_type_overrides = {},
        });

    REQUIRE(snapshot.has_value());
    CHECK(snapshot->plugins.empty());
    CHECK(snapshot->output_gain.db == Catch::Approx(-24.0));
}

// Verifies live input and live rig gain setters clamp requested gain to the public range.
TEST_CASE(
    "Engine live input and output gain setters clamp to range", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ILiveRig& live_rig = harness.engine;
    ILiveInput& live_input = harness.engine;

    const auto input_result = live_input.setInputGain(Gain{25.0});
    const auto output_result = live_rig.setOutputGain(Gain{-100.0});

    REQUIRE(input_result.has_value());
    REQUIRE(output_result.has_value());
    CHECK(live_input.inputGain().db == Catch::Approx(maximumGainDb()));
    CHECK(live_rig.outputGain().db == Catch::Approx(minimumGainDb()));
}

// Verifies new live rig captures use co-located UUID tone document folders.
TEST_CASE("Engine live rig captures UUID tone refs", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    const TemporarySongDirectory song_directory;
    ILiveRig& live_rig = harness.engine;

    const auto snapshot = live_rig.captureActiveRig(
        LiveRigCaptureRequest{
            .song_directory = song_directory.path(),
            .arrangement_id = g_arrangement_id,
            .existing_tone_document_ref = {},
            .block_indices = {},
            .display_type_overrides = {},
        });

    if (!snapshot.has_value())
    {
        FAIL(
            "capture failed with code " << static_cast<int>(snapshot.error().code) << ": "
                                        << snapshot.error().message);
    }
    REQUIRE(snapshot.has_value());
    const std::string& tone_document_ref = snapshot->tone_document_ref;
    const std::string tone_id = toneIdFromRef(tone_document_ref);
    CHECK(snapshot->plugins.empty());
    CHECK(common::core::isCanonicalPackageId(tone_id));
    CHECK(tone_document_ref == "tones/" + tone_id + "/tone.json");
    CHECK(std::filesystem::is_regular_file(song_directory.path() / tone_document_ref));
}

// Verifies the single Tracktion arrangement track can replace its loaded audio.
TEST_CASE("Engine audio port replaces arrangement audio", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    ISongAudio& audio = engine;
    auto song = makeFixtureSong();
    REQUIRE(audio.prepareSong(song).has_value());
    REQUIRE(song.arrangements.size() == 1);
    const common::core::Arrangement& arrangement = song.arrangements.front();

    const auto first_audio_set = audio.setActiveArrangement(arrangement);
    const auto replacement_audio_set = audio.setActiveArrangement(arrangement);

    CHECK(first_audio_set.has_value());
    CHECK(replacement_audio_set.has_value());
}

// Verifies a failed activation leaves the previous transport state visible through state().
TEST_CASE(
    "Failed engine audio activation preserves transport state", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    ISongAudio& audio = engine;
    ITransport& transport = engine;

    [[maybe_unused]] const auto audio_duration = requireLoadedFixtureAudio(audio);

    const auto loaded_state = transport.state();
    const common::core::AudioAsset missing_asset{
        .path = fixtureAudioPath().parent_path() / "missing.wav",
        .normalization = std::nullopt,
    };
    TransportNotificationRecorder recorder;
    transport.addListener(recorder);

    const auto active_set = audio.setActiveArrangement(
        common::core::Arrangement{
            .id = "missing",
            .part = common::core::Part::Lead,
            .difficulty = common::core::DifficultyRating{},
            .audio_asset = missing_asset,
            .audio_duration = common::core::TimeDuration{1.0},
            .tone_document_ref = {},
        });

    transport.removeListener(recorder);

    REQUIRE_FALSE(active_set.has_value());
    CHECK(active_set.error().code == SongAudioErrorCode::UnreadableAudioFile);
    CHECK(transport.state() == loaded_state);
    CHECK(recorder.transport_state_call_count == 0);
    CHECK(recorder.last_transport_state == TransportState{});
}

// Verifies direct ITransport seek commands update the current position without mutating state.
TEST_CASE("Engine seek updates current transport position", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    ISongAudio& audio = engine;
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
    ISongAudio& audio = engine;
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
    ISongAudio& audio = engine;
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
