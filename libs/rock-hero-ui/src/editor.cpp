#include "editor.h"

#include <utility>

namespace rock_hero::ui
{

// Wires the controller and view after construction dependencies are available.
Editor::Editor(
    audio::ITransport& transport, audio::IAudio& audio, audio::IThumbnailFactory& thumbnail_factory,
    ExitFunction exit_function)
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

// Lets the app window close button use the same unsaved-change gate as File > Exit.
void Editor::requestExit()
{
    m_controller.onExitRequested();
}

} // namespace rock_hero::ui
