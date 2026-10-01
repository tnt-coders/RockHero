#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/audio/input/live_input_monitoring_status.h>
#include <string_view>

namespace rock_hero::common::audio
{

// Active needs no words; every other status is one sentence, ending in a period, that both
// products show for it.
TEST_CASE("Live input status text words every off status once", "[audio][live-input]")
{
    CHECK(liveInputStatusText(LiveInputMonitoringStatus::Active).empty());

    for (const LiveInputMonitoringStatus status : {
             LiveInputMonitoringStatus::Measuring,
             LiveInputMonitoringStatus::CalibrationStoreUnavailable,
             LiveInputMonitoringStatus::SessionNotReady,
             LiveInputMonitoringStatus::NoInputDevice,
             LiveInputMonitoringStatus::MissingCalibration,
             LiveInputMonitoringStatus::BackendUnavailable,
         })
    {
        const std::string_view text = liveInputStatusText(status);
        REQUIRE_FALSE(text.empty());
        CHECK(text.back() == '.');
    }

    CHECK(
        liveInputStatusText(LiveInputMonitoringStatus::MissingCalibration) ==
        "Input calibration required.");
}

} // namespace rock_hero::common::audio
