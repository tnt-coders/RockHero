#include "preview/preview_window.h"

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
}

PreviewWindow::~PreviewWindow()
{
    close();
}

void PreviewWindow::open()
{
    setVisible(true);
    toFront(true);
    m_surface->attach();
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

void PreviewWindow::closeButtonPressed()
{
    close();
}

bool PreviewWindow::keyPressed(const juce::KeyPress& key)
{
    if (m_forward_key_press && m_forward_key_press(key))
    {
        return true;
    }
    return juce::DocumentWindow::keyPressed(key);
}

} // namespace rock_hero::editor::ui
