/*!
\file i_audio_device_configuration.h
\brief Project-owned audio hardware configuration port.
*/

#pragma once

#include <optional>
#include <string>

namespace juce
{
class AudioDeviceManager;
}

namespace rock_hero::common::audio
{

/*!
\brief Shared notification boundary for the app's audio hardware configuration.

The port exposes the underlying juce::AudioDeviceManager so settings UI can host JUCE's stock
audio-device selector directly, and broadcasts change notifications so app code can persist and
re-derive state when the hardware route changes.
*/
class IAudioDeviceConfiguration
{
public:
    /*! \brief Listener notified on the message thread when the audio route changes. */
    class Listener
    {
    public:
        /*! \brief Destroys the listener. */
        virtual ~Listener() = default;

        /*! \brief Called after the underlying audio device manager state changes. */
        virtual void onAudioDeviceConfigurationChanged() = 0;

    protected:
        /*! \brief Creates the listener. */
        Listener() = default;

        /*! \brief Copies the listener. */
        Listener(const Listener&) = default;

        /*! \brief Moves the listener. */
        Listener(Listener&&) = default;

        /*!
        \brief Assigns the listener.
        \return Reference to this listener.
        */
        Listener& operator=(const Listener&) = default;

        /*!
        \brief Move-assigns the listener.
        \return Reference to this listener.
        */
        Listener& operator=(Listener&&) = default;
    };

    /*! \brief Destroys the audio-device configuration interface. */
    virtual ~IAudioDeviceConfiguration() = default;

    /*!
    \brief Returns the JUCE audio device manager backing this port.
    \return Reference to the active device manager owned by the audio backend.
    */
    [[nodiscard]] virtual juce::AudioDeviceManager& deviceManager() noexcept = 0;

    /*!
    \brief Returns the name of the currently open audio device, if any.
    \return Current device name, or empty when no device is open.
    */
    [[nodiscard]] virtual std::optional<std::string> currentDeviceName() const = 0;

    /*!
    \brief Registers a listener notified after audio device configuration changes.
    \param listener Listener that should be notified until it is removed.
    */
    virtual void addListener(Listener& listener) = 0;

    /*!
    \brief Removes a previously registered listener.
    \param listener Listener previously registered with addListener().
    */
    virtual void removeListener(Listener& listener) = 0;

protected:
    /*! \brief Creates the audio-device configuration interface. */
    IAudioDeviceConfiguration() = default;

    /*! \brief Copies the audio-device configuration interface. */
    IAudioDeviceConfiguration(const IAudioDeviceConfiguration&) = default;

    /*! \brief Moves the audio-device configuration interface. */
    IAudioDeviceConfiguration(IAudioDeviceConfiguration&&) = default;

    /*!
    \brief Assigns the audio-device configuration interface from another interface.
    \return Reference to this audio-device configuration interface.
    */
    IAudioDeviceConfiguration& operator=(const IAudioDeviceConfiguration&) = default;

    /*!
    \brief Move-assigns the audio-device configuration interface from another interface.
    \return Reference to this audio-device configuration interface.
    */
    IAudioDeviceConfiguration& operator=(IAudioDeviceConfiguration&&) = default;
};

} // namespace rock_hero::common::audio
