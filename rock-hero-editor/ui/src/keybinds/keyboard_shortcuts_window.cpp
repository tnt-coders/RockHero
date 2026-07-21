#include "keybinds/keyboard_shortcuts_window.h"

#include "shared/editor_theme.h"

namespace rock_hero::editor::ui
{

namespace
{

// Sized so every category fits without horizontal squeeze at the default chip widths.
constexpr int g_default_width{560};
constexpr int g_default_height{520};
constexpr int g_min_width{420};
constexpr int g_min_height{320};
constexpr int g_max_width{1200};
constexpr int g_max_height{1600};

} // namespace

KeyboardShortcutsWindow::KeyboardShortcutsWindow(
    juce::ApplicationCommandManager& command_manager, juce::Component* centering_component)
    : juce::DocumentWindow(
          "Keyboard Shortcuts", editorTheme().bar_background, juce::DocumentWindow::closeButton)
    , m_editor_view(command_manager)
    , m_centering_component(centering_component)
{
    setUsingNativeTitleBar(true);
    m_editor_view.setComponentID("keyboard_shortcuts_editor_view");
    m_editor_view.setSize(g_default_width, g_default_height);
    setContentNonOwned(&m_editor_view, true);
    setResizable(true, false);
    setResizeLimits(g_min_width, g_min_height, g_max_width, g_max_height);
}

void KeyboardShortcutsWindow::open()
{
    if (!isVisible())
    {
        centreAroundComponent(m_centering_component.getComponent(), getWidth(), getHeight());
        setVisible(true);
    }
    toFront(true);
}

void KeyboardShortcutsWindow::closeButtonPressed()
{
    setVisible(false);
}

} // namespace rock_hero::editor::ui
