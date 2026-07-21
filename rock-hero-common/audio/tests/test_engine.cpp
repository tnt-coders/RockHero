#include "live_rig/tone_document.h"
#include "live_rig/tone_file.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <rock_hero/common/audio/engine/engine.h>
#include <rock_hero/common/audio/input/i_live_input.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>
#include <rock_hero/common/audio/song/i_song_audio.h>
#include <rock_hero/common/audio/song/i_thumbnail.h>
#include <rock_hero/common/core/package/package_id.h>
#include <rock_hero/common/core/tone/tone_schedule.h>
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
constexpr const char* g_fake_audio_type_name = "Fake Audio";
constexpr const char* g_fake_device_a_name = "Fake Device A";
constexpr const char* g_fake_device_b_name = "Fake Device B";

// Serialized route naming fake device A, in the single-name shape createStateXml() writes for a
// device type without separate input and output devices.
constexpr const char* g_fake_device_a_state =
    R"(<DEVICESETUP deviceType="Fake Audio" audioDeviceName="Fake Device A"/>)";

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
    return common::core::AudioAsset{
        .path = fixtureAudioPath(), .normalization = std::nullopt, .start_offset = {}
    };
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
            .tones = {},
            .tone_track = {},
            .tone_automation = {},
            .chart_ref = {},
            .chart = {},
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

// Minimal openable audio device so fallback and replug tests can genuinely open fake hardware.
// It never invokes audio processing; the tests only observe open/closed state.
class FakeAudioIODevice final : public juce::AudioIODevice
{
public:
    FakeAudioIODevice(const juce::String& device_name, const juce::String& type_name)
        : juce::AudioIODevice(device_name, type_name)
    {}

    [[nodiscard]] juce::StringArray getOutputChannelNames() override
    {
        return {"Fake Out L", "Fake Out R"};
    }

    [[nodiscard]] juce::StringArray getInputChannelNames() override
    {
        return {"Fake In"};
    }

    [[nodiscard]] juce::Array<double> getAvailableSampleRates() override
    {
        return {48000.0};
    }

    [[nodiscard]] juce::Array<int> getAvailableBufferSizes() override
    {
        return {512};
    }

    [[nodiscard]] int getDefaultBufferSize() override
    {
        return 512;
    }

    [[nodiscard]] juce::String open(
        const juce::BigInteger& /*input_channels*/, const juce::BigInteger& /*output_channels*/,
        double /*sample_rate*/, int /*buffer_size_samples*/) override
    {
        m_open = true;
        return {};
    }

    void close() override
    {
        m_open = false;
    }

    [[nodiscard]] bool isOpen() override
    {
        return m_open;
    }

    // Announces the start to the callback (the manager expects it) without ever rendering audio.
    void start(juce::AudioIODeviceCallback* callback) override
    {
        m_callback = callback;
        if (m_callback != nullptr)
        {
            m_callback->audioDeviceAboutToStart(this);
        }
    }

    void stop() override
    {
        if (m_callback != nullptr)
        {
            m_callback->audioDeviceStopped();
            m_callback = nullptr;
        }
    }

    [[nodiscard]] bool isPlaying() override
    {
        return m_callback != nullptr;
    }

    [[nodiscard]] juce::String getLastError() override
    {
        return {};
    }

    [[nodiscard]] int getCurrentBufferSizeSamples() override
    {
        return 512;
    }

    [[nodiscard]] double getCurrentSampleRate() override
    {
        return 48000.0;
    }

    [[nodiscard]] int getCurrentBitDepth() override
    {
        return 24;
    }

    [[nodiscard]] juce::BigInteger getActiveOutputChannels() const override
    {
        juce::BigInteger channels;
        channels.setRange(0, 2, true);
        return channels;
    }

    [[nodiscard]] juce::BigInteger getActiveInputChannels() const override
    {
        juce::BigInteger channels;
        channels.setBit(0);
        return channels;
    }

    [[nodiscard]] int getOutputLatencyInSamples() override
    {
        return 0;
    }

    [[nodiscard]] int getInputLatencyInSamples() override
    {
        return 0;
    }

private:
    bool m_open{false};
    juce::AudioIODeviceCallback* m_callback{nullptr};
};

// Device type whose device list tests mutate to unplug and replug fake hardware. Raising the
// list-change notification runs JUCE's own AudioDeviceManager::audioDeviceListChanged handler
// (including its hard-coded disconnect fallback) synchronously, exactly as a platform backend
// notification would.
class FakeAudioDeviceType final : public juce::AudioIODeviceType
{
public:
    explicit FakeAudioDeviceType(juce::StringArray device_names)
        : juce::AudioIODeviceType(g_fake_audio_type_name)
        , m_device_names(std::move(device_names))
    {}

    // Replaces the device list and notifies the manager, like hardware arrival or removal.
    void simulateDeviceListChange(juce::StringArray device_names)
    {
        m_device_names = std::move(device_names);
        callDeviceChangeListeners();
    }

    // The fake list is authoritative; there is no hardware to scan.
    void scanForDevices() override
    {}

    [[nodiscard]] juce::StringArray getDeviceNames(bool /*want_input_names*/) const override
    {
        return m_device_names;
    }

    [[nodiscard]] int getDefaultDeviceIndex(bool /*for_input*/) const override
    {
        return 0;
    }

    [[nodiscard]] int getIndexOfDevice(
        juce::AudioIODevice* device, bool /*want_input_names*/) const override
    {
        return device == nullptr ? -1 : m_device_names.indexOf(device->getName());
    }

    [[nodiscard]] bool hasSeparateInputsAndOutputs() const override
    {
        return false;
    }

    [[nodiscard]] juce::AudioIODevice* createDevice(
        const juce::String& output_device_name, const juce::String& input_device_name) override
    {
        const juce::String name =
            output_device_name.isNotEmpty() ? output_device_name : input_device_name;
        if (!m_device_names.contains(name))
        {
            return nullptr;
        }

        auto device = std::make_unique<FakeAudioIODevice>(name, getTypeName());
        return device.release();
    }

private:
    juce::StringArray m_device_names;
};

// Replaces platform audio types with one fake list-mutable type and returns it for test control.
// The manager owns the returned type; the reference stays valid for the manager's lifetime.
[[nodiscard]] FakeAudioDeviceType& installOnlyFakeAudioDeviceType(
    juce::AudioDeviceManager& manager, juce::StringArray device_names)
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

    auto fake_type = std::make_unique<FakeAudioDeviceType>(std::move(device_names));
    FakeAudioDeviceType& fake_type_ref = *fake_type;
    manager.addAudioDeviceType(std::move(fake_type));
    return fake_type_ref;
}

// Posts the next scenario step; declared before runMessageThreadSteps for the recursive chain.
void postNextMessageThreadStep(const std::shared_ptr<std::deque<std::function<void()>>>& steps)
{
    juce::MessageManager::callAsync([steps] {
        if (steps->empty())
        {
            juce::MessageManager::getInstance()->stopDispatchLoop();
            return;
        }

        const std::function<void()> step = std::move(steps->front());
        steps->pop_front();
        step();
        postNextMessageThreadStep(steps);
    });
}

// Runs the supplied steps in order inside one real dispatch loop, so messages a step posts (the
// engine's coalesced device-configuration refresh) dispatch before the next step runs. The loop
// stops after the last step. One call per test: JUCE's quit flag latches per MessageManager, and
// each EngineTestHarness owns the process's only ScopedJuceInitialiser_GUI, so every test gets a
// fresh manager. Steps must use non-throwing Catch2 macros (CHECK, not REQUIRE): the dispatch
// loop's exception guard would swallow a throwing assertion and spin forever.
void runMessageThreadSteps(std::vector<std::function<void()>> steps)
{
    auto queue = std::make_shared<std::deque<std::function<void()>>>();
    for (std::function<void()>& step : steps)
    {
        queue->push_back(std::move(step));
    }

    postNextMessageThreadStep(queue);
    juce::MessageManager::getInstance()->runDispatchLoop();
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

// Verifies the v1 speed contract on the concrete adapter: 1.0 round-trips, anything else is a
// typed loud failure that leaves the reported speed untouched.
TEST_CASE("Engine playback speed accepts only 1.0", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ITransport& transport = harness.engine;

    CHECK(transport.setPlaybackSpeed(1.0).has_value());
    CHECK_THAT(transport.playbackSpeed(), Catch::Matchers::WithinULP(1.0, 0));

    const auto rejected = transport.setPlaybackSpeed(0.5);
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().code == TransportErrorCode::SpeedNotSupported);
    CHECK_THAT(transport.playbackSpeed(), Catch::Matchers::WithinULP(1.0, 0));
}

// Verifies loop engage/read/clear round-trips through the Tracktion-backed adapter.
TEST_CASE("Engine loop region round-trips and clears", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ITransport& transport = harness.engine;
    const auto duration = requireLoadedFixtureAudio(harness.engine);
    REQUIRE(duration.seconds > 0.5);

    CHECK_FALSE(transport.loopRegion().has_value());

    const common::core::TimeRange region{
        .start = common::core::TimePosition{duration.seconds * 0.1},
        .end = common::core::TimePosition{duration.seconds * 0.6},
    };
    REQUIRE(transport.setLoopRegion(region).has_value());

    const auto engaged = transport.loopRegion();
    REQUIRE(engaged.has_value());
    if (engaged.has_value())
    {
        CHECK(engaged->start.seconds == Catch::Approx(region.start.seconds));
        CHECK(engaged->end.seconds == Catch::Approx(region.end.seconds));
    }

    transport.clearLoopRegion();
    CHECK_FALSE(transport.loopRegion().has_value());
}

// Verifies reversed endpoints engage as a normalized forward region instead of failing.
TEST_CASE("Engine loop region normalizes reversed endpoints", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ITransport& transport = harness.engine;
    const auto duration = requireLoadedFixtureAudio(harness.engine);
    REQUIRE(duration.seconds > 0.5);

    REQUIRE(transport
                .setLoopRegion({
                    .start = common::core::TimePosition{duration.seconds * 0.6},
                    .end = common::core::TimePosition{duration.seconds * 0.1},
                })
                .has_value());

    const auto engaged = transport.loopRegion();
    REQUIRE(engaged.has_value());
    if (engaged.has_value())
    {
        CHECK(engaged->start.seconds == Catch::Approx(duration.seconds * 0.1));
        CHECK(engaged->end.seconds == Catch::Approx(duration.seconds * 0.6));
    }
}

// Verifies the 0.1 s port minimum rejects short regions with the typed error and leaves a
// previously engaged loop untouched, keeping the backend's scattered minima unreachable.
TEST_CASE("Engine loop region rejects sub-minimum durations", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ITransport& transport = harness.engine;
    const auto duration = requireLoadedFixtureAudio(harness.engine);
    REQUIRE(duration.seconds > 0.5);

    const common::core::TimeRange engaged{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{duration.seconds * 0.5},
    };
    REQUIRE(transport.setLoopRegion(engaged).has_value());

    const auto rejected = transport.setLoopRegion({
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{g_minimum_loop_region_duration.seconds / 2.0},
    });
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().code == TransportErrorCode::LoopRegionTooShort);

    const auto surviving = transport.loopRegion();
    REQUIRE(surviving.has_value());
    if (surviving.has_value())
    {
        CHECK(surviving->end.seconds == Catch::Approx(engaged.end.seconds));
    }
}

// Verifies loop endpoints clamp into the loaded audio: the backend would accept a loop end far
// beyond the content, but the engine's end-of-content auto-stop could never reach it.
TEST_CASE("Engine loop region clamps to loaded audio length", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ITransport& transport = harness.engine;
    const auto duration = requireLoadedFixtureAudio(harness.engine);
    REQUIRE(duration.seconds > 0.5);

    REQUIRE(transport
                .setLoopRegion({
                    .start = common::core::TimePosition{-5.0},
                    .end = common::core::TimePosition{duration.seconds + 100.0},
                })
                .has_value());

    const auto engaged = transport.loopRegion();
    REQUIRE(engaged.has_value());
    if (engaged.has_value())
    {
        CHECK(engaged->start.seconds == Catch::Approx(0.0));
        CHECK(engaged->end.seconds == Catch::Approx(duration.seconds));
    }
}

// Verifies arrangement activation disengages an engaged loop: loop state persists in the edit's
// TRANSPORT tree, so without the shared-helper clear a stale region would leak across loads.
TEST_CASE("Engine arrangement activation clears engaged loop", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ITransport& transport = harness.engine;
    const auto duration = requireLoadedFixtureAudio(harness.engine);
    REQUIRE(duration.seconds > 0.5);

    REQUIRE(transport
                .setLoopRegion({
                    .start = common::core::TimePosition{0.0},
                    .end = common::core::TimePosition{duration.seconds * 0.5},
                })
                .has_value());
    REQUIRE(transport.loopRegion().has_value());

    // Re-activating the fixture arrangement replaces the backing media and must clear the loop.
    (void)requireLoadedFixtureAudio(harness.engine);
    CHECK_FALSE(transport.loopRegion().has_value());
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

// A saved device that cannot be opened closes gracefully instead of falling back to another
// device. With only the rejecting type installed there is no device to fall back to, so a
// successful restore that leaves the device closed proves no fallback occurred. Reporting success
// (not RestoreFailed) is what stops the editor caller from clearing the user's saved route.
TEST_CASE(
    "Engine closes an unopenable serialized audio-device route without fallback",
    "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IAudioDeviceConfiguration& audio_devices = harness.engine;
    installOnlyRejectingAudioDeviceType(audio_devices.deviceManager());

    const auto restored = audio_devices.restoreSerializedDeviceState(
        R"(<DEVICESETUP deviceType="Rejected Audio" audioInputDeviceName="Rejected Input" )"
        R"(audioOutputDeviceName="Rejected Output"/>)");

    REQUIRE(restored.has_value());
    CHECK(*restored == DeviceRestoreOutcome::DeviceUnavailable);
    CHECK_FALSE(audio_devices.currentDeviceStatus().open);
    // The backend's own diagnostic survives on the closed status snapshot so the editor's failure
    // prompt can report the real cause.
    CHECK_FALSE(audio_devices.currentDeviceStatus().unavailable_reason.empty());
    // The requested route becomes the saved choice even though it could not open; the settings
    // window's apply relies on this to keep a chosen-but-unopenable device as the saved route.
    const std::optional<std::string> saved = audio_devices.serializedDeviceState();
    CHECK(saved.value_or(std::string{}).find("Rejected Output") != std::string::npos);
}

// Unplugging the saved device makes JUCE's audioDeviceListChanged fall back to another device
// (hard-coded in vendored JUCE); the engine's no-fallback policy must close that substitute while
// the saved choice survives for the next launch.
TEST_CASE(
    "Engine closes JUCE's disconnect fallback and keeps the saved device choice",
    "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IAudioDeviceConfiguration& audio_devices = harness.engine;
    FakeAudioDeviceType& fake_type = installOnlyFakeAudioDeviceType(
        audio_devices.deviceManager(), {g_fake_device_a_name, g_fake_device_b_name});

    const auto restored = audio_devices.restoreSerializedDeviceState(g_fake_device_a_state);
    REQUIRE(restored.has_value());
    REQUIRE(*restored == DeviceRestoreOutcome::Opened);
    REQUIRE(audio_devices.currentDeviceStatus().open);

    juce::AudioDeviceManager& device_manager = audio_devices.deviceManager();
    runMessageThreadSteps({
        [&] {
            // Unplug device A. JUCE's own handler runs synchronously inside this call: it closes A
            // and falls back to device B because selectDefaultDeviceOnFailure is hard-coded true.
            fake_type.simulateDeviceListChange({g_fake_device_b_name});
            CHECK(device_manager.getAudioDeviceSetup().outputDeviceName == g_fake_device_b_name);
            // Deliver the manager's broadcast so the engine schedules its configuration refresh.
            device_manager.dispatchPendingMessages();
        },
        [&] {
            // The refresh ran between steps: the policy closed the fallback device, and the saved
            // route still names device A for the next launch. The closed status snapshot reports
            // the plain disconnect reason so the editor's failure overlay can explain the closure
            // in the same shape as a failed open.
            CHECK_FALSE(audio_devices.currentDeviceStatus().open);
            CHECK(audio_devices.currentDeviceStatus().unavailable_reason == "Disconnected");
            const std::optional<std::string> saved = audio_devices.serializedDeviceState();
            CHECK(saved.has_value());
            CHECK(saved.value_or(std::string{}).find(g_fake_device_a_name) != std::string::npos);
        },
    });
}

// Nothing reopens a device automatically: after a disconnect close, the saved device returning
// produces no reopen (the automatic path crashed flaky ASIO drivers mid-enumeration and was
// removed), and the closed status snapshot explains why the route is closed. The only reopen path
// is an explicit application of the saved route -- the editor's failure-prompt Retry.
TEST_CASE(
    "Engine leaves a closed device closed when its hardware returns",
    "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IAudioDeviceConfiguration& audio_devices = harness.engine;
    FakeAudioDeviceType& fake_type =
        installOnlyFakeAudioDeviceType(audio_devices.deviceManager(), {g_fake_device_a_name});

    const auto restored = audio_devices.restoreSerializedDeviceState(g_fake_device_a_state);
    REQUIRE(restored.has_value());
    REQUIRE(*restored == DeviceRestoreOutcome::Opened);
    REQUIRE(audio_devices.currentDeviceStatus().unavailable_reason.empty());

    juce::AudioDeviceManager& device_manager = audio_devices.deviceManager();
    runMessageThreadSteps({
        [&] {
            // Unplug device A with nothing to fall back to; JUCE closes and stays closed.
            fake_type.simulateDeviceListChange({});
            device_manager.dispatchPendingMessages();
        },
        [&] {
            CHECK_FALSE(audio_devices.currentDeviceStatus().open);
            CHECK(audio_devices.currentDeviceStatus().unavailable_reason == "Disconnected");
            // Replug device A: a device event fires, but nothing may reopen automatically.
            fake_type.simulateDeviceListChange({g_fake_device_a_name});
            device_manager.dispatchPendingMessages();
        },
        [&] {
            CHECK_FALSE(audio_devices.currentDeviceStatus().open);
            const std::optional<std::string> saved = audio_devices.serializedDeviceState();
            CHECK(saved.value_or(std::string{}).find(g_fake_device_a_name) != std::string::npos);

            // The user-driven retry applies the saved route explicitly and clears the reason.
            const auto retried = audio_devices.restoreSerializedDeviceState(g_fake_device_a_state);
            REQUIRE(retried.has_value());
            CHECK(*retried == DeviceRestoreOutcome::Opened);
            CHECK(audio_devices.currentDeviceStatus().open);
            CHECK(audio_devices.currentDeviceStatus().unavailable_reason.empty());
            CHECK(device_manager.getAudioDeviceSetup().outputDeviceName == g_fake_device_a_name);
        },
    });
}

// A deliberately closed device (the settings edit stages with the device closed) stays closed
// through benign device events while its hardware remains attached: JUCE's own list handler is a
// no-op with no open device, and the engine adds no reopen of its own.
TEST_CASE(
    "Engine leaves a deliberately closed device closed while its hardware stays listed",
    "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IAudioDeviceConfiguration& audio_devices = harness.engine;
    FakeAudioDeviceType& fake_type =
        installOnlyFakeAudioDeviceType(audio_devices.deviceManager(), {g_fake_device_a_name});

    const auto restored = audio_devices.restoreSerializedDeviceState(g_fake_device_a_state);
    REQUIRE(restored.has_value());
    REQUIRE(*restored == DeviceRestoreOutcome::Opened);

    juce::AudioDeviceManager& device_manager = audio_devices.deviceManager();
    runMessageThreadSteps({
        [&] {
            // The staging close, then a benign list event with the device still attached.
            device_manager.closeAudioDevice();
            fake_type.simulateDeviceListChange({g_fake_device_a_name});
            device_manager.dispatchPendingMessages();
        },
        [&] {
            // Still closed, and the saved route survived for the edit's commit/cancel to decide.
            CHECK_FALSE(audio_devices.currentDeviceStatus().open);
            const std::optional<std::string> saved = audio_devices.serializedDeviceState();
            CHECK(saved.value_or(std::string{}).find(g_fake_device_a_name) != std::string::npos);
        },
    });
}

// A device the user explicitly selects rewrites the saved choice (treatAsChosenDevice), so the
// no-fallback policy must never mistake it for a disconnect fallback and close it.
TEST_CASE("Engine keeps a device the user explicitly selected", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IAudioDeviceConfiguration& audio_devices = harness.engine;
    static_cast<void>(installOnlyFakeAudioDeviceType(
        audio_devices.deviceManager(), {g_fake_device_a_name, g_fake_device_b_name}));

    const auto restored = audio_devices.restoreSerializedDeviceState(g_fake_device_a_state);
    REQUIRE(restored.has_value());
    REQUIRE(*restored == DeviceRestoreOutcome::Opened);

    juce::AudioDeviceManager& device_manager = audio_devices.deviceManager();
    runMessageThreadSteps({
        [&] {
            // The user picks device B: an explicit choice, so JUCE rewrites the saved route too.
            juce::AudioDeviceManager::AudioDeviceSetup setup = device_manager.getAudioDeviceSetup();
            setup.inputDeviceName = g_fake_device_b_name;
            setup.outputDeviceName = g_fake_device_b_name;
            CHECK(device_manager.setAudioDeviceSetup(setup, true).isEmpty());
            device_manager.dispatchPendingMessages();
        },
        [&] {
            // Saved choice and live device agree, so the policy leaves the new device open.
            CHECK(audio_devices.currentDeviceStatus().open);
            CHECK(device_manager.getAudioDeviceSetup().outputDeviceName == g_fake_device_b_name);
        },
    });
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
        .start_offset = {},
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

// Verifies the tempo mirror runs through the real port without disturbing loaded playback state.
// Sequence contents and content stability are covered by the focused tempo-mirror tests; this
// case guards the Engine-level plumbing (message-thread and edit guards, port dispatch).
TEST_CASE("Engine audio port mirrors tempo maps", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ISongAudio& audio = harness.engine;
    const ITransport& transport = harness.engine;
    const common::core::TimeDuration duration = requireLoadedFixtureAudio(audio);
    REQUIRE(duration.seconds > 0.0);

    const common::core::TempoMap tempo_map{
        {common::core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4}},
        {
            common::core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            common::core::BeatAnchor{.measure = 2, .beat = 1, .seconds = 2.0},
        },
    };
    audio.mirrorTempoMap(tempo_map);

    CHECK(transport.state() == TransportState{});
    CHECK(transport.position() == common::core::TimePosition{});
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
        .start_offset = {},
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
            .block_indices = {},
            .display_type_overrides = {},

            .stable_ids = {},
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

// Verifies minting a new tone persists a canonical, empty tone document file on disk.
TEST_CASE("Engine live rig mints an empty tone document", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    const TemporarySongDirectory song_directory;
    ILiveRig& live_rig = harness.engine;

    const auto minted = live_rig.mintEmptyTone(song_directory.path());

    REQUIRE(minted.has_value());
    CHECK(common::core::isCanonicalToneDocumentRef(*minted));
    CHECK(std::filesystem::exists(song_directory.path() / *minted));
}

// Verifies tone-file export is refused without a loaded rig and round-trips the audible chain
// and output gain once one exists (the placeholder branch of an empty rig is exportable).
TEST_CASE("Engine exports the audible tone to a tone file", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    const TemporarySongDirectory scratch_directory;
    ILiveRig& live_rig = harness.engine;
    const std::filesystem::path tone_file = scratch_directory.path() / "exported.tone";

    const auto refused = live_rig.exportAudibleTone(
        ToneFileExportRequest{
            .tone_file_path = tone_file,
            .block_indices = {},
            .display_type_overrides = {},
        });
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == LiveRigErrorCode::InvalidRequest);

    std::optional<std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>>
        load_result;
    live_rig.loadLiveRig(common::audio::LiveRigLoadRequest{}, [&load_result](auto value) {
        load_result = std::move(value);
    });
    REQUIRE(load_result.has_value());
    REQUIRE(live_rig.setOutputGain(Gain{-6.0}).has_value());

    const auto exported = live_rig.exportAudibleTone(
        ToneFileExportRequest{
            .tone_file_path = tone_file,
            .block_indices = {},
            .display_type_overrides = {},
        });
    REQUIRE(exported.has_value());

    const auto payload = readToneFile(tone_file);
    REQUIRE(payload.has_value());
    CHECK(payload->document.chain.empty());
    CHECK(payload->document.output_gain.db == Catch::Approx(-6.0));
}

// Verifies the whole-chain undo memento is refused without a rig and captures the audible
// chain's gain once one exists.
TEST_CASE("Engine captures the audible tone chain state", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ILiveRig& live_rig = harness.engine;

    const auto refused = live_rig.captureAudibleToneState();
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == LiveRigErrorCode::InvalidRequest);

    std::optional<std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>>
        load_result;
    live_rig.loadLiveRig(common::audio::LiveRigLoadRequest{}, [&load_result](auto value) {
        load_result = std::move(value);
    });
    REQUIRE(load_result.has_value());
    REQUIRE(live_rig.setOutputGain(Gain{-3.0}).has_value());

    const auto state = live_rig.captureAudibleToneState();
    REQUIRE(state.has_value());
    CHECK(state->plugin_states.empty());
    CHECK(state->output_gain.db == Catch::Approx(-3.0));
}

// Verifies a tone-file replace applies the file's chain and gain to the audible branch, and that
// unreadable files or a rig-less engine refuse without touching anything.
TEST_CASE("Engine replaces the audible tone from a tone file", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    const TemporarySongDirectory scratch_directory;
    ILiveRig& live_rig = harness.engine;
    const std::filesystem::path tone_file = scratch_directory.path() / "replacement.tone";

    // An empty-chain tone file carrying only an authored gain exercises the full transactional
    // read + swap path without needing an installed plugin.
    REQUIRE(writeToneFile(tone_file, ToneDocument{.chain = {}, .output_gain = Gain{-4.5}}, {})
                .has_value());

    std::optional<std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>>
        replace_result;
    live_rig.replaceAudibleToneFromFile(
        ToneFileReplaceRequest{
            .tone_file_path = tone_file,
            .progress_callback = {},
            .yield_callback = [](const auto& next) { next(); },
        },
        [&replace_result](auto value) { replace_result = std::move(value); });
    REQUIRE(replace_result.has_value());
    if (replace_result.has_value())
    {
        REQUIRE_FALSE(replace_result->has_value());
        CHECK(replace_result->error().code == LiveRigErrorCode::InvalidRequest);
    }

    std::optional<std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>>
        load_result;
    live_rig.loadLiveRig(common::audio::LiveRigLoadRequest{}, [&load_result](auto value) {
        load_result = std::move(value);
    });
    REQUIRE(load_result.has_value());
    REQUIRE(live_rig.setOutputGain(Gain{2.0}).has_value());

    replace_result.reset();
    live_rig.replaceAudibleToneFromFile(
        ToneFileReplaceRequest{
            .tone_file_path = tone_file,
            .progress_callback = {},
            .yield_callback = [](const auto& next) { next(); },
        },
        [&replace_result](auto value) { replace_result = std::move(value); });
    REQUIRE(replace_result.has_value());
    if (replace_result.has_value())
    {
        REQUIRE(replace_result->has_value());
        CHECK((*replace_result)->plugins.empty());
        CHECK((*replace_result)->output_gain.db == Catch::Approx(-4.5));
    }
    CHECK(live_rig.outputGain().db == Catch::Approx(-4.5));

    replace_result.reset();
    live_rig.replaceAudibleToneFromFile(
        ToneFileReplaceRequest{
            .tone_file_path = scratch_directory.path() / "missing.tone",
            .progress_callback = {},
            .yield_callback = [](const auto& next) { next(); },
        },
        [&replace_result](auto value) { replace_result = std::move(value); });
    REQUIRE(replace_result.has_value());
    if (replace_result.has_value())
    {
        REQUIRE_FALSE(replace_result->has_value());
        CHECK(replace_result->error().code == LiveRigErrorCode::CouldNotReadToneFile);
    }
    // The failed replace never touched the live chain.
    CHECK(live_rig.outputGain().db == Catch::Approx(-4.5));
}

// Verifies a tone file naming an uninstalled plugin is refused with the aggregated
// missing-plugin policy and leaves the previous chain state intact.
TEST_CASE("Engine tone-file replace refuses missing plugins", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    const TemporarySongDirectory scratch_directory;
    ILiveRig& live_rig = harness.engine;
    const std::filesystem::path tone_file = scratch_directory.path() / "missing_amp.tone";

    PluginIdentity missing_identity;
    missing_identity.format_name = "VST3";
    missing_identity.name = "Nonexistent Amp";
    missing_identity.manufacturer = "Nobody";
    missing_identity.unique_id = "feedc0de";
    ToneDocument document;
    document.chain.push_back(
        PluginRecord{
            .id = "plugin-1",
            .identity = missing_identity,
            .tracktion_state_ref = {},
            .block_index = 0,
            .display_type_override = {},
            .stable_id = {},
        });
    document.output_gain = Gain{};
    juce::ValueTree state{tracktion::IDs::PLUGIN};
    state.setProperty(tracktion::IDs::type, tracktion::ExternalPlugin::xmlTypeName, nullptr);
    const std::vector<juce::ValueTree> states{state};
    REQUIRE(writeToneFile(tone_file, document, states).has_value());

    std::optional<std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>>
        load_result;
    live_rig.loadLiveRig(common::audio::LiveRigLoadRequest{}, [&load_result](auto value) {
        load_result = std::move(value);
    });
    REQUIRE(load_result.has_value());
    REQUIRE(live_rig.setOutputGain(Gain{-1.5}).has_value());

    std::optional<std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>>
        replace_result;
    live_rig.replaceAudibleToneFromFile(
        ToneFileReplaceRequest{
            .tone_file_path = tone_file,
            .progress_callback = {},
            .yield_callback = [](const auto& next) { next(); },
        },
        [&replace_result](auto value) { replace_result = std::move(value); });

    REQUIRE(replace_result.has_value());
    if (replace_result.has_value())
    {
        REQUIRE_FALSE(replace_result->has_value());
        CHECK(replace_result->error().code == LiveRigErrorCode::MissingPlugins);
    }
    // Refusal is transactional: the previous chain state stays untouched.
    CHECK(live_rig.outputGain().db == Catch::Approx(-1.5));
}

// Verifies the memento restore round trip: capture, mutate, restore, and the audible chain's
// gain returns to the captured value.
TEST_CASE("Engine restores the audible tone chain from a memento", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    ILiveRig& live_rig = harness.engine;

    std::optional<std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>>
        load_result;
    live_rig.loadLiveRig(common::audio::LiveRigLoadRequest{}, [&load_result](auto value) {
        load_result = std::move(value);
    });
    REQUIRE(load_result.has_value());
    REQUIRE(live_rig.setOutputGain(Gain{-3.0}).has_value());

    const auto state = live_rig.captureAudibleToneState();
    REQUIRE(state.has_value());
    REQUIRE(live_rig.setOutputGain(Gain{6.0}).has_value());

    const auto restore_result = live_rig.restoreAudibleToneState(*state);

    REQUIRE(restore_result.has_value());
    CHECK(restore_result->output_gain.db == Catch::Approx(-3.0));
    CHECK(live_rig.outputGain().db == Catch::Approx(-3.0));
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
        CHECK_THAT(load_result->output_gain.db, Catch::Matchers::WithinULP(0.0, 0));
        CHECK_THAT(live_rig.outputGain().db, Catch::Matchers::WithinULP(0.0, 0));
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

    // Load a real minted tone, author a gain on it, and capture so the branch document carries
    // that gain; a rig-less capture writes nothing under the all-branch capture model.
    const auto minted = live_rig.mintEmptyTone(song_directory.path());
    REQUIRE(minted.has_value());
    std::optional<std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>>
        first_load;
    live_rig.loadLiveRig(
        LiveRigLoadRequest{
            .song_directory = song_directory.path(),
            .tone_document_refs = {*minted},
            .audible_tone_ref = {},
            .progress_callback = {},
            .yield_callback = [](const auto& next) { next(); },
        },
        [&first_load](auto value) { first_load = std::move(value); });
    REQUIRE(first_load.has_value());
    // clang-tidy does not treat Catch2 REQUIRE as an optional guard, so assert engagement
    // explicitly before dereferencing.
    if (!first_load.has_value())
    {
        return;
    }
    REQUIRE(first_load->has_value());

    REQUIRE(live_rig.setOutputGain(Gain{-9.0}).has_value());
    const auto snapshot = live_rig.captureActiveRig(
        LiveRigCaptureRequest{
            .song_directory = song_directory.path(),
            .arrangement_id = g_arrangement_id,
            .block_indices = {},
            .display_type_overrides = {},

            .stable_ids = {},
        });

    REQUIRE(snapshot.has_value());
    REQUIRE(live_input.setInputGain(Gain{9.0}).has_value());
    REQUIRE(live_rig.setOutputGain(Gain{3.0}).has_value());

    std::optional<std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>>
        result;
    live_rig.loadLiveRig(
        LiveRigLoadRequest{
            .song_directory = song_directory.path(),
            .tone_document_refs = {*minted},
            .audible_tone_ref = {},
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
            .block_indices = {},
            .display_type_overrides = {},

            .stable_ids = {},
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

// Verifies capture rewrites every loaded branch's document, not just the audible one, and routes
// the structural output gain to the audible branch while others keep their retained gain.
TEST_CASE("Engine live rig captures every loaded tone branch", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    const TemporarySongDirectory song_directory;
    ILiveRig& live_rig = harness.engine;

    const auto first_ref = live_rig.mintEmptyTone(song_directory.path());
    const auto second_ref = live_rig.mintEmptyTone(song_directory.path());
    REQUIRE(first_ref.has_value());
    REQUIRE(second_ref.has_value());

    std::optional<std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>>
        loaded;
    live_rig.loadLiveRig(
        LiveRigLoadRequest{
            .song_directory = song_directory.path(),
            .tone_document_refs = {*first_ref, *second_ref},
            .audible_tone_ref = *first_ref,
            .progress_callback = {},
            .yield_callback = [](const auto& next) { next(); },
        },
        [&loaded](auto value) { loaded = std::move(value); });
    REQUIRE(loaded.has_value());
    // clang-tidy does not treat Catch2 REQUIRE as an optional guard, so assert engagement
    // explicitly before dereferencing.
    if (!loaded.has_value())
    {
        return;
    }
    REQUIRE(loaded->has_value());

    // Author a gain on the audible (first) tone, then capture: both documents must be rewritten,
    // the first carrying the authored gain and the second keeping its minted unity gain.
    REQUIRE(live_rig.setOutputGain(Gain{-6.0}).has_value());
    const auto snapshot = live_rig.captureActiveRig(
        LiveRigCaptureRequest{
            .song_directory = song_directory.path(),
            .arrangement_id = g_arrangement_id,
            .block_indices = {},
            .display_type_overrides = {},

            .stable_ids = {},
        });
    REQUIRE(snapshot.has_value());
    CHECK(snapshot->output_gain.db == Catch::Approx(-6.0));

    const auto first_document = readToneDocument(song_directory.path(), *first_ref);
    const auto second_document = readToneDocument(song_directory.path(), *second_ref);
    REQUIRE(first_document.has_value());
    REQUIRE(second_document.has_value());
    CHECK(first_document->output_gain.db == Catch::Approx(-6.0));
    CHECK(second_document->output_gain.db == Catch::Approx(defaultGainDb()));
}

// Verifies the master gain reports the backend's fresh-edit default truthfully (the port never
// renormalizes — the editor shares this engine) and round-trips a set value.
TEST_CASE("Engine master gain reports default and round-trips", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IMixControls& mix = harness.engine;

    // Tracktion constructs a fresh edit's master volume at -3 dB (Edit::Options default).
    CHECK(mix.masterGain().db == Catch::Approx(-3.0));

    REQUIRE(mix.setMasterGain(Gain{-6.0}).has_value());
    CHECK(mix.masterGain().db == Catch::Approx(-6.0));
}

// Verifies the backing gain is a track-level stage that round-trips independently of the clip
// normalization gain the song loader applies (separate stages compose; neither overwrites the
// other), including across an arrangement load carrying normalization metadata.
TEST_CASE("Engine backing gain composes with normalization", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IMixControls& mix = harness.engine;
    ISongAudio& audio = harness.engine;

    // Track volume defaults to 0 dB before any song loads. An absolute tolerance absorbs the float
    // linear->dB round-trip error (macOS returns ~-2.4e-7 dB where MSVC returns exactly 0.0).
    CHECK_THAT(mix.backingGain().db, Catch::Matchers::WithinAbs(0.0, 1e-6));
    REQUIRE(mix.setBackingGain(Gain{-4.5}).has_value());
    CHECK(mix.backingGain().db == Catch::Approx(-4.5));

    // Loading an arrangement WITH normalization sets the clip gain; the track-level backing
    // gain must survive untouched because it is a different processing stage.
    auto song = makeFixtureSong();
    song.arrangements.front().audio_asset.normalization =
        common::core::AudioNormalization{.gain_db = -8.0, .validation_sha256 = {}};
    REQUIRE(audio.prepareSong(song).has_value());
    REQUIRE(audio.setActiveArrangement(song.arrangements.front()).has_value());

    CHECK(mix.backingGain().db == Catch::Approx(-4.5));

    // And the backing gain stays writable after the load without disturbing playback state.
    REQUIRE(mix.setBackingGain(Gain{-2.0}).has_value());
    CHECK(mix.backingGain().db == Catch::Approx(-2.0));
}

// Verifies the tone timeline refuses to bake before a rig is loaded.
TEST_CASE("Engine tone timeline requires a loaded rig", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    IToneTimelinePlayer& timeline = harness.engine;

    const auto prepared = timeline.prepareToneTimeline(std::filesystem::temp_directory_path(), {});

    REQUIRE_FALSE(prepared.has_value());
    CHECK(prepared.error().code == LiveRigErrorCode::InvalidRequest);
}

// Verifies schedule baking succeeds over a loaded rig, is idempotent on re-prepare, and rejects
// a schedule referencing a tone the rig never loaded.
TEST_CASE("Engine tone timeline bakes the switch schedule", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    const TemporarySongDirectory song_directory;
    ILiveRig& live_rig = harness.engine;
    IToneTimelinePlayer& timeline = harness.engine;

    const auto first_ref = live_rig.mintEmptyTone(song_directory.path());
    const auto second_ref = live_rig.mintEmptyTone(song_directory.path());
    REQUIRE(first_ref.has_value());
    REQUIRE(second_ref.has_value());

    std::optional<std::expected<LiveRigLoadResult, LiveRigError>> loaded;
    live_rig.loadLiveRig(
        LiveRigLoadRequest{
            .song_directory = song_directory.path(),
            .tone_document_refs = {*first_ref, *second_ref},
            .audible_tone_ref = *first_ref,
            .progress_callback = {},
            .yield_callback = [](const auto& next) { next(); },
        },
        [&loaded](auto value) { loaded = std::move(value); });
    REQUIRE(loaded.has_value());
    if (!loaded.has_value())
    {
        return;
    }
    REQUIRE(loaded->has_value());

    const std::vector<common::core::ToneSwitchRegion> schedule{
        common::core::ToneSwitchRegion{
            .time_range =
                {.start = common::core::TimePosition{0.0}, .end = common::core::TimePosition{4.0}},
            .tone_document_ref = *first_ref,
        },
        common::core::ToneSwitchRegion{
            .time_range =
                {.start = common::core::TimePosition{4.0}, .end = common::core::TimePosition{8.0}},
            .tone_document_ref = *second_ref,
        },
    };
    // The load result surfaces each tone's summed reported latency (plan 21 Phase 5): empty
    // chains report exactly zero, and the field exists for the editor's export warning to read.
    REQUIRE((*loaded)->tone_chains.size() == 2);
    CHECK_THAT(
        (*loaded)->tone_chains[0].summed_reported_latency_seconds,
        Catch::Matchers::WithinULP(0.0, 0));
    CHECK_THAT(
        (*loaded)->tone_chains[1].summed_reported_latency_seconds,
        Catch::Matchers::WithinULP(0.0, 0));

    CHECK(timeline.prepareToneTimeline(song_directory.path(), schedule).has_value());

    // Re-preparing (a new load of the same session) must not accumulate stale points.
    CHECK(timeline.prepareToneTimeline(song_directory.path(), schedule).has_value());

    // An empty schedule is legal (tone-less arrangement) and clears any baked curves.
    CHECK(timeline.prepareToneTimeline(song_directory.path(), {}).has_value());

    const auto unknown = timeline.prepareToneTimeline(
        song_directory.path(),
        std::vector<common::core::ToneSwitchRegion>{
            common::core::ToneSwitchRegion{
                .time_range =
                    {.start = common::core::TimePosition{0.0},
                     .end = common::core::TimePosition{4.0}},
                .tone_document_ref = "tones/unloaded/tone.json",
            },
        });
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().code == LiveRigErrorCode::InvalidRequest);
}

// Verifies the rig load scans to completion and refuses ONCE with the complete missing-plugin
// list (gameplay policy 21-Q1(A)) instead of aborting at the first uninstalled plugin.
TEST_CASE("Engine live rig lists every missing plugin", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    const TemporarySongDirectory song_directory;
    ILiveRig& live_rig = harness.engine;

    // A tone document whose two plugins point at files no machine has: identity resolution
    // fails with PluginNotFound for each, and both must appear in the final refusal. Sidecar
    // refs must be canonical (state/ dir, .tracktion-plugin extension) and exist on disk to
    // pass document validation; their content is never read because identity resolution fails
    // before the restore step.
    const std::string tone_ref = "tones/f0e1d2c3-b4a5-4697-8879-9a0b1c2d3e4f/tone.json";
    ToneDocument document;
    std::size_t record_index = 0;
    for (const char* const missing_name : {"Missing One", "Missing Two"})
    {
        PluginRecord record;
        record.id = std::string{"record-"} + std::to_string(record_index);
        record.identity.format_name = "VST3";
        record.identity.name = missing_name;
        record.identity.original_file_or_identifier = "Z:/missing/RockHeroMissing.vst3";
        const std::filesystem::path state_ref = generatedPluginStatePath(
            toneDocumentStateDirectory(std::filesystem::path{tone_ref}), record_index);
        record.tracktion_state_ref = state_ref.generic_string();
        REQUIRE(writeTextFile(
                    song_directory.path() / state_ref,
                    "<PLUGIN type=\"vst\" name=\"placeholder\"/>",
                    LiveRigErrorCode::CouldNotWritePluginState)
                    .has_value());
        document.chain.push_back(std::move(record));
        ++record_index;
    }
    REQUIRE(writeToneDocument(song_directory.path() / tone_ref, document).has_value());

    std::optional<std::expected<LiveRigLoadResult, LiveRigError>> loaded;
    live_rig.loadLiveRig(
        LiveRigLoadRequest{
            .song_directory = song_directory.path(),
            .tone_document_refs = {tone_ref},
            .audible_tone_ref = tone_ref,
            .progress_callback = {},
            .yield_callback = [](const auto& next) { next(); },
        },
        [&loaded](auto value) { loaded = std::move(value); });

    REQUIRE(loaded.has_value());
    if (!loaded.has_value())
    {
        return;
    }
    REQUIRE_FALSE(loaded->has_value());
    INFO("load error message: " << loaded->error().message);
    CHECK(loaded->error().code == LiveRigErrorCode::MissingPlugins);
    CHECK(loaded->error().message.find("Missing One") != std::string::npos);
    CHECK(loaded->error().message.find("Missing Two") != std::string::npos);
}

// Verifies the incremental empty-branch add registers a switchable, capturable branch without a
// rig reload, and that adding an already-loaded reference is an idempotent no-op.
TEST_CASE("Engine live rig adds an empty tone branch incrementally", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    const TemporarySongDirectory song_directory;
    ILiveRig& live_rig = harness.engine;

    // No rig loaded yet: the add must fail so callers fall back to a full load.
    CHECK_FALSE(live_rig.addEmptyToneBranch("tones/unloaded/tone.json").has_value());

    const auto first_ref = live_rig.mintEmptyTone(song_directory.path());
    const auto second_ref = live_rig.mintEmptyTone(song_directory.path());
    REQUIRE(first_ref.has_value());
    REQUIRE(second_ref.has_value());

    std::optional<std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>>
        loaded;
    live_rig.loadLiveRig(
        LiveRigLoadRequest{
            .song_directory = song_directory.path(),
            .tone_document_refs = {*first_ref},
            .audible_tone_ref = {},
            .progress_callback = {},
            .yield_callback = [](const auto& next) { next(); },
        },
        [&loaded](auto value) { loaded = std::move(value); });
    REQUIRE(loaded.has_value());
    // clang-tidy does not treat Catch2 REQUIRE as an optional guard, so assert engagement
    // explicitly before dereferencing.
    if (!loaded.has_value())
    {
        return;
    }
    REQUIRE(loaded->has_value());

    REQUIRE(live_rig.addEmptyToneBranch(*second_ref).has_value());
    // Adding the same reference again is a no-op success (lingering branches satisfy re-adds).
    REQUIRE(live_rig.addEmptyToneBranch(*second_ref).has_value());

    // The new branch is switchable and capture persists it like any loaded branch, proving the
    // parallel bookkeeping arrays stayed coherent.
    CHECK(live_rig.setAudibleTone(*second_ref).has_value());
    REQUIRE(live_rig.setOutputGain(Gain{-3.0}).has_value());
    const auto snapshot = live_rig.captureActiveRig(
        LiveRigCaptureRequest{
            .song_directory = song_directory.path(),
            .arrangement_id = g_arrangement_id,
            .block_indices = {},
            .display_type_overrides = {},

            .stable_ids = {},
        });
    REQUIRE(snapshot.has_value());
    const auto second_document = readToneDocument(song_directory.path(), *second_ref);
    REQUIRE(second_document.has_value());
    CHECK(second_document->output_gain.db == Catch::Approx(-3.0));
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
        .start_offset = {},
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
            .tones = {},
            .tone_track = {},
            .tone_automation = {},
            .chart_ref = {},
            .chart = {},
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

// The signed asset start offset moves the clip's timeline end in either direction: a negative
// offset skips the recording's pre-score head (Guitar Pro's negative FramePadding), a positive
// one delays the whole clip. Seek clamping observes that end through the public transport port.
TEST_CASE("Engine clamps seeks to the offset-adjusted audio end", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    ISongAudio& audio = engine;
    ITransport& transport = engine;

    auto song = makeFixtureSong();
    REQUIRE(audio.prepareSong(song).has_value());
    REQUIRE(song.arrangements.size() == 1);
    common::core::Arrangement& arrangement = song.arrangements.front();
    const double duration_seconds = arrangement.audio_duration.seconds;
    REQUIRE(duration_seconds > 0.5);

    SECTION("negative offset trims the head, ending the clip early")
    {
        arrangement.audio_asset.start_offset = common::core::TimeDuration{-0.25};
        REQUIRE(audio.setActiveArrangement(arrangement).has_value());

        transport.seek(common::core::TimePosition{duration_seconds + 5.0});
        CHECK(transport.position().seconds == Catch::Approx(duration_seconds - 0.25));
    }

    SECTION("positive offset delays the clip, ending it late")
    {
        arrangement.audio_asset.start_offset = common::core::TimeDuration{0.25};
        REQUIRE(audio.setActiveArrangement(arrangement).has_value());

        transport.seek(common::core::TimePosition{duration_seconds + 5.0});
        CHECK(transport.position().seconds == Catch::Approx(duration_seconds + 0.25));
    }
}

// A negative offset larger than the recording would trim away every sample; activation must fail
// with the typed duration error instead of inserting an empty clip.
TEST_CASE(
    "Engine rejects a start offset that trims the whole recording", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    ISongAudio& audio = engine;

    auto song = makeFixtureSong();
    REQUIRE(audio.prepareSong(song).has_value());
    REQUIRE(song.arrangements.size() == 1);
    common::core::Arrangement& arrangement = song.arrangements.front();
    arrangement.audio_asset.start_offset =
        common::core::TimeDuration{-(arrangement.audio_duration.seconds + 1.0)};

    const auto active_set = audio.setActiveArrangement(arrangement);

    REQUIRE_FALSE(active_set.has_value());
    CHECK(active_set.error().code == SongAudioErrorCode::InvalidAudioDuration);
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

// The clock port must be meaningful immediately after construction, before any load or playback:
// zero position, stopped, rate 1.0, with a real construction-time capture stamp.
TEST_CASE("Engine clock snapshot starts stopped at zero with rate one", "[audio][engine][clock]")
{
    const EngineTestHarness harness;
    const PlaybackClockSnapshot snapshot = harness.engine.snapshot();

    CHECK(snapshot.position == common::core::TimePosition{});
    CHECK_FALSE(snapshot.playing);
    CHECK(std::is_eq(snapshot.playback_rate <=> 1.0));
    CHECK(snapshot.monotonic_capture_time > std::chrono::nanoseconds{0});
}

// Activating an arrangement resets playback, so the clock must publish stopped-at-start.
TEST_CASE("Engine clock reports stopped at start after arrangement load", "[audio][engine][clock]")
{
    EngineTestHarness harness;
    (void)requireLoadedFixtureAudio(harness.engine);

    const PlaybackClockSnapshot snapshot = harness.engine.snapshot();
    CHECK(snapshot.position == common::core::TimePosition{});
    CHECK_FALSE(snapshot.playing);
}

// Seek publishes the clamped position: in-range values land where they were sent (within the
// clock's nanosecond quantization), negatives clamp to zero, beyond-length clamps to duration.
TEST_CASE("Engine clock publishes clamped seek positions", "[audio][engine][clock]")
{
    EngineTestHarness harness;
    const common::core::TimeDuration duration = requireLoadedFixtureAudio(harness.engine);
    REQUIRE(duration.seconds > 0.0);

    const double mid_seconds = duration.seconds / 2.0;
    harness.engine.seek(common::core::TimePosition{mid_seconds});
    CHECK(std::abs(harness.engine.snapshot().position.seconds - mid_seconds) < 1.0e-9);

    harness.engine.seek(common::core::TimePosition{-5.0});
    CHECK(harness.engine.snapshot().position == common::core::TimePosition{});

    harness.engine.seek(common::core::TimePosition{duration.seconds + 100.0});
    CHECK(std::abs(harness.engine.snapshot().position.seconds - duration.seconds) < 1.0e-9);
}

// After each transport verb the clock's playing flag must agree with the listener-facing coarse
// state — robust to headless transport behavior, where play() may not actually start.
TEST_CASE("Engine clock playing flag matches coarse transport state", "[audio][engine][clock]")
{
    EngineTestHarness harness;
    (void)requireLoadedFixtureAudio(harness.engine);

    harness.engine.play();
    CHECK(harness.engine.snapshot().playing == harness.engine.state().playing);

    harness.engine.pause();
    CHECK(harness.engine.snapshot().playing == harness.engine.state().playing);

    harness.engine.stop();
    CHECK(harness.engine.snapshot().playing == harness.engine.state().playing);
    CHECK(harness.engine.snapshot().position == common::core::TimePosition{});
}

// Every boundary publish restamps the snapshot with a non-zero, non-decreasing capture time so
// extrapolating consumers can trust stamp arithmetic across transport operations.
TEST_CASE("Engine clock capture stamps never decrease across publishes", "[audio][engine][clock]")
{
    EngineTestHarness harness;
    const std::chrono::nanoseconds construction_stamp =
        harness.engine.snapshot().monotonic_capture_time;
    CHECK(construction_stamp > std::chrono::nanoseconds{0});

    (void)requireLoadedFixtureAudio(harness.engine);
    const std::chrono::nanoseconds load_stamp = harness.engine.snapshot().monotonic_capture_time;
    CHECK(load_stamp >= construction_stamp);

    harness.engine.seek(common::core::TimePosition{0.1});
    const std::chrono::nanoseconds seek_stamp = harness.engine.snapshot().monotonic_capture_time;
    CHECK(seek_stamp >= load_stamp);

    harness.engine.stop();
    CHECK(harness.engine.snapshot().monotonic_capture_time >= seek_stamp);
}

// While the transport plays, a message-thread republisher restamps the clock at render-adjacent
// cadence; once stopped it is retired. Headless tests drive the real timer through JUCE's public
// synchronous-timer hook because no message loop is pumped here. The playing branch is guarded:
// without an audio device the headless transport may refuse to enter play, and the retired-timer
// half of the contract is what must hold unconditionally.
TEST_CASE("Engine clock republisher runs only while playing", "[audio][engine][clock]")
{
    EngineTestHarness harness;
    (void)requireLoadedFixtureAudio(harness.engine);

    harness.engine.play();
    const PlaybackClockSnapshot play_snapshot = harness.engine.snapshot();

    // The republisher arms exactly when the play boundary published playing=true, so that
    // published flag — not a later transport read — is the correct precondition. Headless
    // transports may report playing only after asynchronous context work, in which case the
    // republisher correctly stayed off and there is nothing to observe here.
    if (play_snapshot.playing)
    {
        bool republished = false;
        for (int attempt = 0; attempt < 10 && !republished; ++attempt)
        {
            juce::Thread::sleep(20);
            juce::Timer::callPendingTimersSynchronously();
            republished = harness.engine.snapshot().monotonic_capture_time >
                          play_snapshot.monotonic_capture_time;
        }
        CHECK(republished);
    }

    harness.engine.stop();
    const std::chrono::nanoseconds stop_stamp = harness.engine.snapshot().monotonic_capture_time;
    juce::Thread::sleep(40);
    juce::Timer::callPendingTimersSynchronously();

    CHECK(harness.engine.snapshot().monotonic_capture_time == stop_stamp);
    CHECK(harness.engine.snapshot().position == common::core::TimePosition{});
}

} // namespace rock_hero::common::audio
