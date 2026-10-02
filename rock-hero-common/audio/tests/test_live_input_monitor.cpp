#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/input/live_input_monitor_error.h>
#include <rock_hero/common/audio/settings/audio_config_error.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/testing/configurable_audio_device_configuration.h>
#include <rock_hero/common/audio/testing/fake_live_input.h>
#include <rock_hero/common/audio/testing/in_memory_audio_config_store.h>
#include <rock_hero/common/audio/testing/input_device_identity_fixtures.h>
#include <variant>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

using testing::ConfigurableAudioDeviceConfiguration;
using testing::FakeLiveInput;
using testing::InMemoryAudioConfigStore;
using testing::LiveInputSetterCall;
using testing::makeInputDeviceIdentity;
using testing::setCalibrationInputMonitoringCall;
using testing::setInputGainCall;
using testing::setLiveInputMonitoringCall;

constexpr LiveInputMonitoringContext g_ready{.session_ready = true};
constexpr LiveInputMonitoringContext g_not_ready{.session_ready = false};

[[nodiscard]] InputCalibrationState makeCalibration(
    const InputDeviceIdentity& identity, double gain_db)
{
    return InputCalibrationState{
        .calibration_gain = Gain{gain_db},
        .input_device_identity = identity,
    };
}

[[nodiscard]] LiveInputError routeUnavailable()
{
    return LiveInputError{LiveInputErrorCode::InputRouteUnavailable, "route gone"};
}

// One monitor over fakes, with the current route optionally calibrated in the store.
struct Harness
{
    explicit Harness(std::optional<double> stored_gain_db = std::nullopt)
    {
        devices.current_input_identity = route;
        if (stored_gain_db.has_value())
        {
            store.input_calibrations.push_back(makeCalibration(route, *stored_gain_db));
        }
    }

    InputDeviceIdentity route{makeInputDeviceIdentity()};
    FakeLiveInput live_input;
    ConfigurableAudioDeviceConfiguration devices;
    InMemoryAudioConfigStore store;
    LiveInputMonitor monitor{live_input, devices, store};
};

// Samples a measurement at a steady raw level until it ends, as a driver's timer would.
[[nodiscard]] InputCalibrationProgress runMeasurement(Harness& harness, double peak_db)
{
    harness.live_input.raw_input_meter_level = AudioMeterLevel{.peak_db = peak_db};
    constexpr std::size_t longest_measurement =
        inputCalibrationSettleSampleCount() + inputCalibrationListenSampleCount();
    for (std::size_t sample = 0; sample < longest_measurement; ++sample)
    {
        const LiveInputSample reading = harness.monitor.sample(g_ready);
        if (!reading.measurement.has_value())
        {
            return InputCalibrationFailed{"The measurement was not running."};
        }
        if (!std::holds_alternative<InputCalibrationRunning>(*reading.measurement))
        {
            return *reading.measurement;
        }
    }
    return InputCalibrationFailed{"The measurement did not finish."};
}

} // namespace

// With no input route the gate turns both monitoring paths off.
TEST_CASE("LiveInputMonitor gate disables with no input device", "[audio][live-input]")
{
    Harness harness;
    harness.devices.current_input_identity.reset();

    const LiveInputMonitoringStatus status = harness.monitor.refresh(g_ready);

    CHECK(status == LiveInputMonitoringStatus::NoInputDevice);
    CHECK(
        harness.live_input.calls == std::vector<LiveInputSetterCall>{
                                        setCalibrationInputMonitoringCall(false),
                                        setLiveInputMonitoringCall(false),
                                    });
}

// A session that does not allow monitoring stays off, but the route's calibration is still read
// so the status can show it.
TEST_CASE(
    "LiveInputMonitor reads calibration while the session is not ready", "[audio][live-input]")
{
    Harness harness{4.0};

    const LiveInputMonitoringStatus status = harness.monitor.refresh(g_not_ready);

    CHECK(status == LiveInputMonitoringStatus::SessionNotReady);
    CHECK(harness.monitor.calibration().has_value());
    CHECK_FALSE(harness.live_input.live_input_monitoring_enabled);
}

// An uncalibrated route never reaches the live output: the editor never plays the rig uncalibrated.
TEST_CASE("LiveInputMonitor keeps an uncalibrated route silent", "[audio][live-input]")
{
    Harness harness;

    const LiveInputMonitoringStatus status = harness.monitor.refresh(g_ready);

    CHECK(status == LiveInputMonitoringStatus::MissingCalibration);
    CHECK_FALSE(harness.monitor.calibration().has_value());
    CHECK_FALSE(harness.live_input.live_input_monitoring_enabled);
}

// A calibrated route applies its stored gain, then turns monitoring on.
TEST_CASE("LiveInputMonitor arms a calibrated route", "[audio][live-input]")
{
    Harness harness{3.1};

    const LiveInputMonitoringStatus status = harness.monitor.refresh(g_ready);

    CHECK(status == LiveInputMonitoringStatus::Active);
    CHECK(
        harness.live_input.calls == std::vector<LiveInputSetterCall>{
                                        setCalibrationInputMonitoringCall(false),
                                        setInputGainCall(3.1),
                                        setLiveInputMonitoringCall(true),
                                    });
}

// The store is the authority: a calibration the other product saved is used at the next refresh.
TEST_CASE("LiveInputMonitor picks up a calibration saved elsewhere", "[audio][live-input]")
{
    Harness harness;
    REQUIRE(harness.monitor.refresh(g_ready) == LiveInputMonitoringStatus::MissingCalibration);

    harness.store.input_calibrations.push_back(makeCalibration(harness.route, 2.0));

    CHECK(harness.monitor.refresh(g_ready) == LiveInputMonitoringStatus::Active);
    CHECK_THAT(harness.live_input.current_input_gain.db, Catch::Matchers::WithinULP(2.0, 0));
}

// An unreadable store reports itself and keeps monitoring off.
TEST_CASE("LiveInputMonitor reports an unreadable calibration store", "[audio][live-input]")
{
    Harness harness{3.0};
    harness.store.next_input_calibration_for_error =
        AudioConfigError{AudioConfigErrorCode::InvalidInputCalibrationHistory, "corrupt"};

    const LiveInputMonitoringStatus status = harness.monitor.refresh(g_ready);

    CHECK(status == LiveInputMonitoringStatus::CalibrationStoreUnavailable);
    CHECK_FALSE(harness.live_input.live_input_monitoring_enabled);
}

// A backend that refuses the calibrated gain leaves monitoring off and says so.
TEST_CASE("LiveInputMonitor reports a backend that refuses the route", "[audio][live-input]")
{
    Harness harness{3.0};
    harness.live_input.next_set_input_gain_error = routeUnavailable();

    const LiveInputMonitoringStatus status = harness.monitor.refresh(g_ready);

    CHECK(status == LiveInputMonitoringStatus::BackendUnavailable);
    CHECK_FALSE(harness.live_input.live_input_monitoring_enabled);
}

// A measurement hears the raw route: processed monitoring off, unity gain, calibration path on.
TEST_CASE("LiveInputMonitor measures the raw route at unity gain", "[audio][live-input]")
{
    Harness harness{3.0};
    harness.monitor.refresh(g_ready);
    harness.live_input.calls.clear();

    REQUIRE(harness.monitor.beginMeasurement(PickupClass::Humbucker, g_ready).has_value());

    CHECK(harness.monitor.status() == LiveInputMonitoringStatus::Measuring);
    CHECK(
        harness.live_input.calls == std::vector<LiveInputSetterCall>{
                                        setLiveInputMonitoringCall(false),
                                        setInputGainCall(defaultGainDb()),
                                        setCalibrationInputMonitoringCall(true),
                                    });
}

// Without an input route there is nothing to measure.
TEST_CASE("LiveInputMonitor refuses a measurement without a route", "[audio][live-input]")
{
    Harness harness;
    harness.devices.current_input_identity.reset();

    const auto began = harness.monitor.beginMeasurement(PickupClass::Humbucker, g_ready);

    REQUIRE_FALSE(began.has_value());
    CHECK(began.error().code == LiveInputMonitorErrorCode::InvalidRequest);
    CHECK(harness.monitor.status() != LiveInputMonitoringStatus::Measuring);
}

// A refused measurement start hands the route back to the gate, which restores the stored gain.
TEST_CASE("LiveInputMonitor recovers a refused measurement start", "[audio][live-input]")
{
    Harness harness{3.0};
    harness.monitor.refresh(g_ready);
    harness.live_input.next_set_calibration_input_monitoring_error = routeUnavailable();

    const auto began = harness.monitor.beginMeasurement(PickupClass::Humbucker, g_ready);

    REQUIRE_FALSE(began.has_value());
    CHECK(began.error().code == LiveInputMonitorErrorCode::BackendRejected);
    CHECK(harness.monitor.status() != LiveInputMonitoringStatus::Measuring);
    CHECK(harness.monitor.status() == LiveInputMonitoringStatus::Active);
    CHECK_THAT(harness.live_input.current_input_gain.db, Catch::Matchers::WithinULP(3.0, 0));
}

// Cancelling a measurement restores the stored calibration with no rollback of its own.
TEST_CASE("LiveInputMonitor cancel restores the stored calibration", "[audio][live-input]")
{
    Harness harness{3.0};
    harness.monitor.refresh(g_ready);
    REQUIRE(harness.monitor.beginMeasurement(PickupClass::Humbucker, g_ready).has_value());

    harness.monitor.cancelMeasurement(g_ready);

    CHECK(harness.monitor.status() != LiveInputMonitoringStatus::Measuring);
    // A measurement its driver ended has nothing to report.
    CHECK_FALSE(harness.monitor.sample(g_ready).measurement.has_value());
    CHECK(harness.live_input.live_input_monitoring_enabled);
    CHECK_FALSE(harness.live_input.calibration_input_monitoring_enabled);
    CHECK_THAT(harness.live_input.current_input_gain.db, Catch::Matchers::WithinULP(3.0, 0));
}

// A finished measurement reports its gain and pickups, stores nothing and hands the route back;
// committing the reported gain then stores it for the route and arms it.
TEST_CASE("LiveInputMonitor reports a finished measurement", "[audio][live-input]")
{
    Harness harness;
    REQUIRE(harness.monitor.beginMeasurement(PickupClass::Humbucker, g_ready).has_value());

    const InputCalibrationProgress progress = runMeasurement(harness, -19.5);

    const auto* const measured = std::get_if<InputCalibrationMeasured>(&progress);
    REQUIRE(measured != nullptr);
    CHECK_THAT(measured->gain.db, Catch::Matchers::WithinULP(12.7, 0));
    CHECK(measured->pickups == PickupClass::Humbucker);
    CHECK(harness.store.input_calibrations.empty());
    CHECK(harness.monitor.status() != LiveInputMonitoringStatus::Measuring);
    CHECK_FALSE(harness.live_input.calibration_input_monitoring_enabled);

    REQUIRE(harness.monitor.commitCalibration(measured->gain.db, g_ready).has_value());
    REQUIRE(harness.store.input_calibrations.size() == 1);
    CHECK(harness.store.input_calibrations.front().input_device_identity == harness.route);
    CHECK(harness.monitor.status() == LiveInputMonitoringStatus::Active);
    CHECK_THAT(harness.live_input.current_input_gain.db, Catch::Matchers::WithinULP(12.7, 0));
}

// The same playing measured as single-coil pickups stores a gain 6 dB higher than as humbuckers:
// a single coil's hard strum is taken to be half the voltage.
TEST_CASE("LiveInputMonitor measures against the stated pickups", "[audio][live-input]")
{
    Harness single_coil_harness;
    REQUIRE(
        single_coil_harness.monitor.beginMeasurement(PickupClass::SingleCoil, g_ready).has_value());

    const InputCalibrationProgress progress = runMeasurement(single_coil_harness, -19.5);

    const auto* const measured = std::get_if<InputCalibrationMeasured>(&progress);
    REQUIRE(measured != nullptr);
    CHECK_THAT(measured->gain.db, Catch::Matchers::WithinULP(6.7, 0));
    CHECK(measured->pickups == PickupClass::SingleCoil);
}

// A measurement that fails (here, on a clipped input) hands the route back to the stored
// calibration.
TEST_CASE("LiveInputMonitor hands the route back after a failed measurement", "[audio][live-input]")
{
    Harness harness{3.0};
    harness.monitor.refresh(g_ready);
    REQUIRE(harness.monitor.beginMeasurement(PickupClass::Humbucker, g_ready).has_value());

    const InputCalibrationProgress progress = runMeasurement(harness, clippingAudioMeterDb());

    CHECK(std::holds_alternative<InputCalibrationFailed>(progress));
    CHECK(harness.monitor.status() == LiveInputMonitoringStatus::Active);
    CHECK_THAT(harness.live_input.current_input_gain.db, Catch::Matchers::WithinULP(3.0, 0));
}

// With no measurement running, a sample is only a meter reading.
TEST_CASE("LiveInputMonitor samples the meter without a measurement", "[audio][live-input]")
{
    Harness harness;
    harness.live_input.raw_input_meter_level = AudioMeterLevel{.peak_db = -18.0};

    const LiveInputSample sample = harness.monitor.sample(g_ready);

    CHECK_THAT(sample.raw_level.peak_db, Catch::Matchers::WithinULP(-18.0, 0));
    CHECK_FALSE(sample.measurement.has_value());
    CHECK(harness.store.input_calibrations.empty());
}

// A typed gain calibrates the current route.
TEST_CASE("LiveInputMonitor commits a manual calibration", "[audio][live-input]")
{
    Harness harness;

    REQUIRE(harness.monitor.commitCalibration(3.1, g_ready).has_value());

    REQUIRE(harness.store.input_calibrations.size() == 1);
    CHECK_THAT(
        harness.store.input_calibrations.front().calibration_gain.db,
        Catch::Matchers::WithinULP(3.1, 0));
    CHECK(harness.monitor.status() == LiveInputMonitoringStatus::Active);
}

// A failed store write keeps the old calibration in force.
TEST_CASE("LiveInputMonitor keeps the stored calibration when a save fails", "[audio][live-input]")
{
    Harness harness{3.0};
    harness.monitor.refresh(g_ready);
    harness.store.next_save_input_calibration_error =
        AudioConfigError{AudioConfigErrorCode::CouldNotSave, "disk full"};

    const auto committed = harness.monitor.commitCalibration(8.0, g_ready);

    REQUIRE_FALSE(committed.has_value());
    CHECK(committed.error().code == LiveInputMonitorErrorCode::CalibrationStoreUnavailable);
    CHECK(harness.monitor.status() == LiveInputMonitoringStatus::Active);
    CHECK_THAT(harness.live_input.current_input_gain.db, Catch::Matchers::WithinULP(3.0, 0));
}

// A saved calibration is committed even when the backend then refuses the route: it is a fact
// about the route, and the refusal is gate state that status() reports.
TEST_CASE("LiveInputMonitor commits a calibration the backend refuses", "[audio][live-input]")
{
    Harness harness;
    harness.live_input.next_set_input_gain_error = routeUnavailable();

    REQUIRE(harness.monitor.commitCalibration(4.0, g_ready).has_value());

    CHECK(harness.store.input_calibrations.size() == 1);
    CHECK(harness.monitor.status() == LiveInputMonitoringStatus::BackendUnavailable);
}

// A session that stops allowing monitoring turns both paths off and ends a measurement.
TEST_CASE("LiveInputMonitor session close ends monitoring and measurement", "[audio][live-input]")
{
    Harness harness{3.0};
    harness.monitor.refresh(g_ready);
    REQUIRE(harness.monitor.beginMeasurement(PickupClass::Humbucker, g_ready).has_value());

    harness.monitor.refresh(g_not_ready);

    CHECK(harness.monitor.status() != LiveInputMonitoringStatus::Measuring);
    // A driver still sampling hears once that the measurement was ended, then nothing.
    const LiveInputSample ended = harness.monitor.sample(g_not_ready);
    REQUIRE(ended.measurement.has_value());
    if (ended.measurement.has_value())
    {
        CHECK(std::holds_alternative<InputCalibrationFailed>(*ended.measurement));
    }
    CHECK_FALSE(harness.monitor.sample(g_not_ready).measurement.has_value());
    CHECK_FALSE(harness.live_input.live_input_monitoring_enabled);
    CHECK_FALSE(harness.live_input.calibration_input_monitoring_enabled);
    CHECK(harness.monitor.status() == LiveInputMonitoringStatus::SessionNotReady);
}

} // namespace rock_hero::common::audio
