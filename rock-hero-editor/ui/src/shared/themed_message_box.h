/*!
\file themed_message_box.h
\brief The editor's compact themed one-button message box.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero::editor::ui
{

/*!
\brief Shows the editor's themed warning box with a single OK button.

Constructs the juce::AlertWindow directly — the same pattern as the editor's other in-app prompts —
instead of going through AlertWindow::showAsync, whose LookAndFeel_V4::createAlertWindow factory
pads the content-derived bounds by a hard-coded 50 px and shifts the buttons 40 px down, leaving a
band of empty space under short messages. The window deletes itself when dismissed; Return, Escape,
and the OK button all dismiss it.

\param associated_component Component whose top-level window the box is positioned over; may be
       null for a screen-centered box.
\param title Window title naming what went wrong.
\param message Canonical user-facing text to display.
\param on_dismissed Called exactly once when the box is dismissed through any path; may be empty.
*/
void showThemedWarningBox(
    juce::Component* associated_component, const juce::String& title, const juce::String& message,
    std::function<void()> on_dismissed = {});

} // namespace rock_hero::editor::ui
