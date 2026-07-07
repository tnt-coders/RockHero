/*!
\file tone_automation_projection.h
\brief Builds tone parameter automation lane view state from the audio automation port.
*/

#pragma once

#include <rock_hero/editor/core/tone/tone_automation_view_state.h>
#include <string>

namespace rock_hero::common::audio
{
class IToneAutomation;
} // namespace rock_hero::common::audio

namespace rock_hero::editor::core
{

/*!
\brief Builds the automation-lane view state for the selected tone.

Reads each of the selected tone's automatable parameters through the audio port and emits a lane for
every parameter that currently carries a curve. An empty tone reference (no selection) yields empty
lanes, and a tone that is not loaded yields the tone reference with no lanes.

\param tone_automation Audio automation port used to list parameters and read curves.
\param selected_tone_document_ref Tone whose lanes are shown; empty when nothing is selected.
\return The automation lanes for the selected tone.
*/
[[nodiscard]] ToneAutomationViewState toneAutomationViewStateFor(
    const common::audio::IToneAutomation& tone_automation,
    const std::string& selected_tone_document_ref);

} // namespace rock_hero::editor::core
