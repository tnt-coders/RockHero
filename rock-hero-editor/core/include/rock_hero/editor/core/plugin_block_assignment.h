/*!
\file plugin_block_assignment.h
\brief Instance-keyed signal-chain visual block assignment.
*/

#pragma once

#include <cstddef>
#include <string>

namespace rock_hero::editor::core
{

/*! \brief Visual block assigned to one plugin instance. */
struct [[nodiscard]] PluginBlockAssignment
{
    /*! \brief Opaque plugin instance ID assigned by the plugin host. */
    std::string instance_id;

    /*! \brief Fixed visual block occupied by the plugin instance. */
    std::size_t block_index{};

    /*!
    \brief Compares two plugin block assignments by their stored values.
    \param lhs Left-hand plugin block assignment.
    \param rhs Right-hand plugin block assignment.
    \return True when both assignments store equal values.
    */
    friend bool operator==(const PluginBlockAssignment& lhs, const PluginBlockAssignment& rhs) =
        default;
};

} // namespace rock_hero::editor::core
