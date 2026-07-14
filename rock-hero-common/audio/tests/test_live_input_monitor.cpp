#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <juce_audio_devices/juce_audio_devices.h>
#include <optional>
#include <rock_hero/common/audio/device/audio_device_status.h>
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/input/live_input_monitor_error.h>
#include <rock_hero/common/audio/settings/audio_config_error.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/testing/configurable_audio_device_configuration.h>
#include <rock_hero/common/audio/testing/fake_live_input.h>
#include <rock_hero/common/audio/testing/in_memory_audio_config_store.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

using testing::ConfigurableAudioDeviceConfiguration;
using testing::FakeLiveInput;
using testing::InMemoryAudioConfigStore;
using testing::LiveInputSetterCall;
using testing::setCalibrationInputMonitoringCall;
using testing::setInputGainCall;
using testing::setLiveInputMonitoringCall;

constexpr LiveInputMonitoringContext g_ready{.live_input_ready = true, .arrangement_loaded = true};

[[nodiscard]] InputDeviceIdentity makeIdentity(std::string device = "Interface A", int channel = 0)
{
    return InputDeviceIdentity{
        .backend_name = "ASIO",
        .input_device_name = std::move(device),
        .input_channel_index = channel,
        .input_channel_name = "Input " + std::to_string(channel + 1),
    };
}

[[nodiscard]] InputCalibrationState makeCalibration(
    const InputDeviceIdentity& identity, double gain_db)
{
    return InputCalibrationState{
        .calibration_gain = Gain{gain_db},
        .input_device_identity = identity,
    };
}

// Device-config fake that counts identity reads so a test can pin single-sample-per-operation.
class CountingDeviceConfiguration final : public IAudioDeviceConfiguration
{
public:
    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept override
    {
        return device_manager;
    }

    [[nodiscard]] std::expected<DeviceRestoreOutcome, AudioDeviceConfigurationError>
    restoreSerializedDeviceState(const std::string&) override
    {
        return DeviceRestoreOutcome::Opened;
    }

    [[nodiscard]] std::optional<std::string> serializedDeviceState() const override
    {
        return std::nullopt;
    }

    [[nodiscard]] bool deviceStateMatchesActive(const std::string&) const override
    {
        return false;
    }

    [[nodiscard]] AudioDeviceStatus currentDeviceStatus() const override
    {
        return {};
    }

    [[nodiscard]] std::optional<InputDeviceIdentity> currentInputDeviceIdentity() const override
    {
        identity_read_count += 1;
        return identity;
    }

    void addListener(Listener&) override
    {}

    void removeListener(Listener&) override
    {}

    juce::AudioDeviceManager device_manager{};
    std::optional<InputDeviceIdentity> identity{};
    mutable int identity_read_count{0};
};

} // namespace

// A ready session with no input route disables monitoring after the ordered preamble teardown.
TEST_CASE("LiveInputMonitor gate disables with no input device", "[common][audio][live-input]")
{
    FakeLiveInput live_input;
    ConfigurableAudioDeviceConfiguration devices;
    InMemoryAudioConfigStore store;
    LiveInputMonitor monitor{live_input, devices, store};

    const LiveInputMonitoringStatus status = monitor.applyGate(g_ready);

    CHECK(status.state == LiveInputMonitoringState::Disabled);
    CHECK(status.reason == LiveInputMonitoringDisabledReason::NoInputDevice);
    CHECK(
        live_input.calls == std::vector<LiveInputSetterCall>{
                                setCalibrationInputMonitoringCall(false),
                                setLiveInputMonitoringCall(false),
                            });
    CHECK_FALSE(live_input.live_input_monitoring_enabled);
}

// An unready session reports SessionNotReady before the route or calibration is inspected.
TEST_CASE("LiveInputMonitor gate disables when session not ready", "[common][audio][live-input]")
{
    FakeLiveInput live_input;
    ConfigurableAudioDeviceConfiguration devices;
    devices.current_input_identity = makeIdentity();
    InMemoryAudioConfigStore store;
    LiveInputMonitor monitor{live_input, devices, store};

    const LiveInputMonitoringStatus status =
        monitor.applyGate({.live_input_ready = false, .arrangement_loaded = true});

    CHECK(status.reason == LiveInputMonitoringDisabledReason::SessionNotReady);
}

// A ready route with no stored calibration reports MissingCalibration and stays disabled.
TEST_CASE("LiveInputMonitor gate disables without calibration", "[common][audio][live-input]")
{
    FakeLiveInput live_input;
    ConfigurableAudioDeviceConfiguration devices;
    devices.current_input_identity = makeIdentity();
    InMemoryAudioConfigStore store;
    LiveInputMonitor monitor{live_input, devices, store};

    const LiveInputMonitoringStatus status = monitor.applyGate(g_ready);

    CHECK(status.reason == LiveInputMonitoringDisabledReason::MissingCalibration);
    CHECK_FALSE(live_input.live_input_monitoring_enabled);
}

// refresh re-reads the store, finds the matching calibration, and arms in the pinned order.
TEST_CASE("LiveInputMonitor refresh arms matching route in order", "[common][audio][live-input]")
{
    const InputDeviceIdentity identity = makeIdentity();
    FakeLiveInput live_input;
    ConfigurableAudioDeviceConfiguration devices;
    devices.current_input_identity = identity;
    InMemoryAudioConfigStore store;
    REQUIRE(store.saveInputCalibration(makeCalibration(identity, 5.0)).has_value());
    LiveInputMonitor monitor{live_input, devices, store};

    const LiveInputMonitoringStatus status = monitor.refresh(g_ready);

    CHECK(status.state == LiveInputMonitoringState::Active);
    CHECK(status.reason == LiveInputMonitoringDisabledReason::None);
    CHECK(
        live_input.calls == std::vector<LiveInputSetterCall>{
                                setCalibrationInputMonitoringCall(false),
                                setInputGainCall(5.0),
                                setLiveInputMonitoringCall(true),
                            });
    CHECK(live_input.live_input_monitoring_enabled);
    CHECK_THAT(live_input.current_input_gain.db, Catch::Matchers::WithinULP(5.0, 0));
    CHECK(monitor.backendAvailable());
}

// A corrupt store read surfaces CalibrationStoreUnavailable and does not arm any monitoring.
TEST_CASE(
    "LiveInputMonitor refresh surfaces corrupt store and does not arm",
    "[common][audio][live-input]")
{
    const InputDeviceIdentity identity = makeIdentity();
    FakeLiveInput live_input;
    ConfigurableAudioDeviceConfiguration devices;
    devices.current_input_identity = identity;
    InMemoryAudioConfigStore store;
    REQUIRE(store.saveInputCalibration(makeCalibration(identity, 5.0)).has_value());
    store.next_input_calibration_for_error =
        AudioConfigError{AudioConfigErrorCode::InvalidInputCalibrationHistory, "corrupt history"};
    LiveInputMonitor monitor{live_input, devices, store};

    const LiveInputMonitoringStatus status = monitor.refresh(g_ready);

    CHECK(status.state == LiveInputMonitoringState::Disabled);
    CHECK(status.reason == LiveInputMonitoringDisabledReason::CalibrationStoreUnavailable);
    CHECK(
        monitor.status().reason == LiveInputMonitoringDisabledReason::CalibrationStoreUnavailable);
    CHECK(live_input.calls.empty());
    CHECK_FALSE(live_input.live_input_monitoring_enabled);
}

// A route-unavailable gain rejection during arming rolls the gate into BackendUnavailable.
TEST_CASE("LiveInputMonitor gate rolls back on gain failure", "[common][audio][live-input]")
{
    const InputDeviceIdentity identity = makeIdentity();
    FakeLiveInput live_input;
    ConfigurableAudioDeviceConfiguration devices;
    devices.current_input_identity = identity;
    InMemoryAudioConfigStore store;
    REQUIRE(store.saveInputCalibration(makeCalibration(identity, 5.0)).has_value());
    live_input.next_set_input_gain_error =
        LiveInputError{LiveInputErrorCode::InputRouteUnavailable, "route gone"};
    LiveInputMonitor monitor{live_input, devices, store};

    const LiveInputMonitoringStatus status = monitor.refresh(g_ready);

    CHECK(status.reason == LiveInputMonitoringDisabledReason::BackendUnavailable);
    CHECK(
        live_input.calls == std::vector<LiveInputSetterCall>{
                                setCalibrationInputMonitoringCall(false),
                                setInputGainCall(5.0),
                                setLiveInputMonitoringCall(false),
                            });
    CHECK_FALSE(live_input.live_input_monitoring_enabled);
    CHECK_FALSE(monitor.backendAvailable());
}

// A route-unavailable monitoring rejection after gain succeeds still disables and marks the backend.
TEST_CASE("LiveInputMonitor gate rolls back on enable failure", "[common][audio][live-input]")
{
    const InputDeviceIdentity identity = makeIdentity();
    FakeLiveInput live_input;
    ConfigurableAudioDeviceConfiguration devices;
    devices.current_input_identity = identity;
    InMemoryAudioConfigStore store;
    REQUIRE(store.saveInputCalibration(makeCalibration(identity, 5.0)).has_value());
    live_input.next_set_live_input_monitoring_error =
        LiveInputError{LiveInputErrorCode::InputRouteUnavailable, "route gone"};
    LiveInputMonitor monitor{live_input, devices, store};

    const LiveInputMonitoringStatus status = monitor.refresh(g_ready);

    CHECK(status.reason == LiveInputMonitoringDisabledReason::BackendUnavailable);
    CHECK(
        live_input.calls == std::vector<LiveInputSetterCall>{
                                setCalibrationInputMonitoringCall(false),
                                setInputGainCall(5.0),
                                setLiveInputMonitoringCall(true),
                                setLiveInputMonitoringCall(false),
                            });
    CHECK_FALSE(live_input.live_input_monitoring_enabled);
}

// disableMonitoring tears down both the calibration and processed monitoring paths.
TEST_CASE("LiveInputMonitor disableMonitoring tears down both paths", "[common][audio][live-input]")
{
    FakeLiveInput live_input;
    ConfigurableAudioDeviceConfiguration devices;
    InMemoryAudioConfigStore store;
    LiveInputMonitor monitor{live_input, devices, store};

    monitor.disableMonitoring();

    CHECK(
        live_input.calls == std::vector<LiveInputSetterCall>{
                                setCalibrationInputMonitoringCall(false),
                                setLiveInputMonitoringCall(false),
                            });
    CHECK(monitor.status().state == LiveInputMonitoringState::Disabled);
    CHECK(monitor.status().reason == LiveInputMonitoringDisabledReason::SessionNotReady);
}

// Measurement start rolls back the captured route when the neutral-gain reset is rejected.
TEST_CASE(
    "LiveInputMonitor measurement start rolls back on gain failure", "[common][audio][live-input]")
{
    const InputDeviceIdentity identity = makeIdentity();
    FakeLiveInput live_input;
    ConfigurableAudioDeviceConfiguration devices;
    devices.current_input_identity = identity;
    InMemoryAudioConfigStore store;
    LiveInputMonitor monitor{live_input, devices, store};
    REQUIRE(monitor.requestPrompt(g_ready));

    live_input.current_input_gain = Gain{4.0};
    live_input.live_input_monitoring_enabled = false;
    live_input.calibration_input_monitoring_enabled = false;
    live_input.next_set_input_gain_error =
        LiveInputError{LiveInputErrorCode::CouldNotSetInputGain, "gain reset failed"};
    live_input.calls.clear();

    const auto started = monitor.beginMeasurement(g_ready);

    REQUIRE_FALSE(started.has_value());
    CHECK(started.error().code == LiveInputMonitorErrorCode::BackendRejected);
    CHECK(
        live_input.calls == std::vector<LiveInputSetterCall>{
                                setLiveInputMonitoringCall(false),
                                setInputGainCall(0.0),
                                setCalibrationInputMonitoringCall(false),
                                setInputGainCall(4.0),
                                setLiveInputMonitoringCall(false),
                            });
    CHECK_THAT(live_input.current_input_gain.db, Catch::Matchers::WithinULP(4.0, 0));
}

// A completed measurement commit disables audition, applies the gain, enables monitoring, and
// persists the calibration through the store.
TEST_CASE(
    "LiveInputMonitor commit applies gain and persists calibration", "[common][audio][live-input]")
{
    const InputDeviceIdentity identity = makeIdentity();
    FakeLiveInput live_input;
    ConfigurableAudioDeviceConfiguration devices;
    devices.current_input_identity = identity;
    InMemoryAudioConfigStore store;
    LiveInputMonitor monitor{live_input, devices, store};
    REQUIRE(monitor.requestPrompt(g_ready));
    REQUIRE(monitor.beginMeasurement(g_ready).has_value());

    live_input.calls.clear();
    const auto committed = monitor.commitCalibration(7.5, std::nullopt);

    REQUIRE(committed.has_value());
    CHECK(
        live_input.calls == std::vector<LiveInputSetterCall>{
                                setCalibrationInputMonitoringCall(false),
                                setInputGainCall(7.5),
                                setLiveInputMonitoringCall(true),
                            });
    CHECK(live_input.live_input_monitoring_enabled);
    CHECK_THAT(live_input.current_input_gain.db, Catch::Matchers::WithinULP(7.5, 0));

    const auto stored = store.inputCalibrationFor(identity);
    REQUIRE(stored.has_value());
    REQUIRE(stored->has_value());
    CHECK_THAT((*stored)->calibration_gain.db, Catch::Matchers::WithinULP(7.5, 0));
}

// A store write failure at commit surfaces the failure only through logging, not a hard error, and
// the workflow keeps the committed calibration.
TEST_CASE("LiveInputMonitor commit tolerates a store write failure", "[common][audio][live-input]")
{
    const InputDeviceIdentity identity = makeIdentity();
    FakeLiveInput live_input;
    ConfigurableAudioDeviceConfiguration devices;
    devices.current_input_identity = identity;
    InMemoryAudioConfigStore store;
    store.next_save_input_calibration_error =
        AudioConfigError{AudioConfigErrorCode::CouldNotSave, "disk full"};
    LiveInputMonitor monitor{live_input, devices, store};
    REQUIRE(monitor.requestPrompt(g_ready));
    REQUIRE(monitor.beginMeasurement(g_ready).has_value());

    const auto committed = monitor.commitCalibration(7.5, std::nullopt);

    REQUIRE(committed.has_value());
    CHECK(live_input.live_input_monitoring_enabled);
    REQUIRE(monitor.activeCalibrationState().has_value());
    CHECK_THAT(
        monitor.activeCalibrationState()->calibration_gain.db, Catch::Matchers::WithinULP(7.5, 0));
}

// The input identity is sampled exactly once per commit operation, so a route that changes between
// the expected-identity check and the plan build cannot be observed mid-operation.
TEST_CASE(
    "LiveInputMonitor samples the input identity once per operation", "[common][audio][live-input]")
{
    const InputDeviceIdentity identity = makeIdentity();
    FakeLiveInput live_input;
    CountingDeviceConfiguration devices;
    devices.identity = identity;
    InMemoryAudioConfigStore store;
    LiveInputMonitor monitor{live_input, devices, store};
    REQUIRE(monitor.requestPrompt(g_ready));
    REQUIRE(monitor.beginMeasurement(g_ready).has_value());

    const int reads_before = devices.identity_read_count;
    const auto committed = monitor.commitCalibration(7.5, identity);

    REQUIRE(committed.has_value());
    CHECK(devices.identity_read_count - reads_before == 1);
}

} // namespace rock_hero::common::audio
