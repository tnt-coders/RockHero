/*!
\file i_audio_device_settings_view.h
\brief Framework-free view contract for editor audio-device settings.
*/

#pragma once

#include <rock_hero/editor/core/audio_device_settings_view_state.h>

namespace rock_hero::editor::core
{

/*! \brief View contract driven by AudioDeviceSettingsController. */
class IAudioDeviceSettingsView
{
public:
    /*! \brief Destroys the view contract. */
    virtual ~IAudioDeviceSettingsView() = default;

    /*!
    \brief Applies derived render state to the settings view.
    \param state State to render.
    */
    virtual void setState(const AudioDeviceSettingsViewState& state) = 0;

    /*! \brief Requests that the host close the settings window. */
    virtual void requestClose() = 0;

protected:
    /*! \brief Creates the view contract. */
    IAudioDeviceSettingsView() = default;

    /*! \brief Copies the view contract. */
    IAudioDeviceSettingsView(const IAudioDeviceSettingsView&) = default;

    /*! \brief Moves the view contract. */
    IAudioDeviceSettingsView(IAudioDeviceSettingsView&&) = default;

    /*!
    \brief Assigns the view contract.
    \return Reference to this view contract.
    */
    IAudioDeviceSettingsView& operator=(const IAudioDeviceSettingsView&) = default;

    /*!
    \brief Move-assigns the view contract.
    \return Reference to this view contract.
    */
    IAudioDeviceSettingsView& operator=(IAudioDeviceSettingsView&&) = default;
};

} // namespace rock_hero::editor::core
