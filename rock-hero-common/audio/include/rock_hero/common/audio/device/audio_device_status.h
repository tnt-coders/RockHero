/*!
\file audio_device_status.h
\brief Project-owned snapshot of the currently open audio device.
*/

#pragma once

#include <string>

namespace rock_hero::common::audio
{

/*!
\brief Message-thread snapshot of the active audio hardware route.

The snapshot deliberately uses project-owned value fields so editor and game code can inspect
device state without reaching through JUCE or Tracktion types. A default-constructed value
represents a closed, failed, stopped, or unavailable device.
*/
struct AudioDeviceStatus
{
    /*! \brief True when the backend currently has an open audio device. */
    bool open{false};

    /*! \brief Human-readable device name reported by the backend. */
    std::string device_name;

    /*! \brief Backend type reported by the adapter, such as ASIO or Windows Audio. */
    std::string backend_name;

    /*! \brief Current sample rate in Hertz. */
    double sample_rate_hz{0.0};

    /*! \brief Current physical device bit depth. */
    int bit_depth{0};

    /*! \brief Number of active input channels. */
    int input_channels{0};

    /*! \brief Number of active output channels. */
    int output_channels{0};

    /*! \brief Current callback block size in samples. */
    int buffer_size_samples{0};

    /*! \brief Current input latency in milliseconds. */
    double input_latency_ms{0.0};

    /*! \brief Current output latency in milliseconds. */
    double output_latency_ms{0.0};

    /*!
    \brief Why no device is open, when known.

    Carries the backend's open-failure text after a failed route application, or a composed
    disconnect notice after the saved device vanished. Empty while a device is open, and on a
    first run with no saved route.
    */
    std::string unavailable_reason;

    /*!
    \brief Compares two device snapshots by their stored values.
    \param lhs Left-hand device snapshot.
    \param rhs Right-hand device snapshot.
    \return True when both snapshots store equal values.
    */
    friend bool operator==(const AudioDeviceStatus& lhs, const AudioDeviceStatus& rhs) = default;
};

} // namespace rock_hero::common::audio
