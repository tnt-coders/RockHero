#include "tracktion/engine_behaviors.h"

#include "tracktion/live_rig_gain_plugin.h"
#include "tracktion/tracktion_instrument_wave_device_mapping.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Converts the project-owned compact channel role into the Tracktion channel identifier.
[[nodiscard]] juce::AudioChannelSet::ChannelType toTracktionChannelRole(
    InstrumentChannelRole role) noexcept
{
    switch (role)
    {
        case InstrumentChannelRole::Left:
        {
            return juce::AudioChannelSet::left;
        }
        case InstrumentChannelRole::Right:
        {
            return juce::AudioChannelSet::right;
        }
    }

    return juce::AudioChannelSet::unknown;
}

// Converts the testable Rock Hero route description into Tracktion's wave-device type.
[[nodiscard]] tracktion::WaveDeviceDescription toTracktionWaveDeviceDescription(
    const InstrumentWaveDescription& description)
{
    std::vector<tracktion::ChannelIndex> channels;
    channels.reserve(description.channels.size());

    for (const InstrumentChannelDescription& channel : description.channels)
    {
        channels.emplace_back(channel.compact_device_channel, toTracktionChannelRole(channel.role));
    }

    return tracktion::WaveDeviceDescription{
        description.name, channels.data(), static_cast<int>(channels.size()), true
    };
}

} // namespace

bool RockHeroEngineBehavior::autoInitialiseDeviceManager()
{
    return false;
}

bool RockHeroEngineBehavior::canScanPluginsOutOfProcess()
{
    return true;
}

bool RockHeroEngineBehavior::isDescriptionOfWaveDevicesSupported()
{
    return true;
}

void RockHeroEngineBehavior::describeWaveDevices(
    std::vector<tracktion::WaveDeviceDescription>& descriptions, juce::AudioIODevice& device,
    bool is_input)
{
    const std::optional<InstrumentWaveDeviceDescriptions> wave_descriptions =
        createTracktionInstrumentWaveDeviceDescriptions(
            device.getName(),
            device.getActiveInputChannels(),
            device.getActiveOutputChannels(),
            device.getInputChannelNames(),
            device.getOutputChannelNames());
    if (!wave_descriptions.has_value())
    {
        return;
    }

    descriptions.push_back(toTracktionWaveDeviceDescription(
        is_input ? wave_descriptions->input : wave_descriptions->output));
}

tracktion::Plugin::Ptr RockHeroEngineBehavior::createCustomPlugin(
    tracktion::PluginCreationInfo info)
{
    if (info.state[tracktion::IDs::type].toString() == LiveRigGainPlugin::xmlTypeName)
    {
        // Tracktion's custom-plugin factory adopts this into Plugin::Ptr.
        return tracktion::Plugin::Ptr{new LiveRigGainPlugin{info}};
    }

    return {};
}

RockHeroUIBehavior::RockHeroUIBehavior(PluginWindowCommandDispatcher command_dispatcher)
    : m_command_dispatcher(std::move(command_dispatcher))
{}

std::unique_ptr<juce::Component> RockHeroUIBehavior::createPluginWindow(
    tracktion::PluginWindowState& window_state)
{
    auto* const plugin_window_state = dynamic_cast<tracktion::Plugin::WindowState*>(&window_state);
    if (plugin_window_state == nullptr)
    {
        return {};
    }

    return PluginWindow::create(plugin_window_state->plugin, m_command_dispatcher);
}

void RockHeroUIBehavior::recreatePluginWindowContentAsync(tracktion::Plugin& plugin)
{
    if (auto* const window = dynamic_cast<PluginWindow*>(plugin.windowState->pluginWindow.get()))
    {
        window->recreateEditorAsync();
        return;
    }

    tracktion::UIBehaviour::recreatePluginWindowContentAsync(plugin);
}

} // namespace rock_hero::common::audio
