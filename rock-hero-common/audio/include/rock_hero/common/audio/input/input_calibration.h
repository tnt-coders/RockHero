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
#include <string_view>
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

/*! \brief Why a calibration measurement ended without a gain. */
enum class InputCalibrationFailure : std::uint8_t
{
    /*! \brief Too little of the listen was heard as playing to set a gain from. */
    NoUsableSignal,

    /*! \brief The input clipped, which no gain can undo; the hardware input gain must come down. */
    InputClipped,

    /*! \brief The route changed or the session closed before the measurement finished. */
    Interrupted,
};

/*!
\brief Words a failure the one way every surface shows it.
\param failure Why the measurement ended.
\return A sentence ending in a period, suitable for display.
*/
[[nodiscard]] std::string_view inputCalibrationFailureText(
    InputCalibrationFailure failure) noexcept;

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

/*!
\brief Returns a calibration gain in its normal form: rounded to the step and clamped to the
supported range. Every calibration gain, typed, measured, derived or stored, takes this form.
\param gain_db Raw calibration gain in decibels.
\return The normalized gain.
*/
[[nodiscard]] inline Gain normalizedInputCalibrationGain(double gain_db) noexcept
{
    return clampGain(Gain{quantizeInputCalibrationGainDb(gain_db)});
}

/*!
\brief Words a calibration gain the one way every surface shows it, the guide's tables included.
\param gain_db Calibration gain in decibels.
\return The quantized gain, signed to one decimal, and "0.0" without a sign for no change.
*/
[[nodiscard]] std::string inputCalibrationGainText(double gain_db);

/*!
\brief Returns the one instruction for how to play while a gain is measured or checked against
the meter's mark: how the strums the pickup types' peaks come from were played.
\return A sentence ending in a period.
*/
[[nodiscard]] constexpr std::string_view hardStrumInstructionText() noexcept
{
    return "Strum full chords with a pick, as hard as the loudest part of a song you play.";
}

/*!
\brief Returns the one instruction for setting the guitar up before a gain is measured or checked
against the meter's mark: the setup the pickup types' peaks assume.
\return Sentences ending in a period.
*/
[[nodiscard]] constexpr std::string_view guitarSetupInstructionText() noexcept
{
    return "Plug the guitar straight into the instrument (Hi-Z) input, with no pedals in between. "
           "Turn its volume and tone all the way up and select one pickup.";
}

/*!
\brief Words the gain formula against the one reference, for a player typing a gain.
\return A sentence ending in a period.
*/
[[nodiscard]] std::string inputCalibrationGainFormulaText();

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
    std::variant<InputCalibrationRunning, InputCalibrationResult, InputCalibrationFailure>;

/*!
\brief Deterministic state machine for one automatic input calibration capture.

The player plays as hard as they will play in a song. The capture waits, with no limit, for the
first window above the listening threshold (ignoring a short settle span first), then listens for a
fixed span from it and sets the gain so the playing's ceiling lands on the target it was given. A
clipped window after the settle span fails the capture at once, since no gain can undo a clip at
the audio device. A capture is discarded once a sample returns a result or a failure.
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
    [[nodiscard]] InputCalibrationRunning progress() const noexcept;

    // The peaks of the listened windows loud enough to count as playing.
    std::vector<double> m_active_peak_db;
    std::size_t m_settle_samples_remaining{inputCalibrationSettleSampleCount()};
    std::size_t m_listen_samples_remaining{inputCalibrationListenSampleCount()};
    bool m_listening{false};
    double m_target_peak_db;
};

/*!
\brief Calculates the gain that puts the playing's ceiling on a target.

The ceiling is the inputCalibrationCeilingPercentile() nearest-rank peak, so one stray spike does
not set the gain.
\param active_peak_db Peaks of the windows heard as playing, in any order.
\param target_peak_db Where the ceiling should land after the gain.
\return Calibration gain with the ceiling it was set from, or NoUsableSignal when fewer than
        minimumInputCalibrationActiveSampleCount() windows were heard.
*/
[[nodiscard]] std::expected<InputCalibrationResult, InputCalibrationFailure>
calculateInputCalibration(std::vector<double> active_peak_db, double target_peak_db);

} // namespace rock_hero::common::audio
