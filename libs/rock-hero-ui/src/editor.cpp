#include "editor.h"

namespace rock_hero::ui
{

// Wires controller and view together after both construction-only dependencies are available.
Editor::Editor(
    core::Session& session, audio::ITransport& transport, audio::IEdit& edit,
    audio::IThumbnailFactory& thumbnail_factory)
    : m_controller(session, transport, edit)
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

} // namespace rock_hero::ui
