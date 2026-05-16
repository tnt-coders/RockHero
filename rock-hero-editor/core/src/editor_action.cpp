#include "editor_action.h"

#include <utility>

namespace rock_hero::editor::core
{

// Builds an open-project action carrying the chosen package path.
EditorAction EditorAction::openProject(std::filesystem::path file)
{
    return EditorAction{Id::OpenProject, std::move(file)};
}

// Builds a startup-restore action carrying the persisted package path.
EditorAction EditorAction::restoreProject(std::filesystem::path file)
{
    return EditorAction{Id::RestoreProject, std::move(file)};
}

// Builds an import action carrying the chosen source path.
EditorAction EditorAction::importProject(std::filesystem::path file)
{
    return EditorAction{Id::ImportProject, std::move(file)};
}

// Builds a direct save action.
EditorAction EditorAction::saveProject() noexcept
{
    return EditorAction{Id::SaveProject};
}

// Builds a Save As action carrying the chosen package path.
EditorAction EditorAction::saveProjectAs(std::filesystem::path file)
{
    return EditorAction{Id::SaveProjectAs, std::move(file)};
}

// Builds a publish action carrying the chosen native song package path.
EditorAction EditorAction::publishProject(std::filesystem::path file)
{
    return EditorAction{Id::PublishProject, std::move(file)};
}

// Builds a close-project action.
EditorAction EditorAction::closeProject() noexcept
{
    return EditorAction{Id::CloseProject};
}

// Builds an exit-application action.
EditorAction EditorAction::exitApplication() noexcept
{
    return EditorAction{Id::ExitApplication};
}

// Builds an unsaved-changes prompt resolution action.
EditorAction EditorAction::resolveUnsavedChangesPrompt(UnsavedChangesDecision decision) noexcept
{
    return EditorAction{Id::ResolveUnsavedChangesPrompt, decision};
}

// Builds a Save As cancellation action for a controller-requested chooser.
EditorAction EditorAction::cancelSaveAsPrompt() noexcept
{
    return EditorAction{Id::CancelSaveAsPrompt};
}

// Builds a transport play/pause action.
EditorAction EditorAction::playPause() noexcept
{
    return EditorAction{Id::PlayPause};
}

// Builds a transport stop action.
EditorAction EditorAction::stop() noexcept
{
    return EditorAction{Id::Stop};
}

// Builds a waveform seek action carrying the normalized click coordinate.
EditorAction EditorAction::seekWaveform(double normalized_x) noexcept
{
    return EditorAction{Id::SeekWaveform, normalized_x};
}

// Builds an add-plugin action carrying the chosen plugin path.
EditorAction EditorAction::addPlugin(std::filesystem::path file)
{
    return EditorAction{Id::AddPlugin, std::move(file)};
}

// Stores the action id for payload-free actions.
EditorAction::EditorAction(Id id) noexcept
    : m_id(id)
{}

// Stores the action id and path payload.
EditorAction::EditorAction(Id id, std::filesystem::path file)
    : m_id(id)
    , m_file(std::move(file))
{}

// Stores the action id and unsaved-changes decision payload.
EditorAction::EditorAction(Id id, UnsavedChangesDecision decision) noexcept
    : m_id(id)
    , m_decision(decision)
{}

// Stores the action id and normalized waveform coordinate payload.
EditorAction::EditorAction(Id id, double normalized_x) noexcept
    : m_id(id)
    , m_normalized_x(normalized_x)
{}

// Returns the action identity used by controller policy.
EditorAction::Id EditorAction::id() const noexcept
{
    return m_id;
}

// Returns the prompt decision payload for ResolveUnsavedChangesPrompt.
UnsavedChangesDecision EditorAction::decision() const noexcept
{
    return m_decision;
}

// Returns the normalized waveform coordinate payload for SeekWaveform.
double EditorAction::normalizedX() const noexcept
{
    return m_normalized_x;
}

// Moves the path payload out of file-backed actions.
std::filesystem::path EditorAction::takeFile() noexcept
{
    return std::move(m_file);
}

} // namespace rock_hero::editor::core
