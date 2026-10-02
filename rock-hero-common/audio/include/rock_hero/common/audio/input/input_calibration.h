/*!
\file input_calibration.h
\brief Shared input calibration reference and the automatic measurement's policy.
*/

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <numbers>
#include <rock_hero/common/audio/input/audio_meter_snapshot.h>
#include <rock_hero/common/audio/input/pickup_types.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <string>
#include <variant>
#include <vector>

namespace rock_hero::common::audio
{

/*!
\brief Returns the calibrated domain's reference: the dBu an interface input reads at 0 dBFS.

Every calibration derives from this one figure. An interface whose input reaches 0 dBFS at L dBu
needs a gain of L minus this reference, and the automatic measurement aims each pickup kind's
hard strum at the level its peak voltage reaches against it.
\return Reference level in dBu.
*/
[[nodiscard]] constexpr double inputLevelReferenceDbu() noexcept
{
    return 12.0;
}

/*!
\brief Converts the peak voltage of a sine to dBu, against the 0.7746 V RMS of 0 dBu.
\param volts_peak Peak voltage of the sine.
\return Level in dBu; physics, not policy.
*/
[[nodiscard]] inline double peakVoltsToDbu(double volts_peak) noexcept
{
    constexpr double volts_rms_at_0_dbu{0.7746};
    return 20.0 * std::log10(volts_peak / std::numbers::sqrt2 / volts_rms_at_0_dbu);
}

/*!
\brief Returns where a hard strum on these pickups lands after the gain: what the automatic
measurement puts the playing's ceiling on.
\param pickups The pickups the player measures with.
\return Target peak in decibels full scale.
*/
[[nodiscard]] inline double inputCalibrationTargetPeakDb(PickupClass pickups) noexcept
{
    return peakVoltsToDbu(pickupType(pickups).hard_strum_peak_volts) - inputLevelReferenceDbu();
}

/*! \brief Stable failure reasons for input calibration measurement. */
enum class InputCalibrationErrorCode : std::uint8_t
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

/*! \brief What one automatic measurement heard of the player's hardest playing. */
struct [[nodiscard]] InputCalibrationMeasurement
{
    /*! \brief Loudest raw input level observed during measurement, with any clipping. */
    AudioMeterLevel loudest_level;

    /*!
    \brief The playing's ceiling: a high percentile of the active window peaks, so one stray spike
    does not set the gain.
    */
    double ceiling_peak_db{minimumAudioMeterDb()};

    /*! \brief Count of meter windows at or above the listening threshold. */
    std::size_t active_sample_count{0};
};

/*! \brief Gain result produced from a successful input calibration measurement. */
struct [[nodiscard]] InputCalibrationResult
{
    /*! \brief Calibration gain to apply before the live guitar chain. */
    Gain calibration_gain;

    /*! \brief The playing's measured ceiling the gain was set from, raw, in dBFS. */
    double ceiling_peak_db{};
};

/*!
\brief Returns the listening threshold: quieter meter windows are not playing and do not count.
\return Minimum raw input peak in decibels full scale.
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
\brief Returns the active-peak percentile taken as the playing's ceiling.
\return Percentile in [0, 1].
*/
[[nodiscard]] constexpr double inputCalibrationCeilingPercentile() noexcept
{
    return 0.95;
}

/*!
\brief Returns how many increments a measured calibration resolves per decibel: the manual
slider's 0.1 dB resolution.
\return Increments per decibel.
*/
[[nodiscard]] constexpr double inputCalibrationGainStepsPerDb() noexcept
{
    return 10.0;
}

/*!
\brief Rounds a calibration gain to the inputCalibrationGainStepsPerDb() increment.

Dividing the rounded count by the steps per decibel lands on the double nearest the decimal value,
so a quantized 11.2 dB equals the literal 11.2.
\param gain_db Raw calibration gain in decibels.
\return Rounded gain; a value that rounds to zero is +0.0, never -0.0.
*/
[[nodiscard]] inline double quantizeInputCalibrationGainDb(double gain_db) noexcept
{
    return (std::round(gain_db * inputCalibrationGainStepsPerDb()) /
            inputCalibrationGainStepsPerDb()) +
           0.0;
}

/*! \brief Accumulates raw input meter samples for one calibration measurement. */
class InputCalibrationAccumulator final
{
public:
    /*!
    \brief Adds one raw input meter sample to the measurement.
    \param level Meter level sampled from the raw input route.
    */
    void pushSample(AudioMeterLevel level);

    /*!
    \brief Returns the accumulated calibration measurement.
    \return The loudest level, the playing's ceiling and the active window count so far.
    */
    [[nodiscard]] InputCalibrationMeasurement measurement() const;

private:
    // Reads one nearest-rank index from an already sorted active peak sequence.
    [[nodiscard]] static std::size_t percentileIndex(
        const std::vector<double>& sorted_peak_db, double percentile) noexcept;

    InputCalibrationMeasurement m_measurement{};
    std::vector<double> m_active_peak_db;
};

/*!
\brief Returns how often a driver samples the raw input meter during a measurement.
\return Meter samples per second; the sample counts below are stated at this rate.
*/
[[nodiscard]] constexpr int inputCalibrationSampleRateHz() noexcept
{
    return 30;
}

/*!
\brief Returns the samples a capture ignores first, so a meter window that began before the
measurement cannot start or end it.
\return Half a second of samples.
*/
[[nodiscard]] constexpr std::size_t inputCalibrationSettleSampleCount() noexcept
{
    return static_cast<std::size_t>(inputCalibrationSampleRateHz()) / 2;
}

/*!
\brief Returns how long a measurement listens, counted from the first window the player is heard.
\return Ten seconds of samples, the first active window included.
*/
[[nodiscard]] constexpr std::size_t inputCalibrationListenSampleCount() noexcept
{
    return static_cast<std::size_t>(inputCalibrationSampleRateHz()) * 10;
}

/*!
\brief A capture waiting for the player to start. It waits as long as it takes: the driver ends a
measurement nobody plays.
*/
struct InputCalibrationWaiting
{
    /*!
    \brief Compares two waiting reports, which are always equal.
    \param lhs Left-hand report.
    \param rhs Right-hand report.
    \return True.
    */
    friend bool operator==(const InputCalibrationWaiting& lhs, const InputCalibrationWaiting& rhs) =
        default;
};

/*! \brief A capture listening to the player's hardest playing. */
struct InputCalibrationListening
{
    /*! \brief Meter windows the listen still needs after the sample that reported it. */
    std::size_t windows_remaining;

    /*!
    \brief Compares two listening reports by the windows they have left.
    \param lhs Left-hand report.
    \param rhs Right-hand report.
    \return True when both have the same windows left.
    */
    friend bool operator==(
        const InputCalibrationListening& lhs, const InputCalibrationListening& rhs) = default;
};

/*! \brief Where a running capture is: waiting for the first strum, or listening. */
using InputCalibrationRunning = std::variant<InputCalibrationWaiting, InputCalibrationListening>;

/*!
\brief Outcome of one capture sample: still running at a stage, finished with a result, or failed.
*/
using InputCalibrationStep =
    std::variant<InputCalibrationRunning, InputCalibrationResult, InputCalibrationError>;

/*!
\brief Deterministic state machine for one automatic input calibration capture.

The player plays as hard as they will play in a song. The capture waits, with no limit, for the
first window above the listening threshold (ignoring a short settle span first), then listens for a
fixed span from it and sets the gain so the playing's ceiling lands on the target it was given. A
capture is discarded once a sample returns a result or an error.
*/
class InputCalibrationCapture final
{
public:
    /*!
    \brief Starts a capture that will put the playing's ceiling on a target.
    \param target_peak_db Where the ceiling should land after the gain, normally
           inputCalibrationTargetPeakDb() for the player's pickups.
    */
    explicit InputCalibrationCapture(double target_peak_db) noexcept;

    /*!
    \brief Advances the capture by one raw input meter sample.
    \param level Raw input meter level sampled from the calibration route.
    \return Where the capture now is, or its result or error once it has finished.
    */
    [[nodiscard]] InputCalibrationStep pushSample(AudioMeterLevel level);

private:
    [[nodiscard]] InputCalibrationStep pushListenSample(AudioMeterLevel level);
    [[nodiscard]] InputCalibrationRunning progress() const noexcept;

    InputCalibrationAccumulator m_accumulator;
    std::size_t m_settle_samples_remaining{inputCalibrationSettleSampleCount()};
    std::size_t m_listen_samples_remaining{inputCalibrationListenSampleCount()};
    bool m_listening{false};
    double m_target_peak_db;
};

/*!
\brief Calculates the gain that puts the playing's ceiling on a target.
\param measurement The measurement the capture accumulated.
\param target_peak_db Where the ceiling should land after the gain.
\return Calibration gain with the ceiling it was set from, or a typed measurement failure.
*/
[[nodiscard]] std::expected<InputCalibrationResult, InputCalibrationError>
calculateInputCalibration(const InputCalibrationMeasurement& measurement, double target_peak_db);

} // namespace rock_hero::common::audio
