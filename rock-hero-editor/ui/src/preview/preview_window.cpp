#include "preview/preview_window.h"

#include "main_window/composed_character_filter.h"
#include "preview/preview_surface.h"
#include "shared/editor_theme.h"

#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_default_width = 1280;
constexpr int g_default_height = 720;

} // namespace

// Native-titled wrapper owning the surface; centring prefers the editor window so the preview
// opens where the user is looking.
PreviewWindow::PreviewWindow(
    const common::audio::ITransport& transport, const common::audio::IPlaybackClock& playback_clock,
    std::function<bool(const juce::KeyPress&)> forward_key_press,
    juce::Component* centering_component)
    : juce::DocumentWindow(
          "3D Preview", editorTheme().window_background, juce::DocumentWindow::allButtons)
    , m_forward_key_press{std::move(forward_key_press)}
{
    setComponentID("preview_window");
    setUsingNativeTitleBar(true);
    setResizable(true, false);

    auto surface = std::make_unique<PreviewSurface>(transport, playback_clock);
    m_surface = surface.get();
    setContentOwned(surface.release(), false);

    if (centering_component != nullptr)
    {
        centreAroundComponent(centering_component, g_default_width, g_default_height);
    }
    else
    {
        centreWithSize(g_default_width, g_default_height);
    }

    // The same filter the main window installs, because this is the editor's OTHER top-level
    // window and its keys reach the very same mapping set through keyPressed() below. Without it
    // an OS-composed character (Alt plus numpad digits on Windows) arrives bare here and forwards
    // as a real chord. A key listener is offered a press BEFORE the component's own keyPressed
    // (juce_ComponentPeer.cpp:200-216), so this runs ahead of the forwarding hook; registering it
    // last is also what would put it ahead of any mapping-set listener a later change attaches
    // here, since JUCE walks listeners in reverse registration order.
    m_composed_character_filter = std::make_unique<ComposedCharacterFilter>();
    addKeyListener(m_composed_character_filter.get());
}

PreviewWindow::~PreviewWindow()
{
    // Detach before the filter is destroyed: the key-listener list holds a non-owning pointer.
    removeKeyListener(m_composed_character_filter.get());
    close();
}

// Shows the window, attaches the surface's renderer, and routes keyboard focus to the surface.
void PreviewWindow::open()
{
    setVisible(true);
    toFront(true);
    m_surface->attach();
    // Route keyboard focus to the surface so its unhandled keys bubble to keyPressed() here and
    // forward transport shortcuts to the editor; otherwise focus stays on whatever editor
    // component last held it and the preview never sees the keys.
    m_surface->grabKeyboardFocus();
}

void PreviewWindow::close()
{
    // Suspend before hiding: the peer (and the render stack) survive a hide, but the vblank
    // feed does not care about visibility, so the ticks must stop explicitly. The stack itself
    // stays up — bgfx cannot re-initialize in-process, so it lives until destruction.
    m_surface->suspend();
    setVisible(false);
}

void PreviewWindow::setHighwayState(std::shared_ptr<const common::core::HighwayViewState> state)
{
    m_surface->setHighwayState(std::move(state));
}

void PreviewWindow::setCaretSeconds(const std::optional<double> seconds)
{
    m_surface->setCaretSeconds(seconds);
}

void PreviewWindow::closeButtonPressed()
{
    close();
}

// Editor-forwarding hook: transport shortcuts pressed in the preview run in the editor first;
// keys the editor declines fall through to the DocumentWindow behavior.
bool PreviewWindow::keyPressed(const juce::KeyPress& key)
{
    if (m_forward_key_press && m_forward_key_press(key))
    {
        return true;
    }
    return juce::DocumentWindow::keyPressed(key);
}

} // namespace rock_hero::editor::ui
