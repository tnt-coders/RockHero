/*!
\file input_calibration_controller.h
\brief Headless editor controller for the input calibration popup.
*/

#pragma once

#include <rock_hero/common/audio/audio_meter_snapshot.h>
#include <rock_hero/common/audio/i_live_input.h>
#include <rock_hero/common/audio/input_calibration.h>
#include <rock_hero/editor/core/i_input_calibration_controller.h>
#include <rock_hero/editor/core/i_input_calibration_view.h>
#include <rock_hero/editor/core/i_input_calibration_workflow.h>
#include <rock_hero/editor/core/input_calibration_view_state.h>

namespace rock_hero::editor::core
{

/*! \brief Returns the live meter tick rate expected by InputCalibrationController. */
[[nodiscard]] constexpr int inputCalibrationMeterHz() noexcept
{
    return 30;
}

/*! \brief Headless editor workflow controller for one input calibration popup. */
class InputCalibrationController final : public IInputCalibrationController
{
public:
    /*!
    \brief Creates the controller around the editor workflow and live input meter source.
    \param workflow Editor workflow that applies route changes and persists calibration.
    \param live_input Optional live input meter source sampled while the popup is open.
    \param prompt Controller-derived prompt data used to seed display state.
    */
    InputCalibrationController(
        IInputCalibrationWorkflow& workflow, const common::audio::ILiveInput* live_input,
        InputCalibrationPrompt prompt);

    /*!
    \brief Attaches the concrete view and pushes the current calibration state.
    \param view View to update until this controller is destroyed.
    */
    void attachView(IInputCalibrationView& view);

    void onAutomaticCalibrationRequested() override;
    void onManualGainChanged(double gain_db) override;
    void onManualCalibrationRequested() override;
    void onMeterTick() override;
    void onDismissRequested() override;

private:
    void updateView();
    void refreshInputMeter();
    void setDisplayedInputGain(double gain_db);
    void setStatusText(std::string text);
    void finishMeasurementSuccess(
        const common::audio::InputCalibrationResult& result, common::audio::AudioMeterLevel level);
    void finishMeasurementError(std::string message);
    [[nodiscard]] common::audio::AudioMeterLevel displayLevel(
        common::audio::AudioMeterLevel level) const;

    // Editor workflow that owns route setup, persistence, and prompt visibility.
    IInputCalibrationWorkflow& m_workflow;

    // Optional live input port used only as a raw meter source by this controller.
    const common::audio::ILiveInput* m_live_input{};

    // Non-owning view binding installed by attachView().
    IInputCalibrationView* m_view{};

    InputCalibrationViewState m_state{};
    common::audio::InputCalibrationCapture m_capture;
    common::audio::InputCalibrationCapturePhase m_last_capture_phase{
        common::audio::InputCalibrationCapturePhase::Idle,
    };
    double m_input_gain_db{0.0};
    double m_committed_input_gain_db{0.0};
    double m_measurement_restore_gain_db{0.0};
};

} // namespace rock_hero::editor::core
