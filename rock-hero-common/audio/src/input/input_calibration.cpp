#include "input/input_calibration.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rock_hero::common::audio
{

// Records raw input level and maintains the active-window RMS incrementally.
void InputCalibrationAccumulator::pushSample(AudioMeterLevel level)
{
    m_measurement.loudest_level.peak_db =
        std::max(m_measurement.loudest_level.peak_db, level.peak_db);
    m_measurement.loudest_level.clipping = m_measurement.loudest_level.clipping || level.clipping;

    if (level.peak_db < minimumInputCalibrationSignalDb())
    {
        return;
    }

    const double linear_amplitude = decibelsToLinearAmplitude(level.peak_db);
    m_active_square_sum += linear_amplitude * linear_amplitude;
    m_active_peak_db.push_back(level.peak_db);
    m_measurement.active_sample_count += 1;
    const double active_mean_square =
        m_active_square_sum / static_cast<double>(m_measurement.active_sample_count);
    m_measurement.active_rms_db = linearAmplitudeToDecibels(std::sqrt(active_mean_square));
}

// Builds the final measurement with percentile-trimmed RMS and consistency data.
InputCalibrationMeasurement InputCalibrationAccumulator::measurement() const
{
    InputCalibrationMeasurement measurement = m_measurement;
    if (m_active_peak_db.empty())
    {
        return measurement;
    }

    std::vector<double> sorted_peak_db = m_active_peak_db;
    std::ranges::sort(sorted_peak_db);
    const std::size_t reference_index =
        percentileIndex(sorted_peak_db, inputCalibrationReferencePeakPercentile());
    const std::size_t low_index =
        percentileIndex(sorted_peak_db, inputCalibrationConsistencyLowPercentile());

    measurement.reference_peak_db = sorted_peak_db[reference_index];
    measurement.active_rms_db = rmsDbForSortedRange(sorted_peak_db, low_index, reference_index);
    measurement.active_peak_spread_db =
        std::max(0.0, measurement.reference_peak_db - sorted_peak_db[low_index]);
    return measurement;
}

// Reads one nearest-rank index from an already sorted active peak sequence.
std::size_t InputCalibrationAccumulator::percentileIndex(
    const std::vector<double>& sorted_peak_db, double percentile) noexcept
{
    if (sorted_peak_db.empty())
    {
        return 0;
    }

    const double clamped_percentile = std::clamp(percentile, 0.0, 1.0);
    const double raw_index =
        std::ceil(clamped_percentile * static_cast<double>(sorted_peak_db.size())) - 1.0;
    const double clamped_index =
        std::clamp(raw_index, 0.0, static_cast<double>(sorted_peak_db.size() - 1));
    return static_cast<std::size_t>(clamped_index);
}

// Converts a dBFS meter level into linear amplitude for RMS accumulation.
double InputCalibrationAccumulator::decibelsToLinearAmplitude(double db) noexcept
{
    return std::pow(10.0, db / 20.0);
}

// Converts a positive linear RMS amplitude back to a bounded dBFS value.
double InputCalibrationAccumulator::linearAmplitudeToDecibels(double linear_amplitude) noexcept
{
    if (linear_amplitude <= 0.0)
    {
        return minimumAudioMeterDb();
    }

    return std::clamp(20.0 * std::log10(linear_amplitude), minimumAudioMeterDb(), 12.0);
}

// Computes RMS from a sorted inclusive dB range after percentile trimming.
double InputCalibrationAccumulator::rmsDbForSortedRange(
    const std::vector<double>& sorted_peak_db, std::size_t first_index,
    std::size_t last_index) noexcept
{
    if (sorted_peak_db.empty())
    {
        return minimumAudioMeterDb();
    }

    first_index = std::min(first_index, sorted_peak_db.size() - 1);
    last_index = std::min(last_index, sorted_peak_db.size() - 1);
    if (last_index < first_index)
    {
        std::swap(first_index, last_index);
    }

    double square_sum = 0.0;
    std::size_t sample_count = 0;
    for (std::size_t index = first_index; index <= last_index; ++index)
    {
        const double linear_amplitude = decibelsToLinearAmplitude(sorted_peak_db[index]);
        square_sum += linear_amplitude * linear_amplitude;
        ++sample_count;
    }

    const double mean_square = square_sum / static_cast<double>(sample_count);
    return linearAmplitudeToDecibels(std::sqrt(mean_square));
}

// Every window must hold a sample, or a stage would end before it began.
static_assert(inputCalibrationSettleSampleCount() > 0);
static_assert(inputCalibrationWaitSampleCount() > 0);
static_assert(inputCalibrationMeasurementSampleCount() > 0);

// Advances the deterministic capture state machine by one raw meter sample.
InputCalibrationStep InputCalibrationCapture::pushSample(AudioMeterLevel level)
{
    switch (m_stage)
    {
        case InputCalibrationStage::Settling:
        {
            if (--m_settle_samples_remaining == 0)
            {
                m_stage = InputCalibrationStage::WaitingForInput;
            }
            return m_stage;
        }
        case InputCalibrationStage::WaitingForInput:
        {
            if (level.clipping || level.peak_db >= clippingAudioMeterDb())
            {
                return InputCalibrationError{
                    .code = InputCalibrationErrorCode::InputClipped,
                    .message = "Input clipped. Lower the interface input gain and try again.",
                };
            }

            if (level.peak_db >= minimumInputCalibrationSignalDb())
            {
                m_stage = InputCalibrationStage::Measuring;
                return pushMeasurementSample(level);
            }

            if (--m_wait_samples_remaining == 0)
            {
                return InputCalibrationError{
                    .code = InputCalibrationErrorCode::NoUsableSignal,
                    .message =
                        "No usable input signal was detected. Check the input and try again.",
                };
            }
            return m_stage;
        }
        case InputCalibrationStage::Measuring:
        {
            return pushMeasurementSample(level);
        }
    }

    return m_stage;
}

// Adds one sample to the fixed measurement window and finalizes it when the window ends.
InputCalibrationStep InputCalibrationCapture::pushMeasurementSample(AudioMeterLevel level)
{
    m_accumulator.pushSample(level);
    if (--m_measurement_samples_remaining > 0)
    {
        return m_stage;
    }

    auto result = calculateInputCalibration(m_accumulator.measurement());
    if (!result.has_value())
    {
        return std::move(result.error());
    }
    return *result;
}

// Calculates the gain that moves the measured input toward the project calibration targets.
std::expected<InputCalibrationResult, InputCalibrationError> calculateInputCalibration(
    const InputCalibrationMeasurement& measurement)
{
    if (measurement.loudest_level.clipping ||
        measurement.loudest_level.peak_db >= clippingAudioMeterDb())
    {
        return std::unexpected{InputCalibrationError{
            .code = InputCalibrationErrorCode::InputClipped,
            .message = "Input clipped. Lower the interface input gain and try again.",
        }};
    }

    if (measurement.active_sample_count == 0 ||
        measurement.active_sample_count < minimumInputCalibrationActiveSampleCount() ||
        measurement.loudest_level.peak_db < minimumInputCalibrationSignalDb())
    {
        return std::unexpected{InputCalibrationError{
            .code = InputCalibrationErrorCode::NoUsableSignal,
            .message = "Not enough steady input was detected. Strum steadily and try again.",
        }};
    }

    if (measurement.active_peak_spread_db > maximumInputCalibrationActivePeakSpreadDb())
    {
        return std::unexpected{InputCalibrationError{
            .code = InputCalibrationErrorCode::InputInconsistent,
            .message = "Input level varied too much. Use steady moderate strums and try again.",
        }};
    }

    const double reference_peak_db =
        measurement.reference_peak_db >= minimumInputCalibrationSignalDb()
            ? measurement.reference_peak_db
            : measurement.loudest_level.peak_db;
    const double rms_gain_db = inputCalibrationTargetRmsDb() - measurement.active_rms_db;
    const double peak_limited_gain_db = inputCalibrationTargetPeakDb() - reference_peak_db;

    return InputCalibrationResult{
        .calibration_gain = clampGain(
            Gain{quantizeInputCalibrationGainDb(std::min(rms_gain_db, peak_limited_gain_db))}),
    };
}

} // namespace rock_hero::common::audio
