#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/common/audio/input/pickup_types.h>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// The single-coil target, -12.79 dBFS, the target the expected gains below were worked out for.
const double g_single_coil_target = inputCalibrationTargetPeakDb(PickupClass::SingleCoil);

// Peaks of steady playing, by default just enough windows to calibrate from.
[[nodiscard]] std::vector<double> steadyPeaks(
    double peak_db, std::size_t count = minimumInputCalibrationActiveSampleCount())
{
    std::vector<double> peaks(count, peak_db);
    return peaks;
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

// The pickup table states each kind once: one row per kind, looked up by its kind, each with its
// own name. The rows' completeness is a compile-time check in pickup_types.cpp.
TEST_CASE("Pickup types state every kind once", "[audio][input-calibration]")
{
    std::vector<std::string_view> names;
    for (const PickupType& row : pickupTypes())
    {
        CHECK(&pickupType(row.pickups) == &row);
        CHECK(std::ranges::find(names, row.name) == names.end());
        names.push_back(row.name);
    }
    CHECK(names.size() == 5);

    CHECK(pickupType(PickupClass::Humbucker).name == "humbucker");
    CHECK(pickupType(PickupClass::SingleCoil).name == "single-coil");
    CHECK(pickupType(PickupClass::P90).name == "P-90");
    CHECK(pickupType(PickupClass::MiniHumbucker).name == "mini-humbucker");
    CHECK(pickupType(PickupClass::Active).name == "active");
}

// A list shows each name capitalized, and a name that is already capitalized stays as it is.
TEST_CASE("Pickup type labels capitalize the name", "[audio][input-calibration]")
{
    CHECK(pickupTypeLabel(PickupClass::Humbucker) == "Humbucker");
    CHECK(pickupTypeLabel(PickupClass::SingleCoil) == "Single-coil");
    CHECK(pickupTypeLabel(PickupClass::P90) == "P-90");
}

// A gain that rounds to zero is stored and shown as zero, never as negative zero.
TEST_CASE("Input calibration quantizes a near-zero gain to +0", "[audio][input-calibration]")
{
    const double quantized = quantizeInputCalibrationGainDb(-0.04);
    CHECK_FALSE(std::signbit(quantized));
    CHECK_THAT(quantized, Catch::Matchers::WithinAbs(0.0, 1e-12));
}

// The one gain formatter: signed, one decimal, and zero without a sign, even a gain that only
// rounds to zero.
TEST_CASE("Input calibration gain text prints sign and one decimal", "[audio][input-calibration]")
{
    CHECK(inputCalibrationGainText(2.3) == "+2.3");
    CHECK(inputCalibrationGainText(-0.5) == "-0.5");
    CHECK(inputCalibrationGainText(0.0) == "0.0");
    CHECK(inputCalibrationGainText(-0.04) == "0.0");
}

// Steady playing at L puts its ceiling on the target: the gain is the target less L.
TEST_CASE("Input calibration sets the ceiling on the target", "[audio][input-calibration]")
{
    const auto result = calculateInputCalibration(steadyPeaks(-24.0), g_single_coil_target);

    REQUIRE(result.has_value());
    CHECK(result->ceiling_peak_db == Catch::Approx(-24.0));
    CHECK(result->calibration_gain.db == Catch::Approx(11.2));
}

// One stray spike above the playing does not set the gain: the ceiling is a high percentile.
TEST_CASE("Input calibration ignores an isolated spike", "[audio][input-calibration]")
{
    std::vector<double> peaks = steadyPeaks(-24.0, 20);
    peaks.insert(peaks.begin() + 7, -6.0);

    const auto result = calculateInputCalibration(std::move(peaks), g_single_coil_target);

    REQUIRE(result.has_value());
    CHECK(result->ceiling_peak_db == Catch::Approx(-24.0));
    CHECK(result->calibration_gain.db == Catch::Approx(11.2));
}

// The ceiling follows the hardest playing, not its average: decaying strums set the gain by their
// attacks.
TEST_CASE("Input calibration follows the hardest playing", "[audio][input-calibration]")
{
    std::vector<double> peaks;
    for (std::size_t strum = 0; strum < 5; ++strum)
    {
        peaks.insert(peaks.end(), {-12.0, -18.0, -24.0, -30.0});
    }

    const auto result = calculateInputCalibration(std::move(peaks), g_single_coil_target);

    REQUIRE(result.has_value());
    CHECK(result->ceiling_peak_db == Catch::Approx(-12.0));
}

// Too few windows of playing produce no gain.
TEST_CASE("Input calibration rejects sparse active input", "[audio][input-calibration]")
{
    const auto result = calculateInputCalibration(
        steadyPeaks(-24.0, minimumInputCalibrationActiveSampleCount() - 1), g_single_coil_target);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == InputCalibrationFailure::NoUsableSignal);
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
    // A minute of silence.
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

// Windows the listen hears below the listening threshold are not playing: they neither count
// toward the minimum nor pull the ceiling down.
TEST_CASE("Input capture counts only windows heard as playing", "[audio][input-calibration]")
{
    // Feeds one whole listen whose first window is -30 dBFS and whose later ones alternate between
    // silence and the given level.
    const auto listen = [](double later_peak_db) {
        InputCalibrationCapture capture{g_single_coil_target};
        REQUIRE(isWaiting(settle(capture, AudioMeterLevel{.peak_db = minimumAudioMeterDb()})));
        InputCalibrationStep step = capture.pushSample(AudioMeterLevel{.peak_db = -30.0});
        for (std::size_t window = 1; window < inputCalibrationListenSampleCount(); ++window)
        {
            const double peak_db = window % 2 == 0 ? later_peak_db : minimumAudioMeterDb();
            step = capture.pushSample(AudioMeterLevel{.peak_db = peak_db});
        }
        return step;
    };

    const InputCalibrationStep played = listen(-30.0);
    const auto* const result = std::get_if<InputCalibrationResult>(&played);
    REQUIRE(result != nullptr);
    CHECK(result->calibration_gain.db == Catch::Approx(17.2));

    const InputCalibrationStep silent = listen(minimumAudioMeterDb());
    const auto* const failure = std::get_if<InputCalibrationFailure>(&silent);
    REQUIRE(failure != nullptr);
    CHECK(*failure == InputCalibrationFailure::NoUsableSignal);
}

// A clip after the settle span fails the capture at once, before the listen and during it alike:
// no gain undoes a clip at the interface, so listening on would waste the player's time.
TEST_CASE("Input capture fails at the first clip", "[audio][input-calibration]")
{
    const AudioMeterLevel clip{.peak_db = 3.0, .clipping = true};
    const auto fails = [](const InputCalibrationStep& step) {
        const auto* const failure = std::get_if<InputCalibrationFailure>(&step);
        return failure != nullptr && *failure == InputCalibrationFailure::InputClipped;
    };

    InputCalibrationCapture waiting{g_single_coil_target};
    REQUIRE(isWaiting(settle(waiting, AudioMeterLevel{.peak_db = minimumAudioMeterDb()})));
    CHECK(fails(waiting.pushSample(clip)));

    InputCalibrationCapture listening{g_single_coil_target};
    REQUIRE(isWaiting(settle(listening, AudioMeterLevel{.peak_db = minimumAudioMeterDb()})));
    REQUIRE(isListening(
        listening.pushSample(AudioMeterLevel{.peak_db = -24.0}),
        inputCalibrationListenSampleCount() - 1));
    CHECK(fails(listening.pushSample(clip)));
}

} // namespace rock_hero::common::audio
