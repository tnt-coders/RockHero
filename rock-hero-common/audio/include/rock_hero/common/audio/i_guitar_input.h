/*!
\file i_guitar_input.h
\brief Project-owned live guitar input port.
*/

#pragma once

namespace rock_hero::common::audio
{

/*!
\brief Project-owned boundary for live guitar monitoring.

The hardware device used for monitoring is owned by the audio device manager exposed through
IAudioDeviceConfiguration. This port toggles whether the currently open input is routed audibly
to the main output.
*/
class IGuitarInput
{
public:
    /*! \brief Destroys the guitar-input interface. */
    virtual ~IGuitarInput() = default;

    /*! \brief Enables live monitoring of the currently configured input. */
    virtual void enableGuitarMonitoring() = 0;

    /*! \brief Disables live monitoring without affecting device configuration. */
    virtual void disableGuitarMonitoring() = 0;

    /*!
    \brief Reports whether live guitar monitoring is currently enabled.
    \return True when the configured input is being routed to the audio output.
    */
    [[nodiscard]] virtual bool isGuitarMonitoringEnabled() const noexcept = 0;

protected:
    /*! \brief Creates the guitar-input interface. */
    IGuitarInput() = default;

    /*! \brief Copies the guitar-input interface. */
    IGuitarInput(const IGuitarInput&) = default;

    /*! \brief Moves the guitar-input interface. */
    IGuitarInput(IGuitarInput&&) = default;

    /*!
    \brief Assigns the guitar-input interface from another interface.
    \return Reference to this guitar-input interface.
    */
    IGuitarInput& operator=(const IGuitarInput&) = default;

    /*!
    \brief Move-assigns the guitar-input interface from another interface.
    \return Reference to this guitar-input interface.
    */
    IGuitarInput& operator=(IGuitarInput&&) = default;
};

} // namespace rock_hero::common::audio
