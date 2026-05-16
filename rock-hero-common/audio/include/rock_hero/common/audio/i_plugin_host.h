/*!
\file i_plugin_host.h
\brief Tracktion-free live instrument plugin-host port.
*/

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <rock_hero/common/audio/plugin_host_error.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

/*!
\brief Project-owned description of one loadable plugin candidate.

The ID is opaque to callers. It is stable enough to pass back to the same plugin host when adding
the selected plugin, but callers should not parse it or depend on Tracktion/JUCE formatting.
*/
struct [[nodiscard]] PluginCandidate
{
    /*! \brief Opaque candidate ID used by addLiveInstrumentPlugin(). */
    std::string id;

    /*! \brief Plugin display name reported by the plugin scanner. */
    std::string name;

    /*! \brief Plugin manufacturer reported by the plugin scanner. */
    std::string manufacturer;

    /*! \brief Backend plugin format name, currently expected to be VST3 for authored tones. */
    std::string format_name;

    /*! \brief File or bundle path that produced this plugin candidate. */
    std::filesystem::path file_path;
};

/*!
\brief Handle returned after a plugin is appended to the live instrument chain.

The instance ID is opaque to callers and identifies the concrete inserted plugin in the current
backend edit. The chain index describes the current linear live-chain insertion position; future
parallel or rack-based tone graphs may add richer addressing without changing candidate scanning.
*/
struct [[nodiscard]] LivePluginHandle
{
    /*! \brief Opaque ID for the concrete plugin instance in the current backend edit. */
    std::string instance_id;

    /*! \brief Opaque ID of the plugin candidate used to create this instance. */
    std::string plugin_id;

    /*! \brief Zero-based index of the inserted plugin in the current live chain. */
    std::size_t chain_index{};
};

/*!
\brief Project-owned facade for live instrument plugin discovery and chain mutation.

All methods are message-thread operations. Implementations may scan plugin files, load plugin
instances, and rebuild backend playback graphs; callers must never invoke this interface from the
real-time audio callback.
*/
class IPluginHost
{
public:
    /*! \brief Destroys the plugin-host interface. */
    virtual ~IPluginHost() = default;

    /*!
    \brief Scans one plugin file or bundle for loadable candidates.
    \param plugin_path Path to a plugin file or plugin bundle.
    \return Discovered candidates, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<std::vector<PluginCandidate>, PluginHostError>
    scanPluginFile(const std::filesystem::path& plugin_path) = 0;

    /*!
    \brief Appends a previously scanned plugin candidate to the live instrument chain.

    The first implementation appends to the linear Tracktion plugin list owned by the live
    instrument track. It stops and rebuilds backend playback graph state as needed.

    \param plugin_id Opaque candidate ID returned by scanPluginFile().
    \return Handle for the inserted plugin instance, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<LivePluginHandle, PluginHostError> addLiveInstrumentPlugin(
        const std::string& plugin_id) = 0;

protected:
    /*! \brief Creates the plugin-host interface. */
    IPluginHost() = default;

    /*! \brief Copies the plugin-host interface. */
    IPluginHost(const IPluginHost&) = default;

    /*! \brief Moves the plugin-host interface. */
    IPluginHost(IPluginHost&&) = default;

    /*!
    \brief Assigns the plugin-host interface from another interface.
    \return Reference to this plugin-host interface.
    */
    IPluginHost& operator=(const IPluginHost&) = default;

    /*!
    \brief Move-assigns the plugin-host interface from another interface.
    \return Reference to this plugin-host interface.
    */
    IPluginHost& operator=(IPluginHost&&) = default;
};

} // namespace rock_hero::common::audio
