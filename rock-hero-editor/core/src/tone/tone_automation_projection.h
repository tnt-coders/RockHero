/*!
\file tone_automation_projection.h
\brief Builds tone parameter automation lane view state from the arrangement's musical model.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/tone/tone_automation_view_state.h>
#include <string>
#include <unordered_map>

namespace rock_hero::common::audio
{
class IToneAutomation;
} // namespace rock_hero::common::audio

namespace rock_hero::editor::core
{

/*! \brief Runtime binding of one durable plugin id to its live instance and owning tone. */
struct ToneAutomationBinding
{
    /*! \brief Live plugin instance id; empty when the plugin is not currently loaded. */
    std::string instance_id;

    /*! \brief Tone document whose chain the plugin belongs to. */
    std::string tone_document_ref;
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

\param arrangement Arrangement owning the automation entries.
\param tempo_map Song tempo map used to derive display seconds.
\param selected_tone_document_ref Tone whose lanes are shown; empty when nothing is selected.
\param bindings Runtime plugin bindings keyed by durable plugin id.
\param tone_automation Audio automation port used for parameter names and metadata.
\return The automation lanes for the selected tone.
*/
[[nodiscard]] ToneAutomationViewState toneAutomationViewStateFor(
    const common::core::Arrangement& arrangement, const common::core::TempoMap& tempo_map,
    const std::string& selected_tone_document_ref,
    const std::unordered_map<std::string, ToneAutomationBinding>& bindings,
    const common::audio::IToneAutomation& tone_automation);

} // namespace rock_hero::editor::core
