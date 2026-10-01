/*!
\file null_audio_device.h
\brief The silent device the engine runs whenever the user's audio hardware is not open.
*/

#pragma once

#include <cstdint>
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

namespace rock_hero::common::audio
{

/*!
\brief The silent device's one device name, which JUCE's default search finds it by.

Holds no `*` or `?`, since AudioDeviceManager matches a preferred default name as a wildcard.
*/
inline constexpr const char* g_null_audio_device_name = "Silent Output";

/*!
\brief The null device's block clock, as a value so its pacing rule is testable without a thread.

Block n of a run that started at \ref start_ms is due at start_ms + n * \ref period_ms, so the
long-run rate is exact however late any single wake-up is.
*/
struct NullAudioDeviceClock
{
    /*! \brief When the current run started, on JUCE's high-resolution millisecond counter. */
    double start_ms{0.0};

    /*! \brief Blocks rendered since the run started. */
    std::int64_t blocks{0};

    /*! \brief Length of one block in milliseconds. */
    double period_ms{0.0};

    /*!
    \brief Counts one rendered block and says how long to wait before the next.

    A clock more than one period behind restarts its run at \p now_ms instead of rendering the
    backlog in a burst, dropping the lost time the way a real device under-run does.

    \param now_ms The current time on the same counter as \ref start_ms.
    \return Milliseconds to wait before rendering the next block, zero when it is already due.
    */
    [[nodiscard]] double advance(double now_ms) noexcept;
};

/*!
\brief Creates the device type that offers the silent device.

The engine registers it after the platform backends, so hardware is always preferred and the
silent device opens only when the policy asks for it or no hardware exists at all.

\return The silent device type, for AudioDeviceManager::addAudioDeviceType().
*/
[[nodiscard]] std::unique_ptr<juce::AudioIODeviceType> createNullAudioDeviceType();

/*!
\brief Reports whether a device is the silent device.
\param device Device to classify, or null.
\return True only for the silent device.
*/
[[nodiscard]] bool isNullAudioDevice(const juce::AudioIODevice* device) noexcept;

/*!
\brief Reports whether a device type is the silent device's type.
\param type Device type to classify.
\return True only for the silent device's type.
*/
[[nodiscard]] bool isNullAudioDeviceType(const juce::AudioIODeviceType& type) noexcept;

/*!
\brief THE predicate for "the user's audio hardware is open": a current device that is open at a
real sample rate and is not the silent device.
\param device_manager Manager whose current device is classified.
\return True while real hardware runs the engine.
*/
[[nodiscard]] bool hardwareDeviceOpen(juce::AudioDeviceManager& device_manager);

/*!
\brief Replaces whatever the manager has open with the silent device.

Opened through JUCE's default search by name, which neither records it as the user's choice nor
waits the settle delay a device-type switch makes. It also leaves the silent device as JUCE's
preferred default, so JUCE's own disconnect fallback lands on it rather than on audible hardware.

\param device_manager Manager to switch to the silent device.
*/
void openNullAudioDevice(juce::AudioDeviceManager& device_manager);

/*!
\brief Applies a serialized route without falling back to any other audible device.

The route becomes the user's recorded choice whether or not it opens, so the next launch
still asks for it. JUCE's select-default-on-failure is suppressed, and the silent device is
named as JUCE's preferred default, so JUCE's own later disconnect fallback lands on it too.

\param device_manager Manager to apply the route to.
\param route Serialized route, as AudioDeviceManager::createStateXml() writes it.
\return The backend's diagnostic when the hardware stayed closed, else empty.
*/
[[nodiscard]] juce::String openRouteWithoutFallback(
    juce::AudioDeviceManager& device_manager, const juce::XmlElement& route);

} // namespace rock_hero::common::audio
