#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <juce_events/juce_events.h>
#include <optional>
#include <rock_hero/common/audio/device/audio_device_settings.h>
#include <rock_hero/common/audio/input/audio_meter_snapshot.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/input/live_input_monitoring_status.h>
#include <rock_hero/common/audio/testing/configurable_audio_device_configuration.h>
#include <rock_hero/common/audio/testing/fake_live_input.h>
#include <rock_hero/common/audio/testing/in_memory_audio_config_store.h>
#include <rock_hero/common/audio/testing/input_device_identity_fixtures.h>
#include <rock_hero/game/core/audio/game_audio_config.h>
#include <rock_hero/game/core/audio/native_audio_setup.h>
#include <rock_hero/game/core/settings/game_settings.h>
#include <string>
#include <system_error>
#include <variant>

namespace rock_hero::game::core
{

namespace
{

// Minimal IAudioDeviceSettings fake: the native-setup driver only calls apply(); the staged select*
// surface is what the SDL picker drives (plan 26 Phase 8), so it is a no-op here. apply() returns a
// configurable result so the device-apply failure path can be exercised without audio hardware.
class FakeAudioDeviceSettings final : public common::audio::IAudioDeviceSettings
{
public:
    [[nodiscard]] common::audio::AudioDeviceSettingsState state() const override
    {
        return {};
    }

    void selectAudioSystem(int /*choice_id*/) override
    {}
    void selectDevice(int /*choice_id*/) override
    {}
    void selectInputDevice(int /*choice_id*/) override
    {}
    void selectOutputDevice(int /*choice_id*/) override
    {}
    void selectInputChannel(int /*choice_id*/) override
    {}
    void selectStereoOutputPair(int /*choice_id*/) override
    {}
    void selectSampleRate(int /*choice_id*/) override
    {}
    void selectBufferSize(int /*choice_id*/) override
    {}

    [[nodiscard]] std::expected<void, common::audio::AudioDeviceSettingsError> apply() override
    {
        apply_call_count += 1;
        if (next_apply_error.has_value())
        {
            const common::audio::AudioDeviceSettingsError error = *next_apply_error;
            next_apply_error.reset();
            return std::unexpected{error};
        }
        return {};
    }

    [[nodiscard]] std::expected<void, common::audio::AudioDeviceSettingsError> cancel() override
    {
        return {};
    }

    [[nodiscard]] std::expected<void, common::audio::AudioDeviceSettingsError> openControlPanel()
        override
    {
        return {};
    }

    void addListener(Listener& /*listener*/) override
    {}
    void removeListener(Listener& /*listener*/) override
    {}

    int apply_call_count{0};
    std::optional<common::audio::AudioDeviceSettingsError> next_apply_error{};
};

// Test-local temp directory owning one test case's game settings
// file, mirroring test_game_settings.
class TemporarySettingsDirectory final
{
public:
    TemporarySettingsDirectory()
        : m_path(
              std::filesystem::temp_directory_path() /
              std::filesystem::path{
                  "rock-hero-native-audio-setup-test-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
              })
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~TemporarySettingsDirectory() noexcept
    {
        std::error_code cleanup_error;
        std::filesystem::remove_all(m_path, cleanup_error);
    }

    TemporarySettingsDirectory(const TemporarySettingsDirectory&) = delete;
    TemporarySettingsDirectory(TemporarySettingsDirectory&&) = delete;
    TemporarySettingsDirectory& operator=(const TemporarySettingsDirectory&) = delete;
    TemporarySettingsDirectory& operator=(TemporarySettingsDirectory&&) = delete;

    [[nodiscard]] std::filesystem::path settingsFile() const
    {
        return m_path / "game.settings";
    }

private:
    std::filesystem::path m_path;
};

// A complete slot-0 route the setup binds; the fake device configuration resolves it after apply.
[[nodiscard]] common::audio::InputDeviceIdentity guitarRoute()
{
    return common::audio::testing::makeInputDeviceIdentity("ASIO", "Focusrite USB ASIO");
}

// A steady, non-clipping strum level well above the usable-signal floor, so a fixed measurement
// window completes cleanly with a stable calibration gain.
[[nodiscard]] common::audio::AudioMeterLevel steadyStrumLevel()
{
    return common::audio::AudioMeterLevel{.peak_db = -12.0, .clipping = false};
}

// Wires the shared fakes and the driver over one in-memory store shared by the driver and monitor.
struct SetupHarness
{
    explicit SetupHarness(const std::filesystem::path& settings_file)
        : game_settings(settings_file)
        , monitor(live_input, device_configuration, store)
        , setup(device_settings, device_configuration, monitor, game_settings)
    {
        // After a successful apply the fake device resolves this route.
        device_configuration.current_input_identity = guitarRoute();
    }

    FakeAudioDeviceSettings device_settings;
    common::audio::testing::ConfigurableAudioDeviceConfiguration device_configuration;
    common::audio::testing::FakeLiveInput live_input;
    common::audio::testing::InMemoryAudioConfigStore store;
    GameSettings game_settings;
    common::audio::LiveInputMonitor monitor;
    NativeAudioSetup setup;
};

// Drives the metering loop to completion (or an error), feeding the steady strum level each sample.
[[nodiscard]] std::expected<common::audio::InputCalibrationProgress, NativeAudioSetupError>
runGainCalibration(SetupHarness& harness)
{
    harness.live_input.raw_input_meter_level = steadyStrumLevel();
    constexpr std::size_t longest_measurement = common::audio::inputCalibrationSettleSampleCount() +
                                                common::audio::inputCalibrationListenSampleCount();
    for (std::size_t sample = 0; sample < longest_measurement; ++sample)
    {
        auto progress = harness.setup.sampleGainCalibration();
        if (!progress.has_value() ||
            !std::holds_alternative<common::audio::InputCalibrationRunning>(*progress))
        {
            return progress;
        }
    }
    return std::unexpected{NativeAudioSetupError{
        NativeAudioSetupErrorCode::CalibrationFailed, "Calibration did not complete in the loop."
    }};
}

} // namespace

// The audible milestone: a scripted device-select + calibrate leaves the game store holding a
// device route and a matching calibration, and the shared gate arms to Active — proving a later
// GameplaySession Ready transition would make the live guitar audible through the tone.
TEST_CASE("Native setup reaches an armed store state", "[core][audio][setup]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;
    SetupHarness harness{directory.settingsFile()};

    // Device selection: the apply resolves the slot-0 route.
    harness.setup.beginDeviceSelection();
    REQUIRE(harness.setup.applySelectedDevice().has_value());
    CHECK(harness.setup.phase() == NativeAudioSetupPhase::CalibratingGain);

    // The resolved input identity becomes the slot-0 player-to-route mapping in game/core.
    const GameAudioConfig persisted = harness.game_settings.gameAudioConfig();
    REQUIRE(persisted.players.size() == 1);
    CHECK(persisted.players.front().player_slot == 0);
    CHECK(persisted.players.front().route == guitarRoute());

    // Gain calibration: metering the steady strum completes and commits.
    REQUIRE(harness.setup.beginGainCalibration(common::audio::PickupClass::Humbucker).has_value());
    const auto calibrated = runGainCalibration(harness);
    REQUIRE(calibrated.has_value());
    CHECK(std::holds_alternative<common::audio::InputCalibrationMeasured>(*calibrated));
    CHECK(harness.setup.phase() == NativeAudioSetupPhase::Ready);

    // The store now holds a calibration matching the active route — the armed state.
    const auto stored_calibration = harness.store.inputCalibrationFor(guitarRoute());
    REQUIRE(stored_calibration.has_value());
    const auto& calibration = *stored_calibration;
    REQUIRE(calibration.has_value());
    if (calibration.has_value())
    {
        CHECK(calibration->input_device_identity == guitarRoute());
    }

    // Capstone: with the route and matching calibration persisted, the shared calibrate-first gate
    // arms to Active for a ready session — the live guitar is audible.
    const common::audio::LiveInputMonitoringStatus status =
        harness.monitor.refresh(common::audio::LiveInputMonitoringContext{.session_ready = true});
    CHECK(status == common::audio::LiveInputMonitoringStatus::Active);
}

// A failed device apply is terminal and writes nothing to either store.
TEST_CASE("Native setup device-apply failure writes nothing", "[core][audio][setup]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;
    SetupHarness harness{directory.settingsFile()};

    harness.device_settings.next_apply_error = common::audio::AudioDeviceSettingsError{
        common::audio::AudioDeviceSettingsErrorCode::ApplyFailed
    };

    const auto applied = harness.setup.applySelectedDevice();
    REQUIRE(!applied.has_value());
    CHECK(applied.error().code == NativeAudioSetupErrorCode::DeviceApplyFailed);
    CHECK(harness.setup.phase() == NativeAudioSetupPhase::Failed);

    const auto failure = harness.setup.failure();
    REQUIRE(failure.has_value());
    if (failure.has_value())
    {
        CHECK(failure->code == NativeAudioSetupErrorCode::DeviceApplyFailed);
    }

    CHECK(harness.game_settings.gameAudioConfig().players.empty());
}

// An apply that resolves no usable mono input route is terminal and writes nothing.
TEST_CASE("Native setup unresolved route fails and writes nothing", "[core][audio][setup]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;
    SetupHarness harness{directory.settingsFile()};

    harness.device_configuration.current_input_identity = std::nullopt;

    const auto applied = harness.setup.applySelectedDevice();
    REQUIRE(!applied.has_value());
    CHECK(applied.error().code == NativeAudioSetupErrorCode::DeviceRouteUnresolved);
    CHECK(harness.setup.phase() == NativeAudioSetupPhase::Failed);
    CHECK(harness.game_settings.gameAudioConfig().players.empty());
}

// Canceling a measurement leaves the applied device route intact but writes no calibration.
TEST_CASE(
    "Native setup calibration abort keeps the device but no calibration", "[core][audio][setup]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;
    SetupHarness harness{directory.settingsFile()};

    REQUIRE(harness.setup.applySelectedDevice().has_value());
    REQUIRE(harness.setup.beginGainCalibration(common::audio::PickupClass::Humbucker).has_value());

    // Feed a couple of samples, then abort before the measurement window completes.
    harness.live_input.raw_input_meter_level = steadyStrumLevel();
    REQUIRE(harness.setup.sampleGainCalibration().has_value());
    REQUIRE(harness.setup.sampleGainCalibration().has_value());
    harness.setup.cancelGainCalibration();

    CHECK(harness.setup.phase() == NativeAudioSetupPhase::CalibratingGain);

    // No calibration was written for the applied route.
    const auto stored_calibration = harness.store.inputCalibrationFor(guitarRoute());
    REQUIRE(stored_calibration.has_value());
    CHECK(!stored_calibration->has_value());
}

// A measurement a device change ended reports that as a failed measurement the player can retry,
// never as a request the game should not have made.
TEST_CASE("Native setup reports a measurement a device change ended", "[core][audio][setup]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;
    SetupHarness harness{directory.settingsFile()};
    REQUIRE(harness.setup.applySelectedDevice().has_value());
    REQUIRE(harness.setup.beginGainCalibration(common::audio::PickupClass::Humbucker).has_value());
    harness.live_input.raw_input_meter_level = steadyStrumLevel();
    REQUIRE(harness.setup.sampleGainCalibration().has_value());

    // The device change re-runs the gate, which takes the route from the measurement.
    harness.monitor.refresh(common::audio::LiveInputMonitoringContext{.session_ready = false});
    const auto progress = harness.setup.sampleGainCalibration();

    REQUIRE(progress.has_value());
    if (progress.has_value())
    {
        CHECK(std::holds_alternative<common::audio::InputCalibrationFailed>(*progress));
    }
    CHECK(harness.setup.phase() == NativeAudioSetupPhase::CalibratingGain);
}

// Re-running the flow for a different device overwrites the route, player config, and calibration.
TEST_CASE("Native setup re-run overwrites the previous device cleanly", "[core][audio][setup]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;
    SetupHarness harness{directory.settingsFile()};

    // First device reaches Ready.
    REQUIRE(harness.setup.applySelectedDevice().has_value());
    REQUIRE(harness.setup.beginGainCalibration(common::audio::PickupClass::Humbucker).has_value());
    REQUIRE(runGainCalibration(harness).has_value());
    REQUIRE(harness.setup.phase() == NativeAudioSetupPhase::Ready);

    // Re-run for a second device on a different channel.
    common::audio::InputDeviceIdentity second_route = guitarRoute();
    second_route.input_device_name = "Behringer UMC ASIO";
    second_route.input_channel_index = 1;
    harness.device_configuration.current_input_identity = second_route;

    REQUIRE(harness.setup.applySelectedDevice().has_value());
    CHECK(harness.setup.phase() == NativeAudioSetupPhase::CalibratingGain);

    const GameAudioConfig persisted = harness.game_settings.gameAudioConfig();
    REQUIRE(persisted.players.size() == 1);
    CHECK(persisted.players.front().route == second_route);

    REQUIRE(harness.setup.beginGainCalibration(common::audio::PickupClass::Humbucker).has_value());
    REQUIRE(runGainCalibration(harness).has_value());
    CHECK(harness.setup.phase() == NativeAudioSetupPhase::Ready);

    const auto stored_calibration = harness.store.inputCalibrationFor(second_route);
    REQUIRE(stored_calibration.has_value());
    const auto& calibration = *stored_calibration;
    REQUIRE(calibration.has_value());
    if (calibration.has_value())
    {
        CHECK(calibration->input_device_identity == second_route);
    }
}

// Gain calibration is illegal before a device is applied.
TEST_CASE("Native setup rejects calibration before a device is applied", "[core][audio][setup]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;
    SetupHarness harness{directory.settingsFile()};

    const auto began = harness.setup.beginGainCalibration(common::audio::PickupClass::Humbucker);
    REQUIRE(!began.has_value());
    CHECK(began.error().code == NativeAudioSetupErrorCode::InvalidRequest);
    CHECK(harness.setup.phase() == NativeAudioSetupPhase::Idle);
}

// The pure machine enforces the legal phase progression independent of any audio ports.
TEST_CASE("Native setup machine enforces the legal progression", "[core][audio][setup]")
{
    NativeAudioSetupMachine machine;
    CHECK(machine.phase() == NativeAudioSetupPhase::Idle);
    CHECK(machine.canApplyDevice());
    CHECK(!machine.canCalibrate());

    machine.beginDeviceSelection();
    CHECK(machine.phase() == NativeAudioSetupPhase::SelectingDevice);

    machine.deviceApplied();
    CHECK(machine.phase() == NativeAudioSetupPhase::CalibratingGain);
    // No new device apply while a measurement is in progress.
    CHECK(!machine.canApplyDevice());
    CHECK(machine.canCalibrate());

    machine.calibrationCommitted();
    CHECK(machine.phase() == NativeAudioSetupPhase::Ready);
    CHECK(machine.canApplyDevice());

    machine.fail(NativeAudioSetupError{NativeAudioSetupErrorCode::DeviceApplyFailed});
    CHECK(machine.phase() == NativeAudioSetupPhase::Failed);
    REQUIRE(machine.failure().has_value());
    if (machine.failure().has_value())
    {
        CHECK(machine.failure()->code == NativeAudioSetupErrorCode::DeviceApplyFailed);
    }

    // A retry after failure clears the terminal error.
    machine.beginDeviceSelection();
    CHECK(machine.phase() == NativeAudioSetupPhase::SelectingDevice);
    CHECK(!machine.failure().has_value());
}

} // namespace rock_hero::game::core
