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

// Returns the command identity used by prompt state and controller routing.
ProjectCommandId ProjectCommand::id() const noexcept
{
    return m_id;
}

// Moves the path payload out of file-backed commands.
std::filesystem::path ProjectCommand::takeFile() noexcept
{
    return std::move(m_file);
}

} // namespace rock_hero::editor::core
