/*!
\file i_audio_device_settings_controller.h
\brief Framework-free intent contract for editor audio-device settings.
*/

#pragma once

namespace rock_hero::editor::core
{

/*! \brief Controller contract receiving settings-view user intents. */
class IAudioDeviceSettingsController
{
public:
    /*! \brief Destroys the controller contract. */
    virtual ~IAudioDeviceSettingsController() = default;

    /*!
    \brief Handles an audio-system choice selection.
    \param choice_id One-based audio-system choice ID, or zero to clear selection.
    */
    virtual void onAudioSystemSelected(int choice_id) = 0;

    /*!
    \brief Handles a combined device choice selection.
    \param choice_id One-based combined-device choice ID, or zero to clear selection.
    */
    virtual void onDeviceSelected(int choice_id) = 0;

    /*!
    \brief Handles an input device choice selection.
    \param choice_id One-based input-device choice ID, or zero to clear selection.
    */
    virtual void onInputDeviceSelected(int choice_id) = 0;

    /*!
    \brief Handles an output device choice selection.
    \param choice_id One-based output-device choice ID, or zero to clear selection.
    */
    virtual void onOutputDeviceSelected(int choice_id) = 0;

    /*!
    \brief Handles an input channel choice selection.
    \param choice_id One-based input-channel choice ID, or zero to clear selection.
    */
    virtual void onInputChannelSelected(int choice_id) = 0;

    /*!
    \brief Handles a stereo output-pair choice selection.
    \param choice_id One-based stereo-output-pair choice ID, or zero to clear selection.
    */
    virtual void onStereoOutputPairSelected(int choice_id) = 0;

    /*!
    \brief Handles a sample-rate choice selection.
    \param choice_id One-based sample-rate choice ID, or zero to clear selection.
    */
    virtual void onSampleRateSelected(int choice_id) = 0;

    /*!
    \brief Handles a buffer-size choice selection.
    \param choice_id One-based buffer-size choice ID, or zero to clear selection.
    */
    virtual void onBufferSizeSelected(int choice_id) = 0;

    /*! \brief Handles a backend control-panel button press. */
    virtual void onControlPanelRequested() = 0;

    /*! \brief Handles an OK button press. */
    virtual void onOkRequested() = 0;

    /*! \brief Handles a Cancel button press. */
    virtual void onCancelRequested() = 0;

protected:
    /*! \brief Creates the controller contract. */
    IAudioDeviceSettingsController() = default;

    /*! \brief Copies the controller contract. */
    IAudioDeviceSettingsController(const IAudioDeviceSettingsController&) = default;

    /*! \brief Moves the controller contract. */
    IAudioDeviceSettingsController(IAudioDeviceSettingsController&&) = default;

    /*!
    \brief Assigns the controller contract.
    \return Reference to this controller contract.
    */
    IAudioDeviceSettingsController& operator=(const IAudioDeviceSettingsController&) = default;

    /*!
    \brief Move-assigns the controller contract.
    \return Reference to this controller contract.
    */
    IAudioDeviceSettingsController& operator=(IAudioDeviceSettingsController&&) = default;
};

} // namespace rock_hero::editor::core
