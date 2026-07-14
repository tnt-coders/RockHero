/*!
\file device_state_xml.h
\brief Reconstruction of a JUCE audio-device setup from serialized device-state XML.
*/

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

namespace rock_hero::common::audio
{

/*!
\brief Reconstructs the AudioDeviceSetup encoded by a serialized device-state XML.

This mirrors, field for field, the reconstruction JUCE's AudioDeviceManager::initialiseFromXML
performs for the same blob, so two device-state XMLs compare equal (through
AudioDeviceSetup::operator==) exactly when a restore of one would reproduce the other. Keeping the
extraction local avoids reaching into JUCE's private initialiseFromXML while staying tied to the
same attributes. The backend family is not part of the setup; callers read it from the XML's
"deviceType" attribute directly.

\param xml Serialized device state produced by AudioDeviceManager::createStateXml().
\return The device setup a restore of the XML would request.
*/
[[nodiscard]] juce::AudioDeviceManager::AudioDeviceSetup reconstructDeviceSetupFromXml(
    const juce::XmlElement& xml);

} // namespace rock_hero::common::audio
