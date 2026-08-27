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

    /*! \brief Flip the session's grid-snap switch, which decides the placement quantum. */
    ToggleGridSnap,

    /*! \brief Switch the editor to another arrangement of the loaded song. */
    SelectArrangement,

    /*! \brief Select a tone region on the tone track (empty id clears the selection). */
    SelectToneRegion,

    /*! \brief Split the region under a grid position into a new tone-change region. */
    CreateToneRegion,

    /*! \brief Delete a tone region, merging its span into a neighbor. */
    DeleteToneRegion,

    /*! \brief Rename a tone in the arrangement's tone catalog. */
    RenameTone,

    /*! \brief Move the shared boundary between two adjacent tone regions. */
    MoveToneBoundary,

    /*!
    \brief Create a new empty tone and split the region under a grid position to reference it.
    */
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

    /*! \brief Replace a tone-chain plugin parameter's automation curve points. */
    SetToneAutomationPoints,

    /*! \brief Start a fresh untitled tone document in the Tone Designer. */
    NewToneDocument,

    /*! \brief Open a standalone tone file as the Tone Designer document. */
    OpenToneFile,

    /*! \brief Save the Tone Designer document to its associated tone file. */
    SaveToneFile,

    /*! \brief Save the Tone Designer document to a chosen tone file. */
    SaveToneFileAs,

    /*! \brief Replace the active project tone's rig with a tone file's contents. */
    ImportToneFile,

    /*! \brief Export the active project tone's rig to a tone file. */
    ExportToneFile,

    /*! \brief Resolve the tone-import automation-drop confirmation. */
    ResolveToneImportPrompt,

    /*! \brief Step the chart caret one grid line, string, or measure (the arrow keys). */
    StepChartCaret,

    /*! \brief Leap the chart caret to a derived musical position (Home/End, PageUp/Down). */
    JumpChartCaret,

    /*! \brief Extend or create the grid-locked time selection by one unit (Shift+arrows). */
    ExtendTimeSelection,

    /*!
    \brief Nudge the editor-wide selection one step (Alt+arrows) on whichever surface holds it.
    */
    MoveSelection,

    /*! \brief Delete the editor-wide selection, whatever its kind. */
    DeleteSelection,

    /*! \brief The Insert key's neutral create at an armed empty caret slot. */
    InsertAtCaret,

    /*! \brief Type one digit into the chart's fret entry. */
    TypeChartFretDigit,

    /*! \brief Shift every selected note's fret by one, shape-preserving. */
    ShiftChartFrets,

    /*! \brief Grow or shrink the selection's sustains by one grid or fine step. */
    AdjustChartSustain,

    /*! \brief Set or clear one technique across the chart selection. */
    ToggleChartTechnique,

    /*! \brief Set the chart selection to the left-hand tap attack. */
    SetChartLeftTap,

    /*! \brief Author, convert or remove a silently-held shape member at the chart caret. */
    ToggleChartHoldMarker,

    /*! \brief Sever a gesture at each selected waypoint, handing the remainder a new head. */
    DisconnectChartWaypoint,
};

} // namespace rock_hero::editor::core
