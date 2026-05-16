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
    \brief Moves the file payload out of the command.
    \return Stored path for file-backed commands.
    */
    [[nodiscard]] std::filesystem::path takeFile() noexcept;

private:
    explicit ProjectCommand(ProjectCommandId id) noexcept;
    ProjectCommand(ProjectCommandId id, std::filesystem::path file);

    ProjectCommandId m_id{ProjectCommandId::Close};
    std::filesystem::path m_file{};
};

} // namespace rock_hero::editor::core
