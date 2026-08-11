/*!
\file input_device_identity.h
\brief Project-owned identity for one calibrated physical input route.
*/

#pragma once

#include <string>

namespace rock_hero::common::audio
{

/*!
\brief Input route identity used to validate app-local calibration state.

The identity intentionally describes one physical input channel. The channel display name is
metadata and is not part of the stable physical-route key. A default-constructed value is not valid
for calibration and should not be persisted as a calibrated route.
*/
struct [[nodiscard]] InputDeviceIdentity
{
    /*! \brief Audio backend name reported by JUCE, such as ASIO or Windows Audio. */
    std::string backend_name;

    /*! \brief Selected input device name from the JUCE audio-device setup. */
    std::string input_device_name;

    /*! \brief Zero-based physical input channel index selected for live guitar. */
    int input_channel_index{-1};

    /*! \brief Display name for the selected physical input channel. */
    std::string input_channel_name;

    /*!
    \brief Compares two input route identities by their stored values.
    \param lhs Left-hand input device identity.
    \param rhs Right-hand input device identity.
    \return True when both identities store equal values.
    */
    friend bool operator==(const InputDeviceIdentity& lhs, const InputDeviceIdentity& rhs) =
        default;
};

/*!
\brief Persisted property name carrying InputDeviceIdentity::backend_name.

This name and its three siblings below are an on-disk contract rather than a private detail: the
shared audio-config store writes them as XML attributes and the game's settings file writes them as
JSON properties, and each application must find the names the other one wrote. They are declared
here, beside the fields they carry, because a rename in only one of those files would silently drop
the user's saved input-device selection — a missing property reads as absence, not as an error.
*/
inline constexpr const char* g_identity_backend_name_property{"backendName"};

/*! \brief Persisted property name carrying InputDeviceIdentity::input_device_name. */
inline constexpr const char* g_identity_input_device_name_property{"inputDeviceName"};

/*! \brief Persisted property name carrying InputDeviceIdentity::input_channel_index. */
inline constexpr const char* g_identity_input_channel_index_property{"inputChannelIndex"};

/*! \brief Persisted property name carrying InputDeviceIdentity::input_channel_name. */
inline constexpr const char* g_identity_input_channel_name_property{"inputChannelName"};

/*!
\brief Reports whether an input identity is complete enough to validate calibration.
\param identity Identity to inspect.
\return True when the identity names a backend, input device, and physical input channel.
*/
[[nodiscard]] inline bool isValidInputDeviceIdentity(const InputDeviceIdentity& identity)
{
    return !identity.backend_name.empty() && !identity.input_device_name.empty() &&
           identity.input_channel_index >= 0;
}

/*!
\brief Reports whether two identities refer to the same stable physical input route.
\param lhs Left-hand input device identity.
\param rhs Right-hand input device identity.
\return True when both identities name the same backend, input device, and physical channel index.
*/
[[nodiscard]] inline bool samePhysicalInputRoute(
    const InputDeviceIdentity& lhs, const InputDeviceIdentity& rhs)
{
    return isValidInputDeviceIdentity(lhs) && isValidInputDeviceIdentity(rhs) &&
           lhs.backend_name == rhs.backend_name && lhs.input_device_name == rhs.input_device_name &&
           lhs.input_channel_index == rhs.input_channel_index;
}

} // namespace rock_hero::common::audio
