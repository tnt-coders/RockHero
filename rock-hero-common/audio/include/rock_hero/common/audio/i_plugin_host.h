/*!
\file i_plugin_host.h
\brief Tracktion-free plugin-host port.
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
    /*! \brief Opaque candidate ID used by addPlugin(). */
    std::string id;

    /*! \brief Plugin display name reported by the plugin scanner. */
    std::string name;

    /*! \brief Plugin manufacturer reported by the plugin scanner. */
    std::string manufacturer;

    /*! \brief Backend plugin format name, currently expected to be VST3 for authored tones. */
    std::string format_name;

    /*! \brief File or bundle path that produced this plugin candidate. */
    std::filesystem::path file_path;

    /*!
    \brief Compares two plugin candidates by their stored values.
    \param lhs Left-hand plugin candidate.
    \param rhs Right-hand plugin candidate.
    \return True when both plugin candidates store equal values.
    */
    friend bool operator==(const PluginCandidate& lhs, const PluginCandidate& rhs) = default;
};

/*!
\brief Handle returned after a plugin is appended to the hosted chain.

The instance ID is opaque to callers and identifies the concrete inserted plugin in the current
backend edit. The chain index describes the current linear chain insertion position; future
parallel or rack-based tone graphs may add richer addressing without changing candidate scanning.
*/
struct [[nodiscard]] PluginHandle
{
    /*! \brief Opaque ID for the concrete plugin instance in the current backend edit. */
    std::string instance_id;

    /*! \brief Opaque ID of the plugin candidate used to create this instance. */
    std::string plugin_id;

    /*! \brief Zero-based index of the inserted plugin in the current chain. */
    std::size_t chain_index{};
};

/*!
\brief Project-owned facade for plugin discovery and chain mutation.

Plugin catalog discovery may run on a non-realtime worker thread because plugin inspection can
execute slow third-party code; chain mutation and plugin-window methods are message-thread
operations because implementations may mutate or inspect backend playback/UI graph state. Callers
must never invoke this interface from the real-time audio callback.
*/
class IPluginHost
{
public:
    /*! \brief Destroys the plugin-host interface. */
    virtual ~IPluginHost() = default;

    /*!
    \brief Scans plugin files or directories for loadable candidates.

    Implementations may recurse through supplied directories and should return an empty list when
    no compatible candidates are found. Callers should treat individual plugin failures as
    scanner diagnostics rather than as proof that every other plugin failed.

    \param roots Files or directories to inspect for plugin candidates.
    \return Discovered candidates, or a typed failure when scanning itself cannot proceed.
    \note This method may be called from a non-realtime worker thread.
    */
    [[nodiscard]] virtual std::expected<std::vector<PluginCandidate>, PluginHostError>
    scanPluginLocations(const std::vector<std::filesystem::path>& roots) = 0;

    /*!
    \brief Returns plugin candidates already known to the host without scanning.

    This is a lightweight catalog read for UI presentation. It must not traverse plugin folders,
    launch plugin scanner processes, or execute third-party plugin code.

    \return Known plugin candidates currently available to add to the hosted chain.
    \note This method must be called on the message thread.
    */
    [[nodiscard]] virtual std::vector<PluginCandidate> knownPluginCandidates() const = 0;

    /*!
    \brief Appends a previously scanned plugin candidate to the hosted chain.

    The first implementation appends to the linear Tracktion plugin list owned by the instrument
    track. It stops and rebuilds backend playback graph state as needed.

    \param plugin_id Opaque candidate ID returned by knownPluginCandidates() or a catalog scan.
    \return Handle for the inserted plugin instance, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<PluginHandle, PluginHostError> addPlugin(
        const std::string& plugin_id) = 0;

    /*!
    \brief Removes a loaded plugin instance from the hosted chain.

    The first implementation removes from the linear Tracktion plugin list owned by the
    instrument track. It stops and rebuilds backend playback graph state as needed.

    \param instance_id Opaque instance ID returned by addPlugin().
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<void, PluginHostError> removePlugin(
        const std::string& instance_id) = 0;

    /*!
    \brief Opens the hosted editor window for a loaded plugin instance.

    The first implementation asks Tracktion to show the plugin's native/editor component and bring
    it to the front if it is already open.

    \param instance_id Opaque instance ID returned by addPlugin().
    \return Empty success, or a typed failure.
    \note This method must be called on the message thread.
    */
    [[nodiscard]] virtual std::expected<void, PluginHostError> openPluginWindow(
        const std::string& instance_id) = 0;

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
