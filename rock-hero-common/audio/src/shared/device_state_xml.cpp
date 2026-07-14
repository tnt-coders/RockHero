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

} // namespace rock_hero::common::audio
