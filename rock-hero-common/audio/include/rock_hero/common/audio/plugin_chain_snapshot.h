/*!
\file plugin_chain_snapshot.h
\brief Tracktion-free loaded plugin chain snapshot values.
*/

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

/*!
\brief One loaded plugin in the current user-visible signal chain.

The instance ID identifies a runtime plugin object in the active backend edit. The chain index is
the zero-based position among user plugins only; hidden structural gain and meter plugins are not
counted.
*/
struct [[nodiscard]] PluginChainEntry
{
    /*! \brief Opaque instance ID assigned by the current plugin backend. */
    std::string instance_id;

    /*! \brief Best-effort plugin identifier for display and same-runtime operations. */
    std::string plugin_id;

    /*! \brief User-facing plugin name. */
    std::string name;

    /*! \brief User-facing plugin manufacturer, when supplied by the scanner. */
    std::string manufacturer;

    /*! \brief Backend plugin format name, such as VST3. */
    std::string format_name;

    /*! \brief Zero-based position in the current linear user plugin chain. */
    std::size_t chain_index{};

    /*!
    \brief Opaque editor-owned visual block carried alongside this plugin.

    The audio layer only shuttles this value between the tone document and the editor; it does not
    interpret it, enforce gap rules, or use it for playback. The editor owns its meaning and
    validity. Distinct from chain_index, which is the dense playback position.
    */
    std::size_t block_index{};

    /*!
    \brief Compares two plugin chain entries by their stored values.
    \param lhs Left-hand plugin chain entry.
    \param rhs Right-hand plugin chain entry.
    \return True when both entries store equal values.
    */
    friend bool operator==(const PluginChainEntry& lhs, const PluginChainEntry& rhs) = default;
};

/*! \brief Authoritative ordered snapshot of the current user-visible plugin chain. */
struct [[nodiscard]] PluginChainSnapshot
{
    /*! \brief Current user-visible plugin entries in backend order. */
    std::vector<PluginChainEntry> plugins{};

    /*!
    \brief Compares two plugin chain snapshots by their stored values.
    \param lhs Left-hand plugin chain snapshot.
    \param rhs Right-hand plugin chain snapshot.
    \return True when both snapshots store equal values.
    */
    friend bool operator==(const PluginChainSnapshot& lhs, const PluginChainSnapshot& rhs) =
        default;
};

} // namespace rock_hero::common::audio
