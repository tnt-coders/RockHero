/*!
\file device_state_xml.h
\brief Reconstruction of a JUCE audio-device setup from serialized device-state XML.
*/

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

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

/*!
\brief Serializes a device setup into the device-state XML shape JUCE persists.

Mirrors AudioDeviceManager::updateXml() field for field (minus MIDI, which the manager's own
update path owns), so the produced XML round-trips through initialiseFromXML() and
reconstructDeviceSetupFromXml() exactly like a manager-authored blob. Exists to make an explicitly
chosen route the saved choice even when its device cannot open: JUCE runs updateXml() only after a
successful open, so an unavailable chosen device would otherwise leave the previous route standing
as the saved choice.

\param device_type_name Backend family name stored in the "deviceType" attribute.
\param setup Device setup to serialize.
\return Serialized device-state XML.
*/
[[nodiscard]] std::unique_ptr<juce::XmlElement> serializeDeviceSetupToXml(
    const juce::String& device_type_name, const juce::AudioDeviceManager::AudioDeviceSetup& setup);

} // namespace rock_hero::common::audio
