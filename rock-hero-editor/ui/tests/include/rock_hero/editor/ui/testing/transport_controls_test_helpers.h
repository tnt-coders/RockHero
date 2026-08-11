/*!
\file transport_controls_test_helpers.h
\brief Shared accessors for the transport widget's concrete buttons in editor UI tests.
*/

#pragma once

#include "transport/transport_controls.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>

namespace rock_hero::editor::ui::testing
{

/*!
\brief Returns the play/pause button owned by a transport-controls widget.

The widget's button component IDs are named here once, so the widget's own tests and the
editor-view tests that reach through the hosted widget cannot drift apart on a renamed ID.

\param controls Transport controls to inspect.
\return Play/pause button child.
\throws std::runtime_error when the child is missing or has a different type.
*/
[[nodiscard]] inline juce::DrawableButton& getPlayPauseButton(TransportControls& controls)
{
    return findRequiredDirectChild<juce::DrawableButton>(controls, "play_pause_button");
}

/*!
\brief Returns the stop button owned by a transport-controls widget.
\param controls Transport controls to inspect.
\return Stop button child.
\throws std::runtime_error when the child is missing or has a different type.
*/
[[nodiscard]] inline juce::DrawableButton& getStopButton(TransportControls& controls)
{
    return findRequiredDirectChild<juce::DrawableButton>(controls, "stop_button");
}

} // namespace rock_hero::editor::ui::testing
