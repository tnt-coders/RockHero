#include "editor_action.h"

#include <type_traits>
#include <variant>

namespace rock_hero::editor::core
{

static_assert(!std::is_default_constructible_v<EditorAction::Action>);
static_assert(!std::is_default_constructible_v<UnsavedChangesPrompt>);
static_assert(!std::is_default_constructible_v<SaveAsPrompt>);

// Maps each variant alternative to its matching id without exposing payload contents.
EditorAction::Id idOf(const EditorAction::Action& action)
{
    return std::visit(
        [](const auto& alternative) noexcept -> EditorAction::Id {
            using A = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<A, EditorAction::OpenProject>)
            {
                return EditorAction::Id::OpenProject;
            }
            else if constexpr (std::is_same_v<A, EditorAction::RestoreProject>)
            {
                return EditorAction::Id::RestoreProject;
            }
            else if constexpr (std::is_same_v<A, EditorAction::ImportSong>)
            {
                return EditorAction::Id::ImportSong;
            }
            else if constexpr (std::is_same_v<A, EditorAction::SaveProject>)
            {
                return EditorAction::Id::SaveProject;
            }
            else if constexpr (std::is_same_v<A, EditorAction::SaveProjectAs>)
            {
                return EditorAction::Id::SaveProjectAs;
            }
            else if constexpr (std::is_same_v<A, EditorAction::PublishProject>)
            {
                return EditorAction::Id::PublishProject;
            }
            else if constexpr (std::is_same_v<A, EditorAction::CloseProject>)
            {
                return EditorAction::Id::CloseProject;
            }
            else if constexpr (std::is_same_v<A, EditorAction::ExitApplication>)
            {
                return EditorAction::Id::ExitApplication;
            }
            else if constexpr (std::is_same_v<A, EditorAction::ResolveUnsavedChangesPrompt>)
            {
                return EditorAction::Id::ResolveUnsavedChangesPrompt;
            }
            else if constexpr (std::is_same_v<A, EditorAction::CancelSaveAsPrompt>)
            {
                return EditorAction::Id::CancelSaveAsPrompt;
            }
            else if constexpr (std::is_same_v<A, EditorAction::PlayPause>)
            {
                return EditorAction::Id::PlayPause;
            }
            else if constexpr (std::is_same_v<A, EditorAction::Stop>)
            {
                return EditorAction::Id::Stop;
            }
            else if constexpr (std::is_same_v<A, EditorAction::SeekWaveform>)
            {
                return EditorAction::Id::SeekWaveform;
            }
            else if constexpr (std::is_same_v<A, EditorAction::AddPlugin>)
            {
                return EditorAction::Id::AddPlugin;
            }
            else if constexpr (std::is_same_v<A, EditorAction::RemovePlugin>)
            {
                return EditorAction::Id::RemovePlugin;
            }
        },
        action);
}

} // namespace rock_hero::editor::core
