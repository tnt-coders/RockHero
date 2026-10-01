#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <variant>

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

// Reports whether a capture step is still running at the given stage.
[[nodiscard]] bool atStage(const InputCalibrationStep& step, InputCalibrationStage stage)
{
    const auto* const current = std::get_if<InputCalibrationStage>(&step);
    return current != nullptr && *current == stage;
}

// Feeds the settle window, during which the capture ignores what it hears, and returns the step
// the last settle sample produced.
[[nodiscard]] InputCalibrationStep settle(InputCalibrationCapture& capture)
{
    InputCalibrationStep step{InputCalibrationStage::Settling};
    for (std::size_t sample = 0; sample < inputCalibrationSettleSampleCount(); ++sample)
    {
        step = capture.pushSample(AudioMeterLevel{.peak_db = -24.0});
    }
    return step;
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

// The capture discards its settle window, waits for a strum, then measures a steady window.
TEST_CASE("Input capture completes after steady input", "[audio][input-calibration]")
{
    InputCalibrationCapture capture;
    REQUIRE(atStage(settle(capture), InputCalibrationStage::WaitingForInput));

    InputCalibrationStep step{InputCalibrationStage::WaitingForInput};
    for (std::size_t sample = 0; sample < inputCalibrationMeasurementSampleCount() - 1; ++sample)
    {
        step = capture.pushSample(AudioMeterLevel{.peak_db = -24.0});
        REQUIRE(atStage(step, InputCalibrationStage::Measuring));
    }
    step = capture.pushSample(AudioMeterLevel{.peak_db = -24.0});

    const auto* const result = std::get_if<InputCalibrationResult>(&step);
    REQUIRE(result != nullptr);
    CHECK(result->calibration_gain.db == Catch::Approx(12.0));
}

// Active input that varies too much is refused.
TEST_CASE("Input capture rejects inconsistent input", "[audio][input-calibration]")
{
    InputCalibrationCapture capture;
    REQUIRE(atStage(settle(capture), InputCalibrationStage::WaitingForInput));

    InputCalibrationStep step{InputCalibrationStage::WaitingForInput};
    for (std::size_t sample = 0; sample < inputCalibrationMeasurementSampleCount(); ++sample)
    {
        const double peak_db = sample % 2 == 0 ? -12.0 : -32.0;
        step = capture.pushSample(AudioMeterLevel{.peak_db = peak_db});
    }

    const auto* const error = std::get_if<InputCalibrationError>(&step);
    REQUIRE(error != nullptr);
    CHECK(error->code == InputCalibrationErrorCode::InputInconsistent);
}

// A capture that hears nothing usable times out before measuring.
TEST_CASE("Input capture times out waiting for input", "[audio][input-calibration]")
{
    InputCalibrationCapture capture;
    REQUIRE(atStage(settle(capture), InputCalibrationStage::WaitingForInput));

    InputCalibrationStep step{InputCalibrationStage::WaitingForInput};
    for (std::size_t sample = 0; sample < inputCalibrationWaitSampleCount() - 1; ++sample)
    {
        step = capture.pushSample(AudioMeterLevel{.peak_db = minimumAudioMeterDb()});
        REQUIRE(atStage(step, InputCalibrationStage::WaitingForInput));
    }
    step = capture.pushSample(AudioMeterLevel{.peak_db = minimumAudioMeterDb()});

    const auto* const error = std::get_if<InputCalibrationError>(&step);
    REQUIRE(error != nullptr);
    CHECK(error->code == InputCalibrationErrorCode::NoUsableSignal);
}

// Clipped input stops the capture before a measurement window starts.
TEST_CASE("Input capture rejects clipped waiting input", "[audio][input-calibration]")
{
    InputCalibrationCapture capture;
    REQUIRE(atStage(settle(capture), InputCalibrationStage::WaitingForInput));

    const InputCalibrationStep step =
        capture.pushSample(AudioMeterLevel{.peak_db = -3.0, .clipping = true});

    const auto* const error = std::get_if<InputCalibrationError>(&step);
    REQUIRE(error != nullptr);
    CHECK(error->code == InputCalibrationErrorCode::InputClipped);
}

} // namespace rock_hero::common::audio
