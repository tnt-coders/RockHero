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
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
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
\brief One shown automation lane's identity and model source, in display order.

The row identity is (instance, parameter) — never a display index — and \c entry points at the
arrangement's automation entry for model lanes, or is null for open (unauthored, live-tracking)
lanes. Pointers borrow from the arrangement passed to \ref toneAutomationLaneSources.
*/
struct ToneAutomationLaneSource
{
    /*! \brief Owning plugin instance id. */
    std::string instance_id;

    /*! \brief Parameter id within the plugin. */
    std::string param_id;

    /*! \brief The arrangement's automation entry backing the lane; null for an open lane. */
    const common::core::ToneParameterAutomation* entry{nullptr};
};

/*!
\brief The shown lanes for a tone, in display order — the one lane-ordering rule.

This is the ordering \ref makeToneAutomationViewState builds lanes from, exposed separately so
the marker's row traversal derives its rows from the same enumeration WITHOUT paying the full
projection (port parameter listing, per-point seconds) on every keystroke: model lanes whose
plugin binding belongs to the tone first, then open lanes not subsumed by a model lane.

\param arrangement Arrangement owning the automation entries.
\param selected_tone_document_ref Tone whose lanes are shown; empty yields no lanes.
\param bindings Runtime plugin bindings keyed by durable plugin id.
\param open_lanes Session-scoped open lanes; entries for other tones are ignored.
\return The shown lanes' identities and model sources, in display order.
*/
[[nodiscard]] std::vector<ToneAutomationLaneSource> toneAutomationLaneSources(
    const common::core::Arrangement& arrangement, const std::string& selected_tone_document_ref,
    const std::unordered_map<std::string, common::audio::ToneAutomationBinding>& bindings,
    const std::vector<OpenAutomationLane>& open_lanes);

/*!
\brief The automation curve's value at a musical position, matching the drawn curve.

Linear segments between points on a continuous parameter, held steps on a discrete one, and
flat extensions outside the authored span — the model-side sibling of the lanes view's own
on-curve evaluation, so a point created at this value lands on the drawn line (placement is
sonically silent, 2026-07-18).

\param points Authored points in ascending musical order.
\param tempo_map Song tempo map used to place positions on the time axis.
\param position Musical position to evaluate at.
\param is_discrete True when the parameter is stepped rather than continuous.
\param fallback_value Value returned when no points are authored (the live tracking line).
\return The curve's normalised value at the position.
*/
[[nodiscard]] float toneAutomationCurveValueAt(
    const std::vector<common::core::ToneAutomationPoint>& points,
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& position,
    bool is_discrete, float fallback_value);

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
