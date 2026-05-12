#include "editor.h"

#include <utility>

namespace rock_hero::ui
{

// Wires the controller and view after construction dependencies are available.
Editor::Editor(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IThumbnailFactory& thumbnail_factory, ExitFunction exit_function)
    : m_controller(
          transport, audio, OpenFunction{}, ImportFunction{}, SaveFunction{}, SaveAsFunction{},
          PublishFunction{}, std::move(exit_function))
    , m_view(m_controller, transport, thumbnail_factory)
{
    m_controller.attachView(m_view);
}

// Uses member declaration order for teardown: the view is destroyed before the controller.
Editor::~Editor() = default;

// Exposes the composed JUCE component without exposing controller/view wiring knobs.
juce::Component& Editor::component() noexcept
{
    return m_view;
}

// Opens a project after construction so app startup can restore the previous session.
void Editor::openProject(std::filesystem::path file)
{
    m_controller.onOpenRequested(std::move(file));
}

// Exposes only the restorable project path, not the controller internals.
std::optional<std::filesystem::path> Editor::currentProjectFile() const
{
    return m_controller.currentProjectFile();
}

// Lets the app window close button use the same unsaved-change gate as File > Exit.
void Editor::requestExit()
{
    m_controller.onExitRequested();
}

} // namespace rock_hero::ui
