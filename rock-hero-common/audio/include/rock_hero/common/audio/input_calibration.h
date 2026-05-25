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

namespace rock_hero::common::audio
{

/*! \brief Stable failure reasons for input calibration measurement. */
enum class InputCalibrationErrorCode
{
    /*! \brief Measurement did not contain enough signal to produce a useful gain. */
    NoUsableSignal,

    /*! \brief Measurement clipped and the hardware input gain must be lowered. */
    InputClipped,
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

    /*! \brief RMS of raw meter-window peaks that met the usable-signal threshold. */
    double active_rms_db{minimumAudioMeterDb()};

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

/*! \brief Accumulates raw input meter samples for one calibration measurement window. */
class InputCalibrationAccumulator final
{
public:
    /*! \brief Clears the accumulated peak and clip state. */
    void reset()
    {
        m_measurement = {};
        m_active_square_sum = 0.0;
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
        m_measurement.active_sample_count += 1;
        const double active_mean_square =
            m_active_square_sum / static_cast<double>(m_measurement.active_sample_count);
        m_measurement.active_rms_db = linearAmplitudeToDecibels(std::sqrt(active_mean_square));
    }

    /*!
    \brief Returns the accumulated calibration measurement.
    \return Loudest level and clip state observed since the last reset().
    */
    [[nodiscard]] InputCalibrationMeasurement measurement() const
    {
        return m_measurement;
    }

private:
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

    InputCalibrationMeasurement m_measurement{};
    double m_active_square_sum{0.0};
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
        measurement.loudest_level.peak_db < minimumInputCalibrationSignalDb())
    {
        return std::unexpected{InputCalibrationError{
            .code = InputCalibrationErrorCode::NoUsableSignal,
            .message = "No usable input signal was detected. Check the input and try again.",
        }};
    }

    const double rms_gain_db = inputCalibrationTargetRmsDb() - measurement.active_rms_db;
    const double peak_limited_gain_db =
        inputCalibrationTargetPeakDb() - measurement.loudest_level.peak_db;

    return InputCalibrationResult{
        .calibration_gain = clampGain(Gain{std::min(rms_gain_db, peak_limited_gain_db)}),
        .measured_level = measurement.loudest_level,
        .measured_rms_db = measurement.active_rms_db,
    };
}

} // namespace rock_hero::common::audio
