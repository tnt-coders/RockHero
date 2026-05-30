/*!
\file editor_action_availability.h
\brief Pure editor action availability policy.
*/

#pragma once

#include "editor_action.h"

namespace rock_hero::editor::core
{

/*!
\brief Current editor conditions used to decide whether an action is available.

The controller owns the underlying state and collects this value for one decision pass. The
availability policy reads only these booleans, so it can stay independent of controller fields,
ports, and view projection.
*/
struct ActionConditions
{
    /*! \brief True when a busy operation is active. */
    bool busy{false};

    /*! \brief True when the input calibration prompt is visible. */
    bool input_calibration_prompt_visible{false};

    /*! \brief True when live input can be auditioned through the current route. */
    bool live_input_audition_available{false};

    /*! \brief True when an editor project is open. */
    bool has_project{false};

    /*! \brief True when an unsaved-changes prompt is active. */
    bool has_unsaved_changes_prompt{false};

    /*! \brief True when a Save As destination prompt is active. */
    bool has_save_as_prompt{false};

    /*! \brief True when the session has a loaded arrangement. */
    bool has_loaded_arrangement{false};

    /*! \brief True when Stop should reset playback or the playhead. */
    bool can_stop_transport{false};

    /*! \brief True when the plugin catalog has candidates that can be inserted. */
    bool has_plugin_candidates{false};

    /*! \brief True when the live rig has loaded plugin instances. */
    bool has_loaded_plugins{false};
};

/*!
\brief Reports whether an action intentionally takes over an active busy operation.
\param action Action to evaluate.
\return True when the action supersedes busy work.
*/
[[nodiscard]] bool actionSupersedesBusy(EditorAction::Id action) noexcept;

/*!
\brief Reports whether an action is currently available.
\param action Action to evaluate.
\param conditions Current editor conditions collected by the controller.
\return True when the action may run.
*/
[[nodiscard]] bool isActionAvailable(
    EditorAction::Id action, const ActionConditions& conditions) noexcept;

} // namespace rock_hero::editor::core
