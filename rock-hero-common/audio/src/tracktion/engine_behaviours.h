/*!
\file engine_behaviours.h
\brief Rock Hero's Tracktion EngineBehaviour and UIBehaviour customization points.
*/

#pragma once

#include "tracktion/plugin_window.h"

#include <memory>
#include <tracktion_engine/tracktion_engine.h>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

/*! \brief Describes the single instrument input and stereo output that Rock Hero exposes to
Tracktion. */
class RockHeroEngineBehaviour final : public tracktion::EngineBehaviour
{
public:
    /*!
    \brief Lets Engine construct its edit before explicitly opening the device manager.
    \return Always false.
    */
    bool autoInitialiseDeviceManager() override;

    /*!
    \brief Scans third-party plugins in a child process so bad scans cannot wedge the editor
    process.
    \return Always true.
    */
    bool canScanPluginsOutOfProcess() override;

    /*!
    \brief Announces that Rock Hero supplies compact wave-device descriptions for the staged JUCE
    route.
    \return Always true.
    */
    bool isDescriptionOfWaveDevicesSupported() override;

    /*!
    \brief Converts the currently open JUCE device masks into Tracktion-visible wave devices.
    \param descriptions Output list receiving the described wave device.
    \param device Currently open JUCE audio device.
    \param is_input True when Tracktion asks for input devices.
    */
    void describeWaveDevices(
        std::vector<tracktion::WaveDeviceDescription>& descriptions, juce::AudioIODevice& device,
        bool is_input) override;

    /*!
    \brief Creates Rock Hero-owned structural plugins from Tracktion plugin state.
    \param info Tracktion creation request carrying the persisted plugin state.
    \return The structural plugin, or null when the state describes no Rock Hero plugin.
    */
    tracktion::Plugin::Ptr createCustomPlugin(tracktion::PluginCreationInfo info) override;
};

/*! \brief Supplies Tracktion with Rock Hero's minimal plugin editor window implementation. */
class RockHeroUIBehaviour final : public tracktion::UIBehaviour
{
public:
    /*!
    \brief Stores the dispatcher handed to every plugin window this behaviour creates.
    \param command_dispatcher Host callback receiving forwarded plugin-window commands.
    */
    explicit RockHeroUIBehaviour(PluginWindowCommandDispatcher command_dispatcher);

    /*!
    \brief Creates windows only for normal plugin instances; rack windows will get their own UI
    later.
    \param window_state Tracktion window state requesting a host window.
    \return The hosting window, or null for unsupported window kinds.
    */
    std::unique_ptr<juce::Component> createPluginWindow(
        tracktion::PluginWindowState& window_state) override;

    /*!
    \brief Refreshes the editor contents without replacing Tracktion's owning plugin window.
    \param plugin Plugin whose editor must be recreated.
    */
    void recreatePluginWindowContentAsync(tracktion::Plugin& plugin) override;

private:
    PluginWindowCommandDispatcher m_command_dispatcher;
};

} // namespace rock_hero::common::audio
