/*!
\file input_calibration_capture_session.h
\brief Deterministic automatic input calibration capture state machine.
*/

#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <rock_hero/common/audio/input_calibration.h>
#include <utility>

namespace rock_hero::common::audio
{

/*! \brief State of the raw-input capture session used by automatic calibration. */
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

/*! \brief Result of advancing an automatic calibration capture session by one meter sample. */
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
class InputCalibrationCaptureSession final
{
public:
    /*!
    \brief Creates a capture session with fixed meter-window counts.
    \param settle_sample_count Number of initial samples to discard after route reset.
    \param wait_sample_count Number of quiet samples accepted while waiting for input.
    \param measurement_sample_count Number of samples in the active measurement window.
    */
    InputCalibrationCaptureSession(
        std::size_t settle_sample_count, std::size_t wait_sample_count,
        std::size_t measurement_sample_count)
        : m_settle_sample_count(settle_sample_count)
        , m_wait_sample_count(wait_sample_count)
        , m_measurement_sample_count(std::max<std::size_t>(1, measurement_sample_count))
    {}

    /*! \brief Starts a new automatic capture pass and clears any previous measurement. */
    void start()
    {
        m_accumulator.reset();
        m_settle_samples_remaining = m_settle_sample_count;
        m_wait_samples_remaining = m_wait_sample_count;
        m_measurement_samples_remaining = 0;
        m_phase = m_settle_samples_remaining > 0 ? InputCalibrationCapturePhase::Settling
                                                 : InputCalibrationCapturePhase::WaitingForInput;
    }

    /*! \brief Returns the session to its inactive state and clears the measurement. */
    void reset()
    {
        m_accumulator.reset();
        m_settle_samples_remaining = 0;
        m_wait_samples_remaining = 0;
        m_measurement_samples_remaining = 0;
        m_phase = InputCalibrationCapturePhase::Idle;
    }

    /*!
    \brief Advances the capture session by one raw input meter sample.
    \param level Raw input meter level sampled from the calibration route.
    \return Current phase plus a result or error when capture has ended.
    */
    [[nodiscard]] InputCalibrationCaptureUpdate pushSample(AudioMeterLevel level)
    {
        switch (m_phase)
        {
            case InputCalibrationCapturePhase::Idle:
            case InputCalibrationCapturePhase::Complete:
            case InputCalibrationCapturePhase::Failed:
            {
                return currentUpdate();
            }
            case InputCalibrationCapturePhase::Settling:
            {
                if (m_settle_samples_remaining > 0)
                {
                    --m_settle_samples_remaining;
                }
                if (m_settle_samples_remaining == 0)
                {
                    m_phase = InputCalibrationCapturePhase::WaitingForInput;
                }
                return currentUpdate();
            }
            case InputCalibrationCapturePhase::WaitingForInput:
            {
                if (level.clipping || level.peak_db >= clippingAudioMeterDb())
                {
                    return fail(
                        InputCalibrationError{
                            .code = InputCalibrationErrorCode::InputClipped,
                            .message =
                                "Input clipped. Lower the interface input gain and try again.",
                        });
                }

                if (level.peak_db >= minimumInputCalibrationSignalDb())
                {
                    m_accumulator.reset();
                    m_measurement_samples_remaining = m_measurement_sample_count;
                    m_phase = InputCalibrationCapturePhase::Measuring;
                    return pushMeasurementSample(level);
                }

                if (m_wait_samples_remaining > 0)
                {
                    --m_wait_samples_remaining;
                }
                if (m_wait_samples_remaining == 0)
                {
                    return fail(
                        InputCalibrationError{
                            .code = InputCalibrationErrorCode::NoUsableSignal,
                            .message = "No usable input signal was detected. Check the input and "
                                       "try again.",
                        });
                }
                return currentUpdate();
            }
            case InputCalibrationCapturePhase::Measuring:
            {
                return pushMeasurementSample(level);
            }
        }

        return currentUpdate();
    }

    /*!
    \brief Returns the current capture phase.
    \return Current phase.
    */
    [[nodiscard]] InputCalibrationCapturePhase phase() const noexcept
    {
        return m_phase;
    }

    /*!
    \brief Reports whether the session is currently consuming measurement samples.
    \return True while settling, waiting for input, or measuring.
    */
    [[nodiscard]] bool active() const noexcept
    {
        return m_phase == InputCalibrationCapturePhase::Settling ||
               m_phase == InputCalibrationCapturePhase::WaitingForInput ||
               m_phase == InputCalibrationCapturePhase::Measuring;
    }

private:
    // Builds a neutral update for phases that have no terminal result.
    [[nodiscard]] InputCalibrationCaptureUpdate currentUpdate() const
    {
        return InputCalibrationCaptureUpdate{.phase = m_phase};
    }

    // Records a terminal calibration error and returns it to the caller in one step.
    [[nodiscard]] InputCalibrationCaptureUpdate fail(InputCalibrationError error)
    {
        m_phase = InputCalibrationCapturePhase::Failed;
        return InputCalibrationCaptureUpdate{.phase = m_phase, .error = std::move(error)};
    }

    // Adds one sample to the fixed measurement window and finalizes it when the window ends.
    [[nodiscard]] InputCalibrationCaptureUpdate pushMeasurementSample(AudioMeterLevel level)
    {
        m_accumulator.pushSample(level);
        if (m_measurement_samples_remaining > 0)
        {
            --m_measurement_samples_remaining;
        }
        if (m_measurement_samples_remaining > 0)
        {
            return currentUpdate();
        }

        const auto result = calculateInputCalibration(m_accumulator.measurement());
        if (!result.has_value())
        {
            return fail(result.error());
        }

        m_phase = InputCalibrationCapturePhase::Complete;
        return InputCalibrationCaptureUpdate{.phase = m_phase, .result = *result};
    }

    InputCalibrationAccumulator m_accumulator;
    std::size_t m_settle_samples_remaining{0};
    std::size_t m_wait_samples_remaining{0};
    std::size_t m_measurement_samples_remaining{0};
    std::size_t m_settle_sample_count{0};
    std::size_t m_wait_sample_count{0};
    std::size_t m_measurement_sample_count{1};
    InputCalibrationCapturePhase m_phase{InputCalibrationCapturePhase::Idle};
};

} // namespace rock_hero::common::audio
