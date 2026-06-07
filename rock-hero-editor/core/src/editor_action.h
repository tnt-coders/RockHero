/*!
\file editor_action.h
\brief Private editor-controller action value used by implementation routing.
*/

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <rock_hero/editor/core/editor_action_id.h>
#include <rock_hero/editor/core/editor_view_state.h>
#include <rock_hero/editor/core/plugin_block_assignment.h>
#include <rock_hero/editor/core/plugin_display_type.h>
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

        UnsavedChangesDecision decision;
    };

    /*! \brief Cancel a controller-requested Save As prompt. */
    struct CancelSaveAsPrompt
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

    /*! \brief Seek from a normalized waveform coordinate. */
    struct SeekWaveform
    {
        /*!
        \brief Creates a waveform seek action.
        \param normalized_x_value Click position normalized to the interval [0, 1].
        */
        explicit constexpr SeekWaveform(double normalized_x_value) noexcept
            : normalized_x(normalized_x_value)
        {}

        double normalized_x;
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

        std::size_t chain_index{};
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

        std::string instance_id;
        std::size_t destination_index{};
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

        std::string instance_id;
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
        CloseProject, ExitApplication, ResolveUnsavedChangesPrompt, CancelSaveAsPrompt, PlayPause,
        Stop, SeekWaveform, ShowPluginBrowser, BeginPluginInsert, ScanPluginCatalog,
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
