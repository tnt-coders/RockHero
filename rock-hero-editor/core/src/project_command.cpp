#include "project_command.h"

#include <utility>

namespace rock_hero::editor::core
{

// Builds an open-project command carrying the package path.
ProjectCommand ProjectCommand::open(std::filesystem::path file)
{
    return ProjectCommand{ProjectCommandId::Open, std::move(file)};
}

// Builds an import command carrying the source path.
ProjectCommand ProjectCommand::importSong(std::filesystem::path file)
{
    return ProjectCommand{ProjectCommandId::Import, std::move(file)};
}

// Builds a save command for the current project destination.
ProjectCommand ProjectCommand::save() noexcept
{
    return ProjectCommand{ProjectCommandId::Save};
}

// Builds a save command used while an unsaved-changes prompt has a deferred command.
ProjectCommand ProjectCommand::saveBeforeDeferredCommand() noexcept
{
    return ProjectCommand{ProjectCommandId::Save, true};
}

// Builds a save-as command carrying the destination path.
ProjectCommand ProjectCommand::saveAs(std::filesystem::path file)
{
    return ProjectCommand{ProjectCommandId::SaveAs, std::move(file)};
}

// Builds a publish command carrying the native song package destination path.
ProjectCommand ProjectCommand::publish(std::filesystem::path file)
{
    return ProjectCommand{ProjectCommandId::Publish, std::move(file)};
}

// Builds a close-project command.
ProjectCommand ProjectCommand::close() noexcept
{
    return ProjectCommand{ProjectCommandId::Close};
}

// Builds an exit-application command.
ProjectCommand ProjectCommand::exit() noexcept
{
    return ProjectCommand{ProjectCommandId::Exit};
}

// Stores the project command id for payload-free commands.
ProjectCommand::ProjectCommand(ProjectCommandId id) noexcept
    : m_id(id)
{}

// Stores the project command id and path payload.
ProjectCommand::ProjectCommand(ProjectCommandId id, std::filesystem::path file)
    : m_id(id)
    , m_file(std::move(file))
{}

// Stores the project command id plus prompt-failure behavior for Save after a prompt.
ProjectCommand::ProjectCommand(ProjectCommandId id, bool clear_deferred_command_on_failure) noexcept
    : m_id(id)
    , m_clear_deferred_command_on_failure(clear_deferred_command_on_failure)
{}

// Returns the command identity used by prompt state and controller routing.
ProjectCommandId ProjectCommand::id() const noexcept
{
    return m_id;
}

// Returns the path payload without consuming it so worker completion can still inspect it.
const std::filesystem::path& ProjectCommand::file() const noexcept
{
    return m_file;
}

// Moves the path payload out of file-backed commands.
std::filesystem::path ProjectCommand::takeFile() noexcept
{
    return std::move(m_file);
}

// Identifies Save commands that were started from an unsaved-changes confirmation.
bool ProjectCommand::clearsDeferredCommandOnFailure() const noexcept
{
    return m_clear_deferred_command_on_failure;
}

} // namespace rock_hero::editor::core
