/*!
\file i_guitar_input.h
\brief Project-owned live guitar input port.
*/

#pragma once

#include <cstddef>
#include <expected>
#include <rock_hero/common/audio/audio_device_error.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

/*! \brief Describes one ASIO input device available for guitar monitoring. */
struct GuitarInputDevice
{
    /*! \brief User-facing ASIO device name reported by the host audio system. */
    std::string name;

    /*! \brief User-facing input channel names reported by the ASIO driver. */
    std::vector<std::string> input_channels;

    /*!
    \brief Compares two guitar input devices by their stored values.
    \param lhs Left-hand guitar input device.
    \param rhs Right-hand guitar input device.
    \return True when both devices store equal values.
    */
    friend bool operator==(const GuitarInputDevice& lhs, const GuitarInputDevice& rhs) = default;
};

/*! \brief Identifies the ASIO input device and channel used for live guitar monitoring. */
struct GuitarInputSelection
{
    /*! \brief User-facing ASIO device name to open. */
    std::string device_name;

    /*! \brief Zero-based input channel index within the selected ASIO device. */
    std::size_t input_channel_index{};

    /*!
    \brief Compares two guitar input selections by their stored values.
    \param lhs Left-hand guitar input selection.
    \param rhs Right-hand guitar input selection.
    \return True when both selections store equal values.
    */
    friend bool operator==(const GuitarInputSelection& lhs, const GuitarInputSelection& rhs) =
        default;
};

/*!
\brief Project-owned boundary for selecting and monitoring live guitar input.

The interface is named for the product capability rather than the backend technology. Concrete
implementations may use ASIO, Tracktion, JUCE, or a future fallback, while callers still present
ASIO-specific device choices when that is the available low-latency path.
*/
class IGuitarInput
{
public:
    /*! \brief Destroys the guitar-input interface. */
    virtual ~IGuitarInput() = default;

    /*!
    \brief Lists currently available ASIO devices and their input channels.
    \return ASIO devices known to the audio backend, or an empty list when ASIO is unavailable.
    */
    [[nodiscard]] virtual std::vector<GuitarInputDevice> availableAsioInputDevices() = 0;

    /*!
    \brief Stores the ASIO input selection that future monitoring should use.
    \param selection Device and channel to validate and remember.
    \return Empty success or a typed failure explaining why the selection was rejected.
    */
    [[nodiscard]] virtual std::expected<void, AudioDeviceError> selectAsioInput(
        const GuitarInputSelection& selection) = 0;

    /*!
    \brief Opens the selected ASIO input and routes it into live dry monitoring.
    \return Empty success or a typed failure explaining why monitoring could not start.
    */
    [[nodiscard]] virtual std::expected<void, AudioDeviceError> enableGuitarMonitoring() = 0;

    /*! \brief Disables live guitar monitoring without clearing the selected input. */
    virtual void disableGuitarMonitoring() = 0;

    /*!
    \brief Reports whether live guitar monitoring is currently enabled.
    \return True when selected ASIO input is being routed to the audio output.
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
