/*!
\file engine_behaviors.h
\brief Rock Hero's Tracktion behaviour and property-storage customization points.
*/

#pragma once

#include "tracktion/plugin_window.h"

#include <memory>
#include <rock_hero/common/audio/engine/engine.h>
#include <tracktion_engine/tracktion_engine.h>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

/*! \brief Describes the single instrument input and stereo output that Rock Hero exposes to
Tracktion. */
class RockHeroEngineBehavior final : public tracktion::EngineBehaviour
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

    /*!
    \brief Keeps Tracktion from marking new plugins for tempo-change remapping.

    Rock Hero owns musical positions and rewrites derived seconds curves itself; the edit's tempo
    sequence is written only by the one-way tempo mirror. Without this override every
    createNewPlugin call persists remapOnTempoChange="1" into plugin state, and Tracktion's remap
    machinery would fight Rock Hero's own musical-to-seconds rebuilds if the edit tempo ever moves.

    \return Always false.
    */
    bool arePluginsRemappedWhenTempoChanges() override;
};

/*! \brief Supplies Tracktion with Rock Hero's minimal plugin editor window implementation. */
class RockHeroUIBehavior final : public tracktion::UIBehaviour
{
public:
    /*!
    \brief Stores the dispatcher handed to every plugin window this behaviour creates.
    \param command_dispatcher Host callback receiving forwarded plugin-window commands.
    */
    explicit RockHeroUIBehavior(PluginWindowCommandDispatcher command_dispatcher);

    /*!
    \brief Creates windows only for normal plugin instances; racks get no host window.
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

    /*!
    \brief Creates waveform thumbnails fine enough for the editor's deepest timeline zoom.

    JUCE's AudioThumbnail switches to reading sample levels straight from the audio file once a
    view zooms past the thumbnail's stored resolution, and that path silently draws nothing for
    Tracktion-managed thumbnails whose reader has been released (see the implementation for the
    source trace). Storing thumbnails at a finer resolution keeps every reachable editor zoom on
    the always-available stored-data path.

    \param source_samples_per_thumbnail_sample Tracktion's default granularity; ignored.
    \param format_manager Format manager the thumbnail reads through.
    \param cache Thumbnail cache the thumbnail registers with.
    \return The thumbnail instance backing a Tracktion SmartThumbnail.
    */
    std::unique_ptr<juce::AudioThumbnailBase> createAudioThumbnail(
        int source_samples_per_thumbnail_sample, juce::AudioFormatManager& format_manager,
        juce::AudioThumbnailCache& cache) override;

private:
    PluginWindowCommandDispatcher m_command_dispatcher;
};

/*!
\brief Keeps Tracktion's own files in the folders the engine was given.

Tracktion's default places its settings under the application name in the platform data folder,
which on macOS is not the folder every other Rock Hero location shares, and its temporary files
beside them, in user data rather than scratch.
*/
class RockHeroPropertyStorage final : public tracktion::PropertyStorage
{
public:
    /*!
    \brief Names the storage for the product and keeps the folders.
    \param folders Where Tracktion's settings and temporary files go.
    */
    explicit RockHeroPropertyStorage(EngineFolders folders);

    /*!
    \brief Returns the folder for Tracktion's settings and plugin-scan state, creating it.
    \return The settings folder.
    */
    juce::File getAppPrefsFolder() override;

    /*!
    \brief Returns the folder Tracktion makes its temporary files under.
    \return The `tracktion` folder under the scratch root; Tracktion creates it.
    */
    juce::File getAppCacheFolder() override;

private:
    EngineFolders m_folders;
};

} // namespace rock_hero::common::audio
