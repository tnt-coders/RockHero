/*!
\file input_calibration_controller.h
\brief Headless presentation controller for the input calibration popup.
*/

#pragma once

#include <cstddef>
#include <expected>
#include <rock_hero/common/audio/input/audio_meter_snapshot.h>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/common/audio/input/live_input_error.h>
#include <rock_hero/editor/core/editor_view_state.h>
#include <rock_hero/editor/core/input_calibration/i_input_calibration_view.h>
#include <rock_hero/editor/core/input_calibration/input_calibration_view_state.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Owns popup-local input calibration state without depending on JUCE widgets.

The controller consumes raw meter samples supplied by the UI boundary, runs the deterministic
capture state machine, projects popup controls into InputCalibrationViewState, and emits
calibration intents through a narrow host boundary.
*/
class InputCalibrationController final
{
public:
    /*! \brief Host operations that leave popup-local state and touch editor runtime state. */
    class Host
    {
    public:
        /*! \brief Destroys the input-calibration host interface. */
        virtual ~Host() = default;

        /*!
        \brief Prepares the current live-input route for automatic measurement.
        \return Empty success, or a typed live-input failure.
        */
        [[nodiscard]] virtual std::expected<void, common::audio::LiveInputError>
        startInputCalibrationMeasurement() = 0;

        /*! \brief Cancels a measurement and restores editor-owned live-input state. */
        virtual void cancelInputCalibrationMeasurement() = 0;

        /*!
        \brief Applies the gain produced by a successful automatic measurement.
        \param gain_db Gain in decibels.
        \return Empty success, or a typed live-input failure.
        */
        [[nodiscard]] virtual std::expected<void, common::audio::LiveInputError>
        applyAutomaticInputCalibration(double gain_db) = 0;

        /*!
        \brief Applies a manually selected calibration gain.
        \param gain_db Gain in decibels.
        \return Empty success, or a typed live-input failure.
        */
        [[nodiscard]] virtual std::expected<void, common::audio::LiveInputError>
        applyManualInputCalibration(double gain_db) = 0;

        /*! \brief Dismisses the input calibration popup. */
        virtual void dismissInputCalibration() = 0;

    protected:
        /*! \brief Creates the input-calibration host interface. */
        Host() = default;

        /*! \brief Copies the input-calibration host interface. */
        Host(const Host&) = default;

        /*! \brief Moves the input-calibration host interface. */
        Host(Host&&) = default;

        /*!
        \brief Assigns the input-calibration host interface from another host.
        \return Reference to this host interface.
        */
        Host& operator=(const Host&) = default;

        /*!
        \brief Move-assigns the input-calibration host interface from another host.
        \return Reference to this host interface.
        */
        Host& operator=(Host&&) = default;
    };

    /*! \brief Meter-window counts used by one automatic calibration capture pass. */
    struct CaptureSettings
    {
        /*! \brief Number of initial samples discarded after route reset. */
        std::size_t settle_sample_count{0};

        /*! \brief Number of quiet samples accepted while waiting for usable input. */
        std::size_t wait_sample_count{0};

        /*! \brief Number of active samples used for measurement. */
        std::size_t measurement_sample_count{1};
    };

    /*!
    \brief Creates a popup-local controller.
    \param host Boundary used for editor-runtime side effects.
    \param prompt Initial prompt state supplied by the editor workflow.
    \param capture_settings Fixed meter-window counts for automatic capture.
    */
    InputCalibrationController(
        Host& host, const InputCalibrationPrompt& prompt, CaptureSettings capture_settings);

    /*! \brief Copies are disabled because the controller stores popup view attachment state. */
    InputCalibrationController(const InputCalibrationController&) = delete;

    /*! \brief Copy assignment is disabled because the controller stores a host reference. */
    InputCalibrationController& operator=(const InputCalibrationController&) = delete;

    /*! \brief Moves are disabled because the attached view stores no back-reference update hook. */
    InputCalibrationController(InputCalibrationController&&) = delete;

    /*! \brief Move assignment is disabled because the controller stores a host reference. */
    InputCalibrationController& operator=(InputCalibrationController&&) = delete;

    /*! \brief Destroys the InputCalibrationController. */
    ~InputCalibrationController() = default;

    /*!
    \brief Attaches a view and immediately pushes the current state.
    \param view View to update while attached.
    */
    void attachView(IInputCalibrationView& view);

    /*!
    \brief Detaches a view if it is still attached.
    \param view View being destroyed or disconnected.
    */
    void detachView(IInputCalibrationView& view) noexcept;

    /*!
    \brief Updates manual gain preview state.
    \param gain_db Gain in decibels selected by the user.
    */
    void onManualGainChanged(double gain_db);

    /*! \brief Applies the current manual gain through the host. */
    void onManualApplyRequested();

    /*!
    \brief Starts automatic measurement when the host can prepare the route.
    \return True when the UI should prime the raw meter reader after route reset.
    */
    [[nodiscard]] bool onMeasurementStartRequested();

    /*!
    \brief Advances popup state from one raw input meter sample.
    \param raw_level Raw input meter level sampled by the UI boundary.
    */
    void onMeterSampled(common::audio::AudioMeterLevel raw_level);

    /*! \brief Reports that the UI boundary cannot supply raw input samples. */
    void onMeterSourceUnavailable();

    /*! \brief Reports that local input calibration documentation could not be opened. */
    void onDocumentationUnavailable();

    /*! \brief Emits the popup-dismissal intent through the host. */
    void onDismissRequested();

private:
    void setDisplayedInputGain(double gain_db);
    void finishMeasurementSuccess(
        const common::audio::InputCalibrationResult& result,
        common::audio::AudioMeterLevel raw_level);
    void finishMeasurementError(std::string message);
    void publishState();

    Host& m_host;
    common::audio::InputCalibrationCapture m_capture;
    IInputCalibrationView* m_view{};
    InputCalibrationViewState m_state;
    common::audio::InputCalibrationCapturePhase m_last_capture_phase{
        common::audio::InputCalibrationCapturePhase::Idle,
    };
    common::audio::AudioMeterLevel m_last_raw_meter_level;
    double m_committed_input_gain_db{0.0};
    double m_measurement_restore_gain_db{0.0};
};

} // namespace rock_hero::editor::core
