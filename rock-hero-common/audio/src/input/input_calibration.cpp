#include "input/input_calibration.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

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

// The listen must hold a sample, or it would end before it began.
static_assert(inputCalibrationListenSampleCount() > 0);

// A nearest rank of at least one needs a percentile above zero.
static_assert(
    inputCalibrationCeilingPercentile() > 0.0 && inputCalibrationCeilingPercentile() <= 1.0);

// The ceiling's rank exists only once something was heard.
static_assert(minimumInputCalibrationActiveSampleCount() > 0);

InputCalibrationCapture::InputCalibrationCapture(double target_peak_db) noexcept
    : m_target_peak_db{target_peak_db}
{}

// Advances the deterministic capture state machine by one raw meter sample.
InputCalibrationStep InputCalibrationCapture::pushSample(AudioMeterLevel level)
{
    // The settle span is part of the wait: its windows neither start the listen nor clip it.
    if (m_settle_samples_remaining > 0)
    {
        --m_settle_samples_remaining;
        return progress();
    }

    if (level.clipping)
    {
        return inputCalibrationError(InputCalibrationErrorCode::InputClipped);
    }

    const bool heard = level.peak_db >= minimumInputCalibrationSignalDb();
    if (!m_listening)
    {
        // The first window the player is heard ends the wait and is the first one listened to.
        if (!heard)
        {
            return progress();
        }
        m_listening = true;
    }

    if (heard)
    {
        m_active_peak_db.push_back(level.peak_db);
    }
    if (--m_listen_samples_remaining > 0)
    {
        return progress();
    }

    auto result = calculateInputCalibration(std::move(m_active_peak_db), m_target_peak_db);
    if (!result.has_value())
    {
        return std::move(result.error());
    }
    return *result;
}

// Reports the current stage, with the windows the listen still needs once it has begun.
InputCalibrationRunning InputCalibrationCapture::progress() const noexcept
{
    if (m_listening)
    {
        return InputCalibrationListening{.windows_remaining = m_listen_samples_remaining};
    }
    return InputCalibrationWaiting{};
}

// Sets the gain that puts the playing's ceiling, its nearest-rank percentile peak, on the target.
std::expected<InputCalibrationResult, InputCalibrationError> calculateInputCalibration(
    std::vector<double> active_peak_db, double target_peak_db)
{
    if (active_peak_db.size() < minimumInputCalibrationActiveSampleCount())
    {
        return std::unexpected{inputCalibrationError(InputCalibrationErrorCode::NoUsableSignal)};
    }

    const auto rank = static_cast<std::size_t>(std::ceil(
        inputCalibrationCeilingPercentile() * static_cast<double>(active_peak_db.size())));
    const auto ceiling = active_peak_db.begin() + static_cast<std::ptrdiff_t>(rank - 1);
    std::ranges::nth_element(active_peak_db, ceiling);

    return InputCalibrationResult{
        .calibration_gain =
            clampGain(Gain{quantizeInputCalibrationGainDb(target_peak_db - *ceiling)}),
        .ceiling_peak_db = *ceiling,
    };
}

} // namespace rock_hero::common::audio
