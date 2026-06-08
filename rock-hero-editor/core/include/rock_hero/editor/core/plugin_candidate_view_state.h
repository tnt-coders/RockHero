/*!
\file plugin_candidate_view_state.h
\brief Editor-core view-state record for one discoverable plugin candidate shown in the plugin
browser.
*/

#pragma once

#include <rock_hero/editor/core/plugin_display_type.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Editor-core view state for one plugin candidate.

Produced from common::audio::PluginCandidate so editor-core owns the plugin-browser presentation
shape without exposing the audio-boundary type to editor-ui. The explicit conversion keeps
backend-shaped fields added to common::audio::PluginCandidate (Tracktion or JUCE handles, raw
plugin descriptions, opaque backend payloads) from reaching editor-ui by accident. Future
editor-only presentation fields such as favorites, match scores, or last-used timestamps belong
here, not on the audio-boundary type.
*/
struct PluginCandidateViewState
{
    /*! \brief Opaque catalog ID emitted by browser selection. */
    std::string id;

    /*! \brief User-facing plugin name reported by the scanner. */
    std::string name;

    /*! \brief User-facing manufacturer name reported by the scanner. */
    std::string manufacturer;

    /*! \brief Backend plugin format name, such as VST3. */
    std::string format_name;

    /*! \brief Canonical type used for browser display and type filtering. */
    PluginDisplayType primary_display_type{PluginDisplayType::Uncategorized};

    /*!
    \brief Compares two plugin candidate view states by their stored values.
    \param lhs Left-hand plugin candidate view state.
    \param rhs Right-hand plugin candidate view state.
    \return True when both plugin candidate view states store equal values.
    */
    friend bool operator==(
        const PluginCandidateViewState& lhs, const PluginCandidateViewState& rhs) = default;
};

} // namespace rock_hero::editor::core
