/*!
\file project_command.h
\brief Private project command value used by editor-controller prompt routing.
*/

#pragma once

#include <filesystem>
#include <rock_hero/editor/core/editor_view_state.h>

namespace rock_hero::editor::core
{

/*!
\brief Value object describing a project-level command waiting on controller prompts.

ProjectCommand stays private to the editor core target. Only ProjectCommandId is public because
the view state needs to identify which command an unsaved-changes or Save As prompt will continue.
*/
class ProjectCommand final
{
public:
    /*!
    \brief Builds an open-project command.
    \param file Project package path selected by the user.
    \return Command carrying the project package path.
    */
    [[nodiscard]] static ProjectCommand open(std::filesystem::path file);

    /*!
    \brief Builds an import-song command.
    \param file Song source path selected by the user.
    \return Command carrying the song source path.
    */
    [[nodiscard]] static ProjectCommand importSong(std::filesystem::path file);

    /*!
    \brief Builds a save-project command for the current project path.
    \return Command without an additional payload.
    */
    [[nodiscard]] static ProjectCommand save() noexcept;

    /*!
    \brief Builds a save-project command that protects a deferred command.

    \return Save command that clears the deferred project command if the write fails.
    */
    [[nodiscard]] static ProjectCommand saveBeforeDeferredCommand() noexcept;

    /*!
    \brief Builds a save-as command.
    \param file Project package path selected by the user.
    \return Command carrying the project package path.
    */
    [[nodiscard]] static ProjectCommand saveAs(std::filesystem::path file);

    /*!
    \brief Builds a publish-song command.
    \param file Native song package path selected by the user.
    \return Command carrying the song package path.
    */
    [[nodiscard]] static ProjectCommand publish(std::filesystem::path file);

    /*!
    \brief Builds a close-project command.
    \return Command without an additional payload.
    */
    [[nodiscard]] static ProjectCommand close() noexcept;

    /*!
    \brief Builds an exit-application command.
    \return Command without an additional payload.
    */
    [[nodiscard]] static ProjectCommand exit() noexcept;

    /*!
    \brief Returns the command identity.
    \return Command id used by prompt state and command routing.
    */
    [[nodiscard]] ProjectCommandId id() const noexcept;

    /*!
    \brief Returns the file payload without consuming it.
    \return Stored path for file-backed commands.
    */
    [[nodiscard]] const std::filesystem::path& file() const noexcept;

    /*!
    \brief Moves the file payload out of the command.
    \return Stored path for file-backed commands.
    */
    [[nodiscard]] std::filesystem::path takeFile() noexcept;

    /*!
    \brief Reports whether a failed Save should abandon the deferred project command.
    \return True when the command was created to protect a deferred command.
    */
    [[nodiscard]] bool clearsDeferredCommandOnFailure() const noexcept;

private:
    explicit ProjectCommand(ProjectCommandId id) noexcept;
    ProjectCommand(ProjectCommandId id, std::filesystem::path file);
    ProjectCommand(ProjectCommandId id, bool clear_deferred_command_on_failure) noexcept;

    ProjectCommandId m_id{ProjectCommandId::Close};
    std::filesystem::path m_file{};
    bool m_clear_deferred_command_on_failure{false};
};

} // namespace rock_hero::editor::core
