/*!
\file plugin_candidate_state.h
\brief Editor-core workflow record for one discoverable plugin shown in the plugin browser.
*/

#pragma once

#include <filesystem>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Editor-core workflow state for one discoverable plugin.

Produced by the editor controller from common::audio::PluginCandidate so editor-core owns the
shape of plugin-browser workflow data without consuming the audio-boundary type directly. The
controller is the only place that crosses this seam; the explicit conversion guarantees that
backend-shaped fields added to common::audio::PluginCandidate (Tracktion or JUCE handles, raw
plugin descriptions, opaque backend payloads) cannot reach editor-ui by accident. Future
editor-only fields such as favorites, match scores, or last-used timestamps belong here, not on
the audio-boundary type.
*/
struct PluginCandidateState
{
    /*! \brief Opaque plugin ID passed back to the plugin host when adding this plugin. */
    std::string id;

    /*! \brief User-facing plugin name reported by the scanner. */
    std::string name;

    /*! \brief User-facing manufacturer name reported by the scanner. */
    std::string manufacturer;

    /*! \brief Backend plugin format name, such as VST3. */
    std::string format_name;

    /*! \brief File or bundle path that produced this plugin. */
    std::filesystem::path file_path;

    /*!
    \brief Compares two plugin candidate states by their stored values.
    \param lhs Left-hand plugin candidate state.
    \param rhs Right-hand plugin candidate state.
    \return True when both plugin candidate states store equal values.
    */
    friend bool operator==(const PluginCandidateState& lhs, const PluginCandidateState& rhs) =
        default;
};

} // namespace rock_hero::editor::core
