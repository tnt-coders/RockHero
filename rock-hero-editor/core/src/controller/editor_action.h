/*!
\file editor_action.h
\brief Private editor-controller action value used by implementation routing.
*/

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/editor/core/chart/chart_pointer.h>
#include <rock_hero/editor/core/chart/chart_technique.h>
#include <rock_hero/editor/core/controller/editor_action_id.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <rock_hero/editor/core/signal_chain/plugin_block_assignment.h>
#include <rock_hero/editor/core/signal_chain/plugin_display_type.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Outer struct holding every controller action case, the dispatch variant, and the id alias.

EditorAction stays private to the editor core target. Case structs are nested values so call sites
brace-init them directly (e.g. EditorAction::OpenProject{file}) and the variant's converting
constructor wraps them on entry to runAction.
*/
struct EditorAction
{
    /*! \brief Alias for the public identity enum used by routing tables and view state. */
    using Id = EditorActionId;

    /*! \brief Open a chosen editor project package. */
    struct OpenProject
    {
        /*!
        \brief Creates an open-project action.
        \param file_value Project package path selected by the user.
        */
        explicit OpenProject(std::filesystem::path file_value)
            : file(std::move(file_value))
        {}

        /*! \brief Project package path selected by the user. */
        std::filesystem::path file;
    };

    /*! \brief Restore the last-open editor project package during startup. */
    struct RestoreProject
    {
        /*!
        \brief Creates a startup-restore action.
        \param file_value Persisted project package path.
        */
        explicit RestoreProject(std::filesystem::path file_value)
            : file(std::move(file_value))
        {}

        /*! \brief Persisted project package path to restore at startup. */
        std::filesystem::path file;
    };

    /*! \brief Import a chosen song source into an unsaved project. */
    struct ImportSong
    {
        /*!
        \brief Creates an import-song action.
        \param file_value Song source path selected by the user.
        */
        explicit ImportSong(std::filesystem::path file_value)
            : file(std::move(file_value))
        {}

        /*! \brief Song source path selected by the user. */
        std::filesystem::path file;
    };

    /*! \brief Save the current project to its existing destination. */
    struct SaveProject
    {
    };

    /*! \brief Save the current project to a chosen destination. */
    struct SaveProjectAs
    {
        /*!
        \brief Creates a Save As action.
        \param file_value Project package destination path.
        */
        explicit SaveProjectAs(std::filesystem::path file_value)
            : file(std::move(file_value))
        {}

        /*! \brief Project package destination path. */
        std::filesystem::path file;
    };

    /*! \brief Publish the current song as a native song package. */
    struct PublishProject
    {
        /*!
        \brief Creates a publish action.
        \param file_value Native song package destination path.
        */
        explicit PublishProject(std::filesystem::path file_value)
            : file(std::move(file_value))
        {}

        /*! \brief Native song package destination path. */
        std::filesystem::path file;
    };

    /*! \brief Close the current project. */
    struct CloseProject
    {
    };

    /*! \brief Exit the editor application. */
    struct ExitApplication
    {
    };

    /*! \brief Resolve the active unsaved-changes prompt. */
    struct ResolveUnsavedChangesPrompt
    {
        /*!
        \brief Creates an unsaved-changes prompt resolution action.
        \param decision_value User-selected prompt decision.
        */
        explicit constexpr ResolveUnsavedChangesPrompt(
            UnsavedChangesDecision decision_value) noexcept
            : decision(decision_value)
        {}

        /*! \brief User-selected unsaved-changes decision. */
        UnsavedChangesDecision decision;
    };

    /*! \brief Cancel a controller-requested Save As prompt. */
    struct CancelSaveAsPrompt
    {
    };

    /*! \brief Cancel the active cancellable busy operation. */
    struct CancelBusyOperation
    {
    };

    /*! \brief Undo the most recent editor history entry. */
    struct Undo
    {
    };

    /*! \brief Redo the next editor history entry. */
    struct Redo
    {
    };

    /*! \brief Toggle transport playback. */
    struct PlayPause
    {
    };

    /*! \brief Stop playback or reset a paused cursor. */
    struct Stop
    {
    };

    /*! \brief Seek the transport to a timeline position. */
    struct SeekTimeline
    {
        /*!
        \brief Creates a timeline seek action.
        \param position_value Requested seek position on the song timeline.
        */
        explicit constexpr SeekTimeline(common::core::TimePosition position_value) noexcept
            : position(position_value)
        {}

        /*! \brief Requested seek position on the song timeline. */
        common::core::TimePosition position;
    };

    /*! \brief Set the timeline grid step as a note value. */
    struct SetGridNoteValue
    {
        /*!
        \brief Creates a grid note-value change action.
        \param note_value_value Grid step as a fraction of a whole note.
        */
        explicit constexpr SetGridNoteValue(common::core::Fraction note_value_value) noexcept
            : note_value(note_value_value)
        {}

        /*! \brief Grid step as a fraction of a whole note. */
        common::core::Fraction note_value;
    };

    /*!
    \brief Flip the session's grid-snap switch.

    Payload-free because the switch is a session fact with exactly two states and one verb: a
    "set to X" payload would let a caller push a state the user did not ask for, and there is no
    caller that wants one.
    */
    struct ToggleGridSnap
    {
    };

    /*! \brief Switch the editor to another arrangement of the loaded song. */
    struct SelectArrangement
    {
        /*!
        \brief Creates an arrangement-switch action.
        \param arrangement_id_value Stable arrangement id selected by the user.
        */
        explicit SelectArrangement(std::string arrangement_id_value)
            : arrangement_id(std::move(arrangement_id_value))
        {}

        /*! \brief Stable arrangement id selected by the user. */
        std::string arrangement_id;
    };

    /*! \brief Select a tone region on the tone track. */
    struct SelectToneRegion
    {
        /*!
        \brief Creates a tone-region selection action.
        \param region_id_value Stable region id, or empty to clear the selection.
        */
        explicit SelectToneRegion(std::string region_id_value)
            : region_id(std::move(region_id_value))
        {}

        /*! \brief Stable region id, or empty to clear the selection. */
        std::string region_id;
    };

    /*! \brief Split the region under a grid position into a new tone-change region. */
    struct CreateToneRegion
    {
        /*!
        \brief Creates a tone-region create action.
        \param position_value Grid position at which the tone changes; must fall strictly inside a
        region.
        \param new_region_id_value Canonical id minted for the new region beginning at the marker.
        \param tone_document_ref_value Existing catalog tone the new region references.
        */
        CreateToneRegion(
            common::core::GridPosition position_value, std::string new_region_id_value,
            std::string tone_document_ref_value)
            : position(position_value)
            , new_region_id(std::move(new_region_id_value))
            , tone_document_ref(std::move(tone_document_ref_value))
        {}

        /*! \brief Grid position at which the tone changes. */
        common::core::GridPosition position;

        /*! \brief Canonical id minted for the new region beginning at the marker. */
        std::string new_region_id;

        /*! \brief Existing catalog tone the new region references. */
        std::string tone_document_ref;
    };

    /*! \brief Delete a tone region, merging its span into a neighbor. */
    struct DeleteToneRegion
    {
        /*!
        \brief Creates a tone-region delete action.
        \param region_id_value Stable id of the region to delete.
        */
        explicit DeleteToneRegion(std::string region_id_value)
            : region_id(std::move(region_id_value))
        {}

        /*! \brief Stable id of the region to delete. */
        std::string region_id;
    };

    /*! \brief Rename a tone in the arrangement's tone catalog. */
    struct RenameTone
    {
        /*!
        \brief Creates a tone-rename action.
        \param tone_document_ref_value Document ref of the catalog tone to rename.
        \param name_value New user-facing tone name.
        */
        RenameTone(std::string tone_document_ref_value, std::string name_value)
            : tone_document_ref(std::move(tone_document_ref_value))
            , name(std::move(name_value))
        {}

        /*! \brief Document ref of the catalog tone to rename. */
        std::string tone_document_ref;

        /*! \brief New user-facing tone name. */
        std::string name;
    };

    /*! \brief Repoint a tone region at a different catalog tone. */
    /*! \brief Retone target naming a tone already in the arrangement's catalog. */
    struct ExistingTone
    {
        /*!
        \brief Names an existing catalog tone.
        \param tone_document_ref_value Catalog tone the region should reference.
        */
        explicit ExistingTone(std::string tone_document_ref_value)
            : tone_document_ref(std::move(tone_document_ref_value))
        {}

        /*! \brief Catalog tone the region should reference. */
        std::string tone_document_ref;
    };

    /*! \brief Retone target that does not exist yet: mint an empty tone under this name. */
    struct NewTone
    {
        /*!
        \brief Names a tone to mint.
        \param name_value User-facing name for the freshly minted tone.
        */
        explicit NewTone(std::string name_value)
            : name(std::move(name_value))
        {}

        /*! \brief User-facing name for the freshly minted tone. */
        std::string name;
    };

    /*!
    \brief What a retone points a region at.

    The two arms differ only in whether the tone exists yet, which is why minting belongs to the
    retone rather than to an action of its own: "use that tone" and "make a fresh one" are one
    change to one region, and therefore one undo entry.
    */
    using RetoneTarget = std::variant<ExistingTone, NewTone>;

    struct SetToneRegionTone
    {
        /*!
        \brief Creates a tone-region retone action.
        \param region_id_value Stable id of the region to repoint.
        \param target_value Tone the region should reference, existing or freshly minted.
        */
        SetToneRegionTone(std::string region_id_value, RetoneTarget target_value)
            : region_id(std::move(region_id_value))
            , target(std::move(target_value))
        {}

        /*! \brief Stable id of the region to repoint. */
        std::string region_id;

        /*! \brief Tone the region should reference, existing or freshly minted. */
        RetoneTarget target;
    };

    /*! \brief Move the shared boundary between two adjacent tone regions. */
    struct MoveToneBoundary
    {
        /*!
        \brief Creates a tone-boundary move action.
        \param right_region_id_value Region on the later side of the boundary (never the first).
        \param position_value New grid position for the shared boundary.
        */
        MoveToneBoundary(
            std::string right_region_id_value, common::core::GridPosition position_value)
            : right_region_id(std::move(right_region_id_value))
            , position(position_value)
        {}

        /*! \brief Region on the later side of the boundary; its predecessor is the earlier side. */
        std::string right_region_id;

        /*! \brief New grid position for the shared boundary (both neighbors move to it). */
        common::core::GridPosition position;
    };

    /*!
    \brief Create a new empty tone and split the region under a grid position to reference it.
    */
    struct CreateNewTone
    {
        /*!
        \brief Creates a new-tone action.
        \param position_value Grid position at which the new tone begins; must
                              fall strictly inside a region.
        \param name_value User-facing name for the new tone.
        */
        CreateNewTone(common::core::GridPosition position_value, std::string name_value)
            : position(position_value)
            , name(std::move(name_value))
        {}

        /*! \brief Grid position at which the new tone begins. */
        common::core::GridPosition position;

        /*! \brief User-facing name for the new tone. */
        std::string name;
    };

    /*! \brief Show the scanned plugin browser. */
    struct ShowPluginBrowser
    {
    };

    /*! \brief Begin inserting a plugin by showing the scanned browser for a chain slot. */
    struct BeginPluginInsert
    {
        /*!
        \brief Creates a begin-plugin-insert action.
        \param chain_index_value User-visible insertion slot while the current chain has capacity.
        \param block_index_value Fixed visual block the inserted plugin should occupy.
        */
        constexpr BeginPluginInsert(
            std::size_t chain_index_value, std::size_t block_index_value) noexcept
            : chain_index(chain_index_value)
            , block_index(block_index_value)
        {}

        /*! \brief User-visible insertion slot while the current chain has capacity. */
        std::size_t chain_index{};

        /*! \brief Fixed visual block the inserted plugin should occupy. */
        std::size_t block_index{};
    };

    /*! \brief Scan configured plugin catalog locations. */
    struct ScanPluginCatalog
    {
    };

    /*! \brief Insert the currently selected browser plugin into the signal chain. */
    struct InsertSelectedPlugin
    {
        /*!
        \brief Creates an insert-selected-plugin action.
        \param plugin_id_value Opaque plugin ID selected by the user.
        */
        explicit InsertSelectedPlugin(std::string plugin_id_value)
            : plugin_id(std::move(plugin_id_value))
        {}

        /*! \brief Opaque plugin ID selected by the user. */
        std::string plugin_id;
    };

    /*! \brief Remove a plugin instance from the signal chain. */
    struct RemovePlugin
    {
        /*!
        \brief Creates a remove-plugin action.
        \param instance_id_value Opaque plugin instance ID selected by the user.
        */
        explicit RemovePlugin(std::string instance_id_value)
            : instance_id(std::move(instance_id_value))
        {}

        /*! \brief Opaque plugin instance ID selected by the user. */
        std::string instance_id;
    };

    /*! \brief Move a plugin instance within the signal chain. */
    struct MovePlugin
    {
        /*!
        \brief Creates a move-plugin action.
        \param instance_id_value Opaque plugin instance ID selected by the user.
        \param destination_index_value Final user-visible chain index for the instance.
        \param placement_value Fixed visual block assignments after the move.
        */
        MovePlugin(
            std::string instance_id_value, std::size_t destination_index_value,
            std::vector<PluginBlockAssignment> placement_value)
            : instance_id(std::move(instance_id_value))
            , destination_index(destination_index_value)
            , placement(std::move(placement_value))
        {}

        /*! \brief Opaque plugin instance ID selected by the user. */
        std::string instance_id;

        /*! \brief Final user-visible chain index for the instance. */
        std::size_t destination_index{};

        /*! \brief Fixed visual block assignments after the move. */
        std::vector<PluginBlockAssignment> placement;
    };

    /*! \brief Set the visual block placement of the current signal chain. */
    struct SetSignalChainPlacement
    {
        /*!
        \brief Creates a set-signal-chain-placement action.
        \param placement_value Fixed visual block assignments for current plugin instances.
        */
        explicit SetSignalChainPlacement(std::vector<PluginBlockAssignment> placement_value)
            : placement(std::move(placement_value))
        {}

        /*! \brief Fixed visual block assignments for current plugin instances. */
        std::vector<PluginBlockAssignment> placement;
    };

    /*! \brief Set or clear a plugin instance's manual signal-chain display type override. */
    struct SetPluginDisplayTypeOverride
    {
        /*!
        \brief Creates a plugin display type override action.
        \param instance_id_value Opaque plugin instance ID selected by the user.
        \param display_type_value Manual display type, or empty to use automatic classification.
        */
        SetPluginDisplayTypeOverride(
            std::string instance_id_value, std::optional<PluginDisplayType> display_type_value)
            : instance_id(std::move(instance_id_value))
            , display_type(display_type_value)
        {}

        /*! \brief Opaque plugin instance ID selected by the user. */
        std::string instance_id;

        /*! \brief Manual display type, or empty to use automatic classification. */
        std::optional<PluginDisplayType> display_type;
    };

    /*! \brief Open a plugin instance editor window. */
    struct OpenPlugin
    {
        /*!
        \brief Creates an open-plugin action.
        \param instance_id_value Opaque plugin instance ID selected by the user.
        */
        explicit OpenPlugin(std::string instance_id_value)
            : instance_id(std::move(instance_id_value))
        {}

        /*! \brief Opaque plugin instance ID selected by the user. */
        std::string instance_id;
    };

    /*! \brief Replace a tone-chain plugin parameter's automation with a new musical point list. */
    struct SetToneAutomationPoints
    {
        /*!
        \brief Creates a set-tone-automation-points action.
        \param instance_id_value Plugin instance owning the parameter.
        \param param_id_value Parameter id within the plugin.
        \param points_value Replacement points at musical positions, in ascending order; empty
        removes the parameter's automation entirely.
        */
        SetToneAutomationPoints(
            std::string instance_id_value, std::string param_id_value,
            std::vector<common::core::ToneAutomationPoint> points_value)
            : instance_id(std::move(instance_id_value))
            , param_id(std::move(param_id_value))
            , points(std::move(points_value))
        {}

        /*! \brief Plugin instance owning the parameter. */
        std::string instance_id;

        /*! \brief Parameter id within the plugin. */
        std::string param_id;

        /*! \brief Replacement points at musical positions, in ascending order. */
        std::vector<common::core::ToneAutomationPoint> points;
    };

    /*! \brief Start a fresh untitled tone document in the Tone Designer. */
    struct NewToneDocument
    {
    };

    /*! \brief Open a standalone tone file as the Tone Designer document. */
    struct OpenToneFile
    {
        /*!
        \brief Creates an open-tone-file action.
        \param file_value Tone file path selected by the user.
        */
        explicit OpenToneFile(std::filesystem::path file_value)
            : file(std::move(file_value))
        {}

        /*! \brief Tone file path selected by the user. */
        std::filesystem::path file;
    };

    /*! \brief Save the Tone Designer document to its associated tone file. */
    struct SaveToneFile
    {
    };

    /*! \brief Save the Tone Designer document to a chosen tone file. */
    struct SaveToneFileAs
    {
        /*!
        \brief Creates a tone Save As action.
        \param file_value Tone file destination path.
        */
        explicit SaveToneFileAs(std::filesystem::path file_value)
            : file(std::move(file_value))
        {}

        /*! \brief Tone file destination path. */
        std::filesystem::path file;
    };

    /*! \brief Replace the active project tone's rig with a tone file's contents. */
    struct ImportToneFile
    {
        /*!
        \brief Creates an import-tone-file action.
        \param file_value Tone file path selected by the user.
        */
        explicit ImportToneFile(std::filesystem::path file_value)
            : file(std::move(file_value))
        {}

        /*! \brief Tone file path selected by the user. */
        std::filesystem::path file;
    };

    /*! \brief Export the active project tone's rig to a tone file. */
    struct ExportToneFile
    {
        /*!
        \brief Creates an export-tone-file action.
        \param file_value Tone file destination path selected by the user.
        */
        explicit ExportToneFile(std::filesystem::path file_value)
            : file(std::move(file_value))
        {}

        /*! \brief Tone file destination path selected by the user. */
        std::filesystem::path file;
    };

    /*! \brief Resolve the tone-import automation-drop confirmation. */
    struct ResolveToneImportPrompt
    {
        /*!
        \brief Creates a tone-import prompt resolution.
        \param decision_value User-selected import decision.
        */
        explicit ResolveToneImportPrompt(ToneImportDecision decision_value) noexcept
            : decision(decision_value)
        {}

        /*! \brief User-selected import decision. */
        ToneImportDecision decision;
    };

    /*! \brief Step the chart caret one grid line, string, or measure (the arrow keys). */
    struct StepChartCaret
    {
        /*! \brief Which way the caret steps. */
        ChartStepDirection direction{};

        /*!
        \brief True for Ctrl's reach: a measure jump along time, the adjacent group of rows (the
        strings, the tone row, the lanes, the "+" row) across them.
        */
        bool reach{};
    };

    /*! \brief Leap the chart caret to a derived musical position (Home/End, PageUp/Down). */
    struct JumpChartCaret
    {
        /*! \brief The destination family. */
        ChartCaretJump target{};
    };

    /*! \brief Extend or create the grid-locked time selection by one unit (Shift+arrows). */
    struct ExtendTimeSelection
    {
        /*! \brief The unit the focus edge moves by. */
        TimeSelectionExtent extent{};

        /*! \brief Left for earlier, Right for later. */
        ChartStepDirection direction{};
    };

    /*!
    \brief Nudge the editor-wide selection one step (Alt+arrows) on whichever surface holds it.
    */
    struct MoveSelection
    {
        /*! \brief Which way the selection moves. */
        ChartStepDirection direction{};
    };

    /*! \brief Delete the editor-wide selection, whatever its kind. */
    struct DeleteSelection
    {
    };

    /*! \brief The Insert key's create: an on-curve point at an armed automation-lane slot. */
    struct InsertLanePoint
    {
    };

    /*! \brief Type one digit into the chart's fret entry, in one of the two entry verbs. */
    struct TypeChartFretDigit
    {
        /*! \brief The digit typed, 0 to 9. */
        int digit{};

        /*!
        \brief True for the PATH verb (`Alt`+digit), false for the bare digit.

        One cell of the tab lane's entry grammar differs between them: at a ring's EXACT END, an
        `Alt` digit states the slide-out the release names while a bare digit places the adjacent
        head. Everywhere else the two say the same thing — a point on a ring the slot falls inside,
        a head on a slot no ring covers, a retype over a non-empty selection.
        */
        bool path{false};
    };

    /*! \brief Shift every selected note's fret by one, shape-preserving. */
    struct ShiftChartFrets
    {
        /*! \brief +1 up the neck, -1 down. */
        int direction{};
    };

    /*! \brief Grow or shrink the selection's sustains by one placement-quantum step. */
    struct AdjustChartSustain
    {
        /*! \brief +1 to grow, -1 to shrink. */
        int direction{};
    };

    /*! \brief Set or clear one technique across the chart selection. */
    struct ToggleChartTechnique
    {
        /*! \brief The technique to toggle. */
        ChartTechnique technique{};
    };

    /*!
    \brief State the fret-hand harmonic on the chart selection at one chosen partial.

    The picker's MOUSE form. The keyboard states its choice inside the pending entry, where a
    second `H` cycles it; a menu row is already a deliberate choice, so it names the partial and
    applies. Carries the ORDINAL rather than a node position because that is the stable name for a
    choice: the position moves with the capo and with each member's own stop, the partial does not.
    */
    struct SetChartHarmonicNode
    {
        /*! \brief Partial whose node the selection's ambiguous members take. */
        int partial{};
    };

    /*! \brief Set the chart selection to the left-hand tap attack. */
    struct SetChartLeftTap
    {
    };

    /*!
    \brief Author, convert or remove a silently-held shape member at the chart caret.

    Carries no payload because the verb is CARET-anchored: the slot the caret sits on is the whole
    operand, and passing one would let a caller state a position the charter is not looking at.
    */
    struct ToggleChartSilentHold
    {
    };

    /*!
    \brief Toggle every selected junction: a keyframe becomes a head, a head becomes a point.

    Carries no payload because the verb is SELECTION-anchored like every other technique verb: the
    selected keyframes and heads are the whole operand, and an empty selection makes the press
    inert.
    */
    struct ToggleChartJunction
    {
    };

    /*! \brief Select a song-structure section on the ruler. */
    struct SelectSongSection
    {
        /*! \brief Position of the section to select, or empty to clear the selection. */
        std::optional<common::core::GridPosition> position{};
    };

    /*! \brief Select a tempo chip on the ruler: the beat anchor it marks. */
    struct SelectTempoAnchor
    {
        /*! \brief Beat the anchor pins; a position naming no anchor selects nothing. */
        common::core::GridPosition position{};
    };

    /*! \brief Select a time-signature chip on the ruler: the change it marks. */
    struct SelectTimeSignature
    {
        /*! \brief Measure the change starts; a measure naming no change selects nothing. */
        int measure{1};
    };

    /*!
    \brief Insert a song-structure section at a position's measure downbeat.

    Carries the position the surface captured AT THE PRESS, exactly as the tone-change insert
    does: a verb that puts a prompt between the key and the effect must not re-read the marker
    after the prompt closes, or a rolling transport lands the section wherever the playhead
    drifted to while the charter typed. The snap to the measure downbeat is the verb's own.
    */
    struct InsertSongSection
    {
        /*! \brief Position the section is asked for; snapped to its measure's downbeat. */
        common::core::GridPosition position;

        /*! \brief Name for the new section; an empty name refuses. */
        std::string name;
    };

    /*!
    \brief Rename the song-structure section at a position.

    Position-anchored rather than selection-anchored, matching the tone rename beside it: a rename
    names its subject, and the chip double-click reaches a section the same way Ctrl+M on a
    selected chip does.
    */
    struct RenameSongSection
    {
        /*! \brief Position of the section to rename. */
        common::core::GridPosition position{};

        /*! \brief New name; an empty name refuses. */
        std::string name;
    };

    /*! \brief Variant carrying project package write actions. */
    using ProjectWriteAction = std::variant<SaveProjectAs, SaveProject, PublishProject>;

    /*!
    \brief Variant carrying lifecycle actions that may be deferred by unsaved-changes prompts.

    Tone Designer document actions belong here too: opening a tone file or starting a fresh
    document replaces the designer's file-backed document, so they defer behind the same
    unsaved-changes gate as project-replacing actions.
    */
    using ProjectAction = std::variant<
        OpenProject, RestoreProject, ImportSong, SaveProject, SaveProjectAs, PublishProject,
        CloseProject, ExitApplication, NewToneDocument, OpenToneFile>;

    /*! \brief Variant carrying any controller action and its payload. */
    using Action = std::variant<
        OpenProject, RestoreProject, ImportSong, SaveProject, SaveProjectAs, PublishProject,
        CloseProject, ExitApplication, ResolveUnsavedChangesPrompt, CancelSaveAsPrompt,
        CancelBusyOperation, Undo, Redo, PlayPause, Stop, SeekTimeline, SetGridNoteValue,
        ToggleGridSnap, SelectArrangement, SelectToneRegion, CreateToneRegion, DeleteToneRegion,
        RenameTone, SetToneRegionTone, MoveToneBoundary, CreateNewTone, ShowPluginBrowser,
        BeginPluginInsert, ScanPluginCatalog, InsertSelectedPlugin, RemovePlugin, MovePlugin,
        SetSignalChainPlacement, SetPluginDisplayTypeOverride, OpenPlugin, SetToneAutomationPoints,
        NewToneDocument, OpenToneFile, SaveToneFile, SaveToneFileAs, ImportToneFile, ExportToneFile,
        ResolveToneImportPrompt, StepChartCaret, JumpChartCaret, ExtendTimeSelection, MoveSelection,
        DeleteSelection, InsertLanePoint, TypeChartFretDigit, ShiftChartFrets, AdjustChartSustain,
        ToggleChartTechnique, SetChartHarmonicNode, SetChartLeftTap, ToggleChartSilentHold,
        ToggleChartJunction, SelectSongSection, InsertSongSection, RenameSongSection,
        SelectTempoAnchor, SelectTimeSignature>;
};

/*!
\brief Returns the identity of an action without exposing its payload.
\param action Action to identify.
\return Matching EditorAction::Id member for the variant's current alternative.
*/
[[nodiscard]] EditorAction::Id idOf(const EditorAction::Action& action);

/*!
\brief Returns the identity of a project-lifecycle action without exposing its payload.
\param action Project-lifecycle action to identify.
\return Matching EditorAction::Id member for the variant's current alternative.
*/
[[nodiscard]] EditorAction::Id idOf(const EditorAction::ProjectAction& action);

/*!
\brief Returns the identity of a project write action without exposing its payload.
\param action Project write action to identify.
\return Matching EditorAction::Id member for the variant's current alternative.
*/
[[nodiscard]] EditorAction::Id idOf(const EditorAction::ProjectWriteAction& action);

} // namespace rock_hero::editor::core
