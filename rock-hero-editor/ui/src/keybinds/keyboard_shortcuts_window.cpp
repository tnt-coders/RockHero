#include "keybinds/keyboard_shortcuts_window.h"

#include "shared/editor_theme.h"

namespace rock_hero::editor::ui
{

namespace
{

// Sized so every category fits without horizontal squeeze at the default chip widths.
constexpr int g_default_width{560};
constexpr int g_default_height{480};
constexpr int g_min_width{420};
constexpr int g_min_height{320};
constexpr int g_max_width{1200};
constexpr int g_max_height{1600};

} // namespace

KeyboardShortcutsWindow::KeyboardShortcutsWindow(
    juce::ApplicationCommandManager& command_manager, juce::Component* centering_component)
    : juce::DocumentWindow(
          "Keyboard Shortcuts", editorTheme().bar_background, juce::DocumentWindow::closeButton)
    , m_mapping_editor(*command_manager.getKeyMappings(), /*showResetToDefaultButton=*/true)
    , m_centering_component(centering_component)
{
    setUsingNativeTitleBar(true);
    setLookAndFeel(&m_look_and_feel);
    m_mapping_editor.setComponentID("keyboard_shortcuts_mapping_editor");
    m_mapping_editor.setColours(editorTheme().panel_background, editorTheme().primary_text);
    m_mapping_editor.setSize(g_default_width, g_default_height);
    setContentNonOwned(&m_mapping_editor, true);
    setResizable(true, false);
    setResizeLimits(g_min_width, g_min_height, g_max_width, g_max_height);
}

KeyboardShortcutsWindow::~KeyboardShortcutsWindow()
{
    setLookAndFeel(nullptr);
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

juce::AlertWindow* KeyboardShortcutsWindow::KeymapDialogLookAndFeel::createAlertWindow(
    const juce::String& title, const juce::String& message, const juce::String& button1,
    const juce::String& button2, const juce::String& button3, juce::MessageBoxIconType icon_type,
    int num_buttons, juce::Component* associated_component)
{
    return juce::LookAndFeel_V2::createAlertWindow(
        title, message, button1, button2, button3, icon_type, num_buttons, associated_component);
}

} // namespace rock_hero::editor::ui
