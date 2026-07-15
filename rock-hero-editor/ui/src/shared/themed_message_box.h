/*!
\file themed_message_box.h
\brief The editor's standard themed message boxes and shared modal dialog launcher.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace rock_hero::editor::ui
{

/*!
\brief Launches an already-built alert window as a self-deleting modal dialog.

This is the single owner of the editor's modal-dialog pattern. Dialogs are constructed directly —
never through AlertWindow::showAsync, whose LookAndFeel_V4::createAlertWindow factory pads the
content-derived bounds by a hard-coded 50 px and shifts the buttons 40 px down, leaving a band of
empty space under short messages — and delete themselves once dismissed. The message boxes below
cover the fixed-shape dialogs; a dialog that needs custom content subclasses juce::AlertWindow to
own its extra components (see GameAudioRecommendationDialog) and launches through this function.

\param window The fully populated alert window; ownership transfers to the modal system.
\param owner The component that requested this dialog, used as a liveness anchor. A self-deleting
       modal dialog can outlive the component that opened it (for example the editor is torn down at
       app shutdown while a prompt is still open), and on_result captures that component, so the
       result is delivered only while owner is still alive. This makes every themed dialog safe
       without each call site tracking a juce::Component::SafePointer. Pass null only when there is
       no owning component (a screen-centered dialog whose callback captures longer-lived state),
       which opts out of the liveness check.
\param on_result Called at most once with the window's modal result: the return value passed to
       addButton() for the pressed button, or 0 when the window is dismissed with Escape. Not
       called at all if owner was destroyed first. Runs before the window is destroyed, so captured
       window pointers may still read its state. May be empty.
*/
void showThemedDialogModally(
    std::unique_ptr<juce::AlertWindow> window, juce::Component* owner,
    std::function<void(int)> on_result);

/*!
\brief Shows the editor's themed warning box with a single OK button.

\param associated_component Component whose top-level window the box is positioned over; may be
       null for a screen-centered box.
\param title Window title naming what went wrong.
\param message Canonical user-facing text to display.
\param on_dismissed Called once when the box is dismissed through any path (the OK button, Return,
       or Escape), unless associated_component was destroyed first; may be empty.
*/
void showThemedWarningBox(
    juce::Component* associated_component, const juce::String& title, const juce::String& message,
    std::function<void()> on_dismissed = {});

/*!
\brief Shows the editor's themed multiple-choice question box.

Buttons appear in the given order, with the keyboard rules fixed here for every question box:
Return activates the first button and Escape reports the last one. Callers therefore list the
primary action first and the cancel/dismiss outcome last.

\param associated_component Component whose top-level window the box is positioned over; may be
       null for a screen-centered box.
\param title Window title naming the decision.
\param message Canonical user-facing text to display.
\param buttons Button labels in display order; must not be empty.
\param on_choice Called once with the zero-based index of the chosen button (Escape reports the
       last index), unless associated_component was destroyed first; may be empty.
*/
void showThemedQuestionBox(
    juce::Component* associated_component, const juce::String& title, const juce::String& message,
    const juce::StringArray& buttons, std::function<void(int)> on_choice);

/*!
\brief Shows the editor's themed single-field text prompt.

\param associated_component Component whose top-level window the prompt is positioned over; may be
       null for a screen-centered prompt.
\param title Window title naming what the text is for.
\param message Canonical user-facing text shown above the field.
\param initial_value Text pre-filled into the field.
\param accept_label Label of the accepting button; Return also accepts.
\param on_accept Called with the entered text only when the prompt is accepted and
       associated_component is still alive; cancelling (the Cancel button or Escape) invokes
       nothing.
*/
void showThemedTextPrompt(
    juce::Component* associated_component, const juce::String& title, const juce::String& message,
    const juce::String& initial_value, const juce::String& accept_label,
    std::function<void(const juce::String&)> on_accept);

} // namespace rock_hero::editor::ui
