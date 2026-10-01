#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
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

    REQUIRE(harness.monitor.beginMeasurement(g_ready).has_value());

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

    const auto began = harness.monitor.beginMeasurement(g_ready);

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

    const auto began = harness.monitor.beginMeasurement(g_ready);

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
    REQUIRE(harness.monitor.beginMeasurement(g_ready).has_value());

    harness.monitor.cancelMeasurement(g_ready);

    CHECK(harness.monitor.status() != LiveInputMonitoringStatus::Measuring);
    CHECK(harness.live_input.live_input_monitoring_enabled);
    CHECK_FALSE(harness.live_input.calibration_input_monitoring_enabled);
    CHECK_THAT(harness.live_input.current_input_gain.db, Catch::Matchers::WithinULP(3.0, 0));
}

// A measured gain is stored for the measured route and armed.
TEST_CASE("LiveInputMonitor commits a measurement", "[audio][live-input]")
{
    Harness harness;
    REQUIRE(harness.monitor.beginMeasurement(g_ready).has_value());

    REQUIRE(harness.monitor.commitMeasurement(6.5, g_ready).has_value());

    CHECK(harness.monitor.status() != LiveInputMonitoringStatus::Measuring);
    REQUIRE(harness.store.input_calibrations.size() == 1);
    CHECK(harness.store.input_calibrations.front().input_device_identity == harness.route);
    CHECK(harness.monitor.status() == LiveInputMonitoringStatus::Active);
    CHECK_THAT(harness.live_input.current_input_gain.db, Catch::Matchers::WithinULP(6.5, 0));
}

// A measurement can never calibrate a route it did not measure.
TEST_CASE("LiveInputMonitor refuses a measurement whose route changed", "[audio][live-input]")
{
    Harness harness;
    REQUIRE(harness.monitor.beginMeasurement(g_ready).has_value());
    harness.devices.current_input_identity = makeInputDeviceIdentity("ASIO", "Interface B");

    const auto committed = harness.monitor.commitMeasurement(6.5, g_ready);

    REQUIRE_FALSE(committed.has_value());
    CHECK(committed.error().code == LiveInputMonitorErrorCode::InvalidRequest);
    CHECK(harness.store.input_calibrations.empty());
    CHECK(harness.monitor.status() != LiveInputMonitoringStatus::Measuring);
}

// A measured commit without a measurement in progress is refused, so it cannot fall back to
// calibrating whatever route is current.
TEST_CASE("LiveInputMonitor refuses a measured commit without a measurement", "[audio][live-input]")
{
    Harness harness;

    const auto committed = harness.monitor.commitMeasurement(6.5, g_ready);

    REQUIRE_FALSE(committed.has_value());
    CHECK(committed.error().code == LiveInputMonitorErrorCode::InvalidRequest);
    CHECK(harness.store.input_calibrations.empty());
}

// A typed gain calibrates the current route.
TEST_CASE("LiveInputMonitor commits a manual calibration", "[audio][live-input]")
{
    Harness harness;

    REQUIRE(harness.monitor.commitManualCalibration(3.1, g_ready).has_value());

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

    const auto committed = harness.monitor.commitManualCalibration(8.0, g_ready);

    REQUIRE_FALSE(committed.has_value());
    CHECK(committed.error().code == LiveInputMonitorErrorCode::CalibrationStoreUnavailable);
    CHECK(harness.monitor.status() == LiveInputMonitoringStatus::Active);
    CHECK_THAT(harness.live_input.current_input_gain.db, Catch::Matchers::WithinULP(3.0, 0));
}

// The calibration is saved even when the backend then refuses the route: it is a fact about the
// route, and the refusal is reported.
TEST_CASE("LiveInputMonitor saves a calibration the backend refuses", "[audio][live-input]")
{
    Harness harness;
    harness.live_input.next_set_input_gain_error = routeUnavailable();

    const auto committed = harness.monitor.commitManualCalibration(4.0, g_ready);

    REQUIRE_FALSE(committed.has_value());
    CHECK(committed.error().code == LiveInputMonitorErrorCode::BackendRejected);
    CHECK(harness.store.input_calibrations.size() == 1);
    CHECK(harness.monitor.status() == LiveInputMonitoringStatus::BackendUnavailable);
}

// A session that stops allowing monitoring turns both paths off and ends a measurement.
TEST_CASE("LiveInputMonitor session close ends monitoring and measurement", "[audio][live-input]")
{
    Harness harness{3.0};
    harness.monitor.refresh(g_ready);
    REQUIRE(harness.monitor.beginMeasurement(g_ready).has_value());

    harness.monitor.refresh(g_not_ready);

    CHECK(harness.monitor.status() != LiveInputMonitoringStatus::Measuring);
    CHECK_FALSE(harness.live_input.live_input_monitoring_enabled);
    CHECK_FALSE(harness.live_input.calibration_input_monitoring_enabled);
    CHECK(harness.monitor.status() == LiveInputMonitoringStatus::SessionNotReady);
}

} // namespace rock_hero::common::audio
