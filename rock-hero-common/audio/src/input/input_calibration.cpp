#include "input/input_calibration.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// The one wording per capture failure, so every site that reports a code says the same thing.
[[nodiscard]] InputCalibrationError inputCalibrationError(InputCalibrationErrorCode code)
{
    switch (code)
    {
        case InputCalibrationErrorCode::NoUsableSignal:
        {
            return InputCalibrationError{
                .code = code,
                .message = "No usable input signal was detected. Check the input and try again.",
            };
        }
        case InputCalibrationErrorCode::InputClipped:
        {
            return InputCalibrationError{
                .code = code,
                .message = "Input clipped. Lower the interface input gain and try again.",
            };
        }
    }
    return InputCalibrationError{.code = code, .message = "Input calibration failed."};
}

} // namespace

std::string_view pickupClassText(PickupClass pickups) noexcept
{
    switch (pickups)
    {
        case PickupClass::Humbucker:
        {
            return "humbucker";
        }
        case PickupClass::SingleCoil:
        {
            return "single-coil";
        }
    }
    return "humbucker";
}

std::string_view pickupClassNote(PickupClass pickups) noexcept
{
    switch (pickups)
    {
        case PickupClass::Humbucker:
        {
            return "Also P-90 and active.";
        }
        case PickupClass::SingleCoil:
        {
            return "Passive, except P-90.";
        }
    }
    return "Also P-90 and active.";
}

// Records the loudest level and keeps every window loud enough to count as playing.
void InputCalibrationAccumulator::pushSample(AudioMeterLevel level)
{
    m_measurement.loudest_level.peak_db =
        std::max(m_measurement.loudest_level.peak_db, level.peak_db);
    m_measurement.loudest_level.clipping = m_measurement.loudest_level.clipping || level.clipping;

    if (level.peak_db < minimumInputCalibrationSignalDb())
    {
        return;
    }

    m_active_peak_db.push_back(level.peak_db);
    m_measurement.active_sample_count += 1;
}

// Reads the playing's ceiling from the sorted active peaks.
InputCalibrationMeasurement InputCalibrationAccumulator::measurement() const
{
    InputCalibrationMeasurement measurement = m_measurement;
    if (m_active_peak_db.empty())
    {
        return measurement;
    }

    std::vector<double> sorted_peak_db = m_active_peak_db;
    std::ranges::sort(sorted_peak_db);
    measurement.ceiling_peak_db =
        sorted_peak_db[percentileIndex(sorted_peak_db, inputCalibrationCeilingPercentile())];
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

// Every window must hold a sample, or a stage would end before it began.
static_assert(inputCalibrationSettleSampleCount() > 0);
static_assert(inputCalibrationWaitSampleCount() > 0);
static_assert(inputCalibrationListenSampleCount() > 0);

InputCalibrationCapture::InputCalibrationCapture(double target_peak_db) noexcept
    : m_target_peak_db{target_peak_db}
{}

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
            return progress();
        }
        case InputCalibrationStage::WaitingForInput:
        {
            if (level.clipping || level.peak_db >= clippingAudioMeterDb())
            {
                return inputCalibrationError(InputCalibrationErrorCode::InputClipped);
            }

            // The first window the player is heard ends the wait and is the first one listened to.
            if (level.peak_db >= minimumInputCalibrationSignalDb())
            {
                m_stage = InputCalibrationStage::Measuring;
                return pushListenSample(level);
            }

            if (--m_wait_samples_remaining == 0)
            {
                return inputCalibrationError(InputCalibrationErrorCode::NoUsableSignal);
            }
            return progress();
        }
        case InputCalibrationStage::Measuring:
        {
            return pushListenSample(level);
        }
    }

    return progress();
}

// Adds one sample to the fixed listen and finalizes the measurement when the listen ends.
InputCalibrationStep InputCalibrationCapture::pushListenSample(AudioMeterLevel level)
{
    m_accumulator.pushSample(level);
    if (--m_listen_samples_remaining > 0)
    {
        return progress();
    }

    auto result = calculateInputCalibration(m_accumulator.measurement(), m_target_peak_db);
    if (!result.has_value())
    {
        return std::move(result.error());
    }
    return *result;
}

// Reports the current stage with the windows it still needs.
InputCalibrationStageProgress InputCalibrationCapture::progress() const noexcept
{
    switch (m_stage)
    {
        case InputCalibrationStage::Settling:
        {
            return {.stage = m_stage, .windows_remaining = m_settle_samples_remaining};
        }
        case InputCalibrationStage::WaitingForInput:
        {
            return {.stage = m_stage, .windows_remaining = m_wait_samples_remaining};
        }
        case InputCalibrationStage::Measuring:
        {
            return {.stage = m_stage, .windows_remaining = m_listen_samples_remaining};
        }
    }

    return {.stage = m_stage, .windows_remaining = 0};
}

// Sets the gain that puts the playing's ceiling on the target peak.
std::expected<InputCalibrationResult, InputCalibrationError> calculateInputCalibration(
    const InputCalibrationMeasurement& measurement, double target_peak_db)
{
    if (measurement.loudest_level.clipping ||
        measurement.loudest_level.peak_db >= clippingAudioMeterDb())
    {
        return std::unexpected{inputCalibrationError(InputCalibrationErrorCode::InputClipped)};
    }

    if (measurement.active_sample_count < minimumInputCalibrationActiveSampleCount())
    {
        return std::unexpected{inputCalibrationError(InputCalibrationErrorCode::NoUsableSignal)};
    }

    return InputCalibrationResult{
        .calibration_gain = clampGain(
            Gain{quantizeInputCalibrationGainDb(target_peak_db - measurement.ceiling_peak_db)}),
        .ceiling_peak_db = measurement.ceiling_peak_db,
    };
}

} // namespace rock_hero::common::audio
