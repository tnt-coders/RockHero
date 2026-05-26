#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/audio/input_calibration.h>

namespace rock_hero::common::audio
{

// Verifies calibration targets active playing RMS when there is enough peak headroom.
TEST_CASE("Input calibration derives gain from active RMS", "[audio][input-calibration]")
{
    InputCalibrationAccumulator accumulator;
    accumulator.pushSample(AudioMeterLevel{.peak_db = -24.0});
    accumulator.pushSample(AudioMeterLevel{.peak_db = -24.0});

    const InputCalibrationMeasurement measurement = accumulator.measurement();
    const auto result = calculateInputCalibration(measurement);

    REQUIRE(result.has_value());
    CHECK(measurement.active_sample_count == 2);
    CHECK(measurement.active_rms_db == Catch::Approx(-24.0));
    CHECK(result->calibration_gain.db == Catch::Approx(12.0));
    CHECK(result->measured_level.peak_db == Catch::Approx(-24.0));
    CHECK(result->measured_rms_db == Catch::Approx(-24.0));
}

// Verifies quiet windows do not drag down the RMS of active playing.
TEST_CASE("Input calibration ignores quiet windows for RMS", "[audio][input-calibration]")
{
    InputCalibrationAccumulator accumulator;
    accumulator.pushSample(AudioMeterLevel{.peak_db = minimumAudioMeterDb()});
    accumulator.pushSample(AudioMeterLevel{.peak_db = -30.0});

    const InputCalibrationMeasurement measurement = accumulator.measurement();
    const auto result = calculateInputCalibration(measurement);

    REQUIRE(result.has_value());
    CHECK(measurement.active_sample_count == 1);
    CHECK(measurement.active_rms_db == Catch::Approx(-30.0));
    CHECK(result->calibration_gain.db == Catch::Approx(18.0));
}

// Verifies the peak target still limits RMS gain so observed transients keep headroom.
TEST_CASE("Input calibration limits RMS gain by measured peak", "[audio][input-calibration]")
{
    const InputCalibrationMeasurement measurement{
        .loudest_level = AudioMeterLevel{.peak_db = -10.0},
        .active_rms_db = -24.0,
        .active_sample_count = 4,
    };

    const auto result = calculateInputCalibration(measurement);

    REQUIRE(result.has_value());
    CHECK(result->calibration_gain.db == Catch::Approx(4.0));
    CHECK(result->measured_rms_db == Catch::Approx(-24.0));
}

// Verifies silence or very quiet input fails without inventing calibration gain.
TEST_CASE("Input calibration rejects missing usable signal", "[audio][input-calibration]")
{
    InputCalibrationAccumulator accumulator;
    accumulator.pushSample(AudioMeterLevel{.peak_db = -42.0});

    const InputCalibrationMeasurement measurement = accumulator.measurement();
    const auto result = calculateInputCalibration(measurement);

    REQUIRE_FALSE(result.has_value());
    CHECK(measurement.active_sample_count == 0);
    CHECK(result.error().code == InputCalibrationErrorCode::NoUsableSignal);
}

// Verifies clipped input asks the user to fix hardware gain before calibrating.
TEST_CASE("Input calibration rejects clipped input", "[audio][input-calibration]")
{
    const InputCalibrationMeasurement measurement{
        .loudest_level = AudioMeterLevel{.peak_db = -3.0, .clipping = true},
        .active_rms_db = -18.0,
        .active_sample_count = 1,
    };

    const auto result = calculateInputCalibration(measurement);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == InputCalibrationErrorCode::InputClipped);
}

} // namespace rock_hero::common::audio
