/*!
\file grid_snap_warning_dialog.h
\brief Private editor UI dialog warning before grid snap turns off.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>

namespace rock_hero::editor::ui
{

/*!
\brief Opens the warning shown before grid snapping turns off.

Free placement is a mode almost no charter wants, and `Ctrl+G` is one slip away from it, so the
editor asks every time the switch would go off — there is no "don't warn me again", and nothing
about grid snap is stored anywhere. Rendered as the editor's standard juce::AlertWindow in the
warning register, with the recommended "keep snapping on" path as the default button: it carries
both the Return and the Escape shortcut, and every other way out of the dialog reports it too, so
only a deliberate press of the other button can turn snapping off.

The copy names the binding itself, because an accidental press is exactly the case where the user
does not know what they pressed. The chord text comes from the live mapping set through
\ref commandChordText, so a rebind cannot make the dialog lie; when nothing is bound to the
command the copy names the command alone.
*/
class GridSnapWarningDialog final
{
public:
    /*!
    \brief Called at most once when the dialog is answered or dismissed.

    Receives GridSnapWarningDecision::KeepSnappingOn for every close path except the explicit
    turn-off button.
    */
    using DecisionCallback = std::function<void(core::GridSnapWarningDecision decision)>;

    /*!
    \brief Opens the self-deleting modal alert associated with the window that owns the anchor.
    \param anchor Component used to find the owning editor window for positioning, and the liveness
           anchor for the callback: if it is destroyed before the dialog closes, on_decision is not
           called.
    \param command_manager Manager the grid-snap command is registered with, read for the chord
           text the copy names.
    \param on_decision Called once with the user's decision, unless the anchor was destroyed first.
    */
    static void show(
        juce::Component& anchor, juce::ApplicationCommandManager& command_manager,
        DecisionCallback on_decision);

private:
    GridSnapWarningDialog() = default;
};

} // namespace rock_hero::editor::ui
