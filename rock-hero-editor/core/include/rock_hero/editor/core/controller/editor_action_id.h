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

    /*! \brief Cancel the active cancellable busy operation. */
    CancelBusyOperation,

    /*! \brief Undo the most recent editor history entry. */
    Undo,

    /*! \brief Redo the next editor history entry. */
    Redo,

    /*! \brief Toggle transport playback. */
    PlayPause,

    /*! \brief Stop playback or reset a paused cursor. */
    Stop,

    /*! \brief Seek the transport to a timeline position. */
    SeekTimeline,

    /*! \brief Set the timeline grid step as a note value. */
    SetGridNoteValue,

    /*! \brief Switch the editor to another arrangement of the loaded song. */
    SelectArrangement,

    /*! \brief Select a tone region on the tone track (empty id clears the selection). */
    SelectToneRegion,

    /*! \brief Resize a tone region to new musical endpoints. */
    ResizeToneRegion,

    /*! \brief Split the region under a grid position into a new tone-change region. */
    CreateToneRegion,

    /*! \brief Delete a tone region, merging its span into a neighbor. */
    DeleteToneRegion,

    /*! \brief Rename a tone in the arrangement's tone catalog. */
    RenameTone,

    /*! \brief Move the shared boundary between two adjacent tone regions. */
    MoveToneBoundary,

    /*! \brief Create a new empty tone and split the region under a grid position to reference it. */
    CreateNewTone,

    /*! \brief Show the scanned plugin browser. */
    ShowPluginBrowser,

    /*! \brief Begin inserting a plugin at a chain slot by showing the scanned browser. */
    BeginPluginInsert,

    /*! \brief Scan default plugin catalog locations for browser plugins. */
    ScanPluginCatalog,

    /*! \brief Insert the currently selected browser plugin into the signal chain. */
    InsertSelectedPlugin,

    /*! \brief Remove a plugin instance from the signal chain. */
    RemovePlugin,

    /*! \brief Move a plugin instance within the signal chain. */
    MovePlugin,

    /*! \brief Set the editor-authored fixed block placement for the signal chain. */
    SetSignalChainPlacement,

    /*! \brief Set or clear a plugin instance's signal-chain display type override. */
    SetPluginDisplayTypeOverride,

    /*! \brief Open a plugin instance editor window. */
    OpenPlugin,
};

} // namespace rock_hero::editor::core
