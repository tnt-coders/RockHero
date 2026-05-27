/*!
\file input_calibration_view.h
\brief Private editor UI view for live input calibration.
*/

#pragma once

#include "audio_level_meter.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <rock_hero/editor/core/i_input_calibration_controller.h>
#include <rock_hero/editor/core/i_input_calibration_view.h>
#include <rock_hero/editor/core/input_calibration_view_state.h>

namespace rock_hero::editor::ui
{

/*!
\brief Presents input calibration state and emits controller intents.

The view owns only JUCE controls, timer cadence, and layout. Calibration measurement policy,
backend route mutations, and persistence live in core controller code.
*/
class InputCalibrationView final : public juce::Component,
                                   private juce::Timer,
                                   public core::IInputCalibrationView
{
public:
    /*!
    \brief Creates the calibration view around an editor calibration controller.
    \param controller Controller that receives all user intents emitted by this view.
    */
    explicit InputCalibrationView(core::IInputCalibrationController& controller);

    /*! \brief Uses default destruction; no backend listener is owned by the view. */
    ~InputCalibrationView() override;

    /*! \brief Returns the default window width for the calibration controls. */
    [[nodiscard]] static int preferredWidth() noexcept;

    /*! \brief Returns the preferred window height for the current control set. */
    [[nodiscard]] int preferredContentHeight() const noexcept;

    /*!
    \brief Applies controller-derived state to the JUCE controls.
    \param state State to render.
    */
    void setState(const core::InputCalibrationViewState& state) override;

    /*! \brief Requests modal shutdown from the host DocumentWindow. */
    void requestClose() override;

    /*! \brief Lays out the calibration controls and window action buttons. */
    void resized() override;

private:
    void timerCallback() override;

    // Sets labels, button text, component IDs, and static presentation properties.
    void configureControls();

    // Resizes the view and host window to match the current content height.
    void syncWindowHeightToContent();

    // Closes the containing DocumentWindow, if the view is currently hosted by one.
    void closeWindow();

    // Controller that owns calibration workflow policy.
    core::IInputCalibrationController& m_controller;

    // Last controller-derived view state rendered by this component.
    core::InputCalibrationViewState m_state{};

    AudioLevelMeter m_input_meter;
    juce::Label m_target_label;
    std::unique_ptr<juce::Drawable> m_help_icon;
    juce::DrawableButton m_help_button{"input_calibration_help", juce::DrawableButton::ImageFitted};
    juce::Label m_manual_label;
    juce::Slider m_manual_gain_slider;
    juce::TextButton m_manual_apply_button;
    juce::Label m_status;
    juce::TextButton m_calibrate_button;
    juce::TextButton m_close_button;
};

} // namespace rock_hero::editor::ui
