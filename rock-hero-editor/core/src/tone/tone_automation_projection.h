/*!
\file tone_automation_projection.h
\brief Builds tone parameter automation lane view state from the arrangement's musical model.
*/

#pragma once

#include "controller/editor_selection.h"

#include <rock_hero/common/audio/automation/tone_automation_rebuild.h>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/tone/tone_automation_view_state.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief One session-scoped open automation lane that has no authored points yet.

Opened by the picker and closed by the lane's remove gesture; it tracks the parameter's live
value until the first point is authored, at which point the arrangement's model entry takes over.
Not persisted and not undoable: an open lane with no points is a view arrangement, not an edit.
Keyed by the durable plugin id, not the instance id, so open lanes survive rig reloads (which
recreate every plugin instance).
*/
struct OpenAutomationLane
{
    /*! \brief Tone whose chain owns the lane's plugin. */
    std::string tone_document_ref;

    /*! \brief Owning plugin's durable id (resolved to the live instance through the bindings). */
    std::string plugin_id;

    /*! \brief Parameter id within the plugin. */
    std::string param_id;

    /*!
    \brief Compares two open lanes by their stored values.
    \param lhs Left-hand open lane.
    \param rhs Right-hand open lane.
    \return True when both open lanes store equal values.
    */
    friend bool operator==(const OpenAutomationLane& lhs, const OpenAutomationLane& rhs) = default;
};

/*!
\brief Converts an exact musical grid position to absolute song seconds.
\param tempo_map Song tempo map defining the grid.
\param position Musical position to convert.
\return The position's absolute time in seconds.
*/
[[nodiscard]] double secondsAtGridPosition(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& position);

/*!
\brief Builds the automation-lane view state for the selected tone.

Lanes come from the arrangement's musical automation entries whose plugin binding belongs to the
selected tone; the derived seconds for each point come from the tempo map. Parameter names and
discrete metadata come from the audio port; an entry whose parameter no longer resolves renders as
an unresolved (disabled) lane. Entries whose plugin id has no runtime binding at all are not shown
(they stay persisted; publish-time cleanup is a separate concern).

Open lanes without authored points follow the model lanes, rendered as live-tracking lanes; an
open lane whose parameter already has a model entry is subsumed by it.

\param arrangement Arrangement owning the automation entries.
\param tempo_map Song tempo map used to derive display seconds.
\param selected_tone_document_ref Tone whose lanes are shown; empty when nothing is selected.
\param bindings Runtime plugin bindings keyed by durable plugin id.
\param open_lanes Session-scoped open lanes; entries for other tones are ignored.
\param tone_automation Audio automation port used for parameter names and metadata.
\param selected_point Editor-wide automation-point selection to resolve against the built lanes,
or null when the selection holds another kind; a selection that no longer resolves publishes as
no selected point.
\return The automation lanes for the selected tone.
*/
[[nodiscard]] ToneAutomationViewState makeToneAutomationViewState(
    const common::core::Arrangement& arrangement, const common::core::TempoMap& tempo_map,
    const std::string& selected_tone_document_ref,
    const std::unordered_map<std::string, common::audio::ToneAutomationBinding>& bindings,
    const std::vector<OpenAutomationLane>& open_lanes,
    const common::audio::IToneAutomation& tone_automation,
    const AutomationPointSelection* selected_point);

} // namespace rock_hero::editor::core
