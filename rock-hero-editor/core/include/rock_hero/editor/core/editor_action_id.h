/*!
\file editor_action_id.h
\brief Public identity enum for editor controller actions used by view state and controllers.
*/

#pragma once

#include <cstdint>

namespace rock_hero::editor::core
{

/*!
\brief Identifies an editor controller action.

Exposed publicly so view state can reference which action a prompt is currently about. Controller
routing tables key on this enum as well; it is the single identity surface for actions.
*/
enum class EditorActionId : std::uint8_t
{
    /*! \brief Open a chosen editor project package. */
    OpenProject,

    /*! \brief Restore the last-open editor project package during startup. */
    RestoreProject,

    /*! \brief Import a chosen song source into an unsaved project. */
    ImportSong,

    /*! \brief Save the current project to its existing destination. */
    SaveProject,

    /*! \brief Save the current project to a chosen destination. */
    SaveProjectAs,

    /*! \brief Publish the current song as a native song package. */
    PublishProject,

    /*! \brief Close the current project. */
    CloseProject,

    /*! \brief Exit the editor application. */
    ExitApplication,

    /*! \brief Resolve the active unsaved-changes prompt. */
    ResolveUnsavedChangesPrompt,

    /*! \brief Cancel a controller-requested Save As prompt. */
    CancelSaveAsPrompt,

    /*! \brief Toggle transport playback. */
    PlayPause,

    /*! \brief Stop playback or reset a paused cursor. */
    Stop,

    /*! \brief Seek from a normalized waveform coordinate. */
    SeekWaveform,

    /*! \brief Add a selected plugin file to the signal chain. */
    AddPlugin,

    /*! \brief Remove a plugin instance from the signal chain. */
    RemovePlugin,

    /*! \brief Open a plugin instance editor window. */
    OpenPlugin,
};

} // namespace rock_hero::editor::core
