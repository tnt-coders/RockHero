/*!
\file editor_action.h
\brief Private editor-controller action value used by implementation routing.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <rock_hero/editor/core/editor_view_state.h>

namespace rock_hero::editor::core
{

/*!
\brief Value object describing a controller action and its optional payload.

EditorAction stays private to the editor core target. It gives controller entry points one
consistent dispatch shape without publishing implementation routing details through
EditorController's public API.
*/
class EditorAction final
{
public:
    /*! \brief Identifies the controller action to run. */
    enum class Id : std::uint8_t
    {
        /*! \brief Open a chosen editor project package. */
        OpenProject,

        /*! \brief Restore the last-open editor project package during startup. */
        RestoreProject,

        /*! \brief Import a chosen song source into an unsaved project. */
        ImportProject,

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
    };

    /*!
    \brief Builds an open-project action.
    \param file Project package path selected by the user.
    \return Action carrying the package path.
    */
    [[nodiscard]] static EditorAction openProject(std::filesystem::path file);

    /*!
    \brief Builds a startup restore action.
    \param file Persisted project package path.
    \return Action carrying the package path.
    */
    [[nodiscard]] static EditorAction restoreProject(std::filesystem::path file);

    /*!
    \brief Builds an import action.
    \param file Song source path selected by the user.
    \return Action carrying the source path.
    */
    [[nodiscard]] static EditorAction importProject(std::filesystem::path file);

    /*!
    \brief Builds a direct save action.
    \return Action without an additional payload.
    */
    [[nodiscard]] static EditorAction saveProject() noexcept;

    /*!
    \brief Builds a Save As action.
    \param file Project package path selected by the user.
    \return Action carrying the save destination.
    */
    [[nodiscard]] static EditorAction saveProjectAs(std::filesystem::path file);

    /*!
    \brief Builds a publish action.
    \param file Native song package path selected by the user.
    \return Action carrying the publish destination.
    */
    [[nodiscard]] static EditorAction publishProject(std::filesystem::path file);

    /*!
    \brief Builds a close-project action.
    \return Action without an additional payload.
    */
    [[nodiscard]] static EditorAction closeProject() noexcept;

    /*!
    \brief Builds an exit-application action.
    \return Action without an additional payload.
    */
    [[nodiscard]] static EditorAction exitApplication() noexcept;

    /*!
    \brief Builds an unsaved-changes prompt resolution action.
    \param decision User-selected prompt decision.
    \return Action carrying the prompt decision.
    */
    [[nodiscard]] static EditorAction resolveUnsavedChangesPrompt(
        UnsavedChangesDecision decision) noexcept;

    /*!
    \brief Builds a Save As cancellation action.
    \return Action without an additional payload.
    */
    [[nodiscard]] static EditorAction cancelSaveAsPrompt() noexcept;

    /*!
    \brief Builds a transport play/pause action.
    \return Action without an additional payload.
    */
    [[nodiscard]] static EditorAction playPause() noexcept;

    /*!
    \brief Builds a transport stop action.
    \return Action without an additional payload.
    */
    [[nodiscard]] static EditorAction stop() noexcept;

    /*!
    \brief Builds a waveform seek action.
    \param normalized_x Click position normalized to the interval [0, 1].
    \return Action carrying the normalized coordinate.
    */
    [[nodiscard]] static EditorAction seekWaveform(double normalized_x) noexcept;

    /*!
    \brief Builds an add-plugin action.
    \param file Plugin file path selected by the user.
    \return Action carrying the plugin file path.
    */
    [[nodiscard]] static EditorAction addPlugin(std::filesystem::path file);

    /*!
    \brief Returns the action identity.
    \return Action id used by routing and availability policy.
    */
    [[nodiscard]] Id id() const noexcept;

    /*!
    \brief Returns the unsaved-changes decision payload.
    \return Stored decision for ResolveUnsavedChangesPrompt actions.
    */
    [[nodiscard]] UnsavedChangesDecision decision() const noexcept;

    /*!
    \brief Returns the normalized waveform coordinate payload.
    \return Stored normalized x coordinate for SeekWaveform actions.
    */
    [[nodiscard]] double normalizedX() const noexcept;

    /*!
    \brief Moves the file payload out of the action.
    \return Stored path for file-backed actions.
    */
    [[nodiscard]] std::filesystem::path takeFile() noexcept;

private:
    explicit EditorAction(Id id) noexcept;
    EditorAction(Id id, std::filesystem::path file);
    EditorAction(Id id, UnsavedChangesDecision decision) noexcept;
    EditorAction(Id id, double normalized_x) noexcept;

    Id m_id{Id::SaveProject};
    std::filesystem::path m_file{};
    UnsavedChangesDecision m_decision{UnsavedChangesDecision::Cancel};
    double m_normalized_x{};
};

} // namespace rock_hero::editor::core
