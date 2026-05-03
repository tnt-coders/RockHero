#include "editor.h"

namespace rock_hero::ui
{

// Wires edit coordination, controller, and view after construction dependencies are available.
Editor::Editor(
    audio::ITransport& transport, audio::IEdit& edit, audio::IThumbnailFactory& thumbnail_factory)
    : m_edit_coordinator(edit)
    , m_controller(transport, m_edit_coordinator)
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
