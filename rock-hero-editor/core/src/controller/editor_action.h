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
#include <rock_hero/common/core/tone/tone_track.h>
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

    /*! \brief Resize a tone region to new musical endpoints. */
    struct ResizeToneRegion
    {
        /*!
        \brief Creates a tone-region resize action.
        \param region_id_value Stable region id selected by the user.
        \param start_value New musical start (inclusive).
        \param end_value New musical end (exclusive).
        */
        ResizeToneRegion(
            std::string region_id_value, common::core::ToneGridPosition start_value,
            common::core::ToneGridPosition end_value)
            : region_id(std::move(region_id_value))
            , start(start_value)
            , end(end_value)
        {}

        /*! \brief Stable region id selected by the user. */
        std::string region_id;

        /*! \brief New musical start (inclusive). */
        common::core::ToneGridPosition start;

        /*! \brief New musical end (exclusive). */
        common::core::ToneGridPosition end;
    };

    /*! \brief Split the region under a grid position into a new tone-change region. */
    struct CreateToneRegion
    {
        /*!
        \brief Creates a tone-region create action.
        \param at_value Grid position at which the tone changes; must fall strictly inside a region.
        \param new_region_id_value Canonical id minted for the new region beginning at \p at_value.
        \param tone_document_ref_value Existing catalog tone the new region references.
        */
        CreateToneRegion(
            common::core::ToneGridPosition at_value, std::string new_region_id_value,
            std::string tone_document_ref_value)
            : at(at_value)
            , new_region_id(std::move(new_region_id_value))
            , tone_document_ref(std::move(tone_document_ref_value))
        {}

        /*! \brief Grid position at which the tone changes. */
        common::core::ToneGridPosition at;

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

    /*! \brief Move the shared boundary between two adjacent tone regions. */
    struct MoveToneBoundary
    {
        /*!
        \brief Creates a tone-boundary move action.
        \param right_region_id_value Region on the later side of the boundary (never the first).
        \param position_value New grid position for the shared boundary.
        */
        MoveToneBoundary(
            std::string right_region_id_value, common::core::ToneGridPosition position_value)
            : right_region_id(std::move(right_region_id_value))
            , position(position_value)
        {}

        /*! \brief Region on the later side of the boundary; its predecessor is the earlier side. */
        std::string right_region_id;

        /*! \brief New grid position for the shared boundary (both neighbors move to it). */
        common::core::ToneGridPosition position;
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

    /*! \brief Variant carrying project package write actions. */
    using ProjectWriteAction = std::variant<SaveProjectAs, SaveProject, PublishProject>;

    /*! \brief Variant carrying project-lifecycle actions that may be deferred by prompts. */
    using ProjectAction = std::variant<
        OpenProject, RestoreProject, ImportSong, SaveProject, SaveProjectAs, PublishProject,
        CloseProject, ExitApplication>;

    /*! \brief Variant carrying any controller action and its payload. */
    using Action = std::variant<
        OpenProject, RestoreProject, ImportSong, SaveProject, SaveProjectAs, PublishProject,
        CloseProject, ExitApplication, ResolveUnsavedChangesPrompt, CancelSaveAsPrompt,
        CancelBusyOperation, Undo, Redo, PlayPause, Stop, SeekTimeline, SetGridNoteValue,
        SelectArrangement, SelectToneRegion, ResizeToneRegion, CreateToneRegion, DeleteToneRegion,
        RenameTone, MoveToneBoundary, ShowPluginBrowser, BeginPluginInsert, ScanPluginCatalog,
        InsertSelectedPlugin, RemovePlugin, MovePlugin, SetSignalChainPlacement,
        SetPluginDisplayTypeOverride, OpenPlugin>;
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
