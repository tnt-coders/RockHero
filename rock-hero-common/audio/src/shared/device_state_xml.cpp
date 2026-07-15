#include "shared/device_state_xml.h"

namespace rock_hero::common::audio
{

juce::AudioDeviceManager::AudioDeviceSetup reconstructDeviceSetupFromXml(
    const juce::XmlElement& xml)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;

    if (xml.getStringAttribute("audioDeviceName").isNotEmpty())
    {
        setup.inputDeviceName = setup.outputDeviceName = xml.getStringAttribute("audioDeviceName");
    }
    else
    {
        setup.inputDeviceName = xml.getStringAttribute("audioInputDeviceName");
        setup.outputDeviceName = xml.getStringAttribute("audioOutputDeviceName");
    }

    setup.bufferSize = xml.getIntAttribute("audioDeviceBufferSize", setup.bufferSize);
    setup.sampleRate = xml.getDoubleAttribute("audioDeviceRate", setup.sampleRate);
    setup.inputChannels.parseString(xml.getStringAttribute("audioDeviceInChans", "11"), 2);
    setup.outputChannels.parseString(xml.getStringAttribute("audioDeviceOutChans", "11"), 2);
    setup.useDefaultInputChannels = !xml.hasAttribute("audioDeviceInChans");
    setup.useDefaultOutputChannels = !xml.hasAttribute("audioDeviceOutChans");

    return setup;
}

std::unique_ptr<juce::XmlElement> serializeDeviceSetupToXml(
    const juce::String& device_type_name, const juce::AudioDeviceManager::AudioDeviceSetup& setup)
{
    auto xml = std::make_unique<juce::XmlElement>("DEVICESETUP");
    xml->setAttribute("deviceType", device_type_name);
    xml->setAttribute("audioOutputDeviceName", setup.outputDeviceName);
    xml->setAttribute("audioInputDeviceName", setup.inputDeviceName);

    // updateXml() reads these from the open device; with no open device the setup's own values
    // are the chosen ones. Zero values mean "unspecified" and are omitted, matching the defaults
    // initialiseFromXML() falls back to.
    if (setup.sampleRate > 0.0)
    {
        xml->setAttribute("audioDeviceRate", setup.sampleRate);
    }
    if (setup.bufferSize > 0)
    {
        xml->setAttribute("audioDeviceBufferSize", setup.bufferSize);
    }
    if (!setup.useDefaultInputChannels)
    {
        xml->setAttribute("audioDeviceInChans", setup.inputChannels.toString(2));
    }
    if (!setup.useDefaultOutputChannels)
    {
        xml->setAttribute("audioDeviceOutChans", setup.outputChannels.toString(2));
    }

    return xml;
}

} // namespace rock_hero::common::audio
