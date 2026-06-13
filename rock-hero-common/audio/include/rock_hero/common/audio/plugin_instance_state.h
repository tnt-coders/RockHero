/*!
\file plugin_instance_state.h
\brief Opaque plugin-instance state memento value.
*/

#pragma once

#include <cstddef>
#include <vector>

namespace rock_hero::common::audio
{

/*! \brief Opaque full plugin-state chunk used by editor-owned undo mementos. */
struct [[nodiscard]] PluginInstanceState
{
    /*! \brief Backend-owned bytes that editor-core stores but never interprets. */
    std::vector<std::byte> opaque_data;

    /*!
    \brief Compares two plugin-instance states by their opaque bytes.
    \param lhs Left-hand plugin-instance state.
    \param rhs Right-hand plugin-instance state.
    \return True when both states store equal bytes.
    */
    friend bool operator==(const PluginInstanceState& lhs, const PluginInstanceState& rhs) =
        default;
};

} // namespace rock_hero::common::audio
