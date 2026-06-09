/*!
\file input_calibration.h
\brief Shared input calibration measurement policy.
*/

#pragma once

#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <rock_hero/common/audio/audio_meter_snapshot.h>
#include <rock_hero/common/audio/gain.h>
#include <string>
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

/*!
\brief Returns the lowest peak accepted as a usable calibration signal.
\return Minimum usable raw input peak in decibels full scale.
*/
[[nodiscard]] constexpr double minimumInputCalibrationSignalDb() noexcept
{
    return -40.0;
}

/*!
\brief Returns the minimum active meter windows required for a useful calibration.
\return Minimum active meter-window count.
*/
[[nodiscard]] constexpr std::size_t minimumInputCalibrationActiveSampleCount() noexcept
{
    return 12;
}

/*!
\brief Returns the high active peak percentile used as the calibration reference.
\return Percentile in [0, 1] used for the robust peak reference.
*/
[[nodiscard]] constexpr double inputCalibrationReferencePeakPercentile() noexcept
{
    return 0.90;
}

/*!
\brief Returns the low active peak percentile used for consistency checks.
\return Percentile in [0, 1] used as the low consistency bound.
*/
[[nodiscard]] constexpr double inputCalibrationConsistencyLowPercentile() noexcept
{
    return 0.10;
}

/*!
\brief Returns the maximum accepted spread between active peak percentiles.
\return Maximum accepted spread in decibels.
*/
[[nodiscard]] constexpr double maximumInputCalibrationActivePeakSpreadDb() noexcept
{
    return 14.0;
}

/*!
\brief Returns the gain increment used to make repeated calibration output stable.
\return Quantization step in decibels.
*/
[[nodiscard]] constexpr double inputCalibrationGainStepDb() noexcept
{
    return 0.5;
}

/*! \brief Accumulates raw input meter samples for one calibration measurement window. */
class InputCalibrationAccumulator final
{
public:
    /*! \brief Clears the accumulated peak and clip state. */
    void reset();

    /*!
    \brief Adds one raw input meter sample to the measurement window.
    \param level Meter level sampled from the raw input route.
    */
    void pushSample(AudioMeterLevel level);

    /*!
    \brief Returns the accumulated calibration measurement.
    \return Peak, RMS, robust reference, and consistency data observed since reset().
    */
    [[nodiscard]] InputCalibrationMeasurement measurement() const;

private:
    // Reads one nearest-rank index from an already sorted active peak sequence.
    [[nodiscard]] static std::size_t percentileIndex(
        const std::vector<double>& sorted_peak_db, double percentile) noexcept;

    // Converts a dBFS meter level into linear amplitude for RMS accumulation.
    [[nodiscard]] static double decibelsToLinearAmplitude(double db) noexcept;

    // Converts a positive linear RMS amplitude back to a bounded dBFS value.
    [[nodiscard]] static double linearAmplitudeToDecibels(double linear_amplitude) noexcept;

    // Computes RMS from a sorted inclusive dB range after percentile trimming.
    [[nodiscard]] static double rmsDbForSortedRange(
        const std::vector<double>& sorted_peak_db, std::size_t first_index,
        std::size_t last_index) noexcept;

    InputCalibrationMeasurement m_measurement{};
    double m_active_square_sum{0.0};
    std::vector<double> m_active_peak_db;
};

/*! \brief State of the raw-input capture pass used by automatic calibration. */
enum class InputCalibrationCapturePhase
{
    /*! \brief No automatic calibration capture is active. */
    Idle,

    /*! \brief Recently reset backend route samples are being discarded. */
    Settling,

    /*! \brief Capture is waiting for the player to produce a usable signal. */
    WaitingForInput,

    /*! \brief Capture is accumulating the fixed active measurement window. */
    Measuring,

    /*! \brief Capture completed and produced a calibration result. */
    Complete,

    /*! \brief Capture stopped because a recoverable calibration error occurred. */
    Failed,
};

/*!
\brief Result of advancing an automatic calibration capture by one meter sample.

The phase field is the status discriminant; result and error carry the payload for terminal phases.
*/
struct [[nodiscard]] InputCalibrationCaptureUpdate
{
    /*! \brief Capture phase after the sample was processed. */
    InputCalibrationCapturePhase phase{InputCalibrationCapturePhase::Idle};

    /*! \brief Calibration result when phase is Complete. */
    std::optional<InputCalibrationResult> result;

    /*! \brief Recoverable calibration error when phase is Failed. */
    std::optional<InputCalibrationError> error;
};

/*! \brief Deterministic state machine for automatic input calibration capture. */
class InputCalibrationCapture final
{
public:
    /*!
    \brief Creates a capture pass with fixed meter-window counts.
    \param settle_sample_count Number of initial samples to discard after route reset.
    \param wait_sample_count Number of quiet samples accepted while waiting for input.
    \param measurement_sample_count Number of samples in the active measurement window.
    */
    InputCalibrationCapture(
        std::size_t settle_sample_count, std::size_t wait_sample_count,
        std::size_t measurement_sample_count);

    /*! \brief Starts a new automatic capture pass and clears any previous measurement. */
    void start();

    /*! \brief Returns the capture to its inactive state and clears the measurement. */
    void reset();

    /*!
    \brief Advances the capture by one raw input meter sample.
    \param level Raw input meter level sampled from the calibration route.
    \return Current phase plus a result or error when capture has ended.
    */
    [[nodiscard]] InputCalibrationCaptureUpdate pushSample(AudioMeterLevel level);

    /*!
    \brief Returns the current capture phase.
    \return Current phase.
    */
    [[nodiscard]] InputCalibrationCapturePhase phase() const noexcept;

    /*!
    \brief Reports whether the capture is currently consuming measurement samples.
    \return True while settling, waiting for input, or measuring.
    */
    [[nodiscard]] bool active() const noexcept;

private:
    [[nodiscard]] InputCalibrationCaptureUpdate currentUpdate() const;
    [[nodiscard]] InputCalibrationCaptureUpdate fail(InputCalibrationError error);
    [[nodiscard]] InputCalibrationCaptureUpdate pushMeasurementSample(AudioMeterLevel level);

    InputCalibrationAccumulator m_accumulator;
    std::size_t m_settle_samples_remaining{0};
    std::size_t m_wait_samples_remaining{0};
    std::size_t m_measurement_samples_remaining{0};
    std::size_t m_settle_sample_count{0};
    std::size_t m_wait_sample_count{0};
    std::size_t m_measurement_sample_count{1};
    InputCalibrationCapturePhase m_phase{InputCalibrationCapturePhase::Idle};
};

/*!
\brief Returns the desired hard-strum peak after calibration.
\return Target peak in decibels full scale.
*/
[[nodiscard]] constexpr double inputCalibrationTargetPeakDb() noexcept
{
    return -6.0;
}

/*!
\brief Returns the desired active-window RMS after calibration.
\return Target RMS in decibels full scale.
*/
[[nodiscard]] constexpr double inputCalibrationTargetRmsDb() noexcept
{
    return -12.0;
}

/*!
\brief Rounds a calibration gain to a stable display and storage increment.
\param gain_db Raw calibration gain in decibels.
\return Gain rounded to the nearest inputCalibrationGainStepDb() increment.
*/
[[nodiscard]] inline double quantizeInputCalibrationGainDb(double gain_db) noexcept
{
    return std::round(gain_db / inputCalibrationGainStepDb()) * inputCalibrationGainStepDb();
}

/*!
\brief Calculates calibration gain from one active RMS measurement plus a peak headroom limit.
\param measurement Raw input RMS and peak measurement.
\return Calibration gain and measured levels, or a typed measurement failure.
*/
[[nodiscard]] std::expected<InputCalibrationResult, InputCalibrationError>
calculateInputCalibration(const InputCalibrationMeasurement& measurement);

} // namespace rock_hero::common::audio
