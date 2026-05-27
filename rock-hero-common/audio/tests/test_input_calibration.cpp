#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <rock_hero/common/audio/input_calibration.h>

namespace rock_hero::common::audio
{

namespace
{

// Adds enough identical active meter windows to satisfy the stability gate.
void pushSteadySamples(InputCalibrationAccumulator& accumulator, double peak_db)
{
    for (std::size_t sample = 0; sample < minimumInputCalibrationActiveSampleCount(); ++sample)
    {
        accumulator.pushSample(AudioMeterLevel{.peak_db = peak_db});
    }
}

} // namespace

// Verifies calibration targets active playing RMS when there is enough peak headroom.
TEST_CASE("Input calibration derives gain from active RMS", "[audio][input-calibration]")
{
    InputCalibrationAccumulator accumulator;
    pushSteadySamples(accumulator, -24.0);

    const InputCalibrationMeasurement measurement = accumulator.measurement();
    const auto result = calculateInputCalibration(measurement);

    REQUIRE(result.has_value());
    CHECK(measurement.active_sample_count == minimumInputCalibrationActiveSampleCount());
    CHECK(measurement.active_rms_db == Catch::Approx(-24.0));
    CHECK(measurement.reference_peak_db == Catch::Approx(-24.0));
    CHECK(result->calibration_gain.db == Catch::Approx(12.0));
    CHECK(result->measured_level.peak_db == Catch::Approx(-24.0));
    CHECK(result->measured_rms_db == Catch::Approx(-24.0));
}

// Verifies quiet windows do not drag down the RMS of active playing.
TEST_CASE("Input calibration ignores quiet windows for RMS", "[audio][input-calibration]")
{
    InputCalibrationAccumulator accumulator;
    accumulator.pushSample(AudioMeterLevel{.peak_db = minimumAudioMeterDb()});
    pushSteadySamples(accumulator, -30.0);

    const InputCalibrationMeasurement measurement = accumulator.measurement();
    const auto result = calculateInputCalibration(measurement);

    REQUIRE(result.has_value());
    CHECK(measurement.active_sample_count == minimumInputCalibrationActiveSampleCount());
    CHECK(measurement.active_rms_db == Catch::Approx(-30.0));
    CHECK(result->calibration_gain.db == Catch::Approx(18.0));
}

// Verifies the peak target still limits RMS gain so observed transients keep headroom.
TEST_CASE("Input calibration limits RMS gain by measured peak", "[audio][input-calibration]")
{
    const InputCalibrationMeasurement measurement{
        .loudest_level = AudioMeterLevel{.peak_db = -10.0},
        .active_rms_db = -24.0,
        .reference_peak_db = -10.0,
        .active_peak_spread_db = 0.0,
        .active_sample_count = minimumInputCalibrationActiveSampleCount(),
    };

    const auto result = calculateInputCalibration(measurement);

    REQUIRE(result.has_value());
    CHECK(result->calibration_gain.db == Catch::Approx(4.0));
    CHECK(result->measured_rms_db == Catch::Approx(-24.0));
}

// Verifies one unusually loud window does not dominate the calibration reference.
TEST_CASE("Input calibration ignores isolated peak outliers", "[audio][input-calibration]")
{
    InputCalibrationAccumulator accumulator;
    for (std::size_t sample = 0; sample < minimumInputCalibrationActiveSampleCount(); ++sample)
    {
        accumulator.pushSample(AudioMeterLevel{.peak_db = -24.0});
    }
    accumulator.pushSample(AudioMeterLevel{.peak_db = -6.0});

    const InputCalibrationMeasurement measurement = accumulator.measurement();
    const auto result = calculateInputCalibration(measurement);

    REQUIRE(result.has_value());
    CHECK(measurement.loudest_level.peak_db == Catch::Approx(-6.0));
    CHECK(measurement.reference_peak_db == Catch::Approx(-24.0));
    CHECK(result->calibration_gain.db == Catch::Approx(12.0));
}

// Verifies calibration rejects captures that do not contain enough active playing.
TEST_CASE("Input calibration rejects sparse active input", "[audio][input-calibration]")
{
    InputCalibrationAccumulator accumulator;
    accumulator.pushSample(AudioMeterLevel{.peak_db = -24.0});

    const InputCalibrationMeasurement measurement = accumulator.measurement();
    const auto result = calculateInputCalibration(measurement);

    REQUIRE_FALSE(result.has_value());
    CHECK(measurement.active_sample_count == 1);
    CHECK(result.error().code == InputCalibrationErrorCode::NoUsableSignal);
}

// Verifies widely varying active levels ask the user for steadier strums.
TEST_CASE("Input calibration rejects inconsistent active input", "[audio][input-calibration]")
{
    InputCalibrationAccumulator accumulator;
    for (std::size_t sample = 0; sample < minimumInputCalibrationActiveSampleCount(); ++sample)
    {
        const double peak_db = sample % 2 == 0 ? -12.0 : -32.0;
        accumulator.pushSample(AudioMeterLevel{.peak_db = peak_db});
    }

    const InputCalibrationMeasurement measurement = accumulator.measurement();
    const auto result = calculateInputCalibration(measurement);

    REQUIRE_FALSE(result.has_value());
    CHECK(measurement.active_peak_spread_db > maximumInputCalibrationActivePeakSpreadDb());
    CHECK(result.error().code == InputCalibrationErrorCode::InputInconsistent);
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

// Verifies the automatic capture state machine completes after a steady active window.
TEST_CASE("Input capture completes after steady input", "[audio][input-calibration]")
{
    InputCalibrationCapture capture{
        1,
        4,
        minimumInputCalibrationActiveSampleCount(),
    };
    capture.start();

    InputCalibrationCaptureUpdate update =
        capture.pushSample(AudioMeterLevel{.peak_db = minimumAudioMeterDb()});
    CHECK(update.phase == InputCalibrationCapturePhase::WaitingForInput);
    CHECK(capture.active());

    for (std::size_t sample = 0; sample < minimumInputCalibrationActiveSampleCount() - 1; ++sample)
    {
        update = capture.pushSample(AudioMeterLevel{.peak_db = -24.0});
        CHECK(update.phase == InputCalibrationCapturePhase::Measuring);
        CHECK_FALSE(update.result.has_value());
    }

    update = capture.pushSample(AudioMeterLevel{.peak_db = -24.0});

    REQUIRE(update.result.has_value());
    CHECK(update.phase == InputCalibrationCapturePhase::Complete);
    CHECK(update.result->calibration_gain.db == Catch::Approx(12.0));
    CHECK_FALSE(capture.active());
}

// Verifies retry capture starts from a clean measurement accumulator.
TEST_CASE("Input capture retry clears previous measurement", "[audio][input-calibration]")
{
    InputCalibrationCapture capture{
        1,
        4,
        minimumInputCalibrationActiveSampleCount(),
    };
    capture.start();

    InputCalibrationCaptureUpdate update =
        capture.pushSample(AudioMeterLevel{.peak_db = minimumAudioMeterDb()});
    REQUIRE(update.phase == InputCalibrationCapturePhase::WaitingForInput);
    for (std::size_t sample = 0; sample < minimumInputCalibrationActiveSampleCount(); ++sample)
    {
        update = capture.pushSample(AudioMeterLevel{.peak_db = -24.0});
    }
    REQUIRE(update.result.has_value());
    CHECK(update.result->calibration_gain.db == Catch::Approx(12.0));

    capture.start();
    update = capture.pushSample(AudioMeterLevel{.peak_db = minimumAudioMeterDb()});
    REQUIRE(update.phase == InputCalibrationCapturePhase::WaitingForInput);
    for (std::size_t sample = 0; sample < minimumInputCalibrationActiveSampleCount(); ++sample)
    {
        update = capture.pushSample(AudioMeterLevel{.peak_db = -30.0});
    }

    REQUIRE(update.result.has_value());
    CHECK(update.result->calibration_gain.db == Catch::Approx(18.0));
}

// Verifies the automatic capture rejects active input that varies too much.
TEST_CASE("Input capture rejects inconsistent input", "[audio][input-calibration]")
{
    InputCalibrationCapture capture{
        1,
        4,
        minimumInputCalibrationActiveSampleCount(),
    };
    capture.start();

    InputCalibrationCaptureUpdate update =
        capture.pushSample(AudioMeterLevel{.peak_db = minimumAudioMeterDb()});
    REQUIRE(update.phase == InputCalibrationCapturePhase::WaitingForInput);

    for (std::size_t sample = 0; sample < minimumInputCalibrationActiveSampleCount(); ++sample)
    {
        const double peak_db = sample % 2 == 0 ? -12.0 : -32.0;
        update = capture.pushSample(AudioMeterLevel{.peak_db = peak_db});
    }

    REQUIRE(update.error.has_value());
    CHECK(update.phase == InputCalibrationCapturePhase::Failed);
    CHECK(update.error->code == InputCalibrationErrorCode::InputInconsistent);
    CHECK_FALSE(capture.active());
}

// Verifies the automatic capture times out before measuring when no usable input arrives.
TEST_CASE("Input capture times out waiting for input", "[audio][input-calibration]")
{
    InputCalibrationCapture capture{
        1,
        2,
        minimumInputCalibrationActiveSampleCount(),
    };
    capture.start();

    InputCalibrationCaptureUpdate update =
        capture.pushSample(AudioMeterLevel{.peak_db = minimumAudioMeterDb()});
    REQUIRE(update.phase == InputCalibrationCapturePhase::WaitingForInput);

    update = capture.pushSample(AudioMeterLevel{.peak_db = minimumAudioMeterDb()});
    CHECK(update.phase == InputCalibrationCapturePhase::WaitingForInput);
    update = capture.pushSample(AudioMeterLevel{.peak_db = minimumAudioMeterDb()});

    REQUIRE(update.error.has_value());
    CHECK(update.phase == InputCalibrationCapturePhase::Failed);
    CHECK(update.error->code == InputCalibrationErrorCode::NoUsableSignal);
    CHECK_FALSE(capture.active());
}

// Verifies clipped input stops automatic capture before a measurement window starts.
TEST_CASE("Input capture rejects clipped waiting input", "[audio][input-calibration]")
{
    InputCalibrationCapture capture{
        1,
        2,
        minimumInputCalibrationActiveSampleCount(),
    };
    capture.start();

    InputCalibrationCaptureUpdate update =
        capture.pushSample(AudioMeterLevel{.peak_db = minimumAudioMeterDb()});
    REQUIRE(update.phase == InputCalibrationCapturePhase::WaitingForInput);

    update = capture.pushSample(AudioMeterLevel{.peak_db = -3.0, .clipping = true});

    REQUIRE(update.error.has_value());
    CHECK(update.phase == InputCalibrationCapturePhase::Failed);
    CHECK(update.error->code == InputCalibrationErrorCode::InputClipped);
    CHECK_FALSE(capture.active());
}

} // namespace rock_hero::common::audio
