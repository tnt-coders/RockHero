/*!
\file input_calibration.h
\brief Shared input calibration measurement policy.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <rock_hero/common/audio/audio_meter_snapshot.h>
#include <rock_hero/common/audio/gain.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

/*! \brief Stable failure reasons for input calibration measurement. */
enum class InputCalibrationErrorCode
{
    /*! \brief Measurement did not contain enough signal to produce a useful gain. */
    NoUsableSignal,

    /*! \brief Measurement clipped and the hardware input gain must be lowered. */
    InputClipped,

    /*! \brief Active input varied too much to produce a stable calibration value. */
    InputInconsistent,
};

/*! \brief Recoverable input calibration failure with displayable detail. */
struct [[nodiscard]] InputCalibrationError
{
    /*! \brief Stable error code used by callers for branching. */
    InputCalibrationErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display. */
    std::string message;
};

/*! \brief Peak and active RMS window captured during one input calibration pass. */
struct [[nodiscard]] InputCalibrationMeasurement
{
    /*! \brief Loudest raw input level observed during measurement. */
    AudioMeterLevel loudest_level;

    /*! \brief Trimmed RMS of raw meter-window peaks that met the usable-signal threshold. */
    double active_rms_db{minimumAudioMeterDb()};

    /*! \brief Robust active peak used for headroom limiting without one-sample spikes. */
    double reference_peak_db{minimumAudioMeterDb()};

    /*! \brief Difference between low and high active peak percentiles. */
    double active_peak_spread_db{0.0};

    /*! \brief Count of meter windows that contributed to active_rms_db. */
    std::size_t active_sample_count{0};
};

/*! \brief Gain result produced from a successful input calibration measurement. */
struct [[nodiscard]] InputCalibrationResult
{
    /*! \brief Calibration gain to apply before the live guitar chain. */
    Gain calibration_gain;

    /*! \brief Peak level used to limit the calibration gain. */
    AudioMeterLevel measured_level;

    /*! \brief Active-window RMS level used to derive the calibration gain. */
    double measured_rms_db{minimumAudioMeterDb()};
};

/*! \brief Returns the lowest peak accepted as a usable calibration signal. */
[[nodiscard]] constexpr double minimumInputCalibrationSignalDb() noexcept
{
    return -40.0;
}

/*! \brief Returns the minimum active meter windows required for a useful calibration. */
[[nodiscard]] constexpr std::size_t minimumInputCalibrationActiveSampleCount() noexcept
{
    return 12;
}

/*! \brief Returns the high active peak percentile used as the calibration reference. */
[[nodiscard]] constexpr double inputCalibrationReferencePeakPercentile() noexcept
{
    return 0.90;
}

/*! \brief Returns the low active peak percentile used for consistency checks. */
[[nodiscard]] constexpr double inputCalibrationConsistencyLowPercentile() noexcept
{
    return 0.10;
}

/*! \brief Returns the maximum accepted spread between active peak percentiles. */
[[nodiscard]] constexpr double maximumInputCalibrationActivePeakSpreadDb() noexcept
{
    return 14.0;
}

/*! \brief Returns the gain increment used to make repeated calibration output stable. */
[[nodiscard]] constexpr double inputCalibrationGainStepDb() noexcept
{
    return 0.5;
}

/*! \brief Accumulates raw input meter samples for one calibration measurement window. */
class InputCalibrationAccumulator final
{
public:
    /*! \brief Clears the accumulated peak and clip state. */
    void reset()
    {
        m_measurement = {};
        m_active_square_sum = 0.0;
        m_active_peak_db.clear();
    }

    /*!
    \brief Adds one raw input meter sample to the measurement window.
    \param level Meter level sampled from the raw input route.
    */
    void pushSample(AudioMeterLevel level)
    {
        m_measurement.loudest_level.peak_db =
            std::max(m_measurement.loudest_level.peak_db, level.peak_db);
        m_measurement.loudest_level.clipping =
            m_measurement.loudest_level.clipping || level.clipping;

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

    /*!
    \brief Returns the accumulated calibration measurement.
    \return Peak, RMS, robust reference, and consistency data observed since reset().
    */
    [[nodiscard]] InputCalibrationMeasurement measurement() const
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

private:
    // Reads one nearest-rank index from an already sorted active peak sequence.
    [[nodiscard]] static std::size_t percentileIndex(
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
    [[nodiscard]] static double decibelsToLinearAmplitude(double db) noexcept
    {
        return std::pow(10.0, db / 20.0);
    }

    // Converts a positive linear RMS amplitude back to a bounded dBFS value.
    [[nodiscard]] static double linearAmplitudeToDecibels(double linear_amplitude) noexcept
    {
        if (linear_amplitude <= 0.0)
        {
            return minimumAudioMeterDb();
        }

        return std::clamp(20.0 * std::log10(linear_amplitude), minimumAudioMeterDb(), 12.0);
    }

    // Computes RMS from a sorted inclusive dB range after percentile trimming.
    [[nodiscard]] static double rmsDbForSortedRange(
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

    InputCalibrationMeasurement m_measurement{};
    double m_active_square_sum{0.0};
    std::vector<double> m_active_peak_db;
};

/*! \brief Returns the desired hard-strum peak after calibration. */
[[nodiscard]] constexpr double inputCalibrationTargetPeakDb() noexcept
{
    return -6.0;
}

/*! \brief Returns the desired active-window RMS after calibration. */
[[nodiscard]] constexpr double inputCalibrationTargetRmsDb() noexcept
{
    return -12.0;
}

/*! \brief Rounds a calibration gain to a stable display and storage increment. */
[[nodiscard]] inline double quantizeInputCalibrationGainDb(double gain_db) noexcept
{
    return std::round(gain_db / inputCalibrationGainStepDb()) * inputCalibrationGainStepDb();
}

/*!
\brief Calculates calibration gain from one active RMS measurement plus a peak headroom limit.
\param measurement Raw input RMS and peak measurement.
\return Calibration gain and measured levels, or a typed measurement failure.
*/
[[nodiscard]] inline std::expected<InputCalibrationResult, InputCalibrationError>
calculateInputCalibration(const InputCalibrationMeasurement& measurement)
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
        .measured_level = measurement.loudest_level,
        .measured_rms_db = measurement.active_rms_db,
    };
}

} // namespace rock_hero::common::audio
