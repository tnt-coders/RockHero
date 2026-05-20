#include "editor_action.h"

#include <type_traits>
#include <variant>

namespace rock_hero::editor::core
{

namespace
{

template <typename> inline constexpr bool g_dependent_false = false;

// Maps an action alternative type to the single public id used by policy and view state.
template <typename Alternative> [[nodiscard]] constexpr EditorAction::Id idOfAlternative() noexcept
{
    using A = std::decay_t<Alternative>;
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
    else if constexpr (std::is_same_v<A, EditorAction::ShowPluginBrowser>)
    {
        return EditorAction::Id::ShowPluginBrowser;
    }
    else if constexpr (std::is_same_v<A, EditorAction::ScanPluginCatalog>)
    {
        return EditorAction::Id::ScanPluginCatalog;
    }
    else if constexpr (std::is_same_v<A, EditorAction::AddPluginCandidate>)
    {
        return EditorAction::Id::AddPluginCandidate;
    }
    else if constexpr (std::is_same_v<A, EditorAction::RemovePlugin>)
    {
        return EditorAction::Id::RemovePlugin;
    }
    else if constexpr (std::is_same_v<A, EditorAction::OpenPlugin>)
    {
        return EditorAction::Id::OpenPlugin;
    }
    else
    {
        static_assert(g_dependent_false<A>, "Unhandled editor action alternative");
    }
}

// Shares id mapping across the full action variant and narrower subset variants.
template <typename ActionVariant>
[[nodiscard]] EditorAction::Id idOfVariant(const ActionVariant& action)
{
    return std::visit(
        [](const auto& alternative) noexcept -> EditorAction::Id {
            return idOfAlternative<decltype(alternative)>();
        },
        action);
}

} // namespace

static_assert(!std::is_default_constructible_v<EditorAction::Action>);
static_assert(!std::is_default_constructible_v<EditorAction::ProjectAction>);
static_assert(!std::is_default_constructible_v<EditorAction::ProjectWriteAction>);
static_assert(!std::is_constructible_v<EditorAction::ProjectAction, EditorAction::PlayPause>);
static_assert(
    !std::is_constructible_v<EditorAction::ProjectWriteAction, EditorAction::OpenProject>);
static_assert(!std::is_default_constructible_v<UnsavedChangesPrompt>);
static_assert(!std::is_default_constructible_v<SaveAsPrompt>);

// Maps each variant alternative to its matching id without exposing payload contents.
EditorAction::Id idOf(const EditorAction::Action& action)
{
    return idOfVariant(action);
}

// Maps each project-lifecycle action alternative to its matching id.
EditorAction::Id idOf(const EditorAction::ProjectAction& action)
{
    return idOfVariant(action);
}

// Maps each project write action alternative to its matching id.
EditorAction::Id idOf(const EditorAction::ProjectWriteAction& action)
{
    return idOfVariant(action);
}

} // namespace rock_hero::editor::core
