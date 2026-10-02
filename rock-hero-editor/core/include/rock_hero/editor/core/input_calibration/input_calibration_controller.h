/*!
\file input_calibration_controller.h
\brief Headless presentation controller for the input calibration popup.
*/

#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <rock_hero/common/audio/input/audio_meter_snapshot.h>
#include <rock_hero/common/audio/input/live_input_monitor_error.h>
#include <rock_hero/common/audio/input/live_input_sample.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <rock_hero/editor/core/input_calibration/i_input_calibration_view.h>
#include <rock_hero/editor/core/input_calibration/input_calibration_view_state.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Owns popup-local input calibration state without depending on JUCE widgets.

The shared live-input monitor runs the measurement; this controller samples it through a narrow host
boundary once per UI tick, projects the readings into InputCalibrationViewState, and emits the
popup's intents through the same host.
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
        \brief Hands the current live-input route to an automatic measurement.
        \param pickups The pickups the player measures with.
        \return Empty success, or a typed live-input failure.
        */
        [[nodiscard]] virtual std::expected<void, common::audio::LiveInputMonitorError>
        startInputCalibrationMeasurement(common::audio::PickupClass pickups) = 0;

        /*!
        \brief Reads the raw input once, advancing a measurement in progress.
        \return The raw level, and the measurement's progress if one was running.
        */
        [[nodiscard]] virtual common::audio::LiveInputSample sampleInputCalibration() = 0;

        /*!
        \brief Stores the shown calibration gain.
        \param gain_db Gain in decibels.
        \return Empty success, or a typed live-input failure.
        */
        [[nodiscard]] virtual std::expected<void, common::audio::LiveInputMonitorError>
        applyInputCalibration(double gain_db) = 0;

        /*! \brief Closes the input calibration popup, ending any measurement. */
        virtual void closeInputCalibration() = 0;

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

    /*!
    \brief Creates a popup-local controller.
    \param host Boundary used for editor-runtime side effects.
    \param prompt Initial prompt state supplied by the editor workflow.
    */
    InputCalibrationController(Host& host, const InputCalibrationPrompt& prompt);

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

    /*!
    \brief Chooses a known interface: its derived gain fills the slider, and the status says how
    far to trust the figure and how to set the interface up for it.
    \param index Row of common::audio::knownInterfaces().
    */
    void onInterfaceSelected(std::size_t index);

    /*!
    \brief Opens or closes the "Calibrate by playing" section. Opening shows the setup the
    measurement needs; closing returns to the idle message. Ignored while measuring.
    \param open True to open the section.
    */
    void onMeasurementSectionToggled(bool open);

    /*!
    \brief Chooses the pickups the automatic measurement assumes.
    \param pickups The pickups the player will measure with.
    */
    void onPickupsSelected(common::audio::PickupClass pickups);

    /*!
    \brief Stores the shown gain through the host and closes the popup; a refused store keeps it
    open with the reason.
    */
    void onApplyRequested();

    /*! \brief Starts an automatic measurement when the host can hand over the route. */
    void onMeasurementStartRequested();

    /*!
    \brief Samples the raw input once.

    The UI calls it at common::audio::inputCalibrationSampleRateHz() while the popup is open.
    */
    void onSampleTick();

    /*! \brief Reports that local input calibration documentation could not be opened. */
    void onDocumentationUnavailable();

    /*! \brief Closes the popup through the host without storing anything. */
    void onCloseRequested();

private:
    void setDisplayedInputGain(double gain_db);
    void finishMeasurementSuccess(const common::audio::InputCalibrationMeasured& measured);
    void finishMeasurementError(std::string message);
    void setRestingStatus(std::string text);
    void publishState();

    Host& m_host;
    IInputCalibrationView* m_view{};
    InputCalibrationViewState m_state;
    common::audio::AudioMeterLevel m_last_raw_meter_level;
    // The route's committed gain, absent until this input is calibrated.
    std::optional<double> m_committed_input_gain_db;
    // The status outside the measuring section's own texts (setup, waiting, countdown).
    std::string m_resting_status;
};

} // namespace rock_hero::editor::core
