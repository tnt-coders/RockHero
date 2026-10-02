#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <string_view>
#include <variant>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// The single-coil target, -12.79 dBFS, the target the expected gains below were worked out for.
const double g_single_coil_target = inputCalibrationTargetPeakDb(PickupClass::SingleCoil);

// Adds enough identical active meter windows to satisfy the minimum window count.
void pushSteadySamples(InputCalibrationAccumulator& accumulator, double peak_db)
{
    for (std::size_t sample = 0; sample < minimumInputCalibrationActiveSampleCount(); ++sample)
    {
        accumulator.pushSample(AudioMeterLevel{.peak_db = peak_db});
    }
}

// Reports whether a capture step is still waiting for the first strum.
[[nodiscard]] bool isWaiting(const InputCalibrationStep& step)
{
    const auto* const progress = std::get_if<InputCalibrationRunning>(&step);
    return progress != nullptr && std::holds_alternative<InputCalibrationWaiting>(*progress);
}

// Reports whether a capture step is listening with the given windows left.
[[nodiscard]] bool isListening(const InputCalibrationStep& step, std::size_t windows_remaining)
{
    const auto* const progress = std::get_if<InputCalibrationRunning>(&step);
    return progress != nullptr &&
           *progress == InputCalibrationRunning{
                            InputCalibrationListening{.windows_remaining = windows_remaining}
                        };
}

// Feeds the settle span, which the capture ignores whatever it hears, and returns the step the last
// settle sample produced.
[[nodiscard]] InputCalibrationStep settle(InputCalibrationCapture& capture, AudioMeterLevel level)
{
    InputCalibrationStep step{InputCalibrationRunning{InputCalibrationWaiting{}}};
    for (std::size_t sample = 0; sample < inputCalibrationSettleSampleCount(); ++sample)
    {
        step = capture.pushSample(level);
    }
    return step;
}

} // namespace

// The target is where a hard strum on the stated pickups lands against the +12 dBu reference: a
// humbucker's 2 V at -6.8 dBFS, a single coil's 1 V a humbucker's two coils (6 dB) below it.
TEST_CASE("Input calibration target derives from the pickups", "[audio][input-calibration]")
{
    const double humbucker = inputCalibrationTargetPeakDb(PickupClass::Humbucker);
    const double single_coil = inputCalibrationTargetPeakDb(PickupClass::SingleCoil);
    CHECK_THAT(humbucker, Catch::Matchers::WithinAbs(-6.77, 0.01));
    CHECK_THAT(single_coil, Catch::Matchers::WithinAbs(-12.79, 0.01));
    CHECK_THAT(humbucker - single_coil, Catch::Matchers::WithinAbs(6.02, 0.01));
    CHECK_THAT(
        inputCalibrationTargetPeakDb(PickupClass::MiniHumbucker),
        Catch::Matchers::WithinAbs(-11.21, 0.01));
    CHECK_THAT(
        inputCalibrationTargetPeakDb(PickupClass::Active), Catch::Matchers::WithinAbs(-6.35, 0.01));
}

// Each pickup class has one name, so every surface words it the same, and every listed class has
// its own.
TEST_CASE("Pickup class words every class once", "[audio][input-calibration]")
{
    std::vector<std::string_view> names;
    for (const PickupClass pickups : pickupClasses())
    {
        const std::string_view name = pickupClassText(pickups);
        CHECK_FALSE(name.empty());
        CHECK(std::ranges::find(names, name) == names.end());
        names.push_back(name);
    }
    CHECK(names.size() == 5);

    CHECK(pickupClassText(PickupClass::Humbucker) == "humbucker");
    CHECK(pickupClassText(PickupClass::SingleCoil) == "single-coil");
    CHECK(pickupClassText(PickupClass::P90) == "P-90");
    CHECK(pickupClassText(PickupClass::MiniHumbucker) == "mini-humbucker");
    CHECK(pickupClassText(PickupClass::Active) == "active");
}

// A gain that rounds to zero is stored and shown as zero, never as negative zero.
TEST_CASE("Input calibration quantizes a near-zero gain to +0", "[audio][input-calibration]")
{
    const double quantized = quantizeInputCalibrationGainDb(-0.04);
    CHECK_FALSE(std::signbit(quantized));
    CHECK_THAT(quantized, Catch::Matchers::WithinAbs(0.0, 1e-12));
}

// Steady playing at L puts its ceiling on the target: the gain is the target less L.
TEST_CASE("Input calibration sets the ceiling on the target", "[audio][input-calibration]")
{
    InputCalibrationAccumulator accumulator;
    pushSteadySamples(accumulator, -24.0);

    const InputCalibrationMeasurement measurement = accumulator.measurement();
    const auto result = calculateInputCalibration(measurement, g_single_coil_target);

    REQUIRE(result.has_value());
    CHECK(measurement.active_sample_count == minimumInputCalibrationActiveSampleCount());
    CHECK(measurement.ceiling_peak_db == Catch::Approx(-24.0));
    CHECK(result->calibration_gain.db == Catch::Approx(11.2));
}

// Windows below the listening threshold are not playing and do not count toward the ceiling.
TEST_CASE("Input calibration ignores quiet windows", "[audio][input-calibration]")
{
    InputCalibrationAccumulator accumulator;
    accumulator.pushSample(AudioMeterLevel{.peak_db = minimumAudioMeterDb()});
    pushSteadySamples(accumulator, -30.0);

    const InputCalibrationMeasurement measurement = accumulator.measurement();
    const auto result = calculateInputCalibration(measurement, g_single_coil_target);

    REQUIRE(result.has_value());
    CHECK(measurement.active_sample_count == minimumInputCalibrationActiveSampleCount());
    CHECK(result->calibration_gain.db == Catch::Approx(17.2));
}

// One stray spike above the playing does not set the gain: the ceiling is a high percentile.
TEST_CASE("Input calibration ignores an isolated spike", "[audio][input-calibration]")
{
    InputCalibrationAccumulator accumulator;
    for (std::size_t sample = 0; sample < 20; ++sample)
    {
        accumulator.pushSample(AudioMeterLevel{.peak_db = -24.0});
    }
    accumulator.pushSample(AudioMeterLevel{.peak_db = -6.0});

    const InputCalibrationMeasurement measurement = accumulator.measurement();
    const auto result = calculateInputCalibration(measurement, g_single_coil_target);

    REQUIRE(result.has_value());
    CHECK(measurement.loudest_level.peak_db == Catch::Approx(-6.0));
    CHECK(measurement.ceiling_peak_db == Catch::Approx(-24.0));
    CHECK(result->calibration_gain.db == Catch::Approx(11.2));
}

// The ceiling follows the hardest playing, not its average: decaying strums set the gain by their
// attacks.
TEST_CASE("Input calibration follows the hardest playing", "[audio][input-calibration]")
{
    InputCalibrationAccumulator accumulator;
    for (std::size_t strum = 0; strum < 5; ++strum)
    {
        for (const double peak_db : {-12.0, -18.0, -24.0, -30.0})
        {
            accumulator.pushSample(AudioMeterLevel{.peak_db = peak_db});
        }
    }

    const InputCalibrationMeasurement measurement = accumulator.measurement();

    CHECK(measurement.ceiling_peak_db == Catch::Approx(-12.0));
}

// Too few windows of playing produce no gain.
TEST_CASE("Input calibration rejects sparse active input", "[audio][input-calibration]")
{
    InputCalibrationAccumulator accumulator;
    accumulator.pushSample(AudioMeterLevel{.peak_db = -24.0});

    const InputCalibrationMeasurement measurement = accumulator.measurement();
    const auto result = calculateInputCalibration(measurement, g_single_coil_target);

    REQUIRE_FALSE(result.has_value());
    CHECK(measurement.active_sample_count == 1);
    CHECK(result.error().code == InputCalibrationErrorCode::NoUsableSignal);
}

// Silence or very quiet input fails without inventing a gain.
TEST_CASE("Input calibration rejects missing usable signal", "[audio][input-calibration]")
{
    InputCalibrationAccumulator accumulator;
    accumulator.pushSample(AudioMeterLevel{.peak_db = -42.0});

    const InputCalibrationMeasurement measurement = accumulator.measurement();
    const auto result = calculateInputCalibration(measurement, g_single_coil_target);

    REQUIRE_FALSE(result.has_value());
    CHECK(measurement.active_sample_count == 0);
    CHECK(result.error().code == InputCalibrationErrorCode::NoUsableSignal);
}

// Clipped input asks the player to lower the interface gain before calibrating.
TEST_CASE("Input calibration rejects clipped input", "[audio][input-calibration]")
{
    const InputCalibrationMeasurement measurement{
        .loudest_level = AudioMeterLevel{.peak_db = -3.0, .clipping = true},
        .ceiling_peak_db = -6.0,
        .active_sample_count = minimumInputCalibrationActiveSampleCount(),
    };

    const auto result = calculateInputCalibration(measurement, g_single_coil_target);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == InputCalibrationErrorCode::InputClipped);
}

// The settle span is part of the wait: what it hears, even playing or a clip, neither starts the
// listen nor fails the capture.
TEST_CASE("Input capture ignores its settle span", "[audio][input-calibration]")
{
    InputCalibrationCapture capture{g_single_coil_target};
    CHECK(isWaiting(settle(capture, AudioMeterLevel{.peak_db = -24.0})));

    InputCalibrationCapture clipped_capture{g_single_coil_target};
    CHECK(isWaiting(settle(clipped_capture, AudioMeterLevel{.peak_db = -3.0, .clipping = true})));
}

// The wait has no limit: a capture that hears nothing keeps waiting, with no count, until its
// driver ends it.
TEST_CASE("Input capture waits as long as it takes", "[audio][input-calibration]")
{
    InputCalibrationCapture capture{g_single_coil_target};
    InputCalibrationStep step = settle(capture, AudioMeterLevel{.peak_db = minimumAudioMeterDb()});
    // A minute of silence, six times the wait the capture used to give up after.
    for (std::size_t sample = 0;
         sample < static_cast<std::size_t>(inputCalibrationSampleRateHz()) * 60;
         ++sample)
    {
        step = capture.pushSample(AudioMeterLevel{.peak_db = minimumAudioMeterDb()});
    }
    CHECK(isWaiting(step));
}

// The capture waits for playing, then listens a fixed span counted from the first window it
// hears, reporting the windows it has left so a driver can count down without a counter of its
// own.
TEST_CASE("Input capture listens a fixed span from the first strum", "[audio][input-calibration]")
{
    InputCalibrationCapture capture{g_single_coil_target};
    REQUIRE(isWaiting(settle(capture, AudioMeterLevel{.peak_db = minimumAudioMeterDb()})));

    // The first window heard ends the wait and is itself the first window listened to.
    InputCalibrationStep step = capture.pushSample(AudioMeterLevel{.peak_db = -24.0});
    REQUIRE(isListening(step, inputCalibrationListenSampleCount() - 1));
    for (std::size_t remaining = inputCalibrationListenSampleCount() - 1; remaining > 1;
         --remaining)
    {
        step = capture.pushSample(AudioMeterLevel{.peak_db = -24.0});
        REQUIRE(isListening(step, remaining - 1));
    }
    step = capture.pushSample(AudioMeterLevel{.peak_db = -24.0});

    const auto* const result = std::get_if<InputCalibrationResult>(&step);
    REQUIRE(result != nullptr);
    CHECK(result->calibration_gain.db == Catch::Approx(11.2));
}

// Clipped input stops the capture before it starts listening.
TEST_CASE("Input capture rejects clipped waiting input", "[audio][input-calibration]")
{
    InputCalibrationCapture capture{g_single_coil_target};
    REQUIRE(isWaiting(settle(capture, AudioMeterLevel{.peak_db = minimumAudioMeterDb()})));

    const InputCalibrationStep step =
        capture.pushSample(AudioMeterLevel{.peak_db = -3.0, .clipping = true});

    const auto* const error = std::get_if<InputCalibrationError>(&step);
    REQUIRE(error != nullptr);
    CHECK(error->code == InputCalibrationErrorCode::InputClipped);
}

} // namespace rock_hero::common::audio
